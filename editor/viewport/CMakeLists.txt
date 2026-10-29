# illusion::editor_viewport — the editor's GPU viewports: the orbit camera and its navigation, one
# SceneRenderer per viewport rendering into a texture the UI shows, the grid and selection outline overlays,
# and asynchronous picking. It implements the ViewportHost of the viewport.* commands and the Scene panel.

illusion_add_editor_library(illusion_editor_viewport STATIC
    src/EditorCamera.cpp
    src/PickCandidates.cpp
    src/ResizePolicy.cpp
    src/ViewportOverlays.cpp
    src/ViewportService.cpp)
add_library(illusion::editor_viewport ALIAS illusion_editor_viewport)
target_include_directories(illusion_editor_viewport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_editor_viewport PUBLIC
    illusion::editor_core illusion::editor_ui illusion::render illusion::assets)
illusion_add_shaders(illusion_editor_viewport
    NAMESPACE illusion::editor::shaders
    HEADER illusion/editor/viewport/ViewportShaders.hpp
    SHADERS
        gridVertex shaders/Grid.slang vertexMain vertex
        gridFragment shaders/Grid.slang fragmentMain fragment
        outlineVertex shaders/Outline.slang vertexMain vertex
        outlineFragment shaders/Outline.slang fragmentMain fragment)
illusion_configure_target(illusion_editor_viewport)
