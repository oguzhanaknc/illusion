# illusion::ai — behaviour trees, blackboards and perception (ADR 0062): the `.behavior` format, the tree
# state and its tick, the geometry of sight and hearing, the AI Components and AiWorld (perception through
# PhysicsWorld raycasts, trees moving NavAgents through NavWorld). and its AiService adapter, AiWorldService.

add_library(illusion_ai STATIC
    src/AiComponents.cpp
    src/AiWorld.cpp
    src/AiWorldService.cpp
    src/BehaviorTree.cpp
    src/BehaviorTreeState.cpp)
add_library(illusion::ai ALIAS illusion_ai)

target_include_directories(illusion_ai PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_ai
    PUBLIC illusion::core illusion::nav illusion::physics
    PRIVATE illusion::assets)

illusion_configure_target(illusion_ai)

# illusion::ai_api — headers of the AI Components, behaviour trees and AiService, for game modules: no code, so
# a shared module gets no copy of the library (illusion_add_module(... USES ai), ADR 0041).
add_library(illusion_ai_api INTERFACE)
add_library(illusion::ai_api ALIAS illusion_ai_api)
target_include_directories(illusion_ai_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_ai_api INTERFACE illusion::core)
