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

    // 1. Aggressive part: trade against the opposite side while prices cross.
    std::vector<Trade> trades;
    if (o.side == Side::Buy) {
        matchAgainst(o, asks_, trades);
    } else {
        matchAgainst(o, bids_, trades);
    }
    if (o.qty == 0) {
        return trades;  // fully filled, nothing left to rest
    }

    // 2. Passive part: rest whatever is left on our own side.
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

    return trades;
}

/*
=============================================================================
 Week 2 Day 1 Problem: The Matching Loop                Difficulty: Medium
=============================================================================

 Implement matchAgainst(taker, opposite, trades).

 `taker`    : the incoming order (already validated, ts already set).
 `opposite` : the map on the OTHER side -- asks_ if taker is a Buy,
              bids_ if taker is a Sell. Its begin() is always the best
              price for the taker (that's why bids_ uses std::greater).
 `trades`   : append one Trade per fill.

 Rules (price-time priority):
   1. Look at the best opposite level (opposite.begin()).
   2. It CROSSES if:  taker is Buy  and level price <= taker.price
                      taker is Sell and level price >= taker.price
      If it doesn't cross (or opposite is empty), stop.
   3. Fill against the FRONT order of that level (oldest first):
        fill = min(taker.qty, resting.qty)
        Trade price = the RESTING order's price (not the taker's!)
        Trade buyId / sellId: whichever of taker/resting is Buy / Sell.
      Subtract fill from both quantities.
   4. If the resting order hit qty 0: erase it from the level AND from
      index_. If the level is now empty: erase the level from `opposite`.
   5. Repeat until taker.qty == 0 or nothing crosses.

 addLimit (already written) calls this first, then rests whatever qty is
 left on the taker's own side.

-----------------------------------------------------------------------------
 Example 1:  (full fill, one resting order)
   asks: 101 -> [#1 qty=5]
   taker: Buy #9 px=101 qty=5
   trades = [{buy=9, sell=1, px=101, qty=5}]
   book after: asks empty, bids empty

 Example 2:  (price improvement: trade at RESTING price)
   asks: 101 -> [#1 qty=5]
   taker: Buy #9 px=105 qty=5
   trades = [{buy=9, sell=1, px=101, qty=5}]     <- 101, not 105

 Example 3:  (sweep two levels, then rest the remainder)
   asks: 101 -> [#1 qty=3],  102 -> [#2 qty=4]
   taker: Buy #9 px=102 qty=10
   trades = [{9,1,101,3}, {9,2,102,4}]
   book after: asks empty, bids: 102 -> [#9 qty=3]

 Example 4:  (FIFO inside a level + partial fill of a resting order)
   asks: 101 -> [#1 qty=3, #2 qty=3]
   taker: Buy #9 px=101 qty=4
   trades = [{9,1,101,3}, {9,2,101,1}]
   book after: asks: 101 -> [#2 qty=2]  (#2 keeps its place in the queue)

 Example 5:  (no cross)
   asks: 101 -> [#1 qty=5]
   taker: Buy #9 px=100 qty=5
   trades = []
   book after: asks 101 -> [#1], bids 100 -> [#9]

-----------------------------------------------------------------------------
 Constraints:
   - Each fill is O(1) apart from the level erase (O(log L) amortised).
   - Never dereference begin() of an empty map or list.
   - Don't erase an element and then use its iterator (iterator
     invalidation!). Read what you need first, or use the iterator that
     erase() returns.

 Hints:
   H1: Loop shape:
         while (taker.qty > 0 && !opposite.empty()) {
             auto lvlIt = opposite.begin();
             if (!crosses) break;
             Level& level = lvlIt->second;
             Order& resting = level.front();
             ...
         }
   H2: "crosses" without an if on Side: opposite.key_comp() is the map's
       comparator (std::less for asks, std::greater for bids). The level
       does NOT cross exactly when key_comp()(taker.price, levelPrice) is
       true. Try it on paper for both sides. (A plain if/else on
       taker.side is also fine.)
   H3: std::min needs both arguments of the same type (Qty).
   H4: Trade is {buyId, sellId, price, qty}.
=============================================================================
*/
template <class OppositeSide>
void MapOrderBook::matchAgainst(Order& taker, OppositeSide& opposite, std::vector<Trade>& trades) {
    // ---------------- write your code below ----------------
    const bool takerBuys = taker.side == Side::Buy;

    while (taker.qty > 0 && !opposite.empty()) {
        auto lvlIt = opposite.begin();  // best opposite level

        // key_comp() is std::less for asks, std::greater for bids. It is
        // true exactly when the level is worse than the taker's limit.
        if (opposite.key_comp()(taker.price, lvlIt->first)) {
            break;
        }

        Level& level = lvlIt->second;
        Order& resting = level.front();  // oldest order = first to fill

        const Qty fill = std::min(taker.qty, resting.qty);
        trades.push_back({
            .buyId = takerBuys ? taker.id : resting.id,
            .sellId = takerBuys ? resting.id : taker.id,
            .price = lvlIt->first,  // trade at the resting price
            .qty = fill,
        });
        taker.qty -= fill;
        resting.qty -= fill;

        if (resting.qty == 0) {
            index_.erase(resting.id);  // read the id before pop_front frees it
            level.pop_front();
        }
        if (level.empty()) {
            opposite.erase(lvlIt);
        }
    }
    // ---------------- write your code above ----------------
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
