# illusion_editor: the editor process (the window, or --headless with the UI offscreen). It links every engine
# module whose Components a scene may hold.

illusion_add_editor_executable(illusion_editor main.cpp)
target_link_libraries(illusion_editor PRIVATE
    illusion::editor_core illusion::editor_play illusion::editor_rpc illusion::editor_shell illusion::editor_viewport illusion::render
    illusion::assets illusion::input illusion::physics
    illusion::audio illusion::save)
# project.create names the illusion_mcp next to the editor in .mcp.json.
target_compile_definitions(illusion_editor PRIVATE ILLUSION_EXECUTABLE_SUFFIX="${CMAKE_EXECUTABLE_SUFFIX}")
# module.build compiles project modules with the module SDK of this build tree (ADR 0043), or, once installed,
# with the SDK installed beside the editor (cmake/IllusionSdk.cmake), named relative to the executable.
if(NOT ILLUSION_PROFILING)
    file(RELATIVE_PATH installed_sdk "${PROJECT_BINARY_DIR}/prefix/${CMAKE_INSTALL_BINDIR}/config"
        "${PROJECT_BINARY_DIR}/prefix/${CMAKE_INSTALL_LIBDIR}/cmake/illusion")
    target_compile_definitions(illusion_editor PRIVATE
        ILLUSION_MODULE_SDK="${PROJECT_BINARY_DIR}/sdk/$<CONFIG>/illusion-sdk.json"
        ILLUSION_INSTALLED_MODULE_SDK="${installed_sdk}/$<CONFIG>/illusion-sdk.json")
endif()
add_dependencies(illusion_editor illusion_editor_data)
illusion_configure_target(illusion_editor)
install(TARGETS illusion_editor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>" COMPONENT editor)
