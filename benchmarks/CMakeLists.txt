illusion_require_dependency(nanobench)

add_executable(illusion_benchmarks
    main.cpp
    Budget.cpp
    BenchAssets.cpp
    BenchAudio.cpp
    BenchCore.cpp
    BenchDebugDraw.cpp
    BenchEcs.cpp
    BenchEventBus.cpp
    BenchInput.cpp
    BenchJobSystem.cpp
    BenchMath.cpp
    BenchModules.cpp
    BenchPhysics.cpp
    BenchReflection.cpp
    BenchRuntime.cpp
    BenchText.cpp)

target_include_directories(illusion_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_benchmarks PRIVATE
    illusion::core illusion::input illusion::assets illusion::audio illusion::physics illusion::modules
    illusion::runtime illusion::text illusion::render illusion::nanobench)
illusion_configure_target(illusion_benchmarks)

# The two builds of the modules/migrate benchmark module (a shared module, so not in profiling builds).
if(NOT ILLUSION_PROFILING)
    foreach(variant narrow wide)
        string(TOUPPER ${variant} upper)
        illusion_add_module(illusion_bench_migrate_${variant} SHARED SOURCES modules/MigrateModule.cpp)
        target_compile_definitions(illusion_benchmarks PRIVATE
            BENCH_MIGRATE_${upper}="$<TARGET_FILE:illusion_bench_migrate_${variant}>")
        add_dependencies(illusion_benchmarks illusion_bench_migrate_${variant})
    endforeach()
    target_compile_definitions(illusion_bench_migrate_wide PRIVATE BENCH_MIGRATE_WIDE=1)
endif()

# Perf budgets are only meaningful for optimized builds. The tolerance multiplier absorbs noise on
# shared CI runners; set ILLUSION_BENCH_TOLERANCE in the environment to override it.
add_test(NAME benchmarks COMMAND illusion_benchmarks CONFIGURATIONS Release RelWithDebInfo)
set_tests_properties(benchmarks PROPERTIES LABELS "benchmark" RUN_SERIAL TRUE TIMEOUT 600)

# GPU frame-time benchmarks: need a Vulkan device, so they are labelled "gpu" as well and run locally on
# real hardware (CI excludes the gpu label from its benchmark runs).
add_executable(illusion_gpu_benchmarks
    main.cpp
    Budget.cpp
    gpu/BenchRender.cpp
    gpu/BenchRenderAssets.cpp)
target_include_directories(illusion_gpu_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_gpu_benchmarks PRIVATE illusion::render illusion::nanobench)
illusion_configure_target(illusion_gpu_benchmarks)
add_test(NAME gpu_benchmarks COMMAND illusion_gpu_benchmarks CONFIGURATIONS Release RelWithDebInfo)
set_tests_properties(gpu_benchmarks PROPERTIES LABELS "gpu;benchmark" RUN_SERIAL TRUE TIMEOUT 600)
