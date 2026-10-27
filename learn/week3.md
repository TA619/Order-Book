# Week 3: Replaying real market data

Week 3 feeds a full day of real NASDAQ order events into the matching engine and proves the book is
right: after every one of **938,511 events** (AMZN + MSFT, 21 June 2012), the book's top 10 price
levels on both sides equal the exchange's real book, **100% of the time**.

Real data is messy: half the information is missing, events arrive in bursts, and about 10 orders
are cancelled for every trade. Making the book match anyway is the real skill this week teaches.

| Result | AMZN | MSFT |
|---|---|---|
| Events replayed | 269,747 | 668,764 |
| Rows matching the real book on all 10 levels | 100.00% | 100.00% |
| Shares traded | 810,754 | 10,824,421 |
| VWAP | $222.63 | $30.50 |
| Time-weighted spread | 7.4 cents | 1.0 cent |

If you only have 30 minutes: read sections 1, 6 and 7.

---

## 1. Market data concepts

| Level | What you see | Example |
|---|---|---|
| L1 (top of book) | Best bid and best ask with their sizes | bid $223.75 x 74, ask $223.96 x 306 |
| L2 (market by price) | Total size per price, top N prices | ask $223.96: 306 shares, $223.99: 100 shares, ... |
| L3 (market by order) | Every individual order and every change to it | order 16202496: sell 286 @ $223.96, then cancelled |

The engine works at L3 (individual orders). LOBSTER gives L3 events plus L2 snapshots, exactly
what's needed to check it.

### LOBSTER files

LOBSTER (lobsterdata.com) rebuilds NASDAQ's book from exchange data. One stock-day is two CSV files
with the same number of rows. **Row k of the orderbook file is the book right after the event in
row k of the message file.** That lockstep is what makes ground-truth checking possible.

**Message file**, 6 columns:

```
Time,Type,OrderId,Size,Price,Direction
34200.18960767,1,11885113,21,2238100,1
34200.39141294,4,14585251,100,2239500,-1
```

- Time: seconds after midnight. 34200 = 9:30:00 (the open), 57600 = 16:00 (the close).
- Price: dollars × 10,000. 2238100 = $223.81. Already an integer tick, so it goes straight into
  `Price`.
- Direction: 1 = buy, -1 = sell. For an execution, it's the side of the **resting** order that
  was hit.

**Orderbook file**: 4 columns per level, best first: `AskPx1, AskSz1, BidPx1, BidSz1, AskPx2, ...`
(40 columns for level 10). An empty level is price 9999999999 (ask) or -9999999999 (bid), size 0.

### The 7 event types

| Type | Name | Meaning | What the replayer does |
|---|---|---|---|
| 1 | Submit | New visible limit order | `addLimit` |
| 2 | Cancel | Partial cancel; Size = shares removed | `modify` to a smaller size |
| 3 | Delete | Full cancel; Size = what it had left | `cancel` |
| 4 | Execute | Visible resting order traded; Size = shares filled | market order through the engine |
| 5 | Hidden execute | A hidden order traded | counted only |
| 6 | Cross | Auction trade | counted only |
| 7 | Halt | Trading halted / resumed | counted only |

**Ground truth** = data you trust and test your output against (here, the orderbook file).
**Cancel-to-trade ratio** = cancels / trades: 11 for AMZN, 9 for MSFT. Most orders never trade;
market makers keep cancelling and re-quoting.

---

## 2. File map

```
Message file  --> parseMessage  --> Replayer::apply --> MapOrderBook (your engine)
Orderbook file --> parseSnapshot --> reconcile + compareBook --(fix hidden levels, check)--> MapOrderBook
                                            |
                                            v
                                   replay.exe report --> plot_spread.py --> PNG
```

| File | What it does |
|---|---|
| `include/i_order_book.hpp` | Adds `LevelInfo` and 4 read-only queries: `find`, `frontOrder`, `levelOrders`, `depth` |
| `include/map_order_book.hpp`, `src/map_order_book.cpp` | Implements those queries (+ `topLevels` template helper) |
| `include/lobster.hpp`, `src/lobster.cpp` | The parser: CSV line → `Message` or `Snapshot`; `loadMessages` reads a whole file |
| `include/replayer.hpp`, `src/replayer.cpp` | `Replayer` (events → book calls, hidden-data fixes) and `compareBook` |
| `src/replay_main.cpp` | `replay.exe`: whole-day replay, validation, stats, timing, `--csv` |
| `tests/lobster_tests.cpp`, `tests/replayer_tests.cpp` | 22 new tests |
| `scripts/download_data.ps1` | Downloads the data (LOBSTER now needs an account, so it uses public GitHub copies) |
| `scripts/plot_spread.py` | Mid price + spread plot → `docs/*.png` |

The data (~270 MB) lives in `data/` and isn't committed (`.gitignore` excludes `data/*.csv`).

---

## 3. New book queries

```cpp
struct LevelInfo { Price price; Qty qty; std::size_t orders; };

std::optional<Order> find(OrderId id) const;               // one order, current qty
std::optional<Order> frontOrder(Side s) const;             // the order that fills next
std::vector<Order> levelOrders(Side s, Price px) const;    // one queue, oldest first
std::vector<LevelInfo> depth(Side s, std::size_t n) const; // top n levels = L2
```

| Function | How | Cost | Used for |
|---|---|---|---|
| `find` | `index_` lookup, follow the iterator | O(1) | "do we know this order, what's left?" |
| `frontOrder` | `begin()` of the side's map, then `.front()` of the list | O(1) | priority check before an execution |
| `levelOrders` | `find()` the level, copy the list | O(log L + orders) | choosing which order loses shares when the data doesn't say |
| `depth` | walk the first n levels, sum each list | O(n × orders per level) | comparing with the snapshot |

They're in the interface so any future book can be replayed and validated the same way. They return
copies (`std::optional<Order>`), so callers can't modify the book through them. `topLevels` is a
template for the same reason as `matchAgainst`: bids and asks are different map types.

---

## 4. The parser (`lobster.hpp/.cpp`)

```cpp
enum class EventType : std::uint8_t { Submit = 1, Cancel, Delete, Execute, ExecuteHidden, Cross, Halt };
struct Message  { std::int64_t timeNs; EventType type; OrderId id; Qty size; Price price; Side side; };
struct Snapshot { std::vector<LevelInfo> asks, bids; std::size_t levels = 0; };
```

- **Enum values equal LOBSTER's numbers**, so converting is one `static_cast` after checking 1–7.
- **`Snapshot::levels`** = levels per side in the file (10). If a row shows fewer than 10 asks, the
  ask side really has fewer, and our book must not have more.
- **`std::from_chars`** reads a number straight from a character range: no memory allocation, no
  locale, no exceptions. Several times faster than `std::stringstream`. The `Fields` struct is a
  cursor over one line; each `nextInt<T>()` reads a field and skips the comma; any failure sets
  `ok = false` and the caller checks once at the end.
- **Time as integer nanoseconds**: "34200.18960767" is read as seconds + up to 9 decimal digits,
  padded: 34,200,189,607,670 ns. Same rule as prices: exact values stay integers.
- **Rejects**: missing or extra fields, letters, type outside 1–7, direction other than ±1. Strips a
  trailing `\r` (Windows line endings). `loadMessages` throws with the file name and line number.
- `std::getline` reuses one `std::string`, so memory is allocated once, not per line.

---

## 5. The Replayer (`replayer.cpp`)

`Replayer` holds an `IOrderBook&`. `apply(message)` turns each event into a book call and counts it
in `ReplayStats`.

- **Submit** → `addLimit`. A Submit should never trade: on the real exchange, a crossing order's
  trades are reported as Execute events. If it does trade in our book, `unexpectedTrades` goes up
  (a sign the book is wrong).
- **Delete** → `cancel` (via `deleteOrder`). **Cancel** → `modify` down (via `partialCancel`).
- **Execute** → `execute()`:
  1. Ask `frontOrder(side)`: which order would **my** engine fill first?
  2. If it's the executed order, send an opposite **market order** through `addMarket`, so your
     matching loop does the fill. (`engineTrades`: 8,346 AMZN, 28,442 MSFT.)
  3. If not, FIFO disagrees with the exchange: count a `priorityMismatch` and reduce the order
     directly, so the book stays right.

  This checks price-time priority against real fills. It disagreed on 15 of 8,974 AMZN executions
  and 718 of 29,798 MSFT ones: the exchange filled a newer order while an older visible one at the
  same price stayed. Self-match prevention (an exchange won't trade a firm with itself) is a likely
  reason, but the data doesn't identify firms, so it can't be proven.
- **Types 5, 6, 7** only update counters.

**Ids that can't collide:** the replayer creates its own orders (market orders, placeholders).
LOBSTER ids are below 2^32, so placeholders start at 2^62 (`kSyntheticBase`) and market orders at
2^63 (`kTakerBase`).

**`compareBook(book, snapshot)`** returns `top` (best bid and ask match in price AND size) and
`all` (every level in the snapshot matches). It asks for one level more than the snapshot shows, to
catch an extra level when the snapshot says a side has fewer than 10.

---

## 6. The invisible-data problem and `reconcile()`

**The message file doesn't contain every event**, so applying messages alone drifts away from the
real book. The core idea: work out exactly which parts of the book the data **could not** have told
you about, fix only those from the snapshot, and leave everything else to the engine, so the test
stays honest.

### Two things the message file never shows

1. **Orders resting before 9:30.** The first snapshot shows them as totals per price, with no ids.
2. **Anything deeper than level 10.** A level-10 message file only has events that touch the top 10
   levels. An order pushed to level 12 can be cancelled there without a message. Our book keeps it
   as a **phantom**, which later shows up as fake liquidity (the 7,033 "unexpected crosses" of the
   first run).

### The visible window

```
 Level 1  (best ask)  ┐
 ...                  │  Visible window: every change here arrives as a message,
 Level 10 (edge)      ┘  so the engine alone must get it right. reconcile() never touches it.
 - - - - - - - - - - -   <- the previous snapshot's 10th level
 Level 11             ┐  Hidden: orders can vanish with no message.
 Level 12             ┘  reconcile() fixes these from the next snapshot row.
```

A level can only change unseen if it was **outside the window in the previous row**. If the
previous row showed fewer than 10 levels on a side, the whole side was visible.

After applying event k, `reconcile(&row[k-1], row[k])` compares the book with row k **only at
prices that were hidden in row k-1**, and makes the book agree there. Test
`Reconcile.NeverTouchesLevelsThatWereVisible` guarantees visible levels are never touched.

It shrinks both sides first, then grows: growing first could add an order that crosses a stale
level on the other side and trades with it.

### Synthetic orders

Shares with no known id go into one **synthetic** order per price (`addSynthetic`; `synthetic_`
maps price level → its synthetic id). Seeding at 9:30 is just `reconcile(nullptr, row 0)`, with
everything treated as hidden. A synthetic order stands for several real orders with unknown queue
positions, so FIFO checks involving one count as `seedReorders` (expected).

### Unknown ids are applied to their level

A message can name an order never seen (pre-open, or submitted out of view). Its shares are part of
the level total, so `reduceLevel(side, price, size)` takes them from the level (`unknownOrders`:
7,000 AMZN). Only with no level at all is the message dropped (`droppedMessages`: 0 for both).

### Which order loses the shares? (`reduceLevel`)

1. The level's synthetic order.
2. A **suspect** (an order that has been out of view, so may be stale) whose size is **exactly**
   the amount missing. `markNewlyHidden()` adds orders to `suspect_` when their level leaves the
   window.
3. Other suspects, newest first (most orders are cancelled within seconds).
4. Orders that were always visible, newest first (known to be right, so last).

### Self-healing from the message's own numbers

- A **Delete**'s Size is the order's exact remaining size. If our copy differs, an earlier guess
  was wrong; `deleteOrder()` fixes the level by the difference.
- An **Execute** or partial **Cancel** bigger than what our copy has left means the same thing;
  `coverShortfall()` takes the rest from the level.

Each fix counts as a `hiddenCorrection`. It uses only what the messages say, not the snapshot.

---

## 7. How 17% became 100%

The method every time: find the **first** row where the books disagree, look at the events around
it, explain it, fix it, rerun. `replay.exe` prints that first mismatch side by side.

| Step | Fix | Best bid/ask match | All 10 levels |
|---|---|---|---|
| 1 | Naive replay | 17.4% | 0.1% |
| 2 | FIFO check before fills (`frontOrder`) | 17.4% | 0.1% |
| 3 | Reconcile hidden levels | 46.2% | 12.9% |
| 4 | Unknown ids hit their level | 91.8% | 78.9% |
| 5 | Track out-of-view orders (`suspect_`) | 91.8% | 78.9% |
| 6 | Exact size, newest first | 98.7% | 95.3% |
| 7 | Delete repairs its level | 99.8% | 99.3% |
| 8 | Execute/cancel shortfall | 100.0% | 100.0% |

1. **Naive replay**: first mismatch at event 44. Top 3 levels right, level 10 wrong: a level never
   seen came into the top 10.
2. **FIFO check**: at 9:30:00.19 the opening auction re-reports dozens of orders, and the exchange
   filled them interleaved with pre-open orders. Added `frontOrder()` so only a true front-of-queue
   fill goes through the engine. Correct, but the rate didn't move.
3. **Reconcile**: at event 1,413 our book had a 100-share ask the real book didn't. It had dropped
   out of the top 10 at event 31 and been cancelled unseen. Discovery: the file only covers the top
   10 levels.
4. **Unknown ids**: one order cancelled unseen and another submitted unseen at the same price and
   size, so the total matched. Later the exchange deleted the unseen one and we dropped the message.
5. **Suspects**: groundwork; the next mismatch had another cause.
6. **Exact size, newest first**: a level flickering at the window edge; 100 shares were taken from
   a 1,300-lot instead of the 100-lot that was really cancelled.
7. **Delete repairs**: a delete's true size reveals an earlier wrong guess.
8. **Shortfall**: at event 63,839 the exchange executed 100 shares of an order our copy had only 50
   of.

Then **MSFT**, which played no part in debugging and has 2.5× more events, matched 100% on the first
run: the fixes are general, not tuned to one file.

**Lesson:** with ground truth, debug from the **first** divergence. Later errors are often knock-on
effects.

---

## 8. The replay tool (`replay_main.cpp`)

```
replay <message.csv> <orderbook.csv> [--csv out.csv]
```

1. **Load**: all events into a `std::vector<Message>`; the orderbook file is streamed one row at a
   time (170 MB for MSFT). Row 0 seeds; replay starts at event 1.
2. **Validation pass**: per event, `apply`, read the row, `reconcile`, `compareBook`; count matches;
   print the first mismatch; collect stats. `std::swap(prev, snap)` reuses snapshot memory.
3. **Timing pass**: a fresh book replays the in-memory events alone (no file, no compare, no
   reconcile) to measure the engine.

| Stat | Computed as | Why it matters |
|---|---|---|
| Volume | sum of executed sizes (visible + hidden) | how much traded |
| VWAP | sum(price × size) / volume | the standard benchmark for judging execution quality |
| Spread per event | mean of (ask − bid) after each event | cost of crossing the spread |
| Time-weighted spread | each spread weighted by how long it lasted | a 1 µs blip shouldn't count like a 5 s quote |
| Cancel-to-trade | (cancels + deletes) / executions | how much is quoting vs trading |

`--csv` writes at most one row per second (`time_s, bid, ask, mid, spread`) for
`plot_spread.py`. The AMZN plot shows the spread widest right after the open: uncertainty is highest
then, so market makers quote wider.

---

## 9. Tests (22 new)

| Test | Proves |
|---|---|
| `ParseMessage.SubmitBuy` | every field; time padded to 9 decimals |
| `ParseMessage.SellAndOtherTypes` | -1 = Sell; types 4, 5, halt row parse |
| `ParseMessage.IntegerSecondsAndWindowsLineEnding` | no-dot time; `\r\n` ignored |
| `ParseMessage.RejectsMalformedLines` | all bad inputs rejected |
| `ParseSnapshot.TwoLevels` / `DropsEmptyLevels` / `RejectsIncompleteLevel` | snapshot layout, dummy levels, bad rows |
| `LoadMessages.ReadsFileAndReportsBadLine` | real file; throws on bad line or missing file |
| `BookQueries.FindReturnsCurrentState` / `DepthIsBestFirstAndAggregated` | the new queries |
| `Replayer.SubmitCancelDelete` | types 1–3 mapping |
| `Replayer.ExecuteGoesThroughMatchingEngine` | front-of-queue execution produces an engine trade |
| `Replayer.ExecutionOutOfFifoOrderIsCounted` | out-of-order execution counted |
| `Replayer.HiddenAndAuctionTradesDontTouchTheBook` | types 5–7 only count |
| `Replayer.UnknownIdsHitTheSeededLevel` / `NewOrdersQueueBehindSeededOrders` | pre-open orders |
| `CompareBook.MatchAndMismatch` / `ExtraLevelBeyondFullSnapshotIsFine` | the ground-truth check |
| `Reconcile.FixesLevelsThatWereOutOfView` | hidden levels fixed |
| `Reconcile.NeverTouchesLevelsThatWereVisible` | the honesty rule |
| `Reconcile.UnseenCancelTakesFromOutOfViewOrderOfThatSize` | the exact-size rule |
| `Reconcile.DeleteWithDifferentSizeRepairsTheLevel` | delete self-healing |

---

## 10. Interview questions

**"How do you know your order book is correct?"**
I replayed 938K real NASDAQ events and compared my book with the exchange's real top 10 levels
after every event: 100% match on both stocks. Executions go through my own matching engine, which
also checks price-time priority against real fills.

**"What was the hardest bug?"**
The data was incomplete: a level-10 file only reports events in the top 10 levels, so orders
cancelled deeper become phantoms. I reconcile only levels that were out of view in the previous
snapshot, so visible levels still test the engine honestly. 17% → 46%, then attribution fixes to
100%.

**"L1 vs L2 vs L3?"**
Best bid/ask; size per price level; every individual order.

**"How do you parse a big file fast?"**
`std::from_chars` (no allocation, locale or exceptions) and one reused line buffer.

**"Does the exchange always follow price-time priority?"**
Not always, as far as the data shows: 2.4% of MSFT executions skipped an older order. Self-match
prevention is a likely reason. The replayer detects and counts these.

**"Why is the spread wide at the open?"**
Uncertainty is highest while the market finds the price; market makers widen quotes to protect
themselves.

**"What does a cancel-to-trade ratio of 11 tell you?"**
Most orders never trade, so cancel must be fast. That's why O(1) cancel matters.
