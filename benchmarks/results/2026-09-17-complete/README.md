# Complete benchmark run: 2026-09-17

Verified Release runs on Intel Core i9-12900K, Windows x64 build 26200.9445,
GCC 16.2.0, C++23, `-O3 -DNDEBUG`. Five measured repetitions follow discarded
warm-ups. No debugger or container instrumentation was used.

Base commit: `dbba1477ff874edff6d191b5c7b88e6501a84fd4` with uncommitted benchmark/runner changes.
The archive records dirty state, exact source/executable hashes and workload hashes.
Power configuration: Power Scheme GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (Balanced). Affinity was inherited; no core was pinned.

## Command throughput

Each workload has 10,000 measured commands. Generation, setup, parsing, checking,
and printing are untimed. Batch results include normal result creation/destruction
and result consumption. Every measured result passed the recorded correctness checks.

| Workload | Median ns/command | Min–max ns/command | Commands/second | Start → end live orders |
| --- | ---: | ---: | ---: | ---: |
| submissions-7 | 127.53 | 124.51–134.95 | 7.84 M | 0 → 1713 |
| mixed-7 | 172.58 | 170.54–173.49 | 5.79 M | 1000 → 0 |
| cancellation-heavy-7 | 98.62 | 93.78–111.97 | 10.14 M | 1000 → 0 |
| deep-noncrossing-7 | 164.65 | 153.60–281.98 | 6.07 M | 1000 → 3711 |
| single-price-7 | 86.35 | 84.83–95.00 | 11.58 M | 1000 → 3120 |

The exact operation mixes, rejection counts and live-order ranges are in
[the workload manifest](full-1000-workloads.csv). Ratios describe attempted commands;
unknown cancellations and invalid amendments are part of the declared mixed workload.

## Sampled latency

50,000 mixed-command observations: p50 **100 ns**, p95 **1,300 ns**, p99 **1,400 ns**.
These nearest-rank percentiles time dispatch through result creation, before result
consumption/destruction. The aggregate includes valid and rejected operations;
[per-operation summaries](latency-summary.csv) use the same boundary.

Sampling increased median batch cost from **172.58 to 213.16 ns/command** (**23.5%**).
The minimum positive back-to-back timer interval was **100 ns**; its median was zero
because readings are quantized. Thus short-operation percentiles are coarse, not
nanosecond-accurate claims. No overhead subtraction or p99.9 claim is made.

## Cancellation comparison and evidence

Both 1,000- and 10,000-cancellation options completed at 100,000 resting orders.
Small workloads drain their books. Use the full run for its small-book measurements;
the second run is a separate complete observation, not a pool for selecting minima.

- [Full-run summary](full-1000-summary.csv) and [all repetitions](full-1000-repetitions.csv).
- [10,000-cancellation summary](cancel-10000-summary.csv) and [all repetitions](cancel-10000-repetitions.csv).
- [Raw runs archive](raw-runs.zip): concrete workloads, individual latency/timer samples,
  configuration, machine metadata and console output for both runs.

Extract the archive to reproduce its input, then use `--workload` and the setup count
from `workloads.csv`; choose a fresh output directory. See [benchmark instructions](../../README.md).
These are in-process measurements on one machine, not network or queued latency.
