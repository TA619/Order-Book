# Learning notes

One file per week of the project. Each explains the concepts, what was built, why each design
choice was made, which file does what, and the interview questions it prepares you for. Read them
in order: each week builds on the last.

| File | Topic | Read it to understand |
|---|---|---|
| [week1.md](week1.md) | Order book basics | Bids/asks, price-time priority, the core types, the map + list + hash-map design |
| [week2.md](week2.md) | Matching engine | How orders trade, market orders, O(1) cancel, modify and replace rules |
| [week3.md](week3.md) | Real market data | LOBSTER data, the replayer, hidden data and `reconcile()`, how 17% became 100% |
| [week4.md](week4.md) | Benchmarking and optimisation | Throughput vs latency, percentiles, the memory pool, what was kept and why |

**If you only have an hour before an interview:** week1 section 7, week2 section 8, week3
sections 6–7 and 10, week4 sections 4 and 10.

Run the code alongside the notes:

```powershell
build\Release\orderbook_app.exe     # Weeks 1-2: watch orders trade step by step
build\Release\tests.exe             # all 78 tests
build\Release\replay.exe  data\AMZN_2012-06-21_34200000_57600000_message_10.csv data\AMZN_2012-06-21_34200000_57600000_orderbook_10.csv
build\Release\latency.exe data\AMZN_2012-06-21_34200000_57600000_message_10.csv data\AMZN_2012-06-21_34200000_57600000_orderbook_10.csv
build\Release\bench.exe
```
