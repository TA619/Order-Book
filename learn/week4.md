# Week 4: Benchmarking, latency and optimisation

Week 4 measures how fast the engine is, finds what's slow, and fixes it with data instead of
guesses. Result: about **4.5 M events/s** on real data, **p50 150–190 ns and p99 ≈ 1 µs** per event,
and a **15–30% speed-up** from a memory pool.

**Files:** `bench/book_bench.cpp` (`bench.exe`), `bench/latency_main.cpp` (`latency.exe`),
`include/map_order_book.hpp` (pool allocator, reserving constructor), `src/replayer.cpp` (fewer
lookups), `scripts/plot_latency.py`, `docs/latency_msft.png`, `README.md`, `CMakeLists.txt`.

---

## 1. Concepts

### Throughput vs latency

- **Throughput**: how many operations per second (e.g. 4.5 M events/s). Matters for replaying
  history or handling bursts.
- **Latency**: how long one operation takes (e.g. 190 ns). Matters for reacting first in a live
  market.

They're related (average latency ≈ 1 / throughput for one thread), but the **average hides the
slow cases**.

### Percentiles

Sort all measured times. **p50** (the median) is the middle one: half the events were faster.
**p99**: 99% were faster, 1% slower. **p99.9**: 1 in 1,000 is slower. Trading firms care most
about the tail (p99, p99.9), because the slow event is often the one that matters (a burst of
activity is when you most need to be fast).

Example from MSFT: mean 261 ns, p50 190 ns, p99 1,042 ns, max 1.4 ms. The mean is pulled up by a
few very slow events; the max is usually the OS interrupting the program and says little about the
code.

### Why benchmarks are hard

- **The optimiser deletes unused work.** If a result is never used, Release builds may skip the
  computation, and you measure nothing. `benchmark::DoNotOptimize(x)` tells the compiler `x` is
  used.
- **Setup must not be timed.** Building a book of 65,536 orders shouldn't count toward the cost of
  one add.
- **Noise.** CPU boost clocks, temperature, other programs. On this laptop timings vary by ~±20%
  between runs. Use repetitions and the **median**, and only compare runs made back to back.
- **Always Release** (`/O2`). Debug can be 10–50× slower and tells you nothing about real speed.

---

## 2. Micro-benchmarks: `bench/book_bench.cpp`

Google Benchmark basics:

```cpp
void BM_Cancel(benchmark::State& state) {
    // setup here is not timed
    for (auto _ : state) {             // the timed loop; the library picks the iteration count
        benchmark::DoNotOptimize(book->cancel(ids[i++]));
    }
    state.SetItemsProcessed(state.iterations());   // prints items_per_second
}
BENCHMARK(BM_Cancel)->Arg(10)->Arg(100)->Arg(1000);  // state.range(0) = number of price levels
```

**The batch pattern used everywhere:** pre-generate 65,536 orders before the loop. Each iteration
does one book call. When the batch runs out, `state.PauseTiming()`, rebuild the book,
`state.ResumeTiming()`. Pausing costs a few microseconds, but it happens once per 65,536
iterations, so it averages out to almost nothing.

| Benchmark | What it measures |
|---|---|
| `BM_AddLimit/levels` | Add a resting order (no trade), with orders spread over 10/100/1000 levels |
| `BM_AddLimitReserved/100` | The same with the index pre-sized (section 5) |
| `BM_Cancel/levels` | Cancel resting orders in random order |
| `BM_ModifyReduce` | Reduce an order's size in place (the cheapest operation, ~30 ns) |
| `BM_MarketSweep/k` | A market order that fills exactly k orders on k levels (1, 5, 20) |
| `BM_MixedFlow` | 50% adds, 45% cancels, 5% market orders: roughly the shape of real data |
| `BM_ReplayLobster/AMZN, MSFT` | The full real day through `Replayer` (skipped if the data isn't downloaded) |

Random numbers use `std::mt19937_64` with fixed seeds, so every run sees the same orders.
`OB_DATA_DIR` is a compile-time path (set in CMake) so the replay benchmark finds the data from any
working directory.

What the numbers say: deeper books (more levels) are slower, because the `std::map` is bigger
(O(log L)) and has more cache misses. A market order that sweeps 20 levels costs ~3.4 µs: each
level removed and each trade recorded costs something.

---

## 3. Per-event latency: `bench/latency_main.cpp`

```
latency <message.csv> <orderbook.csv> [--csv latency.csv]
```

### The timer problem

`std::chrono::steady_clock` on Windows ticks every **100 ns** (it uses QueryPerformanceCounter at
10 MHz). An event takes ~200 ns, so that's far too coarse.

Instead it reads the CPU's **time-stamp counter** with `__rdtsc()` (from `<intrin.h>`). On modern
x86 CPUs it ticks at a fixed rate, here 3.19 ticks per ns. `calibrateTsc()` measures that rate:
read the counter and `steady_clock`, spin for 200 ms, read both again, divide. Two back-to-back
`__rdtsc()` calls cost ~10 ns: the timer's own overhead, which is included in every sample.

### Measuring the right thing (a mistake caught on the way)

The first version timed a replay **without** `reconcile()`. Without it, the book drifts away from
reality (phantom orders), and drift triggers the rare self-healing path, which copies whole queues.
That made the tail look worse than it is: MSFT cancels showed a p99 of 4.8 µs, slower in longer
queues, even though `std::list::erase` doesn't depend on queue length. That gave it away.

The fix: time each `apply()` **inside the validated replay**. File reading and `reconcile()` run
between events, outside the timed region, so every timed event runs against the exact real book.
(Side effect: the file reading also cools the CPU caches between events, a bit like a real system
doing other work.)

### Output

```
latency in ns     count    mean     p50     p90     p99    p99.9       max
all events         668764     261     190     441    1042     5570   1368291
submit             329566     238     190     391     852     1533   1368291
delete             300182     274     180     461    1222     7224    285561
execute             29798     422     311     701    1773     6212    432602
```

Executions are slowest: they go through the matching loop and create a `std::vector<Trade>` (a
heap allocation). `--csv` writes every sample; `plot_latency.py` draws a log-scale histogram with
p50/p99/p99.9 lines (`docs/latency_msft.png`). The plot uses 12 bins per decade because samples
come in ~10 ns steps; finer bins showed a fake comb pattern.

---

## 4. Optimisation 1: the memory pool (kept, 15–30% faster)

**The problem:** every resting order costs two heap allocations (a `std::list` node and an
`unordered_map` node), every new price level a third (a `std::map` node), and freeing them costs as
much again. The general-purpose heap (`malloc`/`new`) is fairly slow: it handles any size, and it
must be thread-safe.

**The fix:** C++17 **polymorphic memory resources** (`<memory_resource>`):

```cpp
std::pmr::unsynchronized_pool_resource pool_;              // declared FIRST

std::pmr::map<Price, Level, std::greater<>> bids_{&pool_};
std::pmr::map<Price, Level> asks_{&pool_};
std::pmr::unordered_map<OrderId, Locator> index_{&pool_};
using Level = std::pmr::list<Order>;
```

- `unsynchronized_pool_resource` keeps a **free list per block size**. A freed node goes on the list,
  and the next allocation of that size reuses it immediately. "Unsynchronized" = no locks, because
  the book is used by one thread.
- `std::pmr::map` etc. are the normal containers with a `polymorphic_allocator` that draws from a
  given resource. They pass it down automatically: the `pmr::list` levels created inside the map
  also use the pool.
- **Member order matters:** members are destroyed in reverse order of declaration. `pool_` is
  declared first, so it's destroyed last, after every container that uses its memory. The other
  way round, the containers would free into a pool that no longer exists.
- The book is now **non-copyable** (`= delete`): the pool owns the memory, and a copy would share or
  duplicate it in confusing ways.

| Operation | Before | After |
|---|---|---|
| Add resting limit order | 209 ns | 147 ns |
| Cancel | 306 ns | 223 ns |
| Market order, 1 fill | 485 ns | 356 ns |
| Mixed flow | 230 ns | 181 ns |
| Full AMZN day | 79.7 ms | 60.9 ms |
| Full MSFT day | 201 ms | 171 ms |

---

## 5. Optimisation 2: `reserve()` (inconclusive, kept as an option)

An `unordered_map` that grows past its load factor **rehashes**: allocates a bigger bucket array and
moves every entry. `MapOrderBook(std::size_t expectedOrders)` calls `index_.reserve(n)` up front so
that never happens. It was faster in 3 paired runs and slower in a 4th: within the noise. In the
real replay the book stays at a few thousand orders, so rehashing is rare anyway. Kept as an
optional constructor, not claimed as a win.

---

## 6. Optimisation 3: fewer hash lookups (kept, 1–8%)

A Delete of a known order used to look up the id **four** times: `apply()` called `find()` to check
it exists, `deleteOrder()` called `find()` for its size, `cancel()` looked it up again, and
`suspect_.erase()` hashed it once more. Now `apply()` does one `find()` and passes the size down:

```cpp
const auto known = book_.find(m.id);    // the one index lookup
...
deleteOrder(m, known->qty);             // reduce(id, have, by) also takes the known size
```

Faster in 6 of 6 back-to-back pairs (1–8%), and validation is still 100%.

---

## 7. Tried and reverted: whole-program optimisation

`/GL` + `/LTCG` (CMake `CMAKE_INTERPROCEDURAL_OPTIMIZATION`) lets the compiler inline across
`.cpp` files. Result: slower in one round, faster in the next, by more than any real effect. It
doesn't help much here anyway, because `Replayer` calls the book through `IOrderBook&` (virtual
calls can't be inlined unless the compiler knows the exact type). Reverted: a change with no
measurable gain isn't worth slower builds.

**This is the core lesson of the week:** change one thing, measure against the previous build in
the same session, keep it only if it pays.

---

## 8. Cleanup and the README

- Debug build + all 78 tests; scanned for leftover debug prints and TODOs.
- Found `std::snprintf` used without `#include <cstdio>`: it compiled only because another MSVC
  header happened to include it. Fixed, so it builds on other compilers too.
- `README.md`: results table, Mermaid architecture diagram (GitHub renders it), design decisions,
  how the replay reached 100%, benchmark tables with hardware details, build/run commands and
  sample output. Recruiters read the first screen, so the key numbers come first.

---

## 9. What's still slow (good answers to "what would you do next?")

| Cost | Why | Fix (Add-on A) |
|---|---|---|
| `std::map` lookups | A tree walk with a cache miss per node | Vector of levels indexed by `(price - minPrice)`: O(1) |
| Finding the next best price | Tree `begin()` is fine, but the level vector needs a search | A bitset of non-empty levels + `std::countr_zero` |
| `std::list` nodes scattered in memory | Cache misses when walking a queue | Orders in a contiguous object pool, linked by index |
| `std::vector<Trade>` returned per call | A heap allocation whenever something trades | Append into a caller-owned, reused buffer |
| Virtual calls through `IOrderBook&` | Can't be inlined | Templates (static polymorphism) on the hot path |

---

## 10. Interview questions

**"How fast is your engine, and how did you measure it?"**
About 4.5 M events/s on real NASDAQ data; per event p50 150–190 ns, p99 ≈ 1 µs. Google Benchmark
for throughput; per-event `rdtsc` timestamps for percentiles, because `steady_clock` on Windows is
only 100 ns resolution.

**"Why report p99 and not the average?"**
The average hides the tail. A trading system is judged by its slow cases, which often coincide with
busy, important moments.

**"What was your biggest optimisation?"**
A memory pool (`std::pmr::unsynchronized_pool_resource`) for all book nodes: freed nodes are reused
from per-size free lists with no locking, instead of going through the general heap. 15–30% faster.

**"Did any optimisation not work?"**
Whole-program optimisation showed no consistent gain, so I reverted it. `reserve()` was within
noise. I only kept changes that won back-to-back comparisons.

**"What does `DoNotOptimize` do?"**
It forces the compiler to treat a value as used, so it can't delete the code being benchmarked.

**"Why must `pool_` be declared before the containers?"**
Members are destroyed in reverse declaration order; the pool must outlive everything allocated
from it.

**"What would you do to make it faster?"**
A tick-indexed vector book with a bitset for the best price, an object pool for orders, no
per-call allocations, and templates instead of virtual calls on the hot path. Then measure it
against this book through the same tests and benchmarks.
