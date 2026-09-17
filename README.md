# Matching Engine

C++23 limit order book and price-time-priority matching engine.

## Current version

v0.1

Supports:

- limit buy/sell orders
- partial fills
- FIFO within price levels
- cancellations

## Build and documentation

- [Development environment](docs/environment.md): installed versions, accepted
  tool paths, and editor setup status.
- [Building, testing, and debugging](docs/building.md): complete PowerShell
  commands, CMake target explanations, and recorded verification.
- [Test coverage and contracts](tests/README.md): regression cases, skips, and
  differences from the Windows workbook.
- [Timing benchmarks](benchmarks/README.md): separate executable, Release run
  commands, and the scan-versus-index cancellation exercise.

On 2026-09-17, all **104 CTest entries passed** in Debug, Release, and Checked
configurations, with no failures or skips. Coverage includes market orders,
full snapshots, explicit invariant checks, deliberately corrupted states, and
bounded exhaustive command sequences. Indexed cancellation and amendments remain
later-lesson work.
