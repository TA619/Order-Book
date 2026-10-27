#include <benchmark/benchmark.h>

#include "lobster.hpp"
#include "map_order_book.hpp"
#include "replayer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// Micro-benchmarks for the core operations. Each one pre-generates a batch of
// orders outside the timed region, then times one book call per iteration.
// When a batch runs out, timing is paused while the book is rebuilt, so
// setup never counts. Args: the number of price levels the orders spread over
// (deeper books mean bigger std::maps).

namespace {

constexpr std::size_t kBatch = 1 << 16;  // orders per batch (65,536)
constexpr Price kMid = 100'000;

// Passive orders: buys below the mid, sells above it, so nothing trades.
std::vector<Order> passiveOrders(std::size_t n, int levels, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> level(1, levels);
    std::uniform_int_distribution<Qty> qty(1, 100);
    std::vector<Order> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Side s = (rng() & 1) != 0 ? Side::Buy : Side::Sell;
        const Price px = s == Side::Buy ? kMid - level(rng) : kMid + level(rng);
        out.push_back({.id = i + 1, .price = px, .qty = qty(rng), .ts = 0, .side = s});
    }
    return out;
}

void fill(MapOrderBook& book, const std::vector<Order>& orders) {
    for (const Order& o : orders) {
        book.addLimit(o);
    }
}

// ---------------------------------------------------------------------------

// Add a resting limit order (no match): map lookup + list push + index insert.
void BM_AddLimit(benchmark::State& state) {
    const auto orders = passiveOrders(kBatch, static_cast<int>(state.range(0)), 1);
    auto book = std::make_unique<MapOrderBook>();
    std::size_t i = 0;
    for (auto _ : state) {
        if (i == orders.size()) {
            state.PauseTiming();
            book = std::make_unique<MapOrderBook>();
            i = 0;
            state.ResumeTiming();
        }
        benchmark::DoNotOptimize(book->addLimit(orders[i++]));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddLimit)->Arg(10)->Arg(100)->Arg(1000);

// Same, with the index pre-sized for the whole batch.
void BM_AddLimitReserved(benchmark::State& state) {
    const auto orders = passiveOrders(kBatch, static_cast<int>(state.range(0)), 1);
    auto book = std::make_unique<MapOrderBook>(kBatch);
    std::size_t i = 0;
    for (auto _ : state) {
        if (i == orders.size()) {
            state.PauseTiming();
            book = std::make_unique<MapOrderBook>(kBatch);
            i = 0;
            state.ResumeTiming();
        }
        benchmark::DoNotOptimize(book->addLimit(orders[i++]));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddLimitReserved)->Arg(100);

// Cancel a resting order, in random order: index lookup + list unlink.
void BM_Cancel(benchmark::State& state) {
    const auto orders = passiveOrders(kBatch, static_cast<int>(state.range(0)), 2);
    std::vector<OrderId> ids;
    for (const Order& o : orders) {
        ids.push_back(o.id);
    }
    std::shuffle(ids.begin(), ids.end(), std::mt19937_64(3));

    auto book = std::make_unique<MapOrderBook>();
    fill(*book, orders);
    std::size_t i = 0;
    for (auto _ : state) {
        if (i == ids.size()) {
            state.PauseTiming();
            book = std::make_unique<MapOrderBook>();
            fill(*book, orders);
            i = 0;
            state.ResumeTiming();
        }
        benchmark::DoNotOptimize(book->cancel(ids[i++]));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Cancel)->Arg(10)->Arg(100)->Arg(1000);

// Reduce a resting order's size in place (keeps priority).
void BM_ModifyReduce(benchmark::State& state) {
    const auto orders = passiveOrders(kBatch, 100, 4);
    MapOrderBook book;
    for (const Order& o : orders) {
        Order big = o;
        big.qty = 1'000'000'000;  // never reaches zero during the run
        book.addLimit(big);
    }
    std::size_t i = 0;
    Qty q = 999'999'999;
    for (auto _ : state) {
        if (i == orders.size()) {
            i = 0;
            --q;
        }
        benchmark::DoNotOptimize(book.modify(orders[i++].id, q));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ModifyReduce);

// Market order that fills exactly `range(0)` resting orders, each on its own
// level: measures the matching loop including trade creation and level
// removal.
void BM_MarketSweep(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    const int levels = static_cast<int>(kBatch) / depth;  // orders in the book
    auto build = [&] {
        auto book = std::make_unique<MapOrderBook>();
        for (int l = 0; l < levels; ++l) {
            book->addLimit({.id = static_cast<OrderId>(l + 1), .price = kMid + l, .qty = 10,
                            .side = Side::Sell});
        }
        return book;
    };

    auto book = build();
    int left = levels;
    OrderId takerId = 1'000'000;
    for (auto _ : state) {
        if (left < depth) {
            state.PauseTiming();
            book = build();
            left = levels;
            state.ResumeTiming();
        }
        benchmark::DoNotOptimize(book->addMarket(Side::Buy, 10 * depth, takerId++));
        left -= depth;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MarketSweep)->Arg(1)->Arg(5)->Arg(20);

// A steady mixed flow, roughly the shape of real data: 50% adds, 45%
// cancels, 5% market orders. Book size stays around a few thousand orders.
void BM_MixedFlow(benchmark::State& state) {
    enum class Op : std::uint8_t { Add, Cancel, Market };
    struct Step {
        Op op;
        Order o;
    };

    std::mt19937_64 rng(5);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<int> level(1, 50);
    std::uniform_int_distribution<Qty> qty(1, 100);
    std::vector<Step> steps;
    std::vector<OrderId> live;  // ids added so far (some will have filled)
    OrderId next = 1;
    for (std::size_t i = 0; i < kBatch; ++i) {
        const int p = pct(rng);
        const Side s = (rng() & 1) != 0 ? Side::Buy : Side::Sell;
        if (p < 50 || live.empty()) {
            const Price px = s == Side::Buy ? kMid - level(rng) : kMid + level(rng);
            steps.push_back({Op::Add, {.id = next, .price = px, .qty = qty(rng), .side = s}});
            live.push_back(next++);
        } else if (p < 95) {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const std::size_t k = pick(rng);
            steps.push_back({Op::Cancel, {.id = live[k]}});
            live[k] = live.back();
            live.pop_back();
        } else {
            steps.push_back({Op::Market, {.id = next++, .qty = qty(rng), .side = s}});
        }
    }

    auto book = std::make_unique<MapOrderBook>();
    std::size_t i = 0;
    for (auto _ : state) {
        if (i == steps.size()) {
            state.PauseTiming();
            book = std::make_unique<MapOrderBook>();
            i = 0;
            state.ResumeTiming();
        }
        const Step& st = steps[i++];
        switch (st.op) {
            case Op::Add:
                benchmark::DoNotOptimize(book->addLimit(st.o));
                break;
            case Op::Cancel:
                benchmark::DoNotOptimize(book->cancel(st.o.id));
                break;
            case Op::Market:
                benchmark::DoNotOptimize(book->addMarket(st.o.side, st.o.qty, st.o.id));
                break;
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MixedFlow);

// Full-day replay of real NASDAQ data through Replayer (no validation).
// Skipped if the data files aren't downloaded (scripts/download_data.ps1).
void BM_ReplayLobster(benchmark::State& state, const char* ticker) {
    const std::string base = std::string(OB_DATA_DIR) + "/" + ticker + "_2012-06-21_34200000_57600000_";
    std::vector<lobster::Message> msgs;
    lobster::Snapshot first;
    try {
        msgs = lobster::loadMessages(base + "message_10.csv");
        std::ifstream in(base + "orderbook_10.csv");
        std::string line;
        if (!std::getline(in, line) || !lobster::parseSnapshot(line, first)) {
            throw std::runtime_error("no orderbook file");
        }
    } catch (const std::exception&) {
        state.SkipWithError("LOBSTER data not found; run scripts/download_data.ps1");
        return;
    }

    for (auto _ : state) {
        MapOrderBook book;
        Replayer r(book);
        r.seed(first);
        for (std::size_t i = 1; i < msgs.size(); ++i) {
            r.apply(msgs[i]);
        }
        auto trades = r.stats().engineTrades;
        benchmark::DoNotOptimize(trades);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(msgs.size() - 1));
}
BENCHMARK_CAPTURE(BM_ReplayLobster, AMZN, "AMZN")->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ReplayLobster, MSFT, "MSFT")->Unit(benchmark::kMillisecond);

}  // namespace
