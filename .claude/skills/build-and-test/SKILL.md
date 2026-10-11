---
name: build-and-test
description: Configure, build and test illusion on Windows/macOS/Linux with CMake presets; run benchmarks, sanitizer and Tracy profiling builds. Use whenever you need to compile, run tests or verify a change.
---

# Build and test illusion

## 1. Pick a preset

| Host | Everyday preset | CI-equivalent preset |
| --- | --- | --- |
| Windows | `windows-msvc` (also `windows-clang`) | `ci-windows-msvc`, `ci-windows-clang` |
| Linux | `linux-clang` or `linux-gcc` | `ci-linux-clang`, `ci-linux-gcc` |
| macOS | `macos-clang` | `ci-macos-clang` |

`ci-*` presets turn on `ILLUSION_WARNINGS_AS_ERRORS` and `ILLUSION_ENABLE_ASSERTS`. Always validate a
change with a `ci-*` preset before committing.

Build/test presets are `<configure-preset>-debug`, `-release`, `-relwithdebinfo`.

## 2. Windows specifics

- The compiler needs the Visual Studio environment. Wrap commands:
  `tools\windows\vsdev.cmd cmake --preset ci-windows-msvc`
- From Git Bash, call through `cmd //c "tools\\windows\\vsdev.cmd ..."` and set `MSYS_NO_PATHCONV=1`
  when passing `C:/...` paths, or use PowerShell.
- If the checkout is under OneDrive/Dropbox, create a git-ignored `CMakeUserPresets.json` whose presets
  inherit the normal ones but set `binaryDir` outside the synced folder, e.g.
  `"binaryDir": "C:/Users/<you>/illusion-build/${presetName}"`. Then build with
  `cmake --build <binaryDir> --config Debug` and test with `ctest --test-dir <binaryDir> -C Debug`.

## 3. Build and test

```bash
cmake --preset ci-linux-clang
cmake --build --preset ci-linux-clang-debug
ctest --preset ci-linux-clang-debug
cmake --build --preset ci-linux-clang-release
ctest --preset ci-linux-clang-release      # includes the 'benchmarks' test (label: benchmark)
```

Useful ctest options: `-R <regex>` (by test name), `-L unit` / `-LE benchmark` (by label),
`--output-on-failure`. Each doctest `TEST_CASE` is its own CTest test.

Run the test binary directly for doctest options: `illusion_tests -tc="*Query*" -s`.

## 4. GPU tests and the sample

- `illusion_gpu_tests` (CTest label `gpu`) needs a Vulkan 1.3 driver. Exclude with `-LE gpu` on machines
  without one; run only them with `-L gpu`. They require zero validation errors.
- The swapchain test opens a real window; on headless Linux run it under `xvfb-run -a`. CI does this
  with Mesa lavapipe (`mesa-vulkan-drivers vulkan-validationlayers xvfb`).
- Smoke-run the sample: `illusion_hello_triangle --frames 300` (exit code 2 means validation errors).

## 5. Benchmarks

- Binary: `<build>/benchmarks/Release/illusion_benchmarks [name-filter]`.
- Each benchmark has a budget in ns/op; the runner exits 1 if a median exceeds
  `budget * ILLUSION_BENCH_TOLERANCE` (default 1.0, CI 3.0).
- Never raise a budget to make a regression pass. Investigate with Tracy first; if the slowdown is an
  intended trade-off, explain it in the commit message.

## 6. Sanitizers (Linux, Clang)

```bash
cmake --preset linux-clang-asan && cmake --build --preset linux-clang-asan-relwithdebinfo \
  && ctest --preset linux-clang-asan-relwithdebinfo
cmake --preset linux-clang-tsan && cmake --build --preset linux-clang-tsan-relwithdebinfo \
  && ctest --preset linux-clang-tsan-relwithdebinfo
```

On kernels with high ASLR entropy TSan aborts with "unexpected memory mapping": run
`sudo sysctl vm.mmap_rnd_bits=28` first.

## 7. Profiling build

Use a `*-profile` preset (sets `ILLUSION_PROFILING=ON`, fetches Tracy 0.14.1), build Release, run, and
connect with the Tracy 0.14.1 GUI.

## 8. Shaders

Write Slang in `<module>/shaders/` and register entry points with `illusion_add_shaders()` (see
`engine/render/CMakeLists.txt`). Compile errors and warnings fail the build. Clip space is y-up.

## 9. Adding source files

- Library sources: list them in `engine/<module>/CMakeLists.txt` (no globbing).
- Tests: add `tests/<area>/Test<Feature>.cpp` to `tests/CMakeLists.txt`.
- Benchmarks: add `benchmarks/Bench<Area>.cpp` to `benchmarks/CMakeLists.txt`.
- Call `illusion_configure_target(<target>)` on every new target to get the warning flags.
