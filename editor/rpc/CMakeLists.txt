# illusion::editor_rpc — the editor's JSON-RPC 2.0 server over NDJSON on stdio and AF_UNIX sockets: framing,
# authentication, permission modes, limits and the main-thread command pump (ADR 0037).

illusion_add_editor_library(illusion_editor_rpc STATIC
    src/RpcProtocol.cpp
    src/RpcServer.cpp
    src/RpcStream.cpp)
add_library(illusion::editor_rpc ALIAS illusion_editor_rpc)

target_include_directories(illusion_editor_rpc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(illusion_editor_rpc PUBLIC illusion::editor_core illusion::platform)
if(WIN32)
    # Winsock (AF_UNIX sockets) and the security API (owner-only access lists).
    target_link_libraries(illusion_editor_rpc PRIVATE ws2_32 advapi32)
endif()

illusion_configure_target(illusion_editor_rpc)
