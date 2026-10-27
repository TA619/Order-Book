#include "lobster.hpp"
#include "map_order_book.hpp"
#include "replayer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

// Per-event latency of a full-day LOBSTER replay.
//
//   latency <message.csv> <orderbook.csv> [--csv latency.csv]
//
// Times every Replayer::apply() call and prints percentiles (p50 ... max),
// overall and per event type. Throughput (events/sec) hides the slow tail;
// percentiles show it: p99 = 99% of events were at least this fast.
// Only apply() is timed; the book is kept exact with reconcile() in between.
//
// Timer: steady_clock on Windows ticks every 100 ns (QueryPerformanceCounter
// at 10 MHz), too coarse for ~200 ns events. The CPU's time-stamp counter
// (__rdtsc) ticks at a fixed rate of ~3 GHz on modern x86, so it is read
// around each event and converted to ns with a rate calibrated against
// steady_clock.

namespace {

using Clock = std::chrono::steady_clock;

// TSC ticks per nanosecond, measured over ~200 ms of wall time.
double calibrateTsc() {
    const auto t0 = Clock::now();
    const std::uint64_t c0 = __rdtsc();
    while (Clock::now() - t0 < std::chrono::milliseconds(200)) {
    }
    const std::uint64_t c1 = __rdtsc();
    const double ns = std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
    return static_cast<double>(c1 - c0) / ns;
}

struct Percentiles {
    std::size_t count = 0;
    double p50 = 0, p90 = 0, p99 = 0, p999 = 0, max = 0, mean = 0;
};

// Sorts `ns` in place.
Percentiles summarize(std::vector<double>& ns) {
    Percentiles p;
    p.count = ns.size();
    if (ns.empty()) {
        return p;
    }
    std::sort(ns.begin(), ns.end());
    auto at = [&](double q) {
        return ns.at(static_cast<std::size_t>(q * static_cast<double>(ns.size() - 1)));
    };
    p.p50 = at(0.50);
    p.p90 = at(0.90);
    p.p99 = at(0.99);
    p.p999 = at(0.999);
    p.max = ns.back();
    double sum = 0;
    for (double v : ns) {
        sum += v;
    }
    p.mean = sum / static_cast<double>(ns.size());
    return p;
}

void printRow(const std::string& name, const Percentiles& p) {
    std::cout << std::left << std::setw(16) << name << std::right << std::setw(9) << p.count
              << std::fixed << std::setprecision(0) << std::setw(8) << p.mean << std::setw(8)
              << p.p50 << std::setw(8) << p.p90 << std::setw(8) << p.p99 << std::setw(9)
              << p.p999 << std::setw(10) << p.max << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: latency <message.csv> <orderbook.csv> [--csv out.csv]\n";
        return 2;
    }
    const std::string csvPath = argc >= 5 && std::string(argv[3]) == "--csv" ? argv[4] : "";

    try {
        const std::vector<lobster::Message> msgs = lobster::loadMessages(argv[1]);
        std::ifstream bookFile(argv[2]);
        std::string line;
        lobster::Snapshot first;
        if (!std::getline(bookFile, line) || !lobster::parseSnapshot(line, first)) {
            std::cerr << "bad first orderbook row\n";
            return 1;
        }

        const double ticksPerNs = calibrateTsc();

        // Timer overhead: two back-to-back reads with nothing between them.
        std::vector<double> empty(100'000);
        for (double& v : empty) {
            const std::uint64_t a = __rdtsc();
            const std::uint64_t b = __rdtsc();
            v = static_cast<double>(b - a) / ticksPerNs;
        }
        const double overhead = summarize(empty).p50;

        // One validated replay: reconcile() keeps the book exactly equal to the
        // real one (see replayer.hpp), so every timed apply() runs against
        // the true book state. File reading and reconcile happen between
        // events, outside the timed region.
        std::vector<std::uint64_t> ticks(msgs.size());
        MapOrderBook book;
        Replayer r(book);
        r.seed(first);
        lobster::Snapshot prev = first;
        lobster::Snapshot snap;
        for (std::size_t i = 1; i < msgs.size(); ++i) {
            const std::uint64_t a = __rdtsc();
            r.apply(msgs[i]);
            const std::uint64_t b = __rdtsc();
            ticks[i] = b - a;

            if (!std::getline(bookFile, line) || !lobster::parseSnapshot(line, snap)) {
                std::cerr << "orderbook file ended early at row " << i << '\n';
                return 1;
            }
            r.reconcile(&prev, snap);
            std::swap(prev, snap);
        }

        // Group by event type (index = LOBSTER type number).
        std::array<std::vector<double>, 8> byType;
        std::vector<double> all;
        all.reserve(msgs.size());
        for (std::size_t i = 1; i < msgs.size(); ++i) {
            const double ns = static_cast<double>(ticks[i]) / ticksPerNs;
            all.push_back(ns);
            byType.at(static_cast<std::size_t>(msgs[i].type)).push_back(ns);
        }

        if (!csvPath.empty()) {
            std::ofstream csv(csvPath);  // raw samples, before summarize() sorts them
            csv << "type,ns\n";
            for (std::size_t i = 1; i < msgs.size(); ++i) {
                csv << static_cast<int>(msgs[i].type) << ','
                    << std::llround(static_cast<double>(ticks[i]) / ticksPerNs) << '\n';
            }
        }

        std::cout << "File      " << argv[1] << '\n'
                  << "Timer     rdtsc at " << std::setprecision(3) << ticksPerNs
                  << " ticks/ns, overhead ~" << std::setprecision(0) << std::fixed << overhead
                  << " ns per sample (included below)\n\n"
                  << "latency in ns     count    mean     p50     p90     p99    p99.9       max\n";
        printRow("all events", summarize(all));
        static constexpr std::array<const char*, 8> kNames = {
            "", "submit", "cancel", "delete", "execute", "hidden exec", "cross", "halt"};
        for (std::size_t t = 1; t < byType.size(); ++t) {
            if (!byType.at(t).empty()) {
                printRow(kNames.at(t), summarize(byType.at(t)));
            }
        }
        if (!csvPath.empty()) {
            std::cout << "\nWrote " << csvPath << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
