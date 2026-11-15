# illusion::ui — the game UI (ADR 0057): geometry of canvases and placements, the draw list of rectangles,
# images, nine-slice panels and text, the saved UI Components and the UI System (UiWorld: layout and the
# frame's draw list). No GPU code: UiRenderer draws the list.

add_library(illusion_ui STATIC
    src/UiComponents.cpp
    src/UiDrawList.cpp
    src/UiGeometry.cpp
    src/UiWorld.cpp
    src/UiWorldService.cpp)
add_library(illusion::ui ALIAS illusion_ui)

target_include_directories(illusion_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_ui PUBLIC illusion::core PRIVATE illusion::assets illusion::text)

illusion_configure_target(illusion_ui)

# illusion::ui_api — headers of the UI Components and the UiService interface, for game modules: no code, so a shared module gets no copy
# of the library (illusion_add_module(... USES ui), ADR 0041).
add_library(illusion_ui_api INTERFACE)
add_library(illusion::ui_api ALIAS illusion_ui_api)
target_include_directories(illusion_ui_api INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_ui_api INTERFACE illusion::core)
