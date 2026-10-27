#pragma once

#include "order.hpp"
#include "trade.hpp"
#include "types.hpp"

#include <cstddef>
#include <optional>
#include <vector>

// Aggregated view of one price level (what L2 market data shows).
struct LevelInfo {
    Price price;
    Qty qty;              // total resting quantity at this price
    std::size_t orders;   // number of resting orders

    friend bool operator==(const LevelInfo&, const LevelInfo&) = default;
};

// Common interface for every book implementation (map-based now, tick-indexed
// vector book in Add-on A), so tests and benchmarks run against both.
//
// Matching rules: price-time priority. An incoming order trades against the
// best opposite price first, oldest order first within a price, and every
// trade happens at the RESTING order's price. Order ids must be unique among
// resting orders; quantities must be > 0.
class IOrderBook {
public:
    // Matches `o` against the opposite side while prices cross, then rests any
    // remainder at o.price. Returns the trades. Rejected (no-op, returns {})
    // if o.qty <= 0 or o.id is already resting.
    virtual std::vector<Trade> addLimit(Order o) = 0;

    // Matches q units at any price until filled or the opposite side is empty.
    // Never rests: any unfilled quantity is dropped. Same rejection rules.
    virtual std::vector<Trade> addMarket(Side s, Qty q, OrderId id) = 0;

    // Removes a resting order. Returns false if the id is not resting.
    virtual bool cancel(OrderId id) = 0;

    // Changes the quantity of a resting order, keeping its price.
    //   newQty <  current: reduced in place, KEEPS its queue position.
    //   newQty >  current: moves to the back of its level (loses priority).
    //   newQty == current: no change.
    // Returns false if the id is not resting or newQty <= 0 (use cancel).
    virtual bool modify(OrderId id, Qty newQty) = 0;

    // Cancel/replace: changes price and/or quantity. Always loses priority and
    // may trade immediately at the new price, so it returns trades. Returns {}
    // and leaves the book unchanged if the id is not resting or newQty <= 0.
    virtual std::vector<Trade> replace(OrderId id, Price newPrice, Qty newQty) = 0;

    [[nodiscard]] virtual std::optional<Price> bestBid() const = 0;
    [[nodiscard]] virtual std::optional<Price> bestAsk() const = 0;

    // The resting order with this id (current remaining qty), if any.
    [[nodiscard]] virtual std::optional<Order> find(OrderId id) const = 0;

    // The order that would fill first on side `s`: oldest at the best price.
    [[nodiscard]] virtual std::optional<Order> frontOrder(Side s) const = 0;

    // Copy of the orders resting at one price level, front (oldest) first.
    // Empty if the level doesn't exist. Not on the hot path.
    [[nodiscard]] virtual std::vector<Order> levelOrders(Side s, Price px) const = 0;

    // Top `n` levels of one side, best price first (L2 snapshot).
    [[nodiscard]] virtual std::vector<LevelInfo> depth(Side s, std::size_t n) const = 0;

    virtual ~IOrderBook() = default;
};
