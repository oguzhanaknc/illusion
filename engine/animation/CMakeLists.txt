# illusion::animation — skeletal animation (ADR 0059): binding and sampling clips, composing poses, skin
# matrices and skinned bounds, the Animator Component and AnimationWorld. GPU-free: the renderer skins the
# meshes. AnimationWorld reads MeshRenderer through reflection, so that engine/render can link this library.

add_library(illusion_animation STATIC
    src/AnimationComponents.cpp
    src/AnimationWorld.cpp
    src/Pose.cpp)
add_library(illusion::animation ALIAS illusion_animation)

target_include_directories(illusion_animation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_animation PUBLIC illusion::core illusion::assets)

illusion_configure_target(illusion_animation)
