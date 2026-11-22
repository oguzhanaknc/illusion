# illusion::nav — navigation (ADR 0062): the navigation Components, the .navmesh format and the bake of
# navmesh tiles with Recast. Recast and Detour are private: no public header includes them.

add_library(illusion_nav STATIC
    src/NavAllocator.cpp
    src/NavBuild.cpp
    src/NavComponents.cpp
    src/NavCrowd.cpp
    src/NavMeshData.cpp
    src/NavWorld.cpp)
add_library(illusion::nav ALIAS illusion_nav)

target_include_directories(illusion_nav PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

illusion_require_dependency(recast)
target_link_libraries(illusion_nav
    PUBLIC illusion::core illusion::physics_api
    PRIVATE illusion::assets illusion::recast illusion::detour illusion::detour_crowd illusion::recast_debug)

illusion_configure_target(illusion_nav)

# illusion::nav_api — headers of the navigation Components and of NavService, for game modules: no code, so a
# shared module gets no copy of the library (illusion_add_module(... USES nav), ADR 0041).
add_library(illusion_nav_api INTERFACE)
add_library(illusion::nav_api ALIAS illusion_nav_api)
target_include_directories(illusion_nav_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_nav_api INTERFACE illusion::core)
