#include "map_order_book.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>

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

/*
=============================================================================
 Day 6 Problem A: Best Bid / Best Ask                     Difficulty: Easy
=============================================================================

 Implement bestBid() and bestAsk():

   - bestBid(): the HIGHEST price anyone is willing to buy at.
   - bestAsk(): the LOWEST  price anyone is willing to sell at.
   - If that side is empty, return std::nullopt.

-----------------------------------------------------------------------------
 Example 1:
   (empty book)
   bestBid() = nullopt, bestAsk() = nullopt

 Example 2:
   addLimit({id=1, Buy,  px=9900,  qty=10})
   addLimit({id=2, Buy,  px=10000, qty=10})
   addLimit({id=3, Sell, px=10200, qty=5})
   addLimit({id=4, Sell, px=10100, qty=5})
   bestBid() = 10000, bestAsk() = 10100      (spread = 100 ticks)

 Example 3:  (one side only)
   addLimit({id=1, Sell, px=10100, qty=5})
   bestBid() = nullopt, bestAsk() = 10100

-----------------------------------------------------------------------------
 Constraints:
   - O(1). No loops.
   - Don't change the header.

 Hints:
   H1: Think about what bids_.begin() points to, given std::greater<>.
   H2: Each map element is a std::pair<const Price, Level>. Which member is
       the price?
=============================================================================
*/
std::optional<Price> MapOrderBook::bestBid() const {
    // ---------------- write your code below ----------------
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
    // ---------------- write your code above ----------------
}

std::optional<Price> MapOrderBook::bestAsk() const {
    // ---------------- write your code below ----------------
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
    // ---------------- write your code above ----------------
}

/*
=============================================================================
 Day 6 Problem B: Print the Book                        Difficulty: Medium
=============================================================================

 Implement printBook(os, depth): write the top `depth` levels of each side
 to `os` in this EXACT format (the tests compare the string):

   ASK <price> qty=<total qty at level> orders=<number of orders>
   ...
   ---
   BID <price> qty=<total qty at level> orders=<number of orders>
   ...

 Rules:
   - Show at most `depth` levels per side: the `depth` levels CLOSEST to the
     spread (the best ones).
   - Asks are printed from HIGH to LOW, so the best ask sits just above "---".
   - Bids are printed from HIGH to LOW, so the best bid sits just below "---".
   - Every line ends with '\n', including "---".
   - qty = sum of o.qty over all orders in that level.

 (This is how a trading screen ("ladder") shows a book: the spread is in the
 middle, and prices go down the screen.)

-----------------------------------------------------------------------------
 Example 1:  (empty book)
   ---

 Example 2:
   addLimit({id=1, Buy,  px=10000, qty=10})
   addLimit({id=2, Buy,  px=10000, qty=20})
   addLimit({id=3, Buy,  px=9900,  qty=30})
   addLimit({id=4, Sell, px=10100, qty=5})
   addLimit({id=5, Sell, px=10200, qty=7})
   printBook(os) prints:
     ASK 10200 qty=7 orders=1
     ASK 10100 qty=5 orders=1
     ---
     BID 10000 qty=30 orders=2
     BID 9900 qty=30 orders=1

 Example 3:  (depth limit)
   asks at 101, 102, 103, 104; bids at 99, 98, 97 (qty 1 each)
   printBook(os, 2) prints:
     ASK 102 qty=1 orders=1
     ASK 101 qty=1 orders=1
     ---
     BID 99 qty=1 orders=1
     BID 98 qty=1 orders=1
   (103, 104 and 97 are NOT shown: they are further from the spread.)

-----------------------------------------------------------------------------
 Constraints:
   - O(depth * orders per level). Don't copy the whole book.
   - Don't change the header.

 Hints:
   H1: Bids are easy: walk from bids_.begin() and stop after `depth` levels
       or at end().
   H2: Asks are the tricky part. The levels you want are the FIRST `depth`
       of asks_ (lowest prices), but you must print them in REVERSE.
       Find the iterator one past the last level to show, then walk it
       backwards to begin(). std::next / std::prev work on map iterators.
   H3: Watch out when depth > asks_.size(), and when depth == 0.
   H4: std::accumulate (<numeric>) or a simple for loop for the qty sum.
=============================================================================
*/
void MapOrderBook::printBook(std::ostream& os, std::size_t depth) const {
    // ---------------- write your code below ----------------
    auto printLevel = [&os](const char* tag, Price px, const Level& level) {
        Qty total = 0;
        for (const Order& o : level) {
            total += o.qty;
        }
        os << tag << ' ' << px << " qty=" << total << " orders=" << level.size() << '\n';
    };

    // Asks: the best `depth` levels are the first ones in asks_ (lowest
    // prices), but they are printed high -> low, so walk them backwards from
    // one past the last level shown.
    const auto askCount = static_cast<std::ptrdiff_t>(std::min(depth, asks_.size()));
    for (auto it = std::next(asks_.begin(), askCount); it != asks_.begin();) {
        --it;
        printLevel("ASK", it->first, it->second);
    }

    os << "---\n";

    // Bids: begin() is already the best (highest) bid, so walk forwards.
    std::size_t shown = 0;
    for (auto it = bids_.begin(); it != bids_.end() && shown < depth; ++it, ++shown) {
        printLevel("BID", it->first, it->second);
    }
    // ---------------- write your code above ----------------
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
