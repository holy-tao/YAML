# Contributing

Contributions are welcome. You can contribute by opening a pull request or submitting an issue.

## Prerequisites

1. **AutoHotkey v2** on PATH (`AutoHotkey64.exe`).
2. **MinGW-w64 GCC** on PATH. MSYS2's UCRT64 toolchain works:
   Verify with `gcc --version`. MCL auto-detects the compiler via PATH.
3. **Submodules initialized:**
   ```
   git submodule update --init --recursive
   ```
   This pulls:
   - `src/Lib/MCL`             - Machine Code Library (A fork of the `v2` branch)
   - `src/native/libyaml`      - libyaml 0.2.5
   - `tests/YUnit`             - test harness
   - `tests/yaml-test-suite`   - upstream conformance corpus (pinned to `data-2022-01-17`)

## Build

Run [`Build.ahk`][Build.ahk] to build the scripts. In VSCode, a build configuration is already
set up, and you can build by pressing `Ctrl + Shift + B`.

This runs `MCL.StandaloneAHKFromC` over `parse.c + dump.c + libyaml/*.c`,
producing both 32-bit and 64-bit machine-code blobs, and writes the
amalgamation to `dist\YAML.ahk`.

[Build.ahk]: ./build/build.ahk

## Test

Run tests via [`RunTests.ahk`](./tests/RunTests.ahk). You can also build and run everything
by running [`run-tests.ps1`](./run-tests.ps1).

> [!NOTE]
> Tests do not run in CI/CD - for whatever reason, the build only succeeds about 10% of
> the time, the rest of the time gcc dies silently.


### [yaml-test-suite] conformance

A separate harness runs the upstream [yaml-test-suite] 
(pinned to `data-2022-01-17`) against `dist/YAML.ahk` and writes a pass/fail summary
to `tests/conformance/report-<timestamp>.txt`. It is **not** part of `RunTests.ahk`. To run these,
run [`RunConformance.ahk`](./tests/RunConformance.ahk). Requires on `cJSON.ahk` being reachable 
via `#include <JSON>`.

Note that LibYAML itself does not pass every conformance test out there, and this library
is expected to fail test that LibYAML fails. Check out the [YAML Test Matrix](https://matrix.yaml.info/)
for details. This repo should probably switch to [`libfyaml`](https://github.com/pantoniou/libfyaml) or
[`RapidYaml`](https://rapidyaml.readthedocs.io/v0.7.1/index.html) (though the latter would require
porting C code to C++) to get proper spec compliance and YAML 1.2 support.

[yaml-test-suite]: https://github.com/yaml/yaml-test-suite

### Performance testing

Run [`run-bench.ps1`](./run-bench.ps1) to run the existing performance test suite. You should do this before and after making your changes. You can also run `run-tests.ps1 -Compare` to run a subset of the performance tests in comparison to [`HotKeyIt/YAML`](https://github.com/HotKeyIt/Yaml), though this isn't super useful except for showboating.

## Troubleshooting

- **`gcc` exits 1 with no output**: check you're not running from git bash - for some
  reason it exits silently with no output. Running AHK from the play button in VSCode
  should work.
- **`Could not find function X from msvcrt.dll`**: MSVCRT exports `_snprintf`,
  `_strtoi64`, etc., not their unprefixed equivalents. Add a `#define`
  alias in the C source.
- **`Reference to undefined symbol ___chkstk_ms`**: GCC inserts a stack
  probe for frames >4KB. Keep `-mno-stack-arg-probe` in `compilerOptions.flags`.
- **Generated `dist/YAML.ahk` is missing commas between `DllCall` entries**:
  Ensure that the MCL submodule is using the correct fork of the repo.