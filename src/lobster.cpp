#include "lobster.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace lobster {

namespace {

// Small cursor over a CSV line. Each next*() reads one field and skips the
// comma after it; any failure sets ok = false and the caller gives up.
struct Fields {
    const char* p;
    const char* end;
    bool ok = true;

    template <class Int>
    Int nextInt() {
        Int v{};
        auto [ptr, ec] = std::from_chars(p, end, v);
        if (ec != std::errc{}) {
            ok = false;
            return v;
        }
        p = ptr;
        skipComma();
        return v;
    }

    // "34200.18960767" -> 34200189607670 ns. Parsed as two integers so no
    // precision is lost (a double has ~16 significant digits; this has 14).
    std::int64_t nextTimeNs() {
        std::int64_t secs = 0;
        auto [ptr, ec] = std::from_chars(p, end, secs);
        if (ec != std::errc{}) {
            ok = false;
            return 0;
        }
        p = ptr;
        std::int64_t frac = 0;
        int digits = 0;
        if (p != end && *p == '.') {
            for (++p; p != end && *p >= '0' && *p <= '9'; ++p) {
                if (digits < 9) {
                    frac = frac * 10 + (*p - '0');
                    ++digits;
                }
            }
        }
        for (; digits < 9; ++digits) {
            frac *= 10;  // pad to nanoseconds
        }
        skipComma();
        return secs * 1'000'000'000 + frac;
    }

    void skipComma() {
        if (p != end && *p == ',') {
            ++p;
        }
    }
};

std::string_view trimLineEnd(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    return line;
}

}  // namespace

std::optional<Message> parseMessage(std::string_view line) {
    line = trimLineEnd(line);
    Fields f{line.data(), line.data() + line.size()};

    Message m{};
    m.timeNs = f.nextTimeNs();
    const int type = f.nextInt<int>();
    m.id = f.nextInt<OrderId>();
    m.size = f.nextInt<Qty>();
    m.price = f.nextInt<Price>();
    const int dir = f.nextInt<int>();

    if (!f.ok || f.p != f.end || type < 1 || type > 7 || (dir != 1 && dir != -1)) {
        return std::nullopt;
    }
    m.type = static_cast<EventType>(type);
    m.side = dir == 1 ? Side::Buy : Side::Sell;
    return m;
}

bool parseSnapshot(std::string_view line, Snapshot& out) {
    line = trimLineEnd(line);
    Fields f{line.data(), line.data() + line.size()};
    out.asks.clear();
    out.bids.clear();
    out.levels = 0;

    // Groups of 4 fields per level: ask px, ask size, bid px, bid size.
    while (f.ok && f.p != f.end) {
        const Price askPx = f.nextInt<Price>();
        const Qty askQty = f.nextInt<Qty>();
        const Price bidPx = f.nextInt<Price>();
        const Qty bidQty = f.nextInt<Qty>();
        if (!f.ok) {
            return false;
        }
        ++out.levels;
        if (askPx != kEmptyAsk) {
            out.asks.push_back({.price = askPx, .qty = askQty, .orders = 0});
        }
        if (bidPx != kEmptyBid) {
            out.bids.push_back({.price = bidPx, .qty = bidQty, .orders = 0});
        }
    }
    return f.ok;
}

std::vector<Message> loadMessages(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }

    std::vector<Message> out;
    std::string line;  // reused: getline keeps its capacity between lines
    std::size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty()) {
            continue;
        }
        auto m = parseMessage(line);
        if (!m) {
            throw std::runtime_error(path + ":" + std::to_string(lineNo) + ": bad message line");
        }
        out.push_back(*m);
    }
    return out;
}

}  // namespace lobster
