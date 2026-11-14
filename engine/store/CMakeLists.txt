# illusion::store — stores a game runs in (ADR 0056): the StoreService interface, the host's StoreBackend and
# NullStoreService, the store of a game without one. The Steam store is the optional library engine/steam.

add_library(illusion_store STATIC
    src/NullStoreService.cpp)
add_library(illusion::store ALIAS illusion_store)

target_include_directories(illusion_store PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_store PUBLIC illusion::core illusion::platform illusion::input_api)

illusion_configure_target(illusion_store)

# illusion::store_api — the StoreService interface, for game modules: no code, so a shared module gets no copy
# of the library (illusion_add_module(... USES store), ADR 0041).
add_library(illusion_store_api INTERFACE)
add_library(illusion::store_api ALIAS illusion_store_api)
target_include_directories(illusion_store_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_store_api INTERFACE illusion::core illusion::input_api)
