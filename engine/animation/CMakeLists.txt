# illusion::animation — skeletal animation (ADR 0059, ADR 0060): binding and sampling clips, composing poses,
# skin matrices and skinned bounds, animation graphs, the Animator Component, AnimationWorld and its
# AnimationService adapter. GPU-free: the renderer skins the meshes. AnimationWorld reads MeshRenderer through
# reflection, so that engine/render can link this library.

add_library(illusion_animation STATIC
    src/AnimationComponents.cpp
    src/AnimationGraph.cpp
    src/AnimationGraphState.cpp
    src/AnimationWorld.cpp
    src/AnimationWorldService.cpp
    src/InverseKinematics.cpp
    src/Pose.cpp
    src/Retarget.cpp
    src/SpringBones.cpp)
add_library(illusion::animation ALIAS illusion_animation)

target_include_directories(illusion_animation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_animation PUBLIC illusion::core illusion::assets)

illusion_configure_target(illusion_animation)

# illusion::animation_api — headers of the animation Components and of AnimationService, for game modules: no
# code, so a shared module gets no copy of the library (illusion_add_module(... USES animation), ADR 0041).
add_library(illusion_animation_api INTERFACE)
add_library(illusion::animation_api ALIAS illusion_animation_api)
target_include_directories(illusion_animation_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/assets/include>)
target_link_libraries(illusion_animation_api INTERFACE illusion::core)
