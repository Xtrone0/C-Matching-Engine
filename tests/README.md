# Tests

After [configuring a build](../docs/building.md), run:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R 'order_book\.(locations_|invariants_)' --output-on-failure
ctest --test-dir build --rerun-failed --output-on-failure
```

Substitute `build/release` or `build/checked` for the other configurations.
Add `--output-junit test-results.xml` to save a report.
All scenarios and regressions live in `order_book_test.cpp` and run through
`order_book_tests`. CTest runs each case in its own process with a 20-second timeout.

```powershell
.\build\order_book_tests.exe --list
.\build\order_book_tests.exe snapshot_empty_book
```

**Verified 2026-09-17: 89 CTest entries passed in Debug, Release, and Checked,
with no failures or skips.**

## Coverage

Mirrored sides, boundary values, and random seeds share parameterized cases.
Where scenarios overlap, one case combines their distinct assertions.

- Price priority, FIFO, partial fills, sweeps, limit residuals, and market expiration.
- Input boundaries, duplicate IDs, rejection without mutation, and ID reuse.
- Head/middle/tail cancellation, empty-level cleanup, and unchanged survivors.
- Locator registration, partial/full fills, level recreation, and index rehashing.
- Independent snapshots, aggregate quantities, and checked arithmetic.
- Corrupted states: invalid fields, duplicate/missing IDs, wrong locations,
  invalid priorities, empty levels, and crossed books.

Scenario tests call invariants after each successful operation and ordinary
rejection. Checks throw `std::logic_error` and remain active in Release.
Tests substitute live iterators when checking corrupt locations; Checked builds
also detect iterator misuse. Sanitizer validation is not claimed.

An independent flat-vector reference checks trades and complete state against a
saved 20-command sequence, five 400-command random streams, three 1,000-command
mixed streams, and all 11,110 sequences of lengths 1–4 over a fixed 10-command
alphabet. Exhaustive coverage is limited to that alphabet and length.

## API contract

Invalid submissions throw `std::invalid_argument` before mutation; valid submissions
return a trade vector. Unknown cancellations return `false` without mutation.

Prices and individual quantities are in `1..1,000,000,000`. IDs are in
`1..UINT64_MAX` and unique while resting. Aggregate quantities may exceed the
per-order limit. Locator checks compare side, level price, and order ID; they
assume live iterators and do not prove object identity across separate books.
