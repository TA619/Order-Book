#include "lobster.hpp"
#include "map_order_book.hpp"
#include "replayer.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// Replays a LOBSTER sample through MapOrderBook.
//
//   replay <message.csv> <orderbook.csv> [--csv spread.csv]
//
// Pass 1 (validation): applies every message, reconciles levels the message
// file can't see (see replayer.hpp), and compares our book with the matching
// orderbook-file row; collects stats and, optionally, writes a
// time series (time, bid, ask, mid, spread) once per second for plotting.
// Pass 2 (timing): replays the same messages into a fresh book with nothing
// else in the loop, to measure raw throughput.

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string clockTime(std::int64_t ns) {
    const auto secs = ns / 1'000'000'000;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld", static_cast<long long>(secs / 3600),
                  static_cast<long long>(secs / 60 % 60), static_cast<long long>(secs % 60));
    return buf;
}

// Side-by-side view of the first few levels, printed at the first mismatch.
void dumpMismatch(std::size_t i, const lobster::Message& m, const MapOrderBook& book,
                  const lobster::Snapshot& snap) {
    std::cout << "First best bid/ask mismatch after message " << i << ": type=" << static_cast<int>(m.type)
              << " id=" << m.id << " size=" << m.size << " px=" << m.price << ' ' << m.side
              << "\n  level  ours(ask)        lobster(ask)     ours(bid)        lobster(bid)\n";
    const auto asks = book.depth(Side::Sell, 3);
    const auto bids = book.depth(Side::Buy, 3);
    auto cell = [](const std::vector<LevelInfo>& v, std::size_t k) {
        return k < v.size() ? std::to_string(v[k].price) + "x" + std::to_string(v[k].qty)
                            : std::string("-");
    };
    for (std::size_t k = 0; k < 3; ++k) {
        std::cout << "  " << k + 1 << "      " << std::left << std::setw(17) << cell(asks, k)
                  << std::setw(17) << cell(snap.asks, k) << std::setw(17) << cell(bids, k)
                  << cell(snap.bids, k) << std::right << '\n';
    }
}

double pct(std::uint64_t part, std::uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: replay <message.csv> <orderbook.csv> [--csv out.csv]\n";
        return 2;
    }
    const std::string msgPath = argv[1];
    const std::string bookPath = argv[2];
    const std::string csvPath = argc >= 5 && std::string(argv[3]) == "--csv" ? argv[4] : "";

    try {
        // ---- Load ---------------------------------------------------------
        auto t0 = Clock::now();
        const std::vector<lobster::Message> msgs = lobster::loadMessages(msgPath);
        const double loadSecs = secondsSince(t0);
        if (msgs.empty()) {
            std::cerr << "no messages in " << msgPath << '\n';
            return 1;
        }

        std::ifstream bookFile(bookPath);
        if (!bookFile) {
            std::cerr << "cannot open " << bookPath << '\n';
            return 1;
        }

        // Row 0 is the book AFTER message 0; it seeds the pre-open orders,
        // and replay starts at message 1.
        std::string line;
        lobster::Snapshot first;
        if (!std::getline(bookFile, line) || !lobster::parseSnapshot(line, first)) {
            std::cerr << "bad first orderbook row\n";
            return 1;
        }

        // ---- Pass 1: replay + validate -------------------------------------
        MapOrderBook book;
        Replayer replayer(book);
        replayer.seed(first);

        std::ofstream csv;
        if (!csvPath.empty()) {
            csv.open(csvPath);
            csv << "time_s,bid,ask,mid,spread\n";
        }

        lobster::Snapshot prev = first;
        lobster::Snapshot snap = first;
        std::uint64_t compared = 0;
        std::uint64_t topMatches = 0;
        std::uint64_t fullMatches = 0;
        std::size_t firstMismatch = 0;     // message index of first full-depth mismatch
        std::size_t firstTopMismatch = 0;  // ... and of first best bid/ask mismatch
        double spreadSum = 0;           // simple average over events
        double spreadTimeSum = 0;       // time-weighted
        double spreadTime = 0;
        std::uint64_t spreadCount = 0;
        std::int64_t nextCsvNs = 0;

        t0 = Clock::now();
        for (std::size_t i = 1; i < msgs.size(); ++i) {
            const lobster::Message& m = msgs[i];
            replayer.apply(m);

            if (!std::getline(bookFile, line) || !lobster::parseSnapshot(line, snap)) {
                std::cerr << "orderbook file ended early at row " << i << '\n';
                return 1;
            }
            replayer.reconcile(&prev, snap);
            const BookMatch match = compareBook(book, snap);
            ++compared;
            topMatches += match.top;
            fullMatches += match.all;
            if (!match.all && firstMismatch == 0) {
                firstMismatch = i;
            }
            if (!match.top && firstTopMismatch == 0) {
                firstTopMismatch = i;
                dumpMismatch(i, m, book, snap);
            }

            const auto bid = book.bestBid();
            const auto ask = book.bestAsk();
            if (bid && ask) {
                const double spread = static_cast<double>(*ask - *bid);
                spreadSum += spread;
                ++spreadCount;
                // The spread holds until the next message arrives.
                if (i + 1 < msgs.size()) {
                    const double dt = static_cast<double>(msgs[i + 1].timeNs - m.timeNs) * 1e-9;
                    spreadTimeSum += spread * dt;
                    spreadTime += dt;
                }
                if (csv && m.timeNs >= nextCsvNs) {
                    csv << std::fixed << std::setprecision(3) << m.timeNs * 1e-9 << ',' << *bid
                        << ',' << *ask << ',' << (*bid + *ask) / 2.0 << ',' << *ask - *bid << '\n';
                    nextCsvNs = (m.timeNs / 1'000'000'000 + 1) * 1'000'000'000;
                }
            }
            std::swap(prev, snap);  // this row is the next message's "previous"
        }
        const double validateSecs = secondsSince(t0);
        const ReplayStats& s = replayer.stats();

        // ---- Pass 2: timing only -------------------------------------------
        // Messages only: no file reading, comparison or reconcile. Without
        // reconcile, invisible deep levels drift, but the per-message work is
        // the same kind (add / cancel / match), so it's a fair throughput.
        MapOrderBook fastBook;
        Replayer fastReplayer(fastBook);
        fastReplayer.seed(first);
        t0 = Clock::now();
        for (std::size_t i = 1; i < msgs.size(); ++i) {
            fastReplayer.apply(msgs[i]);
        }
        const double replaySecs = secondsSince(t0);

        // ---- Report --------------------------------------------------------
        const double tick = 10000.0;  // LOBSTER prices are dollars * 10000
        const Qty allVolume = s.volume + s.hiddenVolume;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "File        " << msgPath << '\n'
                  << "Session     " << clockTime(msgs.front().timeNs) << " - "
                  << clockTime(msgs.back().timeNs) << '\n'
                  << "\nMessages    " << s.messages << " replayed (+1 used to seed)\n"
                  << "  submit    " << s.submits << '\n'
                  << "  cancel    " << s.cancels << " partial, " << s.deletes << " full\n"
                  << "  execute   " << s.executions << " visible, " << s.hiddenExecutions
                  << " hidden\n"
                  << "  other     " << s.crosses << " cross, " << s.halts << " halt\n"
                  << "  cancel-to-trade ratio  "
                  << static_cast<double>(s.cancels + s.deletes) /
                         static_cast<double>(s.executions + s.hiddenExecutions)
                  << '\n'
                  << "\nVolume      " << allVolume << " shares (" << s.volume << " visible, "
                  << s.hiddenVolume << " hidden)\n"
                  << "VWAP        $" << std::setprecision(4)
                  << s.notional / static_cast<double>(allVolume) / tick << '\n'
                  << "Avg spread  $" << spreadSum / static_cast<double>(spreadCount) / tick
                  << " per event, $" << spreadTimeSum / spreadTime / tick << " time-weighted\n"
                  << std::setprecision(2) << "Final book  " << book.orderCount() << " orders, "
                  << book.levelCount(Side::Buy) << " bid levels, "
                  << book.levelCount(Side::Sell) << " ask levels\n"
                  << "\nValidation vs LOBSTER orderbook file (" << compared << " rows)\n"
                  << "  best bid/ask match   " << pct(topMatches, compared) << "%\n"
                  << "  all " << first.levels << " levels match  " << pct(fullMatches, compared)
                  << "%";
        if (fullMatches != compared) {
            std::cout << "  (first mismatch at message " << firstMismatch << ')';
        }
        std::cout << '\n'
                  << "  engine trades        " << s.engineTrades << '\n'
                  << "  priority mismatches  " << s.priorityMismatches << '\n'
                  << "  seed reorders        " << s.seedReorders << " (synthetic orders, expected)\n"
                  << "  hidden corrections   " << s.hiddenCorrections
                  << " (levels changed out of view, expected)\n"
                  << "  unknown order ids    " << s.unknownOrders
                  << " (pre-open / out of view, applied to level, expected)\n"
                  << "  dropped messages     " << s.droppedMessages << '\n'
                  << "  unexpected crosses   " << s.unexpectedTrades << '\n'
                  << "\nTiming\n"
                  << "  load + parse         " << loadSecs * 1e3 << " ms ("
                  << static_cast<double>(msgs.size()) / loadSecs / 1e6 << " M lines/s)\n"
                  << "  replay + validate    " << validateSecs * 1e3 << " ms\n"
                  << "  replay only          " << replaySecs * 1e3 << " ms ("
                  << static_cast<double>(s.messages) / replaySecs / 1e6 << " M msgs/s, "
                  << replaySecs * 1e9 / static_cast<double>(s.messages) << " ns/msg)\n";
        if (!csvPath.empty()) {
            std::cout << "\nWrote " << csvPath << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
