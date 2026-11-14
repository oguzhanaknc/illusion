illusion_require_dependency(doctest)

add_executable(illusion_tests
    main.cpp
    AllocationCounter.cpp
    core/TestAssert.cpp
    core/TestAssetRef.cpp
    core/TestEventBus.cpp
    core/TestExpected.cpp
    core/TestFrameProfiler.cpp
    core/TestHash.cpp
    core/TestInplaceFunction.cpp
    core/TestJobSystem.cpp
    core/TestJson.cpp
    core/TestLog.cpp
    core/TestMath.cpp
    core/TestMemory.cpp
    core/TestRandom.cpp
    core/TestSerialization.cpp
    core/TestThread.cpp
    core/TestTime.cpp
    core/TestTypeName.cpp
    core/TestVersion.cpp
    docs/TestTutorials.cpp
    ecs/TestCommandBuffer.cpp
    ecs/TestComponent.cpp
    ecs/TestComponentJson.cpp
    ecs/TestEntity.cpp
    ecs/TestEntityGuid.cpp
    ecs/TestHierarchy.cpp
    ecs/TestPrefab.cpp
    ecs/TestQuery.cpp
    ecs/TestReflection.cpp
    ecs/TestReflectionSweep.cpp
    ecs/TestSceneSerializer.cpp
    ecs/TestWind.cpp
    ecs/TestWorld.cpp
    platform/TestArchive.cpp
    platform/TestFileSystem.cpp
    platform/TestFileWatcher.cpp
    platform/TestInput.cpp
    platform/TestProcess.cpp
    platform/TestWindow.cpp
    input/TestGamepadState.cpp
    input/TestGamepadSystem.cpp
    input/TestInputActions.cpp
    input/TestInputActionsAsset.cpp
    assets/TestAssetCooker.cpp
    assets/TestAssetDatabase.cpp
    assets/TestAssetFormat.cpp
    assets/TestAssetStreamer.cpp
    assets/TestAtmosphere.cpp
    assets/TestEnvironment.cpp
    assets/TestGltf.cpp
    assets/TestImageMesh.cpp
    assets/TestImportSettings.cpp
    assets/TestMaterialAsset.cpp
    assets/TestMeshLods.cpp
    assets/TestTextureCompression.cpp
    audio/TestAudioEffects.cpp
    audio/TestAudioWorld.cpp
    modules/TestModules.cpp
    modules/TestServices.cpp
    physics/TestPhysicsAssets.cpp
    particles/TestParticles.cpp
    physics/TestPhysicsExtensions.cpp
    physics/TestPhysicsHierarchy.cpp
    physics/TestPhysicsWorld.cpp
    player/TestPlayerSetup.cpp
    render/TestPostProcess.cpp
    render/TestRenderComponents.cpp
    render/TestShadows.cpp
    render/TestSkyAtmosphere.cpp
    render/TestTextureStreamingPolicy.cpp
    render/TestRendererExtensions.cpp
    runtime/TestGameSession.cpp
    runtime/TestProjectSettings.cpp
    showcase/TestShowcaseModule.cpp
    text/TestLocale.cpp
    text/TestLocalization.cpp
    text/TestMessageFormat.cpp
    text/TestPluralRules.cpp
    text/TestSearch.cpp
    text/TestTextTools.cpp
    text/TestUtf8.cpp)

# The render library's internal policies are tested directly (render/TestTextureStreamingPolicy.cpp), as is
# the player's setup (player/TestPlayerSetup.cpp).
target_include_directories(illusion_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/engine/render/src ${PROJECT_SOURCE_DIR}/engine/player/src)
target_link_libraries(illusion_tests PRIVATE
    illusion::core illusion::platform illusion::input illusion::assets illusion::audio illusion::physics
    illusion::particles illusion::render illusion::modules illusion::runtime illusion::player illusion::text
    illusion_text_tools illusion::doctest)
target_compile_definitions(illusion_tests PRIVATE ILLUSION_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
illusion_configure_target(illusion_tests)

# A string table used by tests/text (its generated files are the golden output in data/text).
illusion_add_string_table(illusion_tests
    TABLE test
    SOURCE data/locales/en.json
    NAMESPACE illusion::test::strings
    HEADER TestStrings.hpp)

# The editor boundary (ADR 0030): a project whose engine target reaches an editor-only target must fail
# to configure. CMake wraps long messages, so the expected path allows line breaks.
add_test(NAME editor.boundary
    COMMAND "${CMAKE_COMMAND}"
        "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/editor_boundary"
        "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/editor_boundary"
        "-DEXPECTED=fixture_engine[ 
]+->[ 
]+fixture_middle[ 
]+->[ 
]+fixture_editor_ui"
        "-DILLUSION_CMAKE_DIR=${PROJECT_SOURCE_DIR}/cmake"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExpectConfigureFailure.cmake")
set_tests_properties(editor.boundary PROPERTIES LABELS "unit")

# Checks every string table and its translations (also part of the unit label, so every build job runs it).
get_property(locale_dirs GLOBAL PROPERTY ILLUSION_LOCALE_DIRECTORIES)
list(REMOVE_DUPLICATES locale_dirs)
add_test(NAME text.locales COMMAND illusion_textc check ${locale_dirs})
set_tests_properties(text.locales PROPERTIES LABELS "unit")

# Child program of tests/platform/TestProcess.cpp.
add_executable(illusion_process_helper platform/ProcessHelper.cpp)
illusion_configure_target(illusion_process_helper)
target_compile_definitions(illusion_tests PRIVATE PROCESS_HELPER="$<TARGET_FILE:illusion_process_helper>")
add_dependencies(illusion_tests illusion_process_helper)

# Shared libraries loaded by tests/modules/TestModules.cpp (see TestModule.cpp for the variants).
foreach(variant 1 2 3 4 5 6)
    illusion_add_module(illusion_test_module_v${variant} SHARED SOURCES modules/TestModule.cpp
        USES physics audio input render runtime)
    target_include_directories(illusion_test_module_v${variant} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules)
    target_compile_definitions(illusion_test_module_v${variant} PRIVATE TEST_MODULE_VARIANT=${variant})
    target_compile_definitions(illusion_tests PRIVATE
        TEST_MODULE_V${variant}="$<TARGET_FILE:illusion_test_module_v${variant}>")
    add_dependencies(illusion_tests illusion_test_module_v${variant})
endforeach()
# The showcase project's game module (samples/showcase/modules/showcase), loaded by tests/showcase.
illusion_add_module(illusion_showcase_module SHARED
    SOURCES ${PROJECT_SOURCE_DIR}/samples/showcase/modules/showcase/showcase.cpp
    USES physics audio particles input render runtime)
target_compile_definitions(illusion_tests PRIVATE
    SHOWCASE_MODULE="$<TARGET_FILE:illusion_showcase_module>"
    ILLUSION_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples")
add_dependencies(illusion_tests illusion_showcase_module)
# The C++ of the tutorial "Tetris by Hand", taken from the code blocks of its English page and built as a
# module, so that the tutorial keeps compiling and loading (tests/docs/TestTutorials.cpp).
set(tutorial_page ${PROJECT_SOURCE_DIR}/docs/content/en/tutorials/tetris-by-hand.md)
set(tutorial_source ${CMAKE_CURRENT_BINARY_DIR}/generated/tutorials/tetris_by_hand.cpp)
add_custom_command(OUTPUT ${tutorial_source}
    COMMAND ${CMAKE_COMMAND} -D SOURCE=${tutorial_page} -D OUTPUT=${tutorial_source}
        -P ${PROJECT_SOURCE_DIR}/cmake/ExtractCppBlocks.cmake
    DEPENDS ${tutorial_page} ${PROJECT_SOURCE_DIR}/cmake/ExtractCppBlocks.cmake
    COMMENT "Extracting the code of the Tetris by Hand tutorial"
    VERBATIM)
illusion_add_module(illusion_tutorial_tetris_module SHARED SOURCES ${tutorial_source} USES input render runtime)
target_compile_definitions(illusion_tests PRIVATE
    TUTORIAL_TETRIS_MODULE="$<TARGET_FILE:illusion_tutorial_tetris_module>")
add_dependencies(illusion_tests illusion_tutorial_tetris_module)
target_compile_definitions(illusion_test_module_v3 PRIVATE TEST_MODULE_WIDE_COUNTER=1)
target_compile_definitions(illusion_test_module_v5 PRIVATE TEST_MODULE_LAYOUT_V2=1)
target_compile_definitions(illusion_test_module_v6 PRIVATE TEST_MODULE_WIDE_OPAQUE=1)
add_library(illusion_test_not_a_module MODULE modules/NotAModule.cpp)
illusion_configure_target(illusion_test_not_a_module)
target_compile_definitions(illusion_tests PRIVATE TEST_NOT_A_MODULE="$<TARGET_FILE:illusion_test_not_a_module>")
add_dependencies(illusion_tests illusion_test_not_a_module)

# illusion_add_module() rejects what would give a shared module a second copy of an engine library
# (ADR 0041). The fixtures run in script mode: the checks come before any target is created.
add_test(NAME modules.reject_engine_link
    COMMAND ${CMAKE_COMMAND} -DILLUSION_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/modules/cmake/LinkEngineLibrary.cmake)
add_test(NAME modules.reject_unknown_uses
    COMMAND ${CMAKE_COMMAND} -DILLUSION_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/modules/cmake/UnknownUses.cmake)
set_tests_properties(modules.reject_engine_link PROPERTIES LABELS "unit"
    PASS_REGULAR_EXPRESSION "cannot LINK the engine")
set_tests_properties(modules.reject_unknown_uses PROPERTIES LABELS "unit"
    PASS_REGULAR_EXPRESSION "unknown USES entry 'scripting'")

if(ILLUSION_BUILD_TOOLS)
    add_test(NAME tools.pack_unicode_path
        COMMAND ${CMAKE_COMMAND} "-DPACK=$<TARGET_FILE:illusion_pack>"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/pack_unicode_path"
            "-DMATERIAL=${PROJECT_SOURCE_DIR}/samples/showcase/assets/materials/red.material"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackUnicodePath.cmake)
    set_tests_properties(tools.pack_unicode_path PROPERTIES LABELS "unit" TIMEOUT 300)
endif()

# The package's component `player` (ADR 0055; cmake/IllusionSdk.cmake): a game outside the engine's build is
# built against the build tree's package and against an installed copy that moved, and the installed copy
# holds no editor code and no absolute path of this build. Building needs a compiler (label "sdk"); running
# the games needs a GPU (label "gpu"). Profiling builds have no package; packing the data needs illusion_pack.
if(ILLUSION_BUILD_TOOLS AND NOT ILLUSION_PROFILING)
    set(sdk_script "${CMAKE_CURRENT_SOURCE_DIR}/sdk/PlayerSdk.cmake")
    set(sdk_work "${CMAKE_CURRENT_BINARY_DIR}/sdk/$<CONFIG>")
    set(sdk_installed "${sdk_work}/installed/sdk")
    add_test(NAME sdk.player.build_tree
        COMMAND "${CMAKE_COMMAND}" -DMODE=build "-DSDK=${PROJECT_BINARY_DIR}/sdk/$<CONFIG>/illusion-sdk.json"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/sdk/game" "-DWORK=${sdk_work}/build_tree_game" -P "${sdk_script}")
    add_test(NAME sdk.install
        COMMAND "${CMAKE_COMMAND}" -DMODE=install "-DBUILD_DIR=${PROJECT_BINARY_DIR}" -DCONFIG=$<CONFIG>
            "-DWORK=${sdk_work}/installed" -P "${sdk_script}")
    add_test(NAME sdk.install.contents
        COMMAND "${CMAKE_COMMAND}" -DMODE=contents "-DPREFIX=${sdk_installed}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DBUILD_DIR=${PROJECT_BINARY_DIR}" "-DSTAGING=${sdk_work}/installed/staging" -P "${sdk_script}")
    add_test(NAME sdk.player.installed
        COMMAND "${CMAKE_COMMAND}" -DMODE=build
            "-DSDK=${sdk_installed}/${CMAKE_INSTALL_LIBDIR}/cmake/illusion/$<CONFIG>/illusion-sdk.json"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/sdk/game" "-DWORK=${sdk_work}/installed_game" -P "${sdk_script}")
    foreach(variant build_tree installed)
        add_test(NAME sdk.player.${variant}.run
            COMMAND "${sdk_work}/${variant}_game/build/sdk_game${CMAKE_EXECUTABLE_SUFFIX}" --frames 1 --no-dialogs
                --user-data "${sdk_work}/${variant}_game/user")
        set_tests_properties(sdk.player.${variant}.run PROPERTIES LABELS "gpu;sdk" TIMEOUT 300
            FIXTURES_REQUIRED sdk_game_${variant})
    endforeach()
    set_tests_properties(sdk.player.build_tree PROPERTIES LABELS "unit;sdk" TIMEOUT 1800
        FIXTURES_SETUP sdk_game_build_tree)
    set_tests_properties(sdk.install PROPERTIES LABELS "unit;sdk" TIMEOUT 600 FIXTURES_SETUP sdk_installed)
    set_tests_properties(sdk.install.contents PROPERTIES LABELS "unit;sdk" FIXTURES_REQUIRED sdk_installed)
    set_tests_properties(sdk.player.installed PROPERTIES LABELS "unit;sdk" TIMEOUT 1800
        FIXTURES_REQUIRED sdk_installed FIXTURES_SETUP sdk_game_installed)
endif()

include(${ILLUSION_DOCTEST_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(illusion_tests
    PROPERTIES LABELS "unit"
    ADD_LABELS 0)

# Display tests need a real window system (a desktop, or X11 under Xvfb in CI). They are labelled "display" so
# that jobs without a display can exclude them with `ctest -LE "gpu|display"`.
add_executable(illusion_display_tests
    display/main.cpp
    platform/TestWindowDisplay.cpp)
target_include_directories(illusion_display_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_display_tests PRIVATE illusion::platform illusion::doctest)
illusion_configure_target(illusion_display_tests)
doctest_discover_tests(illusion_display_tests
    PROPERTIES LABELS "display"
    ADD_LABELS 0)

# GPU tests need a Vulkan 1.3 driver (a real GPU, or Mesa lavapipe in CI). They are a separate executable
# labelled "gpu" so machines without Vulkan can exclude them with `ctest -LE gpu`.
add_executable(illusion_gpu_tests
    gpu/main.cpp
    AllocationCounter.cpp
    gpu/TestRenderAssets.cpp
    gpu/TestRenderDevice.cpp
    gpu/TestSceneRenderer.cpp
    gpu/TestRendererExtensions.cpp
    gpu/TestResourceLifetime.cpp
    gpu/TestSwapchain.cpp
    gpu/TestTextureStreaming.cpp
    gpu/TestTextureUpload.cpp
    gpu/TestUploadQueue.cpp)
target_include_directories(illusion_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_gpu_tests PRIVATE illusion::render illusion::doctest)
target_compile_definitions(illusion_gpu_tests PRIVATE ILLUSION_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
illusion_configure_target(illusion_gpu_tests)
# The exported showcase (samples/CMakeLists.txt) against the golden image of the editor's Play mode.
if(ILLUSION_BUILD_SAMPLES AND ILLUSION_BUILD_TOOLS)
    target_sources(illusion_gpu_tests PRIVATE gpu/TestShowcaseGame.cpp)
    target_compile_definitions(illusion_gpu_tests PRIVATE
        SHOWCASE_GAME="$<TARGET_FILE:showcase_game>"
        SHOWCASE_GAME_CAPTURE="${CMAKE_CURRENT_BINARY_DIR}/captures/$<CONFIG>/showcase-game.png"
        SHOWCASE_GAME_GOLDEN="${PROJECT_SOURCE_DIR}/editor/tests/data/golden/showcase-play-300.png")
    add_dependencies(illusion_gpu_tests showcase_game)
endif()
doctest_discover_tests(illusion_gpu_tests
    PROPERTIES LABELS "gpu"
    ADD_LABELS 0)
