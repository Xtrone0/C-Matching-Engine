# Build, test, and debug

## Requirements

- C++23 compiler; verified with MSYS2 UCRT64 GCC 16.2.0 on Windows x64.
- CMake 3.20 or newer and Ninja, both available on `PATH`.
- GDB is optional for debugging.

Run the commands below in PowerShell from the repository root. For a standard
MSYS2 installation, add UCRT64 tools and runtime DLLs to the current terminal's
`PATH` (adjust the installation directory if needed):

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
```

## Debug

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++ -DCMAKE_RC_COMPILER=windres
cmake --build build
ctest --test-dir build --output-on-failure
```

After editing code, repeat the build and test commands.

## Release

```powershell
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_RC_COMPILER=windres -DCMAKE_CXX_FLAGS:STRING=
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

Use Release for [performance measurements](../benchmarks/README.md).

## Checked containers

```powershell
cmake -S . -B build/checked -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++ -DCMAKE_RC_COMPILER=windres '-DCMAKE_CXX_FLAGS:STRING=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC'
cmake --build build/checked
ctest --test-dir build/checked --output-on-failure
```

Keep each configuration in its own directory. Checked-container definitions must
match across all linked C++ targets; this configuration enables them globally.
These checks detect container/iterator misuse and are not memory sanitizers.

## Targets

| Target | Purpose |
| --- | --- |
| `order_book` | Matching-engine library. |
| `order_book_tests` | All scenarios and regressions; supports individual test selection. |
| `order_book_benchmark` | Timing benchmarks; excluded from CTest. |

For debugging, run `gdb .\build\order_book_tests.exe`, set
`break passive_orders_both_sides`, then use `run`, `next`, and `step`.
In VS Code, point a `cppdbg` launch configuration at that executable and UCRT64 GDB.
