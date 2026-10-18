#include "map_order_book.hpp"

/*
=============================================================================
 Day 5 Problem: Add Limit Order (insert only)            Difficulty: Easy
=============================================================================

 You are given an order book with two sides:

   bids_  : std::map<Price, std::list<Order>, std::greater<>>  (high -> low)
   asks_  : std::map<Price, std::list<Order>>                  (low  -> high)
   index_ : std::unordered_map<OrderId, Locator>
   nextTs_: Timestamp counter, starts at 0

   struct Locator { Side side; Price price; std::list<Order>::iterator it; };

 Implement addLimit(Order o):

   - If o is INVALID, do nothing and return {}.
       Invalid means: o.qty <= 0, OR o.id is already in the book.
   - Otherwise:
       1. Set o.ts = nextTs_, then increment nextTs_.
       2. Put o at the BACK of the list for price o.price on side o.side
          (create the level if it doesn't exist).
       3. Save where it went in index_[o.id].
   - Return {} (no matching today -- that's Week 2).

-----------------------------------------------------------------------------
 Example 1:
   addLimit({id=1, Buy,  px=10000, qty=10})
   addLimit({id=2, Sell, px=10100, qty=5})
   Book after:
     asks: 10100 -> [#2]
     bids: 10000 -> [#1]
   orderCount() = 2, levelCount(Buy) = 1, levelCount(Sell) = 1

 Example 2:  (same price -> same level, FIFO)
   addLimit({id=1, Buy, px=10000, qty=10})
   addLimit({id=2, Buy, px=10000, qty=20})
   addLimit({id=3, Buy, px=9900,  qty=30})
   Book after:
     bids: 10000 -> [#1 (ts=0), #2 (ts=1)]     <- #1 is at the FRONT
            9900 -> [#3 (ts=2)]
   orderCount() = 3, levelCount(Buy) = 2

 Example 3:  (rejections)
   addLimit({id=1, Buy, px=10000, qty=10})    -> accepted
   addLimit({id=1, Buy, px=10100, qty=10})    -> rejected (duplicate id)
   addLimit({id=2, Buy, px=10000, qty=0})     -> rejected (qty <= 0)
   addLimit({id=3, Buy, px=10000, qty=-5})    -> rejected (qty <= 0)
   orderCount() = 1, levelCount(Buy) = 1

-----------------------------------------------------------------------------
 Constraints:
   - Up to 10^6 calls. Target: O(log L) per call, L = number of price levels.
   - Prices are integer ticks (any int64 value). Never use double.
   - Don't change any other function or the header today.

 Tests: tests/order_book_tests.cpp  (run: ctest --preset debug)

-----------------------------------------------------------------------------
 Hints (try without them first):
   H1: index_.contains(id) is C++20 and tells you if an id exists.
   H2: map[price] returns a reference to the list, creating it if missing.
   H3: After list.push_back(x), the new element is std::prev(list.end()).
   H4: bids_ and asks_ are DIFFERENT types (different comparators), so
       `auto& side = buy ? bids_ : asks_;` will not compile. A plain
       if/else is fine for today.
=============================================================================
*/
std::vector<Trade> MapOrderBook::addLimit(Order o) {
    // ---------------- write your code below ----------------
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
    // ---------------- write your code above ----------------
    return {};
}

// ---------------------------------------------------------------------------
// Day 6
// ---------------------------------------------------------------------------
std::optional<Price> MapOrderBook::bestBid() const {
    // TODO(Day 6): empty book -> std::nullopt, else the first key of bids_.
    return std::nullopt;
}

std::optional<Price> MapOrderBook::bestAsk() const {
    // TODO(Day 6)
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Week 2+
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
