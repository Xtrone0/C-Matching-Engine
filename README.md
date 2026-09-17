# Matching Engine

C++23 limit order book with price-time-priority matching.

- Limit and market orders, partial fills, and multi-level matching.
- FIFO within each price level.
- Indexed cancellation using stored level and order iterators.
- Amendments with priority-preserving reductions and priority-losing replacements.
- Owned snapshots, input validation, and invariant checking.
- A reusable typed command runner for replay and benchmarks.

See [build instructions](docs/building.md), [test coverage](tests/README.md),
[command runner API](docs/runner.md), and [benchmark commands and methodology](benchmarks/README.md).

## Repository structure

```text
.
| -- include/             # Order types and order-book interface             |
| -------------------------------------------------------------------------- |
| -- tests/               # Unified regression suite and test helpers        |
| -- benchmarks/          # Timing benchmarks and scan cancellation baseline |
| `-- results/         # Recorded measurements and console logs              |
| -- docs/                # Build, test, and debugging instructions          |
`-- CMakeLists.txt       # Library, test, and benchmark targets
```

## Benchmarks

Measured on an Intel Core i9-12900K, Windows x64, GCC 16.2.0, C++23,
`-O3 -DNDEBUG` (2026-09-17). Results use medians of five repetitions after
warm-up. Generation, setup, file loading, verification, and printing are untimed.

### Command throughput

Each workload executes 10,000 commands. Submission runs start empty; the other
families start with 1,000 orders. Batch costs include dispatch, normal result
creation/destruction, and result consumption. Mixed commands include limit orders,
markets, cancellations, amendments, and ordinary rejections.

| Workload           | Median ns/command | Commands/second |
| ------------------ | ----------------: | --------------: |
| Submissions        |            127.53 |    7.84 million |
| Mixed commands     |            172.58 |    5.79 million |
| Cancellation-heavy |             98.62 |   10.14 million |
| Deep noncrossing   |            164.65 |    6.07 million |
| Single price level |             86.35 |   11.58 million |

### Indexed cancellation versus scanning

Both implementations use the same map/list storage. These workloads use 100 price
levels per side, equal buy/sell counts, and identical shuffled cancellation IDs.
Small books drain completely; the 100,000-order cases cancel a sample.

| Starting orders | Cancellations | Scan ns/cancel | Indexed ns/cancel |   Speedup |
| --------------: | ------------: | -------------: | ----------------: | --------: |
|           1,000 |         1,000 |         416.70 |             52.20 |     7.98x |
|          10,000 |        10,000 |       4,668.60 |             48.43 |    96.40x |
|         100,000 |         1,000 |   1,410,742.30 |            187.20 | 7,536.02x |
|         100,000 |        10,000 |   1,286,377.11 |            249.59 | 5,153.96x |

### Sampled command latency

A separate run measured individual commands from dispatch through result creation.

| Workload       | Samples |    p50 |      p95 |      p99 |
| -------------- | ------: | -----: | -------: | -------: |
| Mixed commands |  50,000 | 100 ns | 1,300 ns | 1,400 ns |

Timer readings had 100 ns granularity, and sampling increased median batch cost by **23.5%**.
These are coarse in-process measurements, not end-to-end trading latency.

[Full results, workload mixes, measurement limits, and raw data](benchmarks/results/2026-09-17-complete/README.md)
include every repetition and exact inputs. The measured source state and machine
configuration are recorded. [Earlier cancellation baseline](benchmarks/results/2026-09-17/README.md).

Run the benchmark suite from PowerShell:

```powershell
.\benchmarks\run.ps1
```

See [benchmark options and methodology](benchmarks/README.md) for the 10,000-cancellation
option and reproducing saved workloads.
