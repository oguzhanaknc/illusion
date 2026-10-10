# Profiling

illusion is instrumented for the [Tracy](https://github.com/wolfpld/tracy) profiler. Instrumentation
compiles to nothing unless profiling is enabled, so the macros stay in shipping code.

## Enabling profiling

Configure with one of the `*-profile` presets, or set `ILLUSION_PROFILING=ON`:

```bash
cmake --preset linux-clang-profile
cmake --build --preset linux-clang-profile-release
```

Run the program and connect with the Tracy profiler GUI version **0.14.1**; the GUI and the client
must be the same version. Profiling is "on demand": data is only collected while a profiler is
connected.

## Instrumenting code

Header: `#include <illusion/core/Profiler.hpp>`

```cpp
void updatePhysics()
{
    ILL_PROFILE_SCOPE(); // zone named after the function
    {
        ILL_PROFILE_SCOPE_N("Broadphase");
        // ...
    }
}

void runFrame()
{
    // ...
    ILL_PROFILE_FRAME(); // marks the end of a frame
}
```

`ILL_PROFILE_THREAD("name")` names the calling thread. Job System workers are named automatically
and every executed Job appears as a "Job" zone.

## Performance budgets

Profiling explains where time goes; benchmarks keep it from regressing. Hot paths have benchmarks with
budgets that run in CI (see [Getting Started](../getting-started.md#benchmarks)). When you optimise a
system, add or tighten a benchmark in `benchmarks/`.
