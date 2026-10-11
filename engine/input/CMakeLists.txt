# illusion::input — gamepads (SDL3, private), action mapping, rebinding, glyphs.

add_library(illusion_input STATIC
    src/Gamepad.cpp
    src/GamepadSystem.cpp
    src/InputActions.cpp)
add_library(illusion::input ALIAS illusion_input)

target_include_directories(illusion_input PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

illusion_require_dependency(sdl3)
target_link_libraries(illusion_input
    PUBLIC illusion::core illusion::platform
    PRIVATE SDL3::SDL3-static)

illusion_configure_target(illusion_input)
