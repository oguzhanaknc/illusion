# illusion::physics — rigid-body physics for the ECS (Jolt Physics is private).

add_library(illusion_physics STATIC
    src/JoltJobSystem.cpp
    src/PhysicsAssetResolver.cpp
    src/PhysicsComponents.cpp
    src/PhysicsWorld.cpp)
add_library(illusion::physics ALIAS illusion_physics)

target_include_directories(illusion_physics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

illusion_require_dependency(jolt)
target_link_libraries(illusion_physics
    PUBLIC illusion::core
    PRIVATE illusion::assets Jolt::Jolt)

illusion_configure_target(illusion_physics)
