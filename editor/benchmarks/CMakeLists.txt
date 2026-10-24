# Editor benchmarks (CPU). Share the runner of benchmarks/ and run with it in the "benchmark" label.

illusion_require_dependency(nanobench)
illusion_add_editor_executable(illusion_editor_benchmarks
    ${PROJECT_SOURCE_DIR}/benchmarks/main.cpp
    ${PROJECT_SOURCE_DIR}/benchmarks/Budget.cpp
    BenchEditorCore.cpp
    BenchEditorRpc.cpp
    BenchEditorShell.cpp
    BenchMcp.cpp
    BenchGallery.cpp)
target_include_directories(illusion_editor_benchmarks PRIVATE ${PROJECT_SOURCE_DIR}/benchmarks)
target_link_libraries(illusion_editor_benchmarks PRIVATE illusion_ui_gallery_pages illusion::editor_core illusion::editor_rpc illusion::editor_shell illusion::mcp_protocol illusion::nanobench)
add_dependencies(illusion_editor_benchmarks illusion_editor_data)
illusion_configure_target(illusion_editor_benchmarks)
add_test(NAME editor_benchmarks COMMAND illusion_editor_benchmarks CONFIGURATIONS Release RelWithDebInfo)
set_tests_properties(editor_benchmarks PROPERTIES LABELS "benchmark" RUN_SERIAL TRUE TIMEOUT 600)
