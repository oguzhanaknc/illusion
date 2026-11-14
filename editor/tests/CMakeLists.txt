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
    core/TestAssetCatalog.cpp
    core/TestEditorAssets.cpp
    core/TestEditorImportSettings.cpp
    core/TestEditorClipboard.cpp
    core/TestEditorCommands.cpp
    core/TestEditorCore.cpp
    core/TestEditorExport.cpp
    core/TestEditorFuzz.cpp
    core/TestEditorGamepad.cpp
    core/TestEditorGizmo.cpp
    core/TestEditorHistory.cpp
    core/TestEditorLog.cpp
    core/TestEditorModules.cpp
    core/TestParticleCommands.cpp
    core/TestEditorPrefabs.cpp
    core/TestEditorProfiler.cpp
    core/TestEditorSettings.cpp
    core/TestEditorSceneEditing.cpp
    core/TestEditorSnap.cpp
    core/TestEditorThumbnails.cpp
    core/TestEditorTransactions.cpp
    core/TestEditorTransforms.cpp
    core/TestEditorUi.cpp
    core/TestEditorViewport.cpp
    mcp/TestMcpBridge.cpp
    mcp/TestMcpProtocol.cpp
    play/TestEditorGamepads.cpp
    play/TestEditorPhysicsQueries.cpp
    play/TestPlayMode.cpp
    play/TestShowcase.cpp
    play/TestTetris.cpp
    rpc/TestEditorClient.cpp
    rpc/TestRpcApprovals.cpp
    rpc/TestRpcAsync.cpp
    rpc/TestRpcProtocol.cpp
    rpc/TestRpcServer.cpp
    rpc/TestRpcTransport.cpp
    shell/TestEditorShell.cpp
    shell/TestExportWindow.cpp
    viewport/TestEditorCamera.cpp
    viewport/TestRenderQuality.cpp
    viewport/TestVisualisers.cpp)
target_include_directories(illusion_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(illusion_editor_tests PRIVATE
    illusion_ui_gallery_pages illusion::editor_core illusion::editor_client illusion::editor_shell
    illusion::editor_viewport illusion::editor_play
    illusion::render illusion::input illusion::physics illusion::audio illusion::doctest)
# Golden files of the editor core (ILLUSION_UPDATE_GOLDENS=1 rewrites them).
target_compile_definitions(illusion_editor_tests PRIVATE ILLUSION_EDITOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
    ILLUSION_ENGINE_TEST_DATA="${PROJECT_SOURCE_DIR}/tests/data" ILLUSION_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples")
# The client tests launch the editor.
target_compile_definitions(illusion_editor_tests PRIVATE ILLUSION_EDITOR_EXECUTABLE="$<TARGET_FILE:illusion_editor>"
    ILLUSION_MCP_EXECUTABLE="$<TARGET_FILE:illusion_mcp>")
add_dependencies(illusion_editor_tests illusion_editor_data illusion_editor illusion_mcp)
# The Tetris reference module of the acceptance test (tests/acceptance/tetris/SPEC.md), loaded by the play
# tests and by the C5 checks of the acceptance transcript.
illusion_add_module(illusion_tetris_module SHARED
    SOURCES ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/module/Tetris.cpp
    USES input render runtime)
# The module the recorded agent session wrote (tests/acceptance/tetris/files), built in-tree so that an engine
# API change that breaks it fails the build; the play tests check every rule on it too.
illusion_add_module(illusion_tetris_agent_module SHARED
    SOURCES ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/files/modules/tetris/tetris.cpp
    USES input render runtime)
target_compile_definitions(illusion_editor_tests PRIVATE
    ILLUSION_TETRIS_MODULE="$<TARGET_FILE:illusion_tetris_module>"
    ILLUSION_TETRIS_AGENT_MODULE="$<TARGET_FILE:illusion_tetris_agent_module>")
add_dependencies(illusion_editor_tests illusion_tetris_module illusion_tetris_agent_module)
# The module build tests run a stand-in for CMake, and one real build with the module SDK of this build tree
# (ADR 0043; profiling builds have no SDK).
illusion_add_editor_executable(illusion_fake_cmake FakeCMake.cpp)
# The same program as cpack next to it: exports run cpack from the directory of their cmake.
illusion_add_editor_executable(illusion_fake_cpack FakeCMake.cpp)
set_target_properties(illusion_fake_cmake illusion_fake_cpack PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/fake-tools")
set_target_properties(illusion_fake_cpack PROPERTIES OUTPUT_NAME cpack)
illusion_configure_target(illusion_fake_cmake)
illusion_configure_target(illusion_fake_cpack)
target_compile_definitions(illusion_editor_tests PRIVATE ILLUSION_FAKE_CMAKE="$<TARGET_FILE:illusion_fake_cmake>")
add_dependencies(illusion_editor_tests illusion_fake_cmake illusion_fake_cpack)
# Exports (export.build) cook and pack with illusion_pack, also with the stand-in for CMake.
if(NOT TARGET illusion_pack)
    message(FATAL_ERROR "The editor tests need illusion_pack: configure with ILLUSION_BUILD_TOOLS=ON")
endif()
target_compile_definitions(illusion_editor_tests PRIVATE ILLUSION_PACK_EXECUTABLE="$<TARGET_FILE:illusion_pack>")
add_dependencies(illusion_editor_tests illusion_pack)
if(NOT ILLUSION_PROFILING)
    target_compile_definitions(illusion_editor_tests PRIVATE
        ILLUSION_MODULE_SDK="${PROJECT_BINARY_DIR}/sdk/$<CONFIG>/illusion-sdk.json")
endif()
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
    gpu/TestThumbnailGpu.cpp
    gpu/TestShowcaseGpu.cpp
    gpu/TestUiRenderer.cpp
    gpu/TestViewportGpu.cpp)
target_include_directories(illusion_editor_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(illusion_editor_gpu_tests PRIVATE
    illusion_ui_gallery_pages illusion::editor_shell illusion::editor_client illusion::editor_viewport
    illusion::editor_play illusion::render
    illusion::physics illusion::audio illusion::particles illusion::doctest)
target_compile_definitions(illusion_editor_gpu_tests PRIVATE
    ILLUSION_FAKE_CMAKE="$<TARGET_FILE:illusion_fake_cmake>"
    ILLUSION_PACK_EXECUTABLE="$<TARGET_FILE:illusion_pack>"
    ILLUSION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/golden"
    ILLUSION_UI_CAPTURE_DIR="${CMAKE_BINARY_DIR}/ui-captures"
    ILLUSION_EDITOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
    ILLUSION_ENGINE_TEST_DATA="${PROJECT_SOURCE_DIR}/tests/data"
    ILLUSION_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples"
    ILLUSION_EDITOR_EXECUTABLE="$<TARGET_FILE:illusion_editor>")
add_dependencies(illusion_editor_gpu_tests illusion_editor_data illusion_editor)
# The showcase module (tests/CMakeLists.txt) for the Play mode golden of tests/gpu/TestShowcaseGpu.cpp.
target_compile_definitions(illusion_editor_gpu_tests PRIVATE
    SHOWCASE_MODULE="$<TARGET_FILE:illusion_showcase_module>")
add_dependencies(illusion_editor_gpu_tests illusion_showcase_module)
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
        --var "tetrisModule=\"$<TARGET_FILE:illusion_tetris_module>\""
    CONFIGURATIONS Release RelWithDebInfo)
set_tests_properties(acceptance.tetris.board PROPERTIES LABELS "acceptance" TIMEOUT 360)

# The same scenario with a GPU (labels "gpu" and "acceptance"): the C6 rendering checks run, including the
# game-view golden image tests/acceptance/tetris/golden/board.png. Every configuration, for the gpu CI job.
add_test(NAME acceptance.tetris.board.gpu
    COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
        --editor-arg --deterministic --editor-arg --seed --editor-arg 1
        replay ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/scripted.jsonl
        --checks ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/checks.jsonl --temp-workspace --gpu
        --var "tetrisModule=\"$<TARGET_FILE:illusion_tetris_module>\"")
set_tests_properties(acceptance.tetris.board.gpu PROPERTIES LABELS "acceptance;gpu" TIMEOUT 600
    ENVIRONMENT "ILLUSION_UI_CAPTURE_DIR=${CMAKE_BINARY_DIR}/ui-captures")

# The recorded agent session (tests/acceptance/tetris/recorded.jsonl): an agent built the game through the MCP
# tools, writing and compiling its own module (files/), and the same checks verify it. module.build compiles
# the module with this build tree's module SDK, which profiling builds do not have.
if(NOT ILLUSION_PROFILING)
    add_test(NAME acceptance.tetris.recorded
        COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
            --editor-arg --deterministic --editor-arg --seed --editor-arg 1 --editor-arg --no-gpu
            replay ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/recorded.jsonl
            --checks ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/checks.jsonl --temp-workspace)
    set_tests_properties(acceptance.tetris.recorded PROPERTIES LABELS "acceptance" TIMEOUT 600)
    add_test(NAME acceptance.tetris.recorded.gpu
        COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
            --editor-arg --deterministic --editor-arg --seed --editor-arg 1
            replay ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/recorded.jsonl
            --checks ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/checks.jsonl --temp-workspace --gpu)
    set_tests_properties(acceptance.tetris.recorded.gpu PROPERTIES LABELS "acceptance;gpu" TIMEOUT 600
        ENVIRONMENT "ILLUSION_UI_CAPTURE_DIR=${CMAKE_BINARY_DIR}/ui-captures")
    add_test(NAME acceptance.tetris.recorded.mcp
        COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
            --editor-arg --deterministic --editor-arg --seed --editor-arg 1 --editor-arg --no-gpu
            replay ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/recorded.jsonl
            --checks ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/checks.jsonl --temp-workspace
            --via-mcp $<TARGET_FILE:illusion_mcp> --era modern)
    set_tests_properties(acceptance.tetris.recorded.mcp PROPERTIES LABELS "acceptance" TIMEOUT 600)

    # Exported games (ADR 0055): a headless editor exports the showcase and the Tetris of the recorded agent
    # session with this build tree's SDK (label "sdk": it compiles), each game runs 300 frames (labels "gpu" and
    # "sdk"), and neither executable holds editor or Dear ImGui code (ADR 0030).
    set(export_script ${PROJECT_SOURCE_DIR}/tests/sdk/ExportGame.cmake)
    set(export_transcript ${PROJECT_SOURCE_DIR}/tests/sdk/export.jsonl)
    set(export_work ${CMAKE_BINARY_DIR}/export/$<CONFIG>)
    add_test(NAME sdk.export.showcase.project
        COMMAND ${CMAKE_COMMAND} -DMODE=copy -DSOURCE=${PROJECT_SOURCE_DIR}/samples/showcase
            -DDESTINATION=${export_work}/showcase -P ${export_script})
    add_test(NAME sdk.export.showcase
        COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
            --editor-arg --headless --editor-arg --no-gpu --editor-arg --project --editor-arg ${export_work}/showcase
            replay ${export_transcript}
            --var "output=\"${export_work}/showcase-out\"" --var "configuration=\"$<CONFIG>\"")
    add_test(NAME sdk.export.tetris
        COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
            --editor-arg --deterministic --editor-arg --seed --editor-arg 1 --editor-arg --no-gpu
            replay ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/recorded.jsonl --checks ${export_transcript}
            --temp-workspace --var "output=\"${export_work}/tetris-out\"" --var "configuration=\"$<CONFIG>\"")
    set_tests_properties(sdk.export.showcase.project PROPERTIES LABELS "unit;sdk"
        FIXTURES_SETUP export_showcase_project)
    set_tests_properties(sdk.export.showcase PROPERTIES LABELS "unit;sdk" TIMEOUT 1800
        FIXTURES_REQUIRED export_showcase_project FIXTURES_SETUP export_showcase)
    set_tests_properties(sdk.export.tetris PROPERTIES LABELS "unit;sdk" TIMEOUT 1800 FIXTURES_SETUP export_tetris)
    foreach(game showcase tetris)
        if(game STREQUAL "showcase")
            set(product Showcase)
        else()
            set(product Tetris)
        endif()
        if(APPLE)
            set(executable ${export_work}/${game}-out/${product}.app/Contents/MacOS/${product})
        else()
            set(executable ${export_work}/${game}-out/${product}/${game}${CMAKE_EXECUTABLE_SUFFIX})
        endif()
        add_test(NAME sdk.export.${game}.run
            COMMAND ${CMAKE_COMMAND} -DMODE=run -DGAME=${executable} -DUSER=${export_work}/${game}-user
                -P ${export_script})
        add_test(NAME sdk.export.${game}.symbols
            COMMAND ${CMAKE_COMMAND} -DMODE=symbols -DGAME=${executable} -P ${export_script})
        set_tests_properties(sdk.export.${game}.run PROPERTIES LABELS "gpu;sdk" TIMEOUT 600
            FIXTURES_REQUIRED export_${game})
        set_tests_properties(sdk.export.${game}.symbols PROPERTIES LABELS "unit;sdk"
            FIXTURES_REQUIRED export_${game})
    endforeach()

    # Packages and signing (ADR 0055) on the showcase's build tree: Windows signs with a recording sign command
    # (tests/sdk/RecordSign.cmake through ILLUSION_SIGN_COMMAND) and packages a ZIP, and an NSIS installer when
    # makensis is installed (otherwise the export must fail naming it); macOS signs ad hoc and makes a DMG;
    # Linux makes a .tar.gz.
    set(package_output ${export_work}/showcase-package)
    set(package_transcript ${PROJECT_SOURCE_DIR}/tests/sdk/export-package.jsonl)
    set(package_vars --var "configuration=\"$<CONFIG>\"")
    if(WIN32)
        set(program_files_x86 "ProgramFiles(x86)")
        find_program(ILLUSION_TEST_MAKENSIS makensis
            PATHS "$ENV{${program_files_x86}}/NSIS" "$ENV{ProgramFiles}/NSIS")
        set(package_kinds archive)
        if(ILLUSION_TEST_MAKENSIS)
            list(APPEND package_kinds installer)
        else()
            add_test(NAME sdk.export.showcase.installer
                COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
                    --editor-arg --headless --editor-arg --no-gpu --editor-arg --project
                    --editor-arg ${export_work}/showcase
                    replay ${PROJECT_SOURCE_DIR}/tests/sdk/export-missing-tool.jsonl
                    --var "output=\"${package_output}-installer\"" ${package_vars})
            set_tests_properties(sdk.export.showcase.installer PROPERTIES LABELS "unit;sdk" TIMEOUT 600
                FIXTURES_REQUIRED export_showcase)
        endif()
        foreach(kind IN LISTS package_kinds)
            set(log ${package_output}-${kind}/signed.txt)
            add_test(NAME sdk.export.showcase.${kind}
                COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
                    --editor-arg --headless --editor-arg --no-gpu --editor-arg --project
                    --editor-arg ${export_work}/showcase
                    replay ${package_transcript} --var "output=\"${package_output}-${kind}\"" ${package_vars}
                    --var "package=\"${kind}\"" --var "codesignIdentity=\"\"" --var "signed=true")
            add_test(NAME sdk.export.showcase.${kind}.check
                COMMAND ${CMAKE_COMMAND} -DMODE=windows-package -DOUTPUT=${package_output}-${kind} -DNAME=Showcase
                    -DEXE=showcase -DVERSION=0.1.0 -DORGANIZATION=illusion -DKIND=${kind} -DLOG=${log}
                    -DRESOURCES=${PROJECT_SOURCE_DIR}/tests/sdk/CheckWindowsResources.ps1 -P ${export_script})
            # The sign log starts empty: every line is a signature of this export.
            add_test(NAME sdk.export.showcase.${kind}.clean
                COMMAND ${CMAKE_COMMAND} -E rm -rf ${package_output}-${kind})
            set_tests_properties(sdk.export.showcase.${kind}.clean PROPERTIES LABELS "unit;sdk"
                FIXTURES_SETUP export_package_${kind}_clean)
            set_tests_properties(sdk.export.showcase.${kind} PROPERTIES LABELS "unit;sdk" TIMEOUT 1800
                FIXTURES_REQUIRED "export_showcase;export_package_${kind}_clean"
                FIXTURES_SETUP export_package_${kind}
                ENVIRONMENT "ILLUSION_SIGN_COMMAND=\"${CMAKE_COMMAND}\" \"-DLOG=${log}\" \"-DFILE={file}\" -P \"${PROJECT_SOURCE_DIR}/tests/sdk/RecordSign.cmake\""
                RESOURCE_LOCK export_showcase_build)
            set_tests_properties(sdk.export.showcase.${kind}.check PROPERTIES LABELS "unit;sdk"
                FIXTURES_REQUIRED export_package_${kind})
        endforeach()
    else()
        if(APPLE)
            set(kind installer)
            set(identity "-")
            set(signed true)
            set(check_args -DMODE=macos-package -DNAME=Showcase -DVERSION=0.1.0)
        else()
            set(kind archive)
            set(identity "")
            set(signed false)
            set(check_args -DMODE=linux-package -DNAME=Showcase -DEXE=showcase -DVERSION=0.1.0)
        endif()
        add_test(NAME sdk.export.showcase.${kind}
            COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
                --editor-arg --headless --editor-arg --no-gpu --editor-arg --project --editor-arg ${export_work}/showcase
                replay ${package_transcript} --var "output=\"${package_output}-${kind}\"" ${package_vars}
                --var "package=\"${kind}\"" --var "codesignIdentity=\"${identity}\"" --var "signed=${signed}")
        add_test(NAME sdk.export.showcase.${kind}.check
            COMMAND ${CMAKE_COMMAND} ${check_args} -DOUTPUT=${package_output}-${kind} -P ${export_script})
        set_tests_properties(sdk.export.showcase.${kind} PROPERTIES LABELS "unit;sdk" TIMEOUT 1800
            FIXTURES_REQUIRED export_showcase FIXTURES_SETUP export_package_${kind}
            RESOURCE_LOCK export_showcase_build)
        set_tests_properties(sdk.export.showcase.${kind}.check PROPERTIES LABELS "unit;sdk"
            FIXTURES_REQUIRED export_package_${kind})
        if(APPLE)
            # The signed bundle of the package runs (with a GPU).
            add_test(NAME sdk.export.showcase.${kind}.run
                COMMAND ${CMAKE_COMMAND} -DMODE=run
                    -DGAME=${package_output}-${kind}/Showcase.app/Contents/MacOS/Showcase -DFRAMES=1
                    -DUSER=${export_work}/showcase-package-user -P ${export_script})
            set_tests_properties(sdk.export.showcase.${kind}.run PROPERTIES LABELS "gpu;sdk" TIMEOUT 600
                FIXTURES_REQUIRED export_package_${kind})
        endif()
    endif()
endif()

# The screenshots of the editor manual (docs/screenshots/manual.jsonl): a headless editor with a GPU on a copy
# of samples/showcase takes them and compares them with docs/content/en/images/editor/*.png;
# ILLUSION_UPDATE_GOLDENS=1 rewrites them. The copy keeps the editor's cache files out of the source tree.
set(ILLUSION_DOCS_SHOWCASE ${CMAKE_BINARY_DIR}/docs-screenshots/showcase)
add_test(NAME docs.screenshots.project
    COMMAND ${CMAKE_COMMAND} -D SOURCE=${PROJECT_SOURCE_DIR}/samples/showcase -D DESTINATION=${ILLUSION_DOCS_SHOWCASE}
        -P ${PROJECT_SOURCE_DIR}/docs/screenshots/CopyProject.cmake)
add_test(NAME docs.screenshots
    COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
        --editor-arg --headless --editor-arg --deterministic --editor-arg --seed --editor-arg 1
        --editor-arg --project --editor-arg ${ILLUSION_DOCS_SHOWCASE}
        replay ${PROJECT_SOURCE_DIR}/docs/screenshots/manual.jsonl --gpu)
set_tests_properties(docs.screenshots.project PROPERTIES LABELS "gpu" FIXTURES_SETUP docs_showcase)
set_tests_properties(docs.screenshots PROPERTIES LABELS "gpu" TIMEOUT 600 FIXTURES_REQUIRED docs_showcase
    ENVIRONMENT "ILLUSION_UI_CAPTURE_DIR=${CMAKE_BINARY_DIR}/ui-captures")

# The same scenario through the MCP bridge in both protocol eras (ADR 0039): every step is a tool call.
foreach(era legacy modern)
    add_test(NAME acceptance.tetris.board.mcp-${era}
        COMMAND illusion_ctl --launch $<TARGET_FILE:illusion_editor>
            --editor-arg --deterministic --editor-arg --seed --editor-arg 1 --editor-arg --no-gpu
            replay ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/scripted.jsonl
            --checks ${PROJECT_SOURCE_DIR}/tests/acceptance/tetris/checks.jsonl --temp-workspace
            --via-mcp $<TARGET_FILE:illusion_mcp> --era ${era}
            --var "tetrisModule=\"$<TARGET_FILE:illusion_tetris_module>\""
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
