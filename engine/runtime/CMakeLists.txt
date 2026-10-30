# illusion::runtime — the game runtime around the engine libraries: for now the host's SceneService
# (SceneLoaderService); the M7r GameSession joins it.

add_library(illusion_runtime STATIC
    src/SceneLoaderService.cpp)
add_library(illusion::runtime ALIAS illusion_runtime)

target_include_directories(illusion_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(illusion_runtime PUBLIC illusion::core PRIVATE illusion::platform)

illusion_configure_target(illusion_runtime)

# illusion::runtime_api — the SceneService interface, for game modules: no code, so a shared module gets
# no copy of the library (illusion_add_module(... USES runtime), ADR 0041).
add_library(illusion_runtime_api INTERFACE)
add_library(illusion::runtime_api ALIAS illusion_runtime_api)
target_include_directories(illusion_runtime_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_runtime_api INTERFACE illusion::core)
