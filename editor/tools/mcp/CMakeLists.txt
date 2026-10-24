# illusion_mcp: the MCP bridge (ADR 0039). It finds illusion_editor next to itself unless --editor is given.

illusion_add_editor_executable(illusion_mcp main.cpp)
target_link_libraries(illusion_mcp PRIVATE illusion::editor_client)
target_compile_definitions(illusion_mcp PRIVATE ILLUSION_EXECUTABLE_SUFFIX="${CMAKE_EXECUTABLE_SUFFIX}")
add_dependencies(illusion_mcp illusion_editor)
illusion_configure_target(illusion_mcp)
