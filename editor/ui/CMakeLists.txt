# illusion::editor_ui — the editor's UI foundation: Dear ImGui platform and renderer backends, theme,
# fonts, icons, per-frame text, the widget kit and the UiDriver test helper.

illusion_add_editor_library(illusion_editor_ui STATIC
    src/FontManager.cpp
    src/FrameText.cpp
    src/Icon.cpp
    src/Theme.cpp
    src/UiContext.cpp
    src/UiDriver.cpp
    src/UiItemRegistry.cpp
    src/UiPlatformBackend.cpp
    src/UiRenderer.cpp
    src/Widgets.cpp)
add_library(illusion::editor_ui ALIAS illusion_editor_ui)

target_include_directories(illusion_editor_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_editor_ui PUBLIC
    illusion::core illusion::platform illusion::render illusion::text illusion::imgui illusion::implot)

illusion_add_shaders(illusion_editor_ui
    NAMESPACE illusion::editor::ui::shaders
    HEADER illusion/editor/ui/UiShaders.hpp
    SHADERS
        uiVertex shaders/Ui.slang vertexMain vertex
        uiFragment shaders/Ui.slang fragmentMain fragment
        presentVertex shaders/Present.slang vertexMain vertex
        presentFragment shaders/Present.slang fragmentMain fragment)

# Icon constants from the pinned Material Symbols codepoints.
illusion_require_dependency(fonts)
set(codepoints ${ILLUSION_EDITOR_FONTS})
list(FILTER codepoints INCLUDE REGEX "\.codepoints$")
illusion_add_icon_header(illusion_editor_ui
    CODEPOINTS ${codepoints}
    NAMESPACE illusion::editor::ui::icons
    HEADER illusion/editor/ui/Icons.hpp)

illusion_configure_target(illusion_editor_ui)
