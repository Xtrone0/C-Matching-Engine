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

## Implemented: random order submissions

`order_book_benchmark.cpp` submits 10,000 deterministic random orders to a fresh
book for each of five runs. It reports average, minimum, and maximum elapsed
microseconds, plus throughput calculated from the average time.

Order generation and book construction happen before timing; the measured loop
includes submission, matching, and resting-order insertion. It uses ordinary
`OrderBook`, so per-operation invariant scans are excluded. This is the benchmark
extracted from the original scenario test executable, with its workload and
measurements preserved.

## Next exercise: scan versus indexed cancellation

This comparison is not implemented yet. Keep its timing code in this folder too.

1. Provide a benchmark-only scan cancellation baseline with the same map/list
   storage and cleanup behavior as indexed cancellation.
2. Populate two fresh books with identical noncrossing orders. Prepare one
   shuffled cancellation-ID sequence using a fixed seed.
3. Time only cancellation: use the scan baseline for one book and `cancel` for
   the other. Exclude setup, shuffling, printing, and invariant checks.
4. After timing, verify successful cancellation counts, empty final books, and
   invariants. Rebuild both books for every repetition.
5. Test 1,000 and 10,000 orders, then 100,000; include one price level and many
   price levels. Alternate the timing order and report medians across at least
   five repetitions.
6. Record CPU, compiler, Release flags, workload, nanoseconds per cancellation,
   and scan/indexed time ratio alongside any published results.
