# illusion::input — gamepads (SDL3, private), action mapping, rebinding, glyphs.

add_library(illusion_input STATIC
    src/Gamepad.cpp
    src/GamepadSystem.cpp
    src/InputActions.cpp
    src/InputActionsService.cpp)
add_library(illusion::input ALIAS illusion_input)

target_include_directories(illusion_input PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

illusion_require_dependency(sdl3)
target_link_libraries(illusion_input
    PUBLIC illusion::core illusion::platform
    PRIVATE SDL3::SDL3-static)

# Translatable labels of keys and gamepad glyphs (glyphLabelKey()).
illusion_add_string_table(illusion_input
    TABLE input
    SOURCE data/locales/en.json
    NAMESPACE illusion::input::strings
    HEADER illusion/input/InputStrings.hpp)

illusion_configure_target(illusion_input)

# illusion::input_api — headers of the input types (keys, gamepad buttons, actions) and of InputService, for
# game modules: no code, so a shared module gets no copy of the library (illusion_add_module(... USES
# input), ADR 0041).
add_library(illusion_input_api INTERFACE)
add_library(illusion::input_api ALIAS illusion_input_api)
target_include_directories(illusion_input_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/platform/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/engine/text/include>)
target_link_libraries(illusion_input_api INTERFACE illusion::core)
