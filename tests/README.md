# Order-book tests

See [Building, testing, and debugging](../docs/building.md) for configuration.
Run from the repository root in PowerShell:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R 'edge\.(lesson6|invariants)' --output-on-failure
```

Each `edge.*` case runs in its own process with a 20-second timeout. The original
scenario suite remains in `order_book_tests`. Timing benchmarks run separately
from [the benchmarks folder](../benchmarks/README.md).
Both executables link the production `order_book` library.

## Verification

On 2026-09-17, after adding the Lesson 7 location regressions, **110 tests passed,
8 failed, and none skipped** in each of:

- Debug: `build`
- Release, with `NDEBUG`: `build/release`
- Checked libstdc++ containers: `build/checked`

The checker uses explicit `std::logic_error` conditions and stays active in
Release. There is no `NDEBUG` skip. CTest reports are saved as
`lesson7-results.xml` inside each build directory. All 104 previous tests and six
new location lifecycle tests pass. The eight `locations_checker_rejects_*` tests
fail because the production checker currently checks active keys but not stored
`Location` values. These are real missing checks, not skipped or expected-pass
tests; the full suite is not green until location validation is implemented.

## Checking every scenario operation

`checked_order_book.hpp` wraps the real book for scenario tests. It checks the
initial state, every successful limit/market submission and cancellation, and
state after ordinary `std::invalid_argument` rejection. It does not attempt to
recover an instance after allocation failure. The timed benchmark uses plain
`OrderBook`, so invariant scanning is excluded from its measurements.

The checker validates nonempty levels, ID/price/quantity bounds, side and level
membership, unique active IDs, strictly increasing FIFO priorities, priority
bounds, checked level sums, uncrossed prices, and equality of observed IDs with
the active-ID index. Depth is computed on demand; there is no cached level total
to compare. It does not mutate the book or advance arrival priority.

## Regression coverage

- Both incoming sides, exact/partial fills, FIFO, later arrivals, price priority,
  multi-level sweeps, limit stopping, and residual placement.
- Maker-price selection with shuffled, descending, and full-width IDs, including
  IDs above `INT_MAX`, `UINT64_MAX`, and narrowing-collision values.
- Valid price/quantity boundaries and rejection of zero, over-limit, and maximum
  unsigned quantities; invalid sides, invalid prices, ID zero, and active duplicates.
- Empty/unknown/repeated cancellation, head/middle/tail removal, partial fills,
  best/nonbest level cleanup, survivor priorities, and ID reuse.
- Market execution into empty/one-sided books, FIFO across levels, partial/exact/
  excess liquidity, expired residuals, duplicate rejection, and ID zero.
- Full owned snapshots: every order field, totals, price/FIFO ordering, repeatable
  reads, independence from book mutation, and unchanged state on rejection.
- Wide aggregate quantities above the per-order limit; direct checked-add tests
  at `UINT64_MAX`, including exact-boundary success and overflow rejection.
- Deliberately corrupted internal states: empty levels, invalid fields, wrong
  side/level, duplicate IDs within/across levels and sides, missing/stale index
  entries, zero/equal/reversed/future priorities, and locked/crossed books.
- Valid priority gaps, the latest assigned priority, maximum representable
  priority, repeated level recreation, and invariant checking without mutation.

`order_book_test_access.hpp` defines the book's narrow test friend. It lets tests
construct corrupt states and call the private checked-add helper without unsafe
layout casts or redefining `private`. Tests require the expected logic-error
message, so a different failing check cannot silently satisfy them. The new
location rejection tests allow any `std::logic_error` wording: they change only
one stored location (or swap two locations), while orders and index keys remain
valid.

## Lesson 7 location regressions

Run the new tests with `ctest --test-dir build -R 'edge\.locations_' --output-on-failure`.
Repeat with `build/release` and `build/checked` for the other configurations.

- Verify the exact registered level and order nodes on both sides.
- Cancel head/middle/tail nodes, preserve survivor addresses and priorities,
  and then cancel through every surviving location.
- Keep partially filled makers at the same node; remove fully filled makers;
  register limit residuals and never register expired market residuals.
- Recreate levels, reuse IDs on the opposite side, and preserve locations across
  additional map insertions and a forced active-index rehash.
- Require the invariant checker to reject wrong/invalid sides, level iterators
  into another level or side, order iterators into another order/level/side,
  and swapped complete locations.

Corruption tests substitute valid iterators to live nodes. They do not dereference
intentionally dangling or end iterators. Node-address comparisons permit checking
wrong-container locations without comparing iterators from different containers.
The Checked configuration additionally exercises iterator lifetimes during the
normal lifecycle tests.

Testing aggregate overflow with legal billion-unit orders would require billions
of nodes. The arithmetic-boundary tests therefore call the exact helper used by
the invariant checker. They do not claim to construct that enormous book.

## Mixed commands and reference comparisons

[The saved 20-command fixture](fixtures/lesson6_commands.hpp) combines insertion,
sweeps, duplicate and invalid-input rejection, cancellation, market expiration,
and ID reuse. Exact trade outputs and outcomes are checked separately from the
reference model, and every step compares the complete snapshot. Keep this fixture
for Lesson 11 replay work.

The independent reference uses a flat arrival-ordered vector, scans for the best
maker, and builds snapshots by stable sorting. It does not use production matching,
validation, or snapshot helpers. No production book copies are required.

- Five original 400-event seeded limit/cancel streams check trades, complete
  snapshots, best prices, and quantity conservation.
- Three additional 1,000-command streams mix limit orders, markets, cancellations,
  reused IDs, and malformed submissions. Errors report seed and command details.
- All **11,110 sequences of lengths 1 through 4** over a fixed 10-command alphabet
  are enumerated and compared against the reference after every command.

The enumeration is exhaustive for that bounded alphabet and length, not for all
possible prices, quantities, command histories, or future APIs.

## Contracts and limits

Invalid submissions throw `std::invalid_argument` before mutation. Accepted
submissions return a trade vector, possibly empty. Unknown cancellations return
`false` without mutation. Internal invariant violations throw `std::logic_error`.

Prices and individual quantities are in `1..1,000,000,000`; IDs are in
`1..UINT64_MAX` and unique only while resting. Negative text must be rejected by
future parsing before conversion to unsigned quantities. Aggregate quantities may
exceed the per-order bound. Amendments remain future work. Location validation
now has regression tests but is not yet implemented in the production checker.

The richer workbook command-result interface is deferred by agreement. Market,
depth, and invariant adapters retain missing-API skip support, but no current case
skips. Exit codes are 0 for pass, 1 for failure, 2 for invalid test selection, and
77 for missing features. Sanitizer validation is not claimed.
