#include "map_order_book.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>

// Note: bids_ and asks_ have different types (different comparators), so
// code that touches "the side of order o" branches on Side instead of
// binding one reference to either map.

std::vector<Trade> MapOrderBook::addLimit(Order o) {
    // Reject non-positive quantities and ids already resting in the book.
    if (o.qty <= 0 || index_.contains(o.id)) {
        return {};
    }

    o.ts = nextTs_++;

    // One map lookup per insert: operator[] finds or creates the level, and
    // we keep a reference to it instead of looking it up again.
    if (o.side == Side::Buy) {
        auto& level = bids_[o.price];
        level.push_back(o);
        index_[o.id] = {Side::Buy, o.price, std::prev(level.end())};
    } else {
        auto& level = asks_[o.price];
        level.push_back(o);
        index_[o.id] = {Side::Sell, o.price, std::prev(level.end())};
    }

    // No matching yet (Week 2), so an insert never trades.
    return {};
}

std::optional<Price> MapOrderBook::bestBid() const {
    // bids_ is sorted high -> low, so begin() is the highest bid.
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> MapOrderBook::bestAsk() const {
    // asks_ is sorted low -> high, so begin() is the lowest ask.
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

// Ladder view, spread in the middle and prices descending down the screen:
//
//   ASK 10200 qty=7 orders=1
//   ASK 10100 qty=5 orders=1      <- best ask
//   ---
//   BID 10000 qty=30 orders=2     <- best bid
//   BID 9900 qty=30 orders=1
//
// Shows at most `depth` levels per side, the ones closest to the spread.
// Cost: O(depth * orders per level), since level qty is summed on the fly.
void MapOrderBook::printBook(std::ostream& os, std::size_t depth) const {
    auto printLevel = [&os](const char* tag, Price px, const Level& level) {
        Qty total = 0;
        for (const Order& o : level) {
            total += o.qty;
        }
        os << tag << ' ' << px << " qty=" << total << " orders=" << level.size() << '\n';
    };

    // The best asks are the first levels of asks_ (lowest prices), but they
    // are printed high -> low, so walk backwards from one past the last
    // level shown. min() keeps std::next from running past end().
    const auto askCount = static_cast<std::ptrdiff_t>(std::min(depth, asks_.size()));
    for (auto it = std::next(asks_.begin(), askCount); it != asks_.begin();) {
        --it;
        printLevel("ASK", it->first, it->second);
    }

    os << "---\n";

    std::size_t shown = 0;
    for (auto it = bids_.begin(); it != bids_.end() && shown < depth; ++it, ++shown) {
        printLevel("BID", it->first, it->second);
    }
}

std::vector<Order> MapOrderBook::levelOrders(Side s, Price px) const {
    // find(), not operator[]: a lookup must never create an empty level.
    if (s == Side::Buy) {
        auto lvl = bids_.find(px);
        if (lvl == bids_.end()) {
            return {};
        }
        return {lvl->second.begin(), lvl->second.end()};
    }

    auto lvl = asks_.find(px);
    if (lvl == asks_.end()) {
        return {};
    }
    return {lvl->second.begin(), lvl->second.end()};
}

// ---------------------------------------------------------------------------
// Week 2: matching, cancel, modify
// ---------------------------------------------------------------------------

std::vector<Trade> MapOrderBook::addMarket(Side s, Qty q, OrderId id) {
    // TODO(Week 2)
    (void)s;
    (void)q;
    (void)id;
    return {};
}

bool MapOrderBook::cancel(OrderId id) {
    // TODO(Week 2): look up index_, erase from the level, erase the level if
    // it became empty, erase from index_.
    (void)id;
    return false;
}

bool MapOrderBook::modify(OrderId id, Qty newQty) {
    // TODO(Week 2)
    (void)id;
    (void)newQty;
    return false;
}
