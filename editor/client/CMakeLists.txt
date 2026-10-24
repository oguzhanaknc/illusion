# illusion::editor_client — a client of the editor protocol: socket and endpoint connections, launching an
# editor over stdio, the transcript replayer and checker of the acceptance tests (ADR 0038), and both sides of
# the MCP bridge: the editor tool provider of illusion_mcp and an MCP client of it (ADR 0039).

illusion_add_editor_library(illusion_editor_client STATIC
    src/EditorToolProvider.cpp
    src/McpClient.cpp
    src/RpcClient.cpp
    src/Transcript.cpp)
add_library(illusion::editor_client ALIAS illusion_editor_client)

target_include_directories(illusion_editor_client PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_editor_client PUBLIC illusion::editor_rpc illusion::mcp_protocol illusion::platform)

illusion_configure_target(illusion_editor_client)
