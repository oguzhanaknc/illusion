# Build options for illusion. Every option is documented in docs/en/getting-started.md.

include_guard(GLOBAL)

option(ILLUSION_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(ILLUSION_BUILD_BENCHMARKS "Build benchmark tests (perf budgets)" ${PROJECT_IS_TOP_LEVEL})
option(ILLUSION_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ILLUSION_PROFILING "Enable Tracy profiler instrumentation" OFF)
option(ILLUSION_ENABLE_ASSERTS "Enable ILL_ASSERT checks in all configurations (always on in Debug)" OFF)

set(ILLUSION_SANITIZER "" CACHE STRING "Sanitizer to enable: '', 'address' (ASan+UBSan) or 'thread' (TSan)")
set_property(CACHE ILLUSION_SANITIZER PROPERTY STRINGS "" "address" "thread")
