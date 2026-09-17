# Benchmark results: 2026-09-17

- **CPU/OS:** Intel Core i9-12900K; Windows x64, build 26200.9445.
- **Compiler/build:** MSYS2 UCRT64 GCC 16.2.0 (Rev3), C++23, Release `-O3 -DNDEBUG`.

See [commands and methodology](../../README.md). Both invocations completed
successfully with all benchmark correctness checks passing:

- [`--large-cancels 1000`: complete console output](cancel-1000.txt).
- [`--large-cancels 10000`: complete console output](cancel-10000.txt).

Logs retain the reported medians and submission results; individual repetition
timings are not emitted by this version.

## Cancellation results

Median batch-average nanoseconds per cancellation across five measured samples
after one warm-up. Speedup is the scan median divided by the indexed median.

| Starting orders | Cancellations | Levels per side | Scan (ns/cancel) | Indexed (ns/cancel) | Speedup |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 1,000 | 1 | 400.80 | 46.40 | 8.64x |
| 1,000 | 1,000 | 100 | 438.40 | 57.10 | 7.68x |
| 10,000 | 10,000 | 1 | 8,366.73 | 47.18 | 177.34x |
| 10,000 | 10,000 | 100 | 4,294.47 | 49.75 | 86.32x |
| 100,000 | 1,000 | 1 | 523,774.40 | 274.90 | 1,905.33x |
| 100,000 | 1,000 | 100 | 1,030,746.90 | 154.00 | 6,693.16x |
| 100,000 | 10,000 | 1 | 425,247.29 | 130.86 | 3,249.64x |
| 100,000 | 10,000 | 100 | 875,568.27 | 92.25 | 9,491.25x |

The first six rows use the `1000` invocation; the last two use the `10000`
invocation. Repeated small workloads consistently use the first invocation.
Both logs retain their results, and the main README selects the four
100-level-per-side rows.

Small workloads drain the book; large workloads cancel 1% or 10% of its orders.
Both algorithms use identical storage and inputs; setup and verification are
excluded from timing. 
