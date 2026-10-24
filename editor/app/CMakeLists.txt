# illusion_editor: the editor process (headless until the windowed shell, M7l). It links every engine module
# whose Components a scene may hold.

illusion_add_editor_executable(illusion_editor main.cpp)
target_link_libraries(illusion_editor PRIVATE
    illusion::editor_core illusion::editor_rpc illusion::render illusion::physics illusion::audio)
# project.create names the illusion_mcp next to the editor in .mcp.json.
target_compile_definitions(illusion_editor PRIVATE ILLUSION_EXECUTABLE_SUFFIX="${CMAKE_EXECUTABLE_SUFFIX}")
add_dependencies(illusion_editor illusion_editor_data)
illusion_configure_target(illusion_editor)
