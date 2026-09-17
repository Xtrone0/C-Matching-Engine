# Timing benchmarks

Keep timing benchmarks in this folder. Correctness scenarios and regression tests
live in `tests/`; CTest does not run benchmarks.

## Run

From the repository root in PowerShell, with the Release build configured as
described in [Building](../docs/building.md):

```powershell
cmake -S . -B build/release
cmake --build build/release --target order_book_benchmark
.\build\release\order_book_benchmark.exe
```

Use Release for performance measurements. Debug and Checked builds can run the
executable, but their timings are not Release performance results.

The default cancels 1,000 orders in each 100,000-order workload. To cancel 10,000
instead:

```powershell
.\build\release\order_book_benchmark.exe --large-cancels 10000
```

`--large-cancels` accepts `1000` or `10000`; it only changes the 100,000-order
workloads. Use `--help` to display usage. Invalid arguments exit with code 2
before running any benchmarks.

## Implemented: random order submissions

`order_book_benchmark.cpp` submits 10,000 deterministic random orders to a fresh
book for each of five runs. It reports average, minimum, and maximum elapsed
microseconds, plus throughput calculated from the average time.

Order generation and book construction happen before timing; the measured loop
includes submission, matching, and resting-order insertion. It uses ordinary
`OrderBook`, so per-operation invariant scans are excluded. This is the benchmark
extracted from the original scenario test executable, with its workload and
measurements preserved.

## Scan versus indexed cancellation

The scan baseline is available in `scan_order_book.hpp`. It restores the original
`cancelSide` and `cancel` algorithm from commit `ee73fdc`, including the price-level
reference loop, `std::find_if`, and erasing an empty level by price. The old
`usedids.erase` now uses `active.erase` to maintain the current index.

Use `ScanOrderBook` for the scan baseline and `OrderBook` for indexed cancellation.
Both support the same submission and snapshot calls. They share current storage
and matching behavior, so this compares cancellation algorithms, not two complete
historical engine versions. Call `scan.cancel(id)` on the concrete `ScanOrderBook`
type; cancellation is not virtual, so calling through `OrderBook&` selects the
indexed implementation.

The cancellation benchmark runs automatically after the submission benchmark:

- Workloads contain 1,000, 10,000, or 100,000 orders, equally split between buys and sells,
  with either 1 or 100 price levels per side. Prices do not cross.
- Active IDs are shuffled with seed 7. The two smaller books cancel every order;
  the 100,000-order books cancel the first 1,000 shuffled IDs by default, or
  10,000 with `--large-cancels 10000`. Each algorithm receives identical orders
  and cancellation IDs. Output includes starting order count and cancellation count.
- Each sample populates a fresh book before timing. The timer includes only the
  cancellation loop and accumulation of successful results. Setup, shuffling,
  invariant checks, snapshots, printing, and book destruction are excluded.
- After timing, checks require every cancellation to succeed, canceled IDs to be
  absent, remaining order count to be correct, survivor fields and priorities to
  be unchanged, and invariants to hold. Fully drained workloads must be empty.
  A failed check exits the executable with failure.
- One warm-up pair is discarded, followed by five measured pairs. Execution
  order alternates between scan-first and indexed-first.
- Output shows median batch-average nanoseconds per cancellation and the ratio
  of scan median to indexed median. Small workloads measure a full-book drain;
  large workloads measure a fixed sample from a nearly full book. They are not
  individual-operation latency percentiles, and the two workload types should
  be distinguished when comparing results.

The maximum book size is 100,000 orders. The large-book workload samples
cancellations rather than draining the entire book.
Record CPU, compiler, Release flags, workload, and results when publishing a
comparison; measured speedups apply to those workloads.
