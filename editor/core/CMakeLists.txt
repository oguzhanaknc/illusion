# illusion::editor_core — the headless editor core: projects, the edited scene, typed commands, transactions,
# undo, the change and crash journals (ADR 0036). No GPU, Dear ImGui or sockets.

illusion_add_editor_library(illusion_editor_core STATIC
    src/ChangeJournal.cpp
    src/CommandError.cpp
    src/CommandRegistry.cpp
    src/CrashJournal.cpp
    src/EditorContext.cpp
    src/EntityIndex.cpp
    src/JsonSchema.cpp
    src/Operation.cpp
    src/Project.cpp
    src/ReferenceIndex.cpp
    src/Selection.cpp
    src/SettingsCatalog.cpp
    src/UndoStack.cpp
    src/WorldEditor.cpp
    src/commands/AssetCommands.cpp
    src/commands/ClipboardCommands.cpp
    src/commands/ComponentCommands.cpp
    src/commands/CoreCommands.cpp
    src/commands/EditorCommands.cpp
    src/commands/EntityCommands.cpp
    src/commands/HierarchyCommands.cpp
    src/commands/HistoryCommands.cpp
    src/commands/LogCommands.cpp
    src/commands/MaterialCommands.cpp
    src/commands/ProjectCommands.cpp
    src/commands/SceneCommands.cpp
    src/commands/SelectionCommands.cpp
    src/commands/SettingsCommands.cpp
    src/commands/TransformCommands.cpp)
add_library(illusion::editor_core ALIAS illusion_editor_core)

target_include_directories(illusion_editor_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_editor_core PUBLIC
    illusion::core illusion::platform illusion::text illusion::editor_strings
    PRIVATE illusion::assets)

illusion_configure_target(illusion_editor_core)
