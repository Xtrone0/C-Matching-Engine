# Building, testing, and debugging

Use PowerShell at the repository root with the PATH from
[Development environment](environment.md). The commands below use the project's
existing target names and custom test harness.

## Configure and build Debug

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_RC_COMPILER=C:/msys64/ucrt64/bin/windres.exe -DCMAKE_MAKE_PROGRAM=C:/tools/mingw64/bin/ninja.exe
cmake --build build
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```

Run each command after the preceding command succeeds. A failing CTest result
requires investigation; it is not a build success indicator. The explicit resource
compiler prevents an unrelated inherited `RC` setting from selecting an obsolete
Windows SDK tool.

`-S` selects the source folder, `-B` selects the build folder, `-G` selects Ninja,
and `-D` sets a CMake configuration variable. CMake records those choices in
`build/CMakeCache.txt`. Compilation creates object files; linking combines the
objects and resolves calls into the library. CMake generates the dependency graph,
Ninja builds it, and CTest runs the registered tests.

For subsequent source edits:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

## Targets and files

| Target | Purpose |
| --- | --- |
| `order_book` | Library compiled from `src/order_book.cpp`; owns matching behavior. |
| `order_book_tests` | Original correctness scenario tests. |
| `order_book_edge_cases` | Individually selectable regression cases registered with CTest. |
| `order_book_benchmark` | Standalone timing benchmarks from `benchmarks/`; run manually. |

All three executables link the same library. `target_include_directories(order_book
PUBLIC ...)` gives the library and consumers access to `include/`. The executables
link it with `PRIVATE` because they do not export that dependency to consumers.
Add new source files to the appropriate target in `CMakeLists.txt`.

The project currently has no separate simulator executable or GoogleTest
dependency. Existing custom checks remain active in Release, unlike ordinary
C++ `assert` expressions. `build/` and `.vscode/` are ignored by Git.

## Select tests and inspect commands

```powershell
.\build\order_book_edge_cases.exe --list
.\build\order_book_edge_cases.exe resting_sell_price_with_descending_ids
ctest --test-dir build -R '^edge\.incoming_(buy|sell)_id_' --output-on-failure
ctest --test-dir build --rerun-failed --output-on-failure
cmake --build build --verbose
```

Verbose builds show compilation/linking commands when work is required. A build
with no changed inputs may simply report that there is no work to do.

## Separate Checked and Release configurations

Both configurations were built and tested successfully on 2026-09-17.
Keep each configuration in its own directory. The current CMake file has no
`LOB_CHECKED` option, so apply both container-debug definitions through compiler
flags to every C++ target in the Checked build:

```powershell
cmake -S . -B build/checked -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_RC_COMPILER=C:/msys64/ucrt64/bin/windres.exe -DCMAKE_MAKE_PROGRAM=C:/tools/mingw64/bin/ninja.exe '-DCMAKE_CXX_FLAGS:STRING=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC'
cmake --build build/checked
ctest --test-dir build/checked --output-on-failure
```

Checked containers change standard-container representations. All linked C++
code exchanging these containers must use consistent definitions. They detect
many container/iterator errors, but are not general memory sanitizers.

```powershell
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_RC_COMPILER=C:/msys64/ucrt64/bin/windres.exe -DCMAKE_MAKE_PROGRAM=C:/tools/mingw64/bin/ninja.exe -DCMAKE_CXX_FLAGS:STRING=
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

Release must not inherit the Checked definitions. Invariant tests run in Release, using
explicit logic-error checks that remain active with NDEBUG. Benchmark output from Debug or
Checked is not a Release performance baseline.

## Run timing benchmarks

For performance measurements, run the standalone benchmark after configuring
Release:

```powershell
cmake --build build/release --target order_book_benchmark
.\build\release\order_book_benchmark.exe
```

CTest runs correctness tests only. See [Timing benchmarks](../benchmarks/README.md)
for the existing workload and the cancellation benchmark exercise.

## Debug the library

Build Debug first, then start GDB from the prepared PowerShell:

```powershell
gdb .\build\order_book_tests.exe
```

At the GDB prompt:

```text
break test_resting_buy
run
list
next
```

Use `next` until the next statement calls `book.submit(...)`, then `step` into
`OrderBook::submit`. Use `backtrace` to inspect the call stack, `continue` to
resume, and `quit` to exit. If `step` enters an argument helper first, finish that
call and continue stepping until the library source is reached. This exercises
the existing library instead of restoring the workbook's temporary build probe.

For VS Code, use a `cppdbg` launch configuration with `MIMode` set to `gdb`,
`miDebuggerPath` set to `C:/msys64/ucrt64/bin/gdb.exe`, and `program` pointing to
`${workspaceFolder}/build/order_book_tests.exe`. Set a breakpoint at the call to
`submit` in `test_resting_buy`, launch Debug, and step into the library. Record the
observed source location when completing the L02 exercise.

## Recorded evidence and remaining exercises

On 2026-09-17, Debug, Checked, and Release builds each passed all **104 tests**,
with no failures or skips. The checker runs after every scenario command,
including ordinary rejection, and its failure paths are tested with controlled
internal corruption. The suite also includes a saved 20-command fixture,
3,000 generated mixed commands, and 11,110 bounded exhaustive command sequences.
CTest reports are in each build directory as `lesson6-results.xml`.

The older tests have been adapted to the workbook's numeric limits and unsigned
quantity type. The project retains exception-based rejection by agreement. See
[Test coverage and contracts](../tests/README.md) before treating it as complete
workbook acceptance coverage.

L02 evidence still to record:

- Deliberately change an expected value in an existing passing test, build, observe
  its test failure, restore the expectation, and confirm that case passes again.
  Confirm that the intended case is the one that fails.
- Stop at a breakpoint and step into library source.
- Add the separate simulator target and commit the completed scaffold when ready.

No completed exercise, commit, or time spent is inferred from these instructions.
