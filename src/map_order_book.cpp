#include "map_order_book.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>

// Note: bids_ and asks_ have different types (different comparators). Code
// that works on "one side" is either a template (matchAgainst, unlink) or
// branches on Side, instead of binding one reference to either map.

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

// Price-time priority matching loop, shared by addLimit and addMarket.
//
// `opposite` is asks_ for a buy taker and bids_ for a sell taker; in both
// maps begin() is the best price for the taker. key_comp() is std::less for
// asks and std::greater for bids, and key_comp()(taker.price, levelPrice) is
// true exactly when that level is worse than the taker's limit -- so one
// condition handles both sides.
template <class OppositeSide>
void MapOrderBook::matchAgainst(Order& taker, OppositeSide& opposite, std::vector<Trade>& trades) {
    const bool takerBuys = taker.side == Side::Buy;

    while (taker.qty > 0 && !opposite.empty()) {
        auto lvlIt = opposite.begin();  // best opposite level
        if (opposite.key_comp()(taker.price, lvlIt->first)) {
            break;  // best level doesn't cross: nothing further can either
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
}

template <class SideMap>
void MapOrderBook::unlink(SideMap& side, Price px, Level::iterator it) {
    auto lvl = side.find(px);  // must exist: every resting order has a level
    lvl->second.erase(it);     // O(1); other orders' iterators stay valid
    if (lvl->second.empty()) {
        side.erase(lvl);
    }
}

// ---------------------------------------------------------------------------
// Order entry
// ---------------------------------------------------------------------------

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

std::vector<Trade> MapOrderBook::addMarket(Side s, Qty q, OrderId id) {
    if (q <= 0 || index_.contains(id)) {
        return {};
    }

    // A market order is a limit order whose price crosses every level:
    // key_comp()(max, px) with std::less and key_comp()(min, px) with
    // std::greater are never true, so matchAgainst only stops when the
    // order is filled or the opposite side is empty.
    Order taker{
        .id = id,
        .price = s == Side::Buy ? std::numeric_limits<Price>::max()
                                : std::numeric_limits<Price>::min(),
        .qty = q,
        .ts = nextTs_++,
        .side = s,
    };

    std::vector<Trade> trades;
    if (s == Side::Buy) {
        matchAgainst(taker, asks_, trades);
    } else {
        matchAgainst(taker, bids_, trades);
    }

    // Any unfilled qty is dropped: market orders never rest.
    return trades;
}

// ---------------------------------------------------------------------------
// Order management
// ---------------------------------------------------------------------------

// O(1) apart from the level lookup: index_ gives the order's exact list
// node, so there is no scan through the level. (That is why levels are
// std::list -- its iterators survive other inserts and erases.)
bool MapOrderBook::cancel(OrderId id) {
    auto found = index_.find(id);
    if (found == index_.end()) {
        return false;
    }

    const Locator loc = found->second;  // copy: erase below invalidates `found`
    index_.erase(found);

    if (loc.side == Side::Buy) {
        unlink(bids_, loc.price, loc.it);
    } else {
        unlink(asks_, loc.price, loc.it);
    }
    return true;
}

bool MapOrderBook::modify(OrderId id, Qty newQty) {
    if (newQty <= 0) {
        return false;
    }
    auto found = index_.find(id);
    if (found == index_.end()) {
        return false;
    }

    const Locator& loc = found->second;
    Order& o = *loc.it;

    // Reducing (or keeping) size keeps time priority: nobody behind us in the
    // queue is harmed. O(1), no map lookup at all.
    if (newQty <= o.qty) {
        o.qty = newQty;
        return true;
    }

    // Increasing size loses priority: otherwise you could join the queue
    // small and grow later, jumping everyone who arrived in between. Move the
    // node to the back of its level with a fresh timestamp. splice() relinks
    // the node without copying it, so loc.it (and index_) stay valid.
    o.qty = newQty;
    o.ts = nextTs_++;
    Level& level = loc.side == Side::Buy ? bids_.find(loc.price)->second
                                         : asks_.find(loc.price)->second;
    level.splice(level.end(), level, loc.it);
    return true;
}

std::vector<Trade> MapOrderBook::replace(OrderId id, Price newPrice, Qty newQty) {
    if (newQty <= 0) {
        return {};
    }
    auto found = index_.find(id);
    if (found == index_.end()) {
        return {};
    }

    // Cancel + re-add: the order gets a new timestamp (back of the queue) and
    // goes through matching again, since the new price may cross the spread.
    const Side side = found->second.side;
    cancel(id);
    return addLimit({.id = id, .price = newPrice, .qty = newQty, .side = side});
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

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
