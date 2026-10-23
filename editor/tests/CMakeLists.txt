# Tests of the editor. Built only with the editor (they are editor-only targets) and written to
# bin/<Config>/ next to editor-data.

# CPU tests: no window system and no GPU (label "unit").
illusion_add_editor_executable(illusion_editor_tests
    ${PROJECT_SOURCE_DIR}/tests/main.cpp
    ${PROJECT_SOURCE_DIR}/tests/AllocationCounter.cpp
    TestFontsAndIcons.cpp
    TestFrameText.cpp
    TestGallery.cpp
    TestTheme.cpp
    TestUiPlatform.cpp
    TestWidgets.cpp
    core/TestEditorAssets.cpp
    core/TestEditorClipboard.cpp
    core/TestEditorCommands.cpp
    core/TestEditorCore.cpp
    core/TestEditorFuzz.cpp
    core/TestEditorHistory.cpp
    core/TestEditorLog.cpp
    core/TestEditorSettings.cpp
    core/TestEditorTransforms.cpp)
target_include_directories(illusion_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(illusion_editor_tests PRIVATE illusion_ui_gallery_pages illusion::editor_core illusion::doctest)
# Golden files of the editor core (ILLUSION_UPDATE_GOLDENS=1 rewrites them).
target_compile_definitions(illusion_editor_tests PRIVATE ILLUSION_EDITOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_dependencies(illusion_editor_tests illusion_editor_data)
illusion_configure_target(illusion_editor_tests)
doctest_discover_tests(illusion_editor_tests
    PROPERTIES LABELS "unit"
    ADD_LABELS 0)

# GPU tests: golden images of the gallery and renderer behaviour (label "gpu"). Captures go to
# ui-captures/ in the build tree (or ILLUSION_UI_CAPTURE_DIR) for design review.
illusion_add_editor_executable(illusion_editor_gpu_tests
    ${PROJECT_SOURCE_DIR}/tests/gpu/main.cpp
    gpu/TestUiRenderer.cpp)
target_include_directories(illusion_editor_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_editor_gpu_tests PRIVATE illusion_ui_gallery_pages illusion::doctest)
target_compile_definitions(illusion_editor_gpu_tests PRIVATE
    ILLUSION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/golden"
    ILLUSION_UI_CAPTURE_DIR="${CMAKE_BINARY_DIR}/ui-captures")
add_dependencies(illusion_editor_gpu_tests illusion_editor_data)
illusion_configure_target(illusion_editor_gpu_tests)
doctest_discover_tests(illusion_editor_gpu_tests
    PROPERTIES LABELS "gpu"
    ADD_LABELS 0)
