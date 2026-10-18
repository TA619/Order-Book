#pragma once

#include "order.hpp"
#include "trade.hpp"
#include "types.hpp"

#include <optional>
#include <vector>

// Common interface for every book implementation (map-based now, tick-indexed
// vector book in Add-on A), so tests and benchmarks run against both.
class IOrderBook {
public:
    // Adds a limit order; returns any trades it caused (empty until Week 2).
    virtual std::vector<Trade> addLimit(Order o) = 0;
    virtual std::vector<Trade> addMarket(Side s, Qty q, OrderId id) = 0;
    virtual bool cancel(OrderId id) = 0;
    virtual bool modify(OrderId id, Qty newQty) = 0;
    [[nodiscard]] virtual std::optional<Price> bestBid() const = 0;
    [[nodiscard]] virtual std::optional<Price> bestAsk() const = 0;
    virtual ~IOrderBook() = default;
};
