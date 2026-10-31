# illusion::editor_shell — the editor window's user interface: menu bar, toolbar, dock layouts, status bar,
# toasts, the command palette, console, history, AI Activity with approvals, preferences and the project
# windows. It drives editor state through commands and implements the UiHost of the ui.* commands.

illusion_add_editor_library(illusion_editor_shell STATIC
    src/Agents.cpp
    src/Console.cpp
    src/EditorShell.cpp
    src/GameInput.cpp
    src/Gizmos.cpp
    src/History.cpp
    src/Inspector.cpp
    src/Layouts.cpp
    src/Outliner.cpp
    src/Palette.cpp
    src/PreferencesWindow.cpp
    src/Projects.cpp
    src/SceneView.cpp)
add_library(illusion::editor_shell ALIAS illusion_editor_shell)
target_include_directories(illusion_editor_shell PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_editor_shell PUBLIC
    illusion::editor_core illusion::editor_ui illusion::editor_rpc illusion::editor_strings)
illusion_configure_target(illusion_editor_shell)
