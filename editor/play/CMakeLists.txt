# illusion::editor_play — the editor's game runtime (ADR 0042): PlaySession runs one GameSession at a time on a
# copy of the edit World, owns the editor's ModuleHost and implements the PlayHost of the play.*, input.inject,
# input.read and module.* commands. GPU-free: the Game view renders the play World through editor_viewport.

illusion_add_editor_library(illusion_editor_play STATIC
    src/PlaySession.cpp)
add_library(illusion::editor_play ALIAS illusion_editor_play)
target_include_directories(illusion_editor_play PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_editor_play PUBLIC
    illusion::editor_core illusion::runtime illusion::modules illusion::audio
    PRIVATE illusion::input illusion::physics)
illusion_configure_target(illusion_editor_play)
