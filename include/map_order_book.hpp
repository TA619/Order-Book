#pragma once

#include "i_order_book.hpp"

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <ostream>
#include <unordered_map>

// Map-based book: one std::map per side, keyed by price, each level holding a
// FIFO std::list of orders (front = oldest = first to fill).
class MapOrderBook final : public IOrderBook {
public:
    std::vector<Trade> addLimit(Order o) override;
    std::vector<Trade> addMarket(Side s, Qty q, OrderId id) override;
    bool cancel(OrderId id) override;
    bool modify(OrderId id, Qty newQty) override;
    [[nodiscard]] std::optional<Price> bestBid() const override;
    [[nodiscard]] std::optional<Price> bestAsk() const override;

    // Debug / test helpers (not part of IOrderBook).
    [[nodiscard]] std::size_t orderCount() const noexcept { return index_.size(); }
    [[nodiscard]] std::size_t levelCount(Side s) const noexcept {
        return s == Side::Buy ? bids_.size() : asks_.size();
    }

    // Prints the top `depth` levels of each side (see map_order_book.cpp).
    void printBook(std::ostream& os, std::size_t depth = 5) const;

    // Copy of the orders resting at one price level, front (oldest) first.
    // Empty if the level doesn't exist. For tests; not on the hot path.
    [[nodiscard]] std::vector<Order> levelOrders(Side s, Price px) const;

private:
    using Level = std::list<Order>;

    // Matches `taker` against the opposite side's map (asks_ for a buy,
    // bids_ for a sell), appending fills to `trades` and reducing taker.qty.
    // A template because bids_ and asks_ are different types; one body
    // serves both sides. Defined in map_order_book.cpp (only used there).
    template <class OppositeSide>
    void matchAgainst(Order& taker, OppositeSide& opposite, std::vector<Trade>& trades);

    // Where an order lives, so cancel/modify can find it in O(1) without
    // searching. std::list iterators stay valid when other orders are
    // inserted or erased -- that is why we use list and not vector here.
    struct Locator {
        Side side;
        Price price;
        Level::iterator it;
    };

    // Bids: highest price first (std::greater), so begin() is the best bid.
    std::map<Price, Level, std::greater<>> bids_;
    // Asks: lowest price first (default std::less), so begin() is the best ask.
    std::map<Price, Level> asks_;

    std::unordered_map<OrderId, Locator> index_;

    // Time priority: every accepted order gets the next sequence number.
    Timestamp nextTs_ = 0;
};
