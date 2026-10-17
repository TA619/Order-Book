#pragma once

#include <cstdint>
#include <ostream>

// Core value types shared by every part of the engine.

// Prices are integer ticks, never double: 0.1 + 0.2 != 0.3 in floating point,
// and two "equal" prices must always land on the same book level.
using Price = std::int64_t;

// Signed on purpose: remaining = qty - filled; a bug shows up as a negative
// number instead of silently wrapping around to ~1.8e19.
using Qty = std::int64_t;

using OrderId = std::uint64_t;

// Time priority key. The book assigns a monotonically increasing sequence
// number, which keeps tests deterministic (no wall-clock dependence).
using Timestamp = std::uint64_t;

// 1-byte underlying type keeps Order small (see static_assert in order.hpp).
enum class Side : std::uint8_t { Buy, Sell };

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

inline std::ostream& operator<<(std::ostream& os, Side s) {
    return os << (s == Side::Buy ? "Buy" : "Sell");
}
