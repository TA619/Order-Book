# Week 1: Order book basics

By the end of Week 1 the project can store orders in a price-sorted book, report the best bid and
ask, and print the book. Nothing trades yet; that's Week 2.

**Files:** `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `include/types.hpp`,
`include/order.hpp`, `include/trade.hpp`, `include/i_order_book.hpp`, `include/map_order_book.hpp`,
`src/map_order_book.cpp`, `src/main.cpp`, `tests/sanity_test.cpp`, `tests/types_test.cpp`,
`tests/order_book_tests.cpp`.

---

## 1. Market basics

### What is an order book?

An exchange keeps a list of everyone who wants to buy or sell a stock and at what price. That list
is the **order book**. It has two sides:

- **Bids**: buy orders. "I'll buy 100 shares at $100.00 or less."
- **Asks** (also called offers): sell orders. "I'll sell 50 shares at $100.05 or more."

```
ASK 10200 qty=7  orders=1
ASK 10100 qty=5  orders=1      <- best ask: lowest price anyone will sell at
---                            <- the spread is the gap between the two
BID 10000 qty=30 orders=2      <- best bid: highest price anyone will buy at
BID 9900  qty=30 orders=1
```

| Term | Meaning |
|---|---|
| Best bid | Highest buy price in the book |
| Best ask | Lowest sell price in the book |
| Spread | best ask − best bid. Small spread = cheap to trade, a "liquid" market |
| Mid price | (best bid + best ask) / 2, the usual estimate of the "fair" price |
| Price level | All orders at one price, e.g. "the 10000 bid level holds 30 shares in 2 orders" |
| Top of book | The best bid and best ask |
| Depth | How many shares sit at each level, and how many levels there are |

### Limit orders vs market orders

- **Limit order**: buy or sell at a given price or better. If it can't trade right away it **rests**
  in the book and waits. Resting orders provide liquidity; their owners are called **makers**.
- **Market order**: trade right now at whatever price is available. It never rests. Its owner is
  the **taker**.

### Price-time priority

When several orders could fill, the exchange picks by:

1. **Price first**: the best price wins (highest bid, lowest ask).
2. **Time second**: at the same price, the order that arrived first fills first (FIFO, "first in,
   first out").

This is why each price level is a **queue**. Your position in the queue is valuable: being first
means you trade first.

### Ticks

Prices move in fixed steps called **ticks** (for US stocks usually $0.01). The project stores
prices as an integer number of ticks, never as a `double` (see section 3).

---

## 2. Project setup (Day 3)

| File | What it does |
|---|---|
| `vcpkg.json` | Lists the libraries (gtest, benchmark). vcpkg downloads and builds them automatically ("manifest mode"). `builtin-baseline` pins exact versions so everyone gets the same ones |
| `CMakeLists.txt` | Describes what to build: the `orderbook` static library, the programs, the tests. `set_project_warnings()` turns on `/W4 /permissive-` so the compiler catches mistakes |
| `CMakePresets.json` | Saved build settings. `msvc` = Visual Studio generator + the vcpkg toolchain file; `debug`/`release` build and test presets |

**Debug vs Release:** Debug has no optimisation and lots of checks (good for finding bugs).
Release uses `/O2` optimisation and can be 10–50× faster. Always benchmark in Release.

**Static library:** the book code is compiled once into `orderbook.lib`, and the app, tests and
benchmarks all link to it, instead of each compiling the same `.cpp` files.

---

## 3. Core types (Day 4): `types.hpp`, `order.hpp`, `trade.hpp`

```cpp
using Price = std::int64_t;      // integer ticks, never double
using Qty = std::int64_t;        // signed on purpose
using OrderId = std::uint64_t;
using Timestamp = std::uint64_t; // sequence number assigned by the book

enum class Side : std::uint8_t { Buy, Sell };
```

### Why integers for prices?

`0.1 + 0.2 == 0.3` is **false** with `double`: most decimals can't be stored exactly in binary. If
two "equal" prices differ in the last bit, they land on two different price levels and the book is
wrong. With integer ticks, 10000 is always exactly 10000. This is one of the most common interview
questions about order books.

### Why is `Qty` signed?

Remaining quantity is computed as `qty - filled`. If a bug makes it go below zero, a signed number
shows `-5`, which is obviously wrong. An unsigned number would silently wrap to about 1.8 × 10^19,
which looks like a huge but valid order. Test `Types.QtyIsSignedSoUnderflowIsVisible` checks this.

### Why is `Timestamp` a counter and not the clock?

The book gives each accepted order the next number (`nextTs_++`). Tests become deterministic (same
result every run), and two orders can never get the same time. Only the order matters, not the
actual time.

### `enum class Side : std::uint8_t`

- `enum class` (not plain `enum`): you must write `Side::Buy`, and it won't silently convert to
  `int`.
- `: std::uint8_t`: stored in 1 byte instead of 4, which keeps `Order` small.
- `opposite(Side)` returns the other side; it's `constexpr` so it can run at compile time.

### `struct Order` and memory layout

```cpp
struct Order {
    OrderId id;      // 8 bytes
    Price price;     // 8
    Qty qty;         // 8, remaining quantity
    Timestamp ts;    // 8
    Side side;       // 1, then 7 bytes of padding
};
static_assert(sizeof(Order) == 40);
```

The CPU wants 8-byte fields at addresses that are multiples of 8 ("alignment"), so the compiler
inserts **padding**. Putting the four 8-byte fields first and the 1-byte `Side` last gives
4×8 + 1 = 33 bytes, padded to 40. Putting `Side` first would add 7 bytes of padding in the middle
as well, for 48 bytes. Smaller structs mean more orders per cache line, which matters later for
speed.

The `static_assert`s check at compile time that `Order` is 40 bytes, **trivially copyable** (can be
copied with a plain memory copy) and an **aggregate** (no constructors, so you can write
`Order{.id = 1, .price = 100, ...}`, the C++20 designated initialisers used everywhere in tests).

`friend bool operator==(const Order&, const Order&) = default;` asks the compiler to generate `==`
comparing every field (C++20). `operator<<` lets GoogleTest print an order when a test fails.

### `struct Trade`

```cpp
struct Trade { OrderId buyId; OrderId sellId; Price price; Qty qty; };  // 32 bytes
```

One fill between a buyer and a seller. The price is always the **resting (maker) order's price**,
explained in Week 2.

---

## 4. The interface (Day 5): `i_order_book.hpp`

```cpp
class IOrderBook {
public:
    virtual std::vector<Trade> addLimit(Order o) = 0;
    [[nodiscard]] virtual std::optional<Price> bestBid() const = 0;
    [[nodiscard]] virtual std::optional<Price> bestAsk() const = 0;
    // ... (more methods added in Weeks 2 and 3)
    virtual ~IOrderBook() = default;
};
```

- **Pure virtual (`= 0`)**: an interface with no code. `MapOrderBook` implements it; a faster
  vector-based book can implement it later, and every test and benchmark runs unchanged on both.
- **Virtual destructor**: deleting a book through an `IOrderBook*` must call the real class's
  destructor. Without it, that's undefined behaviour. A classic interview question.
- **`std::optional<Price>`**: an empty side has no best price. `optional` makes "no value" explicit
  instead of a magic number like 0 or -1, which could be a real price.
- **`[[nodiscard]]`**: the compiler warns if you call `bestBid()` and ignore the result, which is
  almost always a bug.

---

## 5. The map-based book (Days 5–7): `map_order_book.hpp/.cpp`

### The data structures

```cpp
using Level = std::list<Order>;                    // one price level = FIFO queue

std::map<Price, Level, std::greater<>> bids_;      // highest price first
std::map<Price, Level> asks_;                      // lowest price first
std::unordered_map<OrderId, Locator> index_;       // id -> where the order lives
Timestamp nextTs_ = 0;
```

| Structure | Why this one |
|---|---|
| `std::map` per side | A balanced binary search tree (red-black tree) kept sorted by price. Insert/find/erase a level: O(log L), L = number of levels. `begin()` is always the best price |
| `std::greater<>` for bids | Sorts high → low, so `bids_.begin()` is the **highest** bid. Asks use the default `std::less`, so `asks_.begin()` is the **lowest** ask. Same code, "best is first" on both sides |
| `std::list<Order>` per level | A FIFO queue: `push_back` new orders, fill from `front()`. Its iterators stay valid when other elements are inserted or erased, which the index relies on |
| `unordered_map` index | Hash table from order id to `Locator {side, price, list iterator}`: average O(1) lookup. Used by cancel/modify in Week 2 |

(In Week 4 these became `std::pmr::` versions using a memory pool. Same structures, faster
memory.)

### `addLimit` (insert only, in Week 1)

```cpp
if (o.qty <= 0 || index_.contains(o.id)) return {};   // reject bad orders
o.ts = nextTs_++;                                     // time priority
auto& level = bids_[o.price];                         // find or create the level (one lookup)
level.push_back(o);                                   // join the back of the queue
index_[o.id] = {Side::Buy, o.price, std::prev(level.end())};  // remember where it is
```

- Rejections: zero/negative quantity, or an id already resting. A rejected order doesn't use up a
  timestamp (test `RejectedOrdersDontConsumeTimestamps`).
- `operator[]` on a map finds the level or creates an empty one: one tree walk instead of `find`
  and then `insert`.
- `std::prev(level.end())` is an iterator to the element just pushed.

### `bestBid` / `bestAsk`

```cpp
if (bids_.empty()) return std::nullopt;
return bids_.begin()->first;          // ->first is the map key (the price)
```

O(1) because the tree caches its first element. A bug you had here: writing `it.first` instead of
`it->first`. `begin()` returns an iterator, which behaves like a pointer, so you need `->`.

### `printBook(os, depth)`

Prints asks from high to low, then `---`, then bids from high to low, so the spread sits in the
middle like a real trading screen. Asks are stored low → high, so it walks backwards from one past
the last level it will show. The level quantity is summed on the fly: O(depth × orders per level).

### `levelOrders(side, price)` (Day 7)

Returns a copy of one queue, oldest first. It uses `find()`, never `operator[]`, because a
read-only query must never create an empty level. Used by tests to check FIFO order.

---

## 6. Tests (Days 3, 4, 7)

GoogleTest basics: `TEST(Suite, Name) { ... }`, `EXPECT_EQ(a, b)` (keeps going on failure),
`ASSERT_TRUE(x)` (stops the test on failure). `gtest_discover_tests` registers every test with
CTest.

| Group | Tests | What they prove |
|---|---|---|
| Sanity | `OnePlusOne`, `TickTypeIs64Bit` | GoogleTest is wired up |
| Types | `OppositeSide`, `OrderAggregateInit`, `QtyIsSignedSoUnderflowIsVisible`, `TradeEquality`, `Printing` | The basic types behave as designed |
| Insertion | `EmptyBook`, `AddLimitInsertsOrders`, `SamePriceSharesALevel`, `StoredOrderKeepsFields`, `ExtremeAndNegativePrices` | Orders land on the right levels; extreme prices work |
| Validation | `RejectsDuplicateIdAndBadQty`, `DuplicateIdRejectedAcrossSides`, `RejectedOrdersDontConsumeTimestamps` | Bad input changes nothing |
| FIFO | `LevelOrdersReturnsQueue`, `FifoWithinLevel`, `FifoAcrossInterleavedLevels` | Each level keeps arrival order |
| Best prices | `BestPricesEmptyBook`, `BestPricesBothSides`, `BestPricesOneSide`, `BestPricesTrackNewOrders` | Top of book is correct |
| Printing | `PrintEmptyBook`, `PrintBookAggregatesLevels`, `PrintBookRespectsDepth` | The ladder output format |

A trick used in Day 7: deliberately break the code (change `push_back` to `push_front`) and check
that tests fail. Three FIFO tests caught it, so they really test something. This is called
**mutation testing**.

---

## 7. Interview questions

**"Design an order book. Which data structures?"**
A sorted map per side (price → level), a FIFO queue per level, and a hash map from order id to the
order's location for O(1) cancel. Best price is `begin()` of each map.

**"What's the complexity of each operation?"**
Add: O(log L) to find the level + O(1) to append. Best bid/ask: O(1). Cancel (Week 2): O(1) to find
the order + O(log L) to find its level. L = number of price levels, usually small.

**"Why not `double` for prices?"**
Most decimals can't be represented exactly in binary, so equal prices may compare unequal. Use
integer ticks.

**"Why a virtual destructor?"**
So deleting through a base-class pointer runs the derived destructor; otherwise it's undefined
behaviour.

**"Why is `sizeof(Order)` 40 and not 33?"**
Alignment padding: the 8-byte fields need 8-byte alignment, so the struct is rounded up to a
multiple of 8. Field order decides how much padding there is.
