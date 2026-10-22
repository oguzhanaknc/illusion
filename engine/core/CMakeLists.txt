# illusion::core — foundation library: logging, memory, jobs, events, time, math, ECS.

add_library(illusion_core STATIC
    src/core/Assert.cpp
    src/core/EventBus.cpp
    src/core/Hash.cpp
    src/core/JobSystem.cpp
    src/core/Json.cpp
    src/core/Log.cpp
    src/core/Math.cpp
    src/core/Memory.cpp
    src/core/Module.cpp
    src/core/Random.cpp
    src/core/Thread.cpp
    src/core/Time.cpp
    src/core/Version.cpp
    src/ecs/Archetype.cpp
    src/ecs/CommandBuffer.cpp
    src/ecs/Component.cpp
    src/ecs/ComponentJson.cpp
    src/ecs/EntityGuid.cpp
    src/ecs/Hierarchy.cpp
    src/ecs/Name.cpp
    src/ecs/Reflection.cpp
    src/ecs/ReflectionData.cpp
    src/ecs/SceneSerializer.cpp
    src/ecs/StructRegistry.cpp
    src/ecs/World.cpp)
add_library(illusion::core ALIAS illusion_core)

target_include_directories(illusion_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(illusion_core PUBLIC cxx_std_20)

find_package(Threads REQUIRED)
illusion_require_dependency(glm)
illusion_require_dependency(yyjson)
target_link_libraries(illusion_core PUBLIC Threads::Threads illusion::glm PRIVATE illusion::yyjson)
if(WIN32)
    # secureRandomBytes() uses BCryptGenRandom.
    target_link_libraries(illusion_core PRIVATE bcrypt)
endif()

# engineVersion() reports the project version; modules record the version they were built against.
target_compile_definitions(illusion_core PUBLIC ILLUSION_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    ILLUSION_VERSION_MINOR=${PROJECT_VERSION_MINOR} ILLUSION_VERSION_PATCH=${PROJECT_VERSION_PATCH})

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
