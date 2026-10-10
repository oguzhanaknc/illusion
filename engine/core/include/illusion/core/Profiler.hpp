#pragma once

/// @file
/// Profiling macros. They map to Tracy when the `ILLUSION_PROFILING` CMake option is ON and compile to
/// nothing otherwise, so they can stay in shipping code.
///
/// - `ILL_PROFILE_SCOPE()` — profiles the enclosing scope, named after the function.
/// - `ILL_PROFILE_SCOPE_N("name")` — profiles the enclosing scope with a string literal name.
/// - `ILL_PROFILE_FRAME()` — marks the end of a frame.
/// - `ILL_PROFILE_THREAD("name")` — names the calling thread in the profiler.

#if defined(ILLUSION_PROFILING) && ILLUSION_PROFILING
#include <tracy/Tracy.hpp>

#define ILL_PROFILE_SCOPE() ZoneScoped
#define ILL_PROFILE_SCOPE_N(name) ZoneScopedN(name)
#define ILL_PROFILE_FRAME() FrameMark
#define ILL_PROFILE_THREAD(name) ::tracy::SetThreadName(name)
#else
/// Profiles the enclosing scope as a zone named after the function.
#define ILL_PROFILE_SCOPE() ((void)0)
/// Profiles the enclosing scope as a zone named @p name (a string literal).
#define ILL_PROFILE_SCOPE_N(name) ((void)0)
/// Marks the end of a frame.
#define ILL_PROFILE_FRAME() ((void)0)
/// Names the calling thread in the profiler.
#define ILL_PROFILE_THREAD(name) ((void)0)
#endif
