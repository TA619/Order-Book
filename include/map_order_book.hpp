#pragma once

#include "i_order_book.hpp"

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <memory_resource>
#include <ostream>
#include <unordered_map>

// Map-based book: one std::map per side, keyed by price, each level holding a
// FIFO std::list of orders (front = oldest = first to fill).
//
// All node-based containers allocate from one pool (pool_), not the global
// heap: a freed list/map/hash node goes on a per-size free list and the next
// insert of the same size reuses it, with no locking. That removes most of
// the malloc/free cost from the hot path (see the README's benchmark table).
// The pool owns the memory, so the book can't be copied or moved.
class MapOrderBook final : public IOrderBook {
public:
    MapOrderBook() = default;

    // Pre-sizes the order index for this many resting orders, so it never
    // rehashes (reallocates and moves every entry) while the book grows.
    explicit MapOrderBook(std::size_t expectedOrders) { index_.reserve(expectedOrders); }
    MapOrderBook(const MapOrderBook&) = delete;
    MapOrderBook& operator=(const MapOrderBook&) = delete;

    std::vector<Trade> addLimit(Order o) override;
    std::vector<Trade> addMarket(Side s, Qty q, OrderId id) override;
    bool cancel(OrderId id) override;
    bool modify(OrderId id, Qty newQty) override;
    std::vector<Trade> replace(OrderId id, Price newPrice, Qty newQty) override;
    [[nodiscard]] std::optional<Price> bestBid() const override;
    [[nodiscard]] std::optional<Price> bestAsk() const override;
    [[nodiscard]] std::optional<Order> find(OrderId id) const override;
    [[nodiscard]] std::optional<Order> frontOrder(Side s) const override;
    [[nodiscard]] std::vector<LevelInfo> depth(Side s, std::size_t n) const override;

    // Debug / test helpers (not part of IOrderBook).
    [[nodiscard]] std::size_t orderCount() const noexcept { return index_.size(); }
    [[nodiscard]] std::size_t levelCount(Side s) const noexcept {
        return s == Side::Buy ? bids_.size() : asks_.size();
    }

    // Prints the top `depth` levels of each side (see map_order_book.cpp).
    void printBook(std::ostream& os, std::size_t depth = 5) const;

    [[nodiscard]] std::vector<Order> levelOrders(Side s, Price px) const override;

private:
    using Level = std::pmr::list<Order>;

    // Matches `taker` against the opposite side's map (asks_ for a buy,
    // bids_ for a sell), appending fills to `trades` and reducing taker.qty.
    // A template because bids_ and asks_ are different types; one body
    // serves both sides. Defined in map_order_book.cpp (only used there).
    template <class OppositeSide>
    void matchAgainst(Order& taker, OppositeSide& opposite, std::vector<Trade>& trades);

    // Unlinks a resting order from its level (and drops the level if it is
    // now empty). Does not touch index_; the caller does that.
    template <class SideMap>
    static std::vector<LevelInfo> topLevels(const SideMap& side, std::size_t n);

    template <class SideMap>
    static void unlink(SideMap& side, Price px, Level::iterator it);

    // Where an order lives, so cancel/modify can find it in O(1) without
    // searching. std::list iterators stay valid when other orders are
    // inserted or erased -- that is why we use list and not vector here.
    struct Locator {
        Side side;
        Price price;
        Level::iterator it;
    };

    // Declared first: members are destroyed in reverse order, so the pool
    // outlives every container that allocates from it.
    std::pmr::unsynchronized_pool_resource pool_;

    // Bids: highest price first (std::greater), so begin() is the best bid.
    // pmr containers pass the pool down to the Level lists they create.
    std::pmr::map<Price, Level, std::greater<>> bids_{&pool_};
    // Asks: lowest price first (default std::less), so begin() is the best ask.
    std::pmr::map<Price, Level> asks_{&pool_};

    std::pmr::unordered_map<OrderId, Locator> index_{&pool_};

    // Time priority: every accepted order gets the next sequence number.
    Timestamp nextTs_ = 0;
};
