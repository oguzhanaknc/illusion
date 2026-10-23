# illusion_ctl: command-line client of the editor protocol and runner of the acceptance transcripts.

illusion_add_editor_executable(illusion_ctl main.cpp)
target_link_libraries(illusion_ctl PRIVATE illusion::editor_client)
illusion_configure_target(illusion_ctl)
