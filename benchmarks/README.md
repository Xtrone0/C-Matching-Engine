# Benchmarks

Configure [Release](../docs/building.md), then run from the repository root:

```powershell
.\benchmarks\run.ps1
.\benchmarks\run.ps1 -Suite cancel -LargeCancels 10000
```

The script builds, runs regression tests, and writes a new timestamped directory
under `build/benchmarks/`. It records CPU, Windows build, power plan, inherited
affinity, commit/dirty state, source and executable hashes, and exact arguments.
Compiler flags and timer information are saved by the executable. Run one instance
at a time, without a debugger or other heavy work.

## Workloads

- Five command families: submissions, mixed operations, cancellation-heavy,
  deep noncrossing books, and a single price level.
- Defaults: 10,000 measured commands, 1,000 setup orders, seed 7, five repetitions.
  Submission-only runs start empty. Actual operation counts and live-order ranges
  are recorded; a mixed workload does not maintain constant occupancy.
- Scan/indexed cancellation: 1,000, 10,000, and 100,000 orders, with 1 or 100 levels
  per side. Small books drain; the largest cancel 1,000 or 10,000 shuffled IDs.
- The maximum resting book size is 100,000. Setup orders and commands are saved
  as concrete files; the manifest identifies the untimed setup prefix.

`-Commands`, `-Initial`, `-Runs`, `-MaxOrders`, `-Suite`, `-LargeCancels`,
`-OutputDirectory`, and `-NoLatency` customize the script. Output directories must
be new or empty. `order_book_benchmark.exe --help` lists the executable options.

Replay an exact saved workload (use its setup count from `workloads.csv`):

```powershell
.\build\release\order_book_benchmark.exe --suite commands --workload .\saved\workloads\mixed-7.trace --setup 1000 --out .\build\replayed-run
```

The direct executable records build metadata; use the script for machine metadata.

## Measurement boundaries

Command workloads are checked against the independent vector reference before
measurement, including exact trade order and final state. Every measured run starts
from the same setup and verifies counts, consumed trade fields, and final snapshot
after timing. Each mode has a discarded warm-up run.

Batch timing includes command dispatch, normal result creation/destruction, and
result consumption. Generation, file loading, setup, invariant checks, snapshots,
and printing are outside timing. Scan and indexed candidates alternate order and
use identical inputs; cancellation samples verify all surviving orders.

The separate mixed-command latency experiment times each `execute()` call through
result creation. Result consumption/destruction and sample recording occur afterward;
its outer batch clock includes that overhead. Sampled and unsampled runs alternate.
Preallocated samples are retained by operation type, including rejected operations.
Report nearest-rank p50/p95/p99 and compare sampled versus unsampled batch cost.

Back-to-back timer observations are saved separately. A zero median can mean clock
quantization, not zero overhead. No constant is subtracted from samples. Nanosecond
units do not establish nanosecond accuracy; these are in-process service measurements,
not network/queue latency. No p99.9 claim is made from the default sample count.

## Saved evidence

- `workloads.csv` and `workloads/`: exact inputs, setup sizes, operation mix and occupancy.
- `repetitions.csv` and `summary.csv`: every measured run, median and min/max spread.
- `latency-samples.csv`, `latency-summary.csv`, `timer-overhead.csv`: individual samples and timer checks.
- `environment.json`, `build.txt`, `console.txt`: run context and output.

[Full-workload results](results/2026-09-17-complete/README.md) |
[Earlier cancellation baseline](results/2026-09-17/README.md).
