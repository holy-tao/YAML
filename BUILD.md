# Building YAML for AutoHotkey

End users do **not** need to build anything - just `#Include dist\YAML.ahk`.
This document is for contributors who need to rebuild the amalgamation after
changing C sources (`src/native/src/*.c`, `src/native/src/*.h`) or the
AHK facade (`src/YAML.ahk`).

## Prerequisites

1. **AutoHotkey v2** on PATH (`AutoHotkey64.exe`).
2. **MinGW-w64 GCC** on PATH. MSYS2's UCRT64 toolchain works:
   ```
   pacman -S mingw-w64-ucrt-x86_64-gcc
   ```
   Verify with `gcc --version`. MCL auto-detects the compiler via PATH.
3. **Submodules initialized:**
   ```
   git submodule update --init --recursive
   ```
   This pulls:
   - `src/Lib/MCL`        - Machine Code Library (Descolada fork, `v2` branch)
   - `src/native/libyaml` - libyaml 0.2.5
   - `tests/YUnit`        - test harness

## Build

From the repo root:

```powershell
AutoHotkey64.exe .\build\build.ahk
```

This runs `MCL.StandaloneAHKFromC` over `parse.c + dump.c + libyaml/*.c`,
producing both 32-bit and 64-bit machine-code blobs, and writes the
amalgamation to `dist\YAML.ahk`.

### Shell caveat

Running `build.ahk` from **git bash / MSYS2 shells** currently fails: gcc
exits with code 1 and no output, likely due to PATH ordering interference
from the MSYS environment. Use **PowerShell** or **cmd** (or the VSCode
"run AHK script" button) until this is resolved. CI should prefer
PowerShell as well.

## Test

```powershell
cd tests
AutoHotkey64.exe .\RunTests.ahk
```

This loads the freshly built `dist\YAML.ahk` and runs the YUnit suites.
JUnit XML is written to `tests\junit.xml`; failures set a non-zero exit code.

For a quick sanity check without the full harness:

```powershell
cd tests
AutoHotkey64.exe .\smoke.ahk
```

## Troubleshooting

- **`gcc` exits 1 with no output**: check you're not running from git bash -
  see the shell caveat above.
- **`Could not find function X from msvcrt.dll`**: MSVCRT exports `_snprintf`,
  `_strtoi64`, etc., not their unprefixed equivalents. Add a `#define`
  alias in the C source.
- **`Reference to undefined symbol ___chkstk_ms`**: GCC inserts a stack
  probe for frames >4KB. Keep `-mno-stack-arg-probe` in `compilerOptions.flags`.
- **Generated `dist/YAML.ahk` is missing commas between `DllCall` entries**:
  MCL submodule drifted off Descolada's `v2` branch. Re-point and re-fetch.
