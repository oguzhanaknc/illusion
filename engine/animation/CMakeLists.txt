# illusion::animation — skeletal animation (ADR 0059): binding and sampling clips, composing poses, skin
# matrices and skinned bounds. GPU-free: the renderer skins the meshes.

add_library(illusion_animation STATIC
    src/Pose.cpp)
add_library(illusion::animation ALIAS illusion_animation)

target_include_directories(illusion_animation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_animation PUBLIC illusion::core illusion::assets)

illusion_configure_target(illusion_animation)
