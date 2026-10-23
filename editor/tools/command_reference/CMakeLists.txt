# illusion_editor_command_reference: writes or checks the generated command table of the editor command
# reference (docs/content/{en,tr}/reference/editor-commands.md).

illusion_add_editor_executable(illusion_editor_command_reference main.cpp)
target_link_libraries(illusion_editor_command_reference PRIVATE illusion::editor_core)
illusion_configure_target(illusion_editor_command_reference)

if(ILLUSION_BUILD_TESTS)
    add_test(NAME editor.command_reference
        COMMAND illusion_editor_command_reference --check ${PROJECT_SOURCE_DIR}/docs/content)
    set_tests_properties(editor.command_reference PROPERTIES LABELS "unit")
endif()
