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
    core/TestEditorArrange.cpp
    core/TestEditorAssets.cpp
    core/TestEditorClipboard.cpp
    core/TestEditorCommands.cpp
    core/TestEditorCore.cpp
    core/TestEditorFuzz.cpp
    core/TestEditorGizmo.cpp
    core/TestEditorHistory.cpp
    core/TestEditorLog.cpp
    core/TestEditorSettings.cpp
    core/TestEditorSceneEditing.cpp
    core/TestEditorTransactions.cpp
    core/TestEditorTransforms.cpp
    core/TestEditorUi.cpp
    core/TestEditorViewport.cpp
    mcp/TestMcpBridge.cpp
    mcp/TestMcpProtocol.cpp
    rpc/TestEditorClient.cpp
    rpc/TestRpcApprovals.cpp
    rpc/TestRpcAsync.cpp
    rpc/TestRpcProtocol.cpp
    rpc/TestRpcServer.cpp
    rpc/TestRpcTransport.cpp
    shell/TestEditorShell.cpp
    viewport/TestEditorCamera.cpp
    viewport/TestVisualisers.cpp)
target_include_directories(illusion_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(illusion_editor_tests PRIVATE
    illusion_ui_gallery_pages illusion::editor_core illusion::editor_client illusion::editor_shell
    illusion::editor_viewport
    illusion::render illusion::physics illusion::audio illusion::doctest)
# Golden files of the editor core (ILLUSION_UPDATE_GOLDENS=1 rewrites them).
target_compile_definitions(illusion_editor_tests PRIVATE ILLUSION_EDITOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
# The client tests launch the editor.
target_compile_definitions(illusion_editor_tests PRIVATE ILLUSION_EDITOR_EXECUTABLE="$<TARGET_FILE:illusion_editor>"
    ILLUSION_MCP_EXECUTABLE="$<TARGET_FILE:illusion_mcp>")
add_dependencies(illusion_editor_tests illusion_editor_data illusion_editor illusion_mcp)
illusion_configure_target(illusion_editor_tests)
doctest_discover_tests(illusion_editor_tests
    PROPERTIES LABELS "unit"
    ADD_LABELS 0)

# GPU tests: golden images of the gallery and renderer behaviour (label "gpu"). Captures go to
# ui-captures/ in the build tree (or ILLUSION_UI_CAPTURE_DIR) for design review.
illusion_add_editor_executable(illusion_editor_gpu_tests
    ${PROJECT_SOURCE_DIR}/tests/gpu/main.cpp
    ${PROJECT_SOURCE_DIR}/tests/AllocationCounter.cpp
    gpu/TestEditorShellGpu.cpp
    gpu/TestUiRenderer.cpp
    gpu/TestViewportGpu.cpp)
target_include_directories(illusion_editor_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(illusion_editor_gpu_tests PRIVATE
    illusion_ui_gallery_pages illusion::editor_shell illusion::editor_client illusion::editor_viewport
    illusion::render
    illusion::physics illusion::audio illusion::doctest)
target_compile_definitions(illusion_editor_gpu_tests PRIVATE
    ILLUSION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/golden"
    ILLUSION_UI_CAPTURE_DIR="${CMAKE_BINARY_DIR}/ui-captures"
    ILLUSION_EDITOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
    ILLUSION_EDITOR_EXECUTABLE="$<TARGET_FILE:illusion_editor>")
add_dependencies(illusion_editor_gpu_tests illusion_editor_data illusion_editor)
illusion_configure_target(illusion_editor_gpu_tests)
doctest_discover_tests(illusion_editor_gpu_tests
    PROPERTIES LABELS "gpu"
    ADD_LABELS 0)

# The acceptance test of the agent workflow (ADR 0038): illusion_ctl replays the scripted Tetris transcript
# against a launched headless editor and runs the checks. Optimized builds only (label "acceptance").
add_test(NAME acceptance.tetris.board
    COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
        --editor-arg --deterministic --editor-arg --seed --editor-arg 1 --editor-arg --no-gpu
        replay ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/scripted.jsonl
        --checks ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/checks.jsonl --temp-workspace
    CONFIGURATIONS Release RelWithDebInfo)
set_tests_properties(acceptance.tetris.board PROPERTIES LABELS "acceptance" TIMEOUT 360)

# The same scenario with a GPU (labels "gpu" and "acceptance"): the C6 rendering checks run, including the
# game-view golden image tests/acceptance/tetris/golden/board.png. Every configuration, for the gpu CI job.
add_test(NAME acceptance.tetris.board.gpu
    COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
        --editor-arg --deterministic --editor-arg --seed --editor-arg 1
        replay ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/scripted.jsonl
        --checks ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/checks.jsonl --temp-workspace --gpu)
set_tests_properties(acceptance.tetris.board.gpu PROPERTIES LABELS "acceptance;gpu" TIMEOUT 600
    ENVIRONMENT "ILLUSION_UI_CAPTURE_DIR=${CMAKE_BINARY_DIR}/ui-captures")

# The same scenario through the MCP bridge in both protocol eras (ADR 0039): every step is a tool call.
foreach(era legacy modern)
    add_test(NAME acceptance.tetris.board.mcp-${era}
        COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
            --editor-arg --deterministic --editor-arg --seed --editor-arg 1 --editor-arg --no-gpu
            replay ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/scripted.jsonl
            --checks ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/checks.jsonl --temp-workspace
            --via-mcp $<TARGET_FILE:illusion_mcp> --era ${era}
        CONFIGURATIONS Release RelWithDebInfo)
    set_tests_properties(acceptance.tetris.board.mcp-${era} PROPERTIES LABELS "acceptance" TIMEOUT 360)
endforeach()

# The Python client (tools/editor/illusion_rpc.py) against the editor.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME editor.python_client
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/editor/test_illusion_rpc.py
            $<TARGET_FILE:illusion_editor>)
    set_tests_properties(editor.python_client PROPERTIES LABELS "unit")
endif()
