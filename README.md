# Matching Engine

C++23 limit order book with price-time-priority matching.

- Limit and market orders, partial fills, and multi-level matching.
- FIFO within each price level.
- Indexed cancellation using stored level and order iterators.
- Amendments with priority-preserving reductions and priority-losing replacements.
- Owned snapshots, input validation, and invariant checking.

See [build instructions](docs/building.md), [test coverage](tests/README.md),
and [benchmark commands and methodology](benchmarks/README.md).

## Repository structure

```text
.
|-- include/             # Order types and order-book interface
|-- src/                 # Matching and cancellation implementation
|-- tests/               # Unified regression suite and test helpers
|-- benchmarks/          # Timing benchmarks and scan cancellation baseline
|   `-- results/         # Recorded measurements and console logs
|-- docs/                # Build, test, and debugging instructions
`-- CMakeLists.txt       # Library, test, and benchmark targets
```

## Cancellation performance

Measured on an Intel Core i9-12900K, Windows x64, GCC 16.2.0, C++23,
`-O3 -DNDEBUG` (2026-09-17). Both implementations use the same map/list storage.

Selected workloads use 100 price levels per side, equal buy/sell counts, and
shuffled IDs. Results are median batch-average nanoseconds per cancellation
over five samples after one warm-up; setup and verification are excluded.

| Starting orders | Cancellations | Scan (ns/cancel) | Indexed (ns/cancel) |   Speedup |
| --------------: | ------------: | ---------------: | ------------------: | --------: |
|           1,000 |         1,000 |           438.40 |               57.10 |     7.68x |
|          10,000 |        10,000 |         4,294.47 |               49.75 |    86.32x |
|         100,000 |         1,000 |     1,030,746.90 |              154.00 | 6,693.16x |
|         100,000 |        10,000 |       875,568.27 |               92.25 | 9,491.25x |

The smaller workloads drain the book; the 100,000-order cases cancel a sample.
These are single-threaded, in-process measurements, not individual-operation
latency percentiles or end-to-end trading latency.
[Full results and console logs](benchmarks/results/2026-09-17/README.md).
