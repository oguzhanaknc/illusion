# illusion::core — foundation library: platform, logging, memory, jobs, ECS.

add_library(illusion_core STATIC
    src/core/Assert.cpp
    src/core/JobSystem.cpp
    src/core/Log.cpp
    src/core/Memory.cpp
    src/core/Thread.cpp
    src/ecs/Archetype.cpp
    src/ecs/CommandBuffer.cpp
    src/ecs/Component.cpp
    src/ecs/World.cpp)
add_library(illusion::core ALIAS illusion_core)

target_include_directories(illusion_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(illusion_core PUBLIC cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(illusion_core PUBLIC Threads::Threads)

# Asserts are always on in Debug; ILLUSION_ENABLE_ASSERTS turns them on in every configuration.
if(ILLUSION_ENABLE_ASSERTS)
    target_compile_definitions(illusion_core PUBLIC ILLUSION_ENABLE_ASSERTS=1)
else()
    target_compile_definitions(illusion_core PUBLIC $<$<CONFIG:Debug>:ILLUSION_ENABLE_ASSERTS=1>)
endif()

if(ILLUSION_PROFILING)
    illusion_require_dependency(tracy)
    target_link_libraries(illusion_core PUBLIC Tracy::TracyClient)
    target_compile_definitions(illusion_core PUBLIC ILLUSION_PROFILING=1)
endif()

illusion_configure_target(illusion_core)
