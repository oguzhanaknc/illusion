# illusion::particles — particle effects for the ECS (ADR 0047): the ParticleEmitter Component and the
# deterministic CPU simulation of ParticleWorld. No GPU code: the renderer draws the pools.

add_library(illusion_particles STATIC
    src/ParticleComponents.cpp
    src/ParticleWorld.cpp)
add_library(illusion::particles ALIAS illusion_particles)

target_include_directories(illusion_particles PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_particles PUBLIC illusion::core)

illusion_configure_target(illusion_particles)
