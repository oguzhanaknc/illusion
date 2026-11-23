# illusion::camera — virtual cameras (ADR 0064): the pure body, aim and noise stages, damping and blending over
# plain data, the camera Components and CameraWorld (the brains, blends and shakes). GPU-free and physics-free:
# the spring arm casts through CameraWorldCollision, which engine/runtime implements; the Camera's lens is
# written through reflection. CameraWorldService adapts it to the scripting API.

add_library(illusion_camera STATIC
    src/CameraAim.cpp
    src/CameraBlend.cpp
    src/CameraBody.cpp
    src/CameraComponents.cpp
    src/CameraDamping.cpp
    src/CameraNoise.cpp
    src/CameraPose.cpp
    src/CameraWorld.cpp
    src/CameraWorldService.cpp)
add_library(illusion::camera ALIAS illusion_camera)

target_include_directories(illusion_camera PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_camera PUBLIC illusion::core illusion::input)

illusion_configure_target(illusion_camera)

# illusion::camera_api: headers of the camera Components and CameraService, for game modules. No code, so a
# shared module gets no copy of the library (illusion_add_module(... USES camera), ADR 0041).
add_library(illusion_camera_api INTERFACE)
add_library(illusion::camera_api ALIAS illusion_camera_api)
target_include_directories(illusion_camera_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_camera_api INTERFACE illusion::core)
