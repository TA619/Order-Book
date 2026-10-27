#pragma once

#include "i_order_book.hpp"
#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Parser for LOBSTER (lobsterdata.com) NASDAQ files. A LOBSTER sample is two
// CSV files with one row per event, kept in lockstep:
//
//   message file:   Time,Type,OrderId,Size,Price,Direction
//                   34200.189607670,1,11885113,21,2238100,1
//   orderbook file: AskPx1,AskSz1,BidPx1,BidSz1,AskPx2,...   (state AFTER
//                   the message on the same row; N levels per side)
//
// Time is seconds after midnight with up to 9 decimals, Price is dollars *
// 10000 (so 2238100 = $223.81 -- already integer ticks), Direction is
// 1 = buy, -1 = sell. For executions, Direction is the side of the RESTING
// order that was hit, not the aggressor.
namespace lobster {

enum class EventType : std::uint8_t {
    Submit = 1,         // new visible limit order
    Cancel = 2,         // partial cancel: Size = quantity removed
    Delete = 3,         // full cancel
    Execute = 4,        // visible resting order executed (Size = fill)
    ExecuteHidden = 5,  // hidden order executed (not in the visible book)
    Cross = 6,          // auction / cross trade
    Halt = 7,           // trading halt indicator
};

struct Message {
    std::int64_t timeNs;  // nanoseconds after midnight (integer, no double)
    EventType type;
    OrderId id;
    Qty size;
    Price price;
    Side side;
};

// LOBSTER marks empty levels with these dummy prices and size 0.
inline constexpr Price kEmptyAsk = 9'999'999'999;
inline constexpr Price kEmptyBid = -9'999'999'999;

// One orderbook-file row: level i of each side, best first. Empty levels
// (dummy price) are dropped, so asks.size() may be < the file's level count.
struct Snapshot {
    std::vector<LevelInfo> asks;  // LevelInfo::orders is unknown (0)
    std::vector<LevelInfo> bids;
    std::size_t levels = 0;  // levels per side in the file (10 for "_10" files)
};

// Parse one CSV line. Return std::nullopt on malformed input. Hand-rolled
// with std::from_chars: no locale, no allocation, no stringstream.
[[nodiscard]] std::optional<Message> parseMessage(std::string_view line);
[[nodiscard]] bool parseSnapshot(std::string_view line, Snapshot& out);  // reuses out's storage

// Reads a whole message file. Throws std::runtime_error if the file can't be
// opened or a line doesn't parse (the error names the line number).
[[nodiscard]] std::vector<Message> loadMessages(const std::string& path);

}  // namespace lobster
