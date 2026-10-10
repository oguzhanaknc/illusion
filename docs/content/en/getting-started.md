# Getting Started

This page shows how to build illusion from source and run its tests and benchmarks.

## Prerequisites

| Tool | Version | Notes |
| --- | --- | --- |
| CMake | 3.28+ | Presets are used for every build. |
| Ninja | 1.11+ | Bundled with Visual Studio; `pip install ninja` elsewhere. |
| C++ compiler | see below | C++20 with `std::format`. |
| Python | 3.10+ | Format and documentation checks. |
| Git | any recent | |

Compilers:

- **Windows:** Visual Studio 2022 17.10+ or Visual Studio 2026 with the "Desktop development with C++"
  workload. clang-cl from the same installation also works.
- **macOS:** Xcode 15+ (Apple Clang). The minimum deployment target is macOS 13.3.
- **Linux:** GCC 13+ or Clang 18+ (Ubuntu 24.04 ships both).

Third-party libraries are downloaded automatically at configure time and verified with SHA-256
hashes. No manual installation is needed.

## Build and test

Clone the repository and pick the preset for your platform:

```bash
git clone https://github.com/oguzhanaknc/illusion.git
cd illusion
cmake --preset linux-clang
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug
```

Available configure presets:

| Preset | Platform |
| --- | --- |
| `windows-msvc`, `windows-clang` | Windows (run from a Visual Studio developer shell) |
| `linux-gcc`, `linux-clang` | Linux |
| `macos-clang` | macOS |
| `linux-clang-asan`, `linux-clang-tsan` | Linux with sanitizers |
| `*-profile` | Tracy profiling enabled |
| `ci-*` | The exact configuration CI uses (warnings are errors, asserts on) |

Every configure preset has build and test presets named `<preset>-debug`, `<preset>-release` and
`<preset>-relwithdebinfo`.

On Windows, the helper script `tools\windows\vsdev.cmd` runs any command inside a Visual Studio
developer environment, so you do not need to open a developer shell first:

```bat
tools\windows\vsdev.cmd cmake --preset windows-msvc
tools\windows\vsdev.cmd cmake --build --preset windows-msvc-debug
```

## Keep the build tree out of synced folders

Build trees go to `build/<preset>` by default. If the repository lives in a folder synchronised by
OneDrive, Dropbox or iCloud, place the build tree elsewhere: sync clients lock files during builds and
waste bandwidth. Create a `CMakeUserPresets.json` (it is ignored by Git) next to `CMakePresets.json`:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "dev",
      "inherits": "windows-msvc",
      "binaryDir": "C:/dev/illusion-build/${presetName}"
    }
  ]
}
```

Then build with `cmake --preset dev` and `cmake --build C:/dev/illusion-build/dev --config Debug`.

## Build options

| Option | Default | Description |
| --- | --- | --- |
| `ILLUSION_BUILD_TESTS` | `ON` | Build the unit tests. |
| `ILLUSION_BUILD_BENCHMARKS` | `ON` | Build the benchmarks (registered as a CTest test in optimized configurations). |
| `ILLUSION_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings as errors (`ON` in CI presets). |
| `ILLUSION_ENABLE_ASSERTS` | `OFF` | Keep `ILL_ASSERT` in every configuration; it is always on in Debug. |
| `ILLUSION_PROFILING` | `OFF` | Instrument the engine for Tracy. |
| `ILLUSION_SANITIZER` | empty | `address` (ASan + UBSan) or `thread` (TSan); GCC/Clang only. |

## Benchmarks

Benchmarks measure hot paths and fail when an operation exceeds its performance budget. They run as
part of `ctest` in Release and RelWithDebInfo builds. You can also run them directly, optionally with a
name filter:

```bash
./build/linux-clang/benchmarks/Release/illusion_benchmarks ecs/
```

The environment variable `ILLUSION_BENCH_TOLERANCE` multiplies every budget (CI uses `3.0` because
shared runners are noisy).

## Next steps

Read the [ECS manual](manual/ecs.md) to learn how game data is organised, then the
[architecture overview](architecture/overview.md).
