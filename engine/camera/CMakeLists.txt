# illusion::camera — virtual cameras (ADR 0064): the pure body, aim and noise stages, damping and blending over
# plain data. GPU-free and physics-free: the spring arm casts through CameraCollisionQuery, which engine/runtime
# implements.

add_library(illusion_camera STATIC
    src/CameraAim.cpp
    src/CameraBlend.cpp
    src/CameraBody.cpp
    src/CameraDamping.cpp
    src/CameraNoise.cpp
    src/CameraPose.cpp)
add_library(illusion::camera ALIAS illusion_camera)

target_include_directories(illusion_camera PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_camera PUBLIC illusion::core)

illusion_configure_target(illusion_camera)
