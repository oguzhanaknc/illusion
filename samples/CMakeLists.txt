add_executable(illusion_hello_triangle hello_triangle/main.cpp)
target_link_libraries(illusion_hello_triangle PRIVATE illusion::render)
illusion_configure_target(illusion_hello_triangle)

add_executable(illusion_input_viewer input_viewer/main.cpp)
target_link_libraries(illusion_input_viewer PRIVATE illusion::input illusion::platform)
illusion_configure_target(illusion_input_viewer)

add_executable(illusion_gltf_viewer gltf_viewer/main.cpp)
target_include_directories(illusion_gltf_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_gltf_viewer PRIVATE illusion::render illusion::input illusion::assets)
illusion_configure_target(illusion_gltf_viewer)

add_executable(illusion_physics_playground physics_playground/main.cpp)
target_include_directories(illusion_physics_playground PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_physics_playground PRIVATE
    illusion::render illusion::input illusion::assets illusion::physics illusion::audio)
illusion_configure_target(illusion_physics_playground)

# The showcase project (samples/showcase) as an exported game: its module linked statically, its data packed
# next to the executable by illusion_pack (ADR 0055).
if(ILLUSION_BUILD_TOOLS)
    illusion_add_module(showcase_game_module STATIC ID showcase
        SOURCES showcase/modules/showcase/showcase.cpp
        USES physics audio particles input render runtime)
    illusion_add_game(showcase_game
        MODULES showcase_game_module
        NAME "Showcase"
        ORGANIZATION "illusion"
        VERSION "0.1.0"
        STARTUP_SCENE "assets:/scenes/everything.scene"
        PROJECT showcase)
endif()
