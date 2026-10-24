# illusion::mcp_protocol — a pure Model Context Protocol library over core Json: era negotiation, tools
# generated from the editor's command catalogue, results, progress, cancellation, resources and prompts
# (ADR 0039). It knows nothing about sockets, processes or the editor core.

illusion_add_editor_library(illusion_mcp_protocol STATIC
    src/McpCatalog.cpp
    src/McpServer.cpp)
add_library(illusion::mcp_protocol ALIAS illusion_mcp_protocol)

target_include_directories(illusion_mcp_protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_mcp_protocol PUBLIC illusion::core)

illusion_configure_target(illusion_mcp_protocol)
