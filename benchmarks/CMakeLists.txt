illusion_require_dependency(nanobench)

add_executable(illusion_benchmarks
    main.cpp
    Budget.cpp
    BenchAssets.cpp
    BenchEcs.cpp
    BenchEventBus.cpp
    BenchInput.cpp
    BenchJobSystem.cpp
    BenchMath.cpp
    BenchPhysics.cpp)

target_include_directories(illusion_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_benchmarks PRIVATE
    illusion::core illusion::input illusion::assets illusion::physics illusion::nanobench)
illusion_configure_target(illusion_benchmarks)

# Perf budgets are only meaningful for optimized builds. The tolerance multiplier absorbs noise on
# shared CI runners; set ILLUSION_BENCH_TOLERANCE in the environment to override it.
add_test(NAME benchmarks COMMAND illusion_benchmarks CONFIGURATIONS Release RelWithDebInfo)
set_tests_properties(benchmarks PROPERTIES LABELS "benchmark" RUN_SERIAL TRUE TIMEOUT 600)

# GPU frame-time benchmarks: need a Vulkan device, so they are labelled "gpu" as well and run locally on
# real hardware (CI excludes the gpu label from its benchmark runs).
add_executable(illusion_gpu_benchmarks
    main.cpp
    Budget.cpp
    gpu/BenchRender.cpp)
target_include_directories(illusion_gpu_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_gpu_benchmarks PRIVATE illusion::render illusion::nanobench)
illusion_configure_target(illusion_gpu_benchmarks)
add_test(NAME gpu_benchmarks COMMAND illusion_gpu_benchmarks CONFIGURATIONS Release RelWithDebInfo)
set_tests_properties(gpu_benchmarks PROPERTIES LABELS "gpu;benchmark" RUN_SERIAL TRUE TIMEOUT 600)
