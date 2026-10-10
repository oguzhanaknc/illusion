illusion_require_dependency(nanobench)

add_executable(illusion_benchmarks
    main.cpp
    Budget.cpp
    BenchEcs.cpp
    BenchJobSystem.cpp)

target_include_directories(illusion_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_benchmarks PRIVATE illusion::core illusion::nanobench)
illusion_configure_target(illusion_benchmarks)

# Perf budgets are only meaningful for optimized builds. The tolerance multiplier absorbs noise on
# shared CI runners; set ILLUSION_BENCH_TOLERANCE in the environment to override it.
add_test(NAME benchmarks COMMAND illusion_benchmarks CONFIGURATIONS Release RelWithDebInfo)
set_tests_properties(benchmarks PROPERTIES LABELS "benchmark" RUN_SERIAL TRUE TIMEOUT 600)
