cmake_minimum_required(VERSION 3.28)

project(illusion
    VERSION 0.1.0
    DESCRIPTION "illusion 3D game engine"
    HOMEPAGE_URL "https://github.com/oguzhanaknc/illusion"
    LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a preset: cmake --preset <name>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(IllusionOptions)
include(IllusionCompilerSettings)
include(Dependencies)
include(IllusionShaders)

add_subdirectory(engine)

if(ILLUSION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ILLUSION_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(ILLUSION_BUILD_TOOLS)
    add_subdirectory(tools/pack)
endif()

if(ILLUSION_BUILD_BENCHMARKS)
    if(NOT ILLUSION_BUILD_TESTS)
        enable_testing()
    endif()
    add_subdirectory(benchmarks)
endif()
