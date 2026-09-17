# Development environment

Recorded on 2026-09-17 for workbook L01. Run project commands from PowerShell.

## Machine and tools

| Item | Recorded value |
| --- | --- |
| Windows | Pro, x64, version 25H2, build 26200.9445 |
| Compiler | MSYS2 UCRT64 GCC 16.2.0, Rev3 |
| Compiler target | `x86_64-w64-mingw32` |
| C++ standard | C++23; compiler extensions disabled |
| CMake | 4.3.1 |
| Ninja | 1.13.2 |
| GDB | 17.2 |

The Windows version, build, edition, and architecture were read locally. The
registry's legacy `ProductName` says Windows 10 Pro; the table uses the actual
version/build fields rather than relying on that product label.

| Tool | Executable |
| --- | --- |
| g++ | `C:\msys64\ucrt64\bin\g++.exe` |
| CMake | `C:\tools\mingw64\bin\cmake.exe` |
| CTest | `C:\tools\mingw64\bin\ctest.exe` |
| Ninja | `C:\tools\mingw64\bin\ninja.exe` |
| GDB | `C:\msys64\ucrt64\bin\gdb.exe` |
| Resource compiler | `C:\msys64\ucrt64\bin\windres.exe` |

These paths are intentional and accepted for this project. CMake and Ninja are
build orchestration tools; their installation directory does not choose the C++
headers, libraries, or runtime. The configured C++ compiler is UCRT64 g++.

## Prepare a terminal

```powershell
Set-Location 'C:\Users\Albert\Desktop\C-Matching-Engine'
$env:Path = "C:\msys64\ucrt64\bin;C:\tools\mingw64\bin;$env:Path"
Get-Command g++, cmake, ctest, ninja, gdb, windres
g++ --version
g++ -dumpmachine
cmake --version
ninja --version
gdb --version
```

This changes PATH only for the current terminal and its child processes. The
UCRT64 directory lets executables find their compiler runtime DLLs. Launching
`code .` here passes that environment to a newly launched editor process.
An executable launched elsewhere may inherit a different PATH.

## Editor and verification status

- VS Code compiler settings: completed, confirmed by Albert on 2026-09-17.
- Current tool paths: accepted by Albert on 2026-09-17; no relocation required.
- CMake configuration, Debug build, and test execution: verified on 2026-09-17.
  Test results are detailed in [the build guide](building.md); a working toolchain
  does not imply that the matching-engine contract is complete.
- A breakpoint and step into the library: not recorded as verified.
- Checked-container and Release test runs: verified on 2026-09-17; all 104 tests
  passed in each configuration, with no failures or skips.

The workbook's editor settings are UCRT64 `g++.exe`, `windows-gcc-x64`, and
`c++23`. IntelliSense settings affect editor analysis; CMake chooses the actual
build compiler. Check `CMAKE_CXX_COMPILER` in `build/CMakeCache.txt` when diagnosing
a disagreement. Use a fresh build directory when changing compiler families.

See [Building, testing, and debugging](building.md) for exact project commands.
