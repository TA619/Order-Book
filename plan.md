# Low-Latency Limit Order Book & Matching Engine (C++) — Learning Plan

## 1. What we're building
A mini stock-exchange engine in C++ that:
1. Accepts orders (limit, market, cancel, modify)
2. Keeps them in an order book sorted by price-time priority
3. Matches buyers and sellers and produces trades
4. Replays real market data (LOBSTER / NASDAQ)
5. Is benchmarked for speed (later: map-based vs tick-vector book)

**Why:** order books are the core of every exchange and HFT firm, and "design an order book" is a
classic quant developer interview question. It uses CP skills directly (data structures, complexity).

## 2. Setup (Windows)
| Tool | How |
|---|---|
| Visual Studio 2022 Community | Workload: "Desktop development with C++" (includes MSVC + CMake) |
| Git + GitHub | git-scm.com, push every day |
| vcpkg | `vcpkg install gtest benchmark` (or CMake `FetchContent`) |
| C++ standard | **C++20** (`std::countr_zero`, `std::optional`, concepts) |
| Python (optional) | pandas + matplotlib for plots |

Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release` → `cmake --build build --config Release` → `ctest --test-dir build -C Release`

## 3. Folder structure
```
orderbook/
├── CMakeLists.txt
├── README.md
├── include/  types.hpp, order.hpp, trade.hpp, i_order_book.hpp,
│             map_order_book.hpp, vector_order_book.hpp (Add-on A), feed_parser.hpp
├── src/      map_order_book.cpp, feed_parser.cpp, main.cpp
├── tests/    order_book_tests.cpp, matching_tests.cpp
├── bench/    bench_orderbook.cpp
├── data/     sample data (gitignored if large)
└── scripts/  plot.py (optional)
```

## 4. Core design
```cpp
using Price   = int64_t;   // in ticks — NEVER double
using Qty     = int64_t;
using OrderId = uint64_t;
enum class Side { Buy, Sell };

struct Order { OrderId id; Side side; Price price; Qty qty; uint64_t ts; };
struct Trade { OrderId buyId, sellId; Price price; Qty qty; };

class IOrderBook {
public:
    virtual std::vector<Trade> addLimit(Order o) = 0;
    virtual std::vector<Trade> addMarket(Side s, Qty q, OrderId id) = 0;
    virtual bool cancel(OrderId id) = 0;
    virtual bool modify(OrderId id, Qty newQty) = 0;
    virtual std::optional<Price> bestBid() const = 0;
    virtual std::optional<Price> bestAsk() const = 0;
    virtual ~IOrderBook() = default;
};
```
Map-based internals:
- `std::map<Price, std::list<Order>, std::greater<>> bids;`
- `std::map<Price, std::list<Order>> asks;`
- `std::unordered_map<OrderId, Locator> index;` (side + price + list iterator → O(1) cancel)

---

## 5. Core project — 4 weeks

### Week 1 — Market basics + basic order book
**📚 Learn before:**
- Bid, ask, spread, mid price, depth, liquidity
- Market makers vs takers, what exchanges and HFT firms do
- Order types: limit, market, cancel, modify
- Price-time priority (best price first, then earliest order)
- Why prices are stored as integer ticks, not `double`
- CMake basics, Git basics

**🔨 Build:**
| Day | Task |
|---|---|
| 1 | Learn the market concepts above (videos + Harris ch. 1–3) |
| 2 | Draw an order book on paper; simulate 10 orders by hand |
| 3 | Project setup: CMake, Git repo, hello-world build in Visual Studio, GoogleTest wired up |
| 4 | `types.hpp`, `Order`, `Trade` |
| 5 | `MapOrderBook::addLimit` — insert only, no matching |
| 6 | `bestBid()`, `bestAsk()`, `printBook()` (top 5 levels) |
| 7 | Tests for insertion, best prices, FIFO order; commit |

**✅ After this week you'll have learned:**
- How a real order book is laid out and why bids/asks sort the way they do
- Choosing STL containers (`std::map` with a custom comparator, `std::list`)
- Setting up a modern CMake C++ project on Windows with tests

### Week 2 — Matching engine
**📚 Learn before:**
- How matching works: crossing the spread, aggressive vs passive orders
- Partial fills, sweeping multiple price levels
- What happens to queue priority on modify (reduce qty keeps place, price change loses it)
- Iterator invalidation rules for `std::list` / `std::map`
- GoogleTest basics (`TEST`, `EXPECT_EQ`, fixtures)

**🔨 Build:**
| Day | Task |
|---|---|
| 1 | Matching loop: incoming buy matches asks while `price >= bestAsk` (and vice versa) |
| 2 | Partial fills; rest the remainder on the book |
| 3 | Market orders (consume liquidity until filled or book empty) |
| 4 | O(1) cancel via the `unordered_map` index |
| 5 | Modify (reduce qty in place; price change = cancel + re-add) |
| 6 | Tests: full fill, partial, multi-level sweep, cancel, empty book, FIFO at same price |
| 7 | `main.cpp` demo: scripted orders → printed trades |

**✅ After this week you'll have learned:**
- Exact exchange matching rules — the core of any interview order-book question
- Designing for O(1) cancel with an index + stable iterators
- Edge-case thinking and test-driven development

### Week 3 — Real market data
**📚 Learn before:**
- LOBSTER data format (message file + orderbook file, event types 1–7)
- What L1 / L2 / L3 market data mean
- Fast file parsing in C++ (`std::ifstream`, `std::from_chars`, avoiding `std::stringstream` overhead)

**🔨 Build:**
| Day | Task |
|---|---|
| 1 | Download LOBSTER free sample (e.g. AAPL, level 10); read the format docs |
| 2 | `FeedParser` for submit / cancel / delete / execute messages |
| 3 | Replay messages into your book |
| 4 | Compare your best bid/ask with LOBSTER's orderbook file; fix bugs |
| 5 | Keep debugging until they match |
| 6 | Print stats: message count, trades, average spread, volume |
| 7 | (Optional) Python plot of spread and mid over time |

**✅ After this week you'll have learned:**
- How real exchange feeds look and behave (bursty, lots of cancels)
- Writing fast parsers
- Debugging against ground truth — a key real-world quant developer skill

### Week 4 — Benchmark, polish, publish
**📚 Learn before:**
- Google Benchmark basics, `benchmark::DoNotOptimize`
- Latency vs throughput; p50 / p99 percentiles
- Debug vs Release builds, MSVC `/O2`
- Visual Studio Performance Profiler (CPU usage)

**🔨 Build:**
| Day | Task |
|---|---|
| 1 | Benchmarks for add / cancel / match |
| 2 | Latency histogram with `std::chrono::steady_clock` → p50 / p99 |
| 3 | Easy wins: `reserve()`, move instead of copy, Release build; re-measure |
| 4 | Code cleanup, naming, comments |
| 5 | README: overview, architecture diagram, design decisions |
| 6 | README: benchmark table, build/run steps, sample output |
| 7 | Push, pin the repo on GitHub, add it to the resume |

**✅ After this week you'll have learned:**
- How to measure performance properly instead of guessing
- Reading a profiler and finding hot spots
- Presenting a technical project (README) the way recruiters want

**📝 Resume line after Week 4:**
> Built a C++20 limit order book and matching engine with price-time priority and O(1) cancels; replayed 1M+
> real NASDAQ (LOBSTER) messages validated against ground truth; benchmarked at X M orders/sec, p99 Y µs.

---

## 6. Optional add-ons (Week 5+, any order; suggested A → B → C)

### Add-on A — Tick-based vector book (1 week) ⭐
- **Learn:** CPU caches, cache lines, contiguous memory, memory pools, bit tricks (`std::countr_zero`)
- **Build:** `VectorOrderBook` (`std::vector<Level>` indexed by `(price - minPrice)`), best-price tracking with a
  bitset, an object pool for orders, the same tests through `IOrderBook`, a map-vs-vector benchmark table
- **You'll learn:** why HFT books use arrays, how to cut allocations on the hot path, how to tell a
  "measured, then optimized" story in interviews

### Add-on B — Multithreading (1 week)
- **Learn:** `std::thread`, `std::atomic`, `memory_order_acquire/release`, false sharing, `alignas(64)`
- **Build:** feed thread → lock-free SPSC ring buffer → engine thread; pin threads with `SetThreadAffinityMask`
- **You'll learn:** lock-free programming and the C++ memory model — very common quant dev interview topics

### Add-on C — Market-making strategy + backtest (1–2 weeks)
- **Learn:** market making, inventory risk, adverse selection, PnL, Sharpe ratio, max drawdown
- **Build:** quote at `mid ± k` with an inventory skew, simulate fills on replayed data, Python plots
- **You'll learn:** how strategies make money and how to evaluate them (researcher side)

### Add-on D — TCP order gateway (1 week)
- **Learn:** Winsock sockets, binary protocol design, serialization
- **Build:** clients send orders over TCP to the engine
- **You'll learn:** networking, how exchanges accept orders

### Add-on E — Dashboard (3–4 days)
- **Build:** a Python or web view of depth, spread and recent trades
- **You'll learn:** data visualization

**📝 Resume line after add-ons:**
> Optimized with a tick-indexed array book + memory pool (Z× faster than `std::map` baseline); added a
> lock-free SPSC pipeline; backtested a market-making strategy (Sharpe S).

---

## 7. Interview questions this project prepares you for
- Design an order book — which data structures and why?
- Why integers for prices instead of `double`?
- How do you make cancel O(1)?
- Map vs array order book — trade-offs?
- What makes code cache-friendly?
- How does a lock-free queue work? What is memory ordering?
- What is price-time priority? What happens to priority on modify?
- How does a market maker make money, and what are its risks?

## 8. Resources
- 📖 *Trading and Exchanges* — Larry Harris (ch. 1–6)
- 📺 CppCon: "When a Microsecond Is an Eternity" — Carl Cook (Optiver)
- 📺 CppCon: "Trading at light speed" — David Gross (Optiver)
- 🌐 lobsterdata.com (free sample data)
- 🌐 GoogleTest and Google Benchmark GitHub READMEs

## 9. Progress checklist
- [ ] Week 1 — basic book
- [ ] Week 2 — matching engine
- [ ] Week 3 — real data replay
- [ ] Week 4 — benchmarks + README + GitHub
- [ ] Add-on A — vector book
- [ ] Add-on B — multithreading
- [ ] Add-on C — strategy
- [ ] Add-on D — TCP gateway
- [ ] Add-on E — dashboard
