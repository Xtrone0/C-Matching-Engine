# Benchmarks

Configure [Release](../docs/building.md) and run from the repository root:

```powershell
cmake --build build/release --target order_book_benchmark
.\build\release\order_book_benchmark.exe
.\build\release\order_book_benchmark.exe --large-cancels 10000
```

Run one instance at a time. `--large-cancels` accepts `1000` (default) or `10000`
and affects only the 100,000-order workloads. `--help` prints usage.
Benchmarks run separately from CTest.

## Workloads and timing

**Submissions:** 10,000 deterministic random orders, five fresh-book runs.
Output reports mean/min/max batch duration and throughput from the mean.

**Cancellation:** scan versus indexed lookup using identical map/list storage.

- 1,000, 10,000, or 100,000 resting orders; 1 or 100 price levels per side.
- Equal buy/sell counts, noncrossing prices, consecutive IDs, and quantity one.
- IDs shuffled with `std::mt19937_64` seed 7. Smaller books are fully drained;
  100,000-order books cancel the first 1,000 or 10,000 shuffled IDs.
- Fresh books for each sample; one discarded warm-up pair and five measured
  pairs, alternating which algorithm runs first.
- Only cancellation and success counting are timed. Setup, invariant checks,
  snapshots, printing, and destruction are excluded.
- Every sample verifies success counts, removed IDs, survivor fields/priorities,
  remaining order count, and invariants outside the timed region.

Output shows median batch-average nanoseconds per cancellation and
`scan median / indexed median`. These are workload-specific in-process costs,
not individual-operation latency percentiles. Compare drain and sampled workloads
separately, and use Release rather than Debug or Checked for performance results.

The scan baseline is `ScanOrderBook`; call it using its concrete type because
`cancel` is not virtual. Both implementations use the current engine storage.

[Recorded results, environment, and console logs](results/2026-09-17/README.md).
