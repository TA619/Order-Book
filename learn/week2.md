# Week 2: The matching engine

In Week 2 the book starts trading: an incoming order that crosses the spread fills against resting
orders by price-time priority. Market orders, O(1) cancel, modify and cancel/replace are added too.

**Files:** `include/i_order_book.hpp` (new methods), `include/map_order_book.hpp`,
`src/map_order_book.cpp` (`matchAgainst`, `unlink`, `addMarket`, `cancel`, `modify`, `replace`),
`src/main.cpp` (scripted demo), `tests/matching_tests.cpp`, `tests/cancel_modify_tests.cpp`.

---

## 1. Concepts

### When does an order trade?

A buy order **crosses** when its price is ≥ the best ask; a sell order crosses when its price is ≤
the best bid. Then it trades immediately instead of resting.

```
Book:  ASK 10200 x7   ASK 10100 x5   |   BID 10000 x30
New:   BUY 15 @ 10200
  -> fills 5 @ 10100 (best ask first)
  -> fills 7 @ 10200 (next level)
  -> 3 left and the ask side is now empty, so the 3 rest as a BID at 10200
```

This is called **sweeping** levels. The leftover becoming a resting order is normal for a limit
order.

### Which price does the trade happen at?

Always the **resting (maker) order's price**. In the example the buyer was willing to pay 10200 but
got 5 shares at 10100. The incoming order gets the better price (**price improvement**) because the
resting order was there first and set its price publicly.

### Partial fills

If the resting order is bigger than the incoming order, it's **partially filled**: its quantity
goes down, and it **keeps its place** at the front of the queue.

### Market orders

A market order trades at any price until it's filled or the other side is empty. Whatever is left
is **dropped** (it never rests), because a resting order needs a price.

### Cancel, modify, replace

| Action | What changes | Queue position |
|---|---|---|
| Cancel | Order removed | gone |
| Modify down (smaller qty) | Quantity reduced in place | **kept** |
| Modify up (bigger qty) | Quantity increased | **lost**: moves to the back |
| Replace (new price and/or qty) | Cancel + add again | **lost**, and it may trade at the new price |

Why does increasing lose priority? Otherwise you could join the queue with 1 share early and grow
to 10,000 later, jumping everyone who arrived in between. Real exchanges work this way.

---

## 2. The matching loop: `matchAgainst`

```cpp
template <class OppositeSide>
void MapOrderBook::matchAgainst(Order& taker, OppositeSide& opposite, std::vector<Trade>& trades) {
    const bool takerBuys = taker.side == Side::Buy;
    while (taker.qty > 0 && !opposite.empty()) {
        auto lvlIt = opposite.begin();                        // best opposite level
        if (opposite.key_comp()(taker.price, lvlIt->first)) {
            break;                                            // doesn't cross: stop
        }
        Level& level = lvlIt->second;
        Order& resting = level.front();                       // oldest order fills first

        const Qty fill = std::min(taker.qty, resting.qty);
        trades.push_back({.buyId = takerBuys ? taker.id : resting.id,
                          .sellId = takerBuys ? resting.id : taker.id,
                          .price = lvlIt->first,              // resting price
                          .qty = fill});
        taker.qty -= fill;
        resting.qty -= fill;

        if (resting.qty == 0) {
            index_.erase(resting.id);   // read the id BEFORE pop_front frees the node
            level.pop_front();
        }
        if (level.empty()) {
            opposite.erase(lvlIt);      // remove the empty price level
        }
    }
}
```

### Why a template?

A buy matches against `asks_` (type `std::map<Price, Level>`) and a sell against `bids_` (type
`std::map<Price, Level, std::greater<>>`). Different comparators make them **different types**, so
one normal function can't accept both. A template is compiled once for each type, so one body
serves both sides with no runtime cost.

### The `key_comp()` trick

`key_comp()` returns the map's comparator: `std::less` for asks, `std::greater` for bids.
`key_comp()(taker.price, levelPrice)` is true exactly when the level is **worse** than the taker's
limit:

- Buy taker vs asks (`less`): `taker.price < askPrice` means the ask is too expensive. Stop.
- Sell taker vs bids (`greater`): `taker.price > bidPrice` means the bid is too low. Stop.

One line handles both sides. The draft had this inverted, a bug that would have traded at prices
the taker never agreed to.

### Bugs found in the first draft (good to remember)

1. **Inverted comparison** (above).
2. **No `pop_front`**: filled orders stayed in the queue with qty 0.
3. **Buy/sell ids swapped** when the taker was a seller.
4. Mixing up the list with an iterator to it.
5. Order of operations: `index_.erase(resting.id)` must come **before** `pop_front()`. After
   `pop_front()` the `resting` reference points to freed memory ("dangling reference").

### `addLimit` now

1. Reject bad orders; assign `ts`.
2. **Aggressive part:** `matchAgainst(o, asks_ or bids_, trades)`.
3. If `o.qty == 0`, fully filled: return the trades.
4. **Passive part:** rest the remainder on its own side (Week 1 code).

---

## 3. `addMarket`

```cpp
Order taker{.id = id,
            .price = s == Side::Buy ? std::numeric_limits<Price>::max()
                                    : std::numeric_limits<Price>::min(),
            .qty = q, .ts = nextTs_++, .side = s};
matchAgainst(taker, s == Side::Buy ? asks_ : bids_, trades);   // (written as an if/else)
// leftover is dropped: never rests
```

A market order is a limit order with an impossible price. A buy at the maximum `int64_t` crosses
every ask, so `matchAgainst` only stops when the order is filled or the asks run out. This reuses
the matching loop instead of writing a second one.

---

## 4. O(1) cancel: `cancel` and `unlink`

```cpp
bool MapOrderBook::cancel(OrderId id) {
    auto found = index_.find(id);                // O(1) average: hash lookup
    if (found == index_.end()) return false;
    const Locator loc = found->second;           // COPY: the erase below invalidates `found`
    index_.erase(found);
    if (loc.side == Side::Buy) unlink(bids_, loc.price, loc.it);
    else                       unlink(asks_, loc.price, loc.it);
    return true;
}

template <class SideMap>
void MapOrderBook::unlink(SideMap& side, Price px, Level::iterator it) {
    auto lvl = side.find(px);       // O(log L)
    lvl->second.erase(it);          // O(1): unlink one list node, no search
    if (lvl->second.empty()) side.erase(lvl);
}
```

**This is why the index stores a `std::list` iterator.** Without it, cancelling means scanning the
level for the id: O(orders at that price). With it, the node is removed directly.

**Why `std::list` and not `std::vector`?** Erasing from the middle of a vector shifts every later
element, and that changes their addresses, so all stored iterators would break. A list node never
moves, so iterators to other orders stay valid after any insert or erase (test
`IteratorsStayValidAcrossOtherChanges`).

The cost: list nodes are scattered in memory, which is slower for the CPU cache. The tick-indexed
vector book (Add-on A) addresses this.

---

## 5. `modify` and `replace`

```cpp
bool MapOrderBook::modify(OrderId id, Qty newQty) {
    if (newQty <= 0) return false;              // use cancel to remove
    auto found = index_.find(id);
    if (found == index_.end()) return false;
    Order& o = *found->second.it;
    if (newQty <= o.qty) { o.qty = newQty; return true; }   // in place, keeps priority, O(1)

    o.qty = newQty;
    o.ts = nextTs_++;                           // new time priority
    Level& level = /* find the order's level */;
    level.splice(level.end(), level, found->second.it);     // move node to the back
    return true;
}
```

`splice` moves a list node to another position **without copying or reallocating** it. The node's
address doesn't change, so the iterator stored in `index_` stays valid.

`replace(id, newPrice, newQty)` = remember the side, `cancel(id)`, then
`addLimit({id, newPrice, newQty, side})`. Going through `addLimit` means it gets a fresh
timestamp and goes through matching again, because the new price may cross the spread (test
`ReplaceAcrossSpreadTrades`). Bad input is rejected **before** cancelling, so a failed replace
leaves the book unchanged.

---

## 6. The scripted demo: `src/main.cpp`

`orderbook_app.exe` builds a small two-sided book, then runs an aggressive buy that sweeps two
levels, a market sell, a passive sell, a cancel, a modify, a replace and a cancel of an unknown id,
printing the trades and the book after each step. Run it to see everything from this week at once.

---

## 7. Tests

| File | Tests | What they prove |
|---|---|---|
| `matching_tests.cpp` | `FullFillOneRestingOrder`, `TradesAtRestingPrice`, `SweepsLevelsAndRestsRemainder`, `FifoAndPartialFillOfRestingOrder`, `NoCrossRestsBothSides`, `SellTakerHitsBidsHighestFirst` | Price-time priority, maker price, sweeping, partial fills, both sides |
| `matching_tests.cpp` | `MarketOrder.SweepsLevelsAtRestingPrices`, `LeftoverIsDroppedNotRested`, `SellHitsHighestBidsFirst`, `EmptyBookNoTrades`, `RejectsBadQtyAndDuplicateId` | Market orders never rest |
| `cancel_modify_tests.cpp` | `CancelRemovesOrderAndKeepsOthersInOrder`, `CancelFrontAndBack`, `CancelTwiceFailsSecondTime`, `CancellingLastOrderRemovesLevel`, `CancelledOrderIsNotMatched`, `CancelledIdCanBeReused`, `IteratorsStayValidAcrossOtherChanges`, `FilledOrderCannotBeCancelled`, `UnknownIdReturnsFalse` | Cancel works anywhere in a queue and never corrupts other orders |
| `cancel_modify_tests.cpp` | `ReduceKeepsQueuePosition`, `IncreaseLosesQueuePosition`, `SameQtyIsANoOp`, `ModifyRejectsBadInput`, `MovedOrderCanStillBeCancelled`, `MatchingUsesModifiedQty` | Modify priority rules |
| `cancel_modify_tests.cpp` | `ReplaceMovesToNewPrice`, `ReplaceAtSamePriceLosesPriority`, `ReplaceAcrossSpreadTrades`, `ReplaceRejectsBadInputWithoutChanges`, `EmptyBook.EveryOperationIsSafe` | Replace rules, empty-book safety |

`cancel_modify_tests.cpp` uses a **test fixture** (`TEST_F(BookWithQueue, ...)`): a class whose
`SetUp()` builds the same starting book (three bids at 100, one ask at 105) before each test, so
tests don't repeat setup code.

---

## 8. Interview questions

**"Walk me through matching an incoming buy order."**
While it has quantity left and the best ask is at or below its limit: fill against the oldest order
at the best ask, at that ask's price; remove filled orders and empty levels. Rest any remainder as
a bid.

**"At what price does a trade happen?"**
The resting order's price. The aggressor can get price improvement.

**"How do you make cancel O(1)?"**
A hash map from order id to an iterator into the level's linked list, so the node is unlinked
directly. It must be a container whose iterators stay valid when other elements change
(`std::list`); a vector would invalidate them.

**"What happens to queue priority on modify?"**
Reducing size keeps it; increasing size or changing price loses it (the order goes to the back).

**"Why is the matching loop a template?"**
The two sides are maps with different comparators, so different types; the template lets one
body serve both, with `key_comp()` giving the side-correct crossing test.

**"What's a dangling reference? Where could you get one here?"**
A reference to memory that has been freed. `resting` refers to a list node; after `pop_front()`
that node is gone, so its id has to be read first.
