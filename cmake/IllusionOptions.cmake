# Build options for illusion. Every option is documented in docs/en/getting-started.md.

include_guard(GLOBAL)

option(ILLUSION_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(ILLUSION_BUILD_BENCHMARKS "Build benchmark tests (perf budgets)" ${PROJECT_IS_TOP_LEVEL})
option(ILLUSION_BUILD_SAMPLES "Build sample applications" ${PROJECT_IS_TOP_LEVEL})
option(ILLUSION_BUILD_TOOLS "Build command-line tools (illusion_pack)" ${PROJECT_IS_TOP_LEVEL})
option(ILLUSION_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ILLUSION_PROFILING "Enable Tracy profiler instrumentation" OFF)
option(ILLUSION_ENABLE_ASSERTS "Enable ILL_ASSERT checks in all configurations (always on in Debug)" OFF)

# Game modules (illusion_add_module): shared libraries that can be hot-reloaded, or static libraries linked
# into the game. Profiling builds always use static modules.
if(ILLUSION_PROFILING)
    set(illusion_shared_modules_default OFF)
else()
    set(illusion_shared_modules_default ON)
endif()
option(ILLUSION_SHARED_MODULES "Build game modules as hot-reloadable shared libraries" ${illusion_shared_modules_default})

set(ILLUSION_SLANGC_EXECUTABLE "" CACHE FILEPATH "Use this slangc instead of the pinned prebuilt Slang release")

set(ILLUSION_SANITIZER "" CACHE STRING "Sanitizer to enable: '', 'address' (ASan+UBSan) or 'thread' (TSan)")
set_property(CACHE ILLUSION_SANITIZER PROPERTY STRINGS "" "address" "thread")
