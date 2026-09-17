# Tests

After [configuring a build](../docs/building.md), run:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R 'order_book\.(locations_|invariants_)' --output-on-failure
ctest --test-dir build -R 'order_book\.amend_' --output-on-failure
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

**Verified 2026-09-17: all 111 cases passed in Debug, Release, and Checked,
with no failures or skips.**

## Coverage

Mirrored sides, boundary values, and random seeds share parameterized cases.
Where scenarios overlap, one case combines their distinct assertions.

- Price priority, FIFO, partial fills, sweeps, limit residuals, and market expiration.
- Input boundaries, duplicate IDs, rejection without mutation, and ID reuse.
- Head/middle/tail cancellation, empty-level cleanup, and unchanged survivors.
- Amendments: no-op/reduction priority, increase/reprice FIFO, sweeps, remaining
  quantities, limits, rejected-state preservation, and repeated changes.
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
mixed streams including amendments, three 1,500-command amendment-heavy streams,
and all 11,110 sequences of lengths 1–4 over a fixed 10-command alphabet.
Exhaustive coverage is limited to that alphabet and length; that alphabet does
not include amendments. Amendment-heavy streams require every transition category.

## Reproduce and reduce failures

Differential failures save the complete command prefix under the build's
`test-failures/` directory. The matching `.report.txt` records the generator/seed
context, compiler/configuration, mismatch, both results, and before/after snapshots.
Every trace starts empty and records the price/quantity limits. Replay uses the
saved commands directly, without the random generator.

```powershell
.\build\order_book_tests.exe --replay .\build\test-failures\mixed-6.trace
.\build\order_book_tests.exe --minimize .\build\test-failures\mixed-6.trace .\build\reduced.trace
```

Use the actual path printed by a failing test. Replay returns 0 for agreement,
1 for a mismatch, and 2 for invalid input. Minimization removes command chunks
and simplifies IDs, prices, and quantities while retaining the same command kind
and mismatch signature. It verifies the saved result and refuses to overwrite
its input. This is greedy reduction, not a guarantee of the smallest possible trace.

The test-only `LOB-TEST-TRACE 1` format stores sequence, command, ID, numeric side,
price, and quantity on every command line. It preserves invalid domain values,
requires contiguous sequences, and supports LF/CRLF. It is separate from a public
simulator interface.

The fixture regression proves a deliberate output-price error is detected and
reduces six commands to two. Injection is confined to the test comparator;
normal engine code is unchanged. To demonstrate it:

```powershell
.\build\order_book_tests.exe --replay .\tests\fixtures\trade_price_original.trace --inject-price-error
.\build\order_book_tests.exe --minimize .\tests\fixtures\trade_price_original.trace .\build\reduced.trace --inject-price-error
.\build\order_book_tests.exe --replay .\build\reduced.trace
```

The first command intentionally exits 1; the last passes without injection.
Self-tests write clearly named `capture-self-test` artifacts in `test-failures/`.

## API contract

Invalid submissions throw `std::invalid_argument` before mutation; valid submissions
return a trade vector. Unknown cancellations return `false` without mutation.
Amendments take a new remaining quantity: same-price reductions keep priority;
increases and price changes resubmit with fresh resting priority. Invalid amendments
throw before mutation.

Prices and individual quantities are in `1..1,000,000,000`. IDs are in
`1..UINT64_MAX` and unique while resting. Aggregate quantities may exceed the
per-order limit. Locator checks compare side, level price, and order ID; they
assume live iterators and do not prove object identity across separate books.
