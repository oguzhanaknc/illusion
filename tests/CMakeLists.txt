illusion_require_dependency(doctest)

add_executable(illusion_tests
    main.cpp
    AllocationCounter.cpp
    ai/TestAiComponents.cpp
    ai/TestAiPerceptionWorld.cpp
    ai/TestAiService.cpp
    ai/TestAiWorld.cpp
    ai/TestBehaviorTree.cpp
    ai/TestBehaviorTreeExample.cpp
    ai/TestBehaviorTreeState.cpp
    ai/TestBlackboard.cpp
    ai/TestPerception.cpp
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
    assets/TestAppIcon.cpp
    assets/TestAssetStreamer.cpp
    assets/TestAtmosphere.cpp
    assets/TestEnvironment.cpp
    assets/TestFont.cpp
    assets/TestFontAsset.cpp
    assets/TestGltf.cpp
    assets/TestGltfSkins.cpp
    assets/TestImageMesh.cpp
    assets/TestImportSettings.cpp
    assets/TestMaterialAsset.cpp
    assets/TestMeshLods.cpp
    assets/TestRigMap.cpp
    assets/TestShaderAsset.cpp
    assets/TestShaderCompiler.cpp
    assets/TestTextureCompression.cpp
    animation/TestAnimationGraph.cpp
    animation/TestAnimationGraphExample.cpp
    animation/TestAnimationGraphState.cpp
    animation/TestAnimationIk.cpp
    animation/TestAnimationRagdoll.cpp
    animation/TestAnimationRuntime.cpp
    animation/TestAnimationSockets.cpp
    animation/TestAnimationWorld.cpp
    animation/TestInverseKinematics.cpp
    animation/TestPose.cpp
    animation/TestRetarget.cpp
    animation/TestSpringBones.cpp
    camera/TestCameraAim.cpp
    camera/TestCameraBlend.cpp
    camera/TestCameraBody.cpp
    camera/TestCameraDamping.cpp
    camera/TestCameraNoise.cpp
    camera/TestCameraRig.cpp
    camera/TestCameraService.cpp
    camera/TestCameraWorld.cpp
    audio/TestAudioEffects.cpp
    audio/TestAudioWorld.cpp
    modules/TestModules.cpp
    modules/TestServices.cpp
    nav/TestNavBuild.cpp
    nav/TestNavComponents.cpp
    nav/TestNavCrowd.cpp
    nav/TestNavMeshData.cpp
    nav/TestNavService.cpp
    nav/TestNavWorld.cpp
    physics/TestPhysicsAssets.cpp
    particles/TestParticles.cpp
    physics/TestPhysicsExtensions.cpp
    physics/TestPhysicsHierarchy.cpp
    physics/TestPhysicsWorld.cpp
    physics/TestRagdoll.cpp
    player/TestPlayerSetup.cpp
    render/TestBuiltinDecalShader.cpp
    render/TestFrameCapture.cpp
    render/TestPostProcess.cpp
    render/TestRenderComponents.cpp
    render/TestShadows.cpp
    render/TestSkyAtmosphere.cpp
    render/TestTextureStreamingPolicy.cpp
    render/TestRendererExtensions.cpp
    runtime/TestCameraRuntime.cpp
    runtime/TestGameSession.cpp
    runtime/TestProjectSettings.cpp
    save/TestSaveCapture.cpp
    save/TestSaveCloud.cpp
    save/TestSaveFile.cpp
    save/TestSaveSystem.cpp
    showcase/TestShowcaseModule.cpp
    store/TestStoreService.cpp
    text/TestLocale.cpp
    text/TestLocalization.cpp
    text/TestMessageFormat.cpp
    text/TestPluralRules.cpp
    text/TestSearch.cpp
    text/TestTextTools.cpp
    text/TestUtf8.cpp
    ui/TestUiDrawList.cpp
    ui/TestUiFields.cpp
    ui/TestUiGeometry.cpp
    ui/TestUiInteraction.cpp
    ui/TestUiService.cpp
    ui/TestUiTransitions.cpp
    ui/TestUiWorld.cpp)

# The render library's internal policies are tested directly (render/TestTextureStreamingPolicy.cpp), as is
# the player's setup (player/TestPlayerSetup.cpp).
target_include_directories(illusion_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/engine/render/src ${PROJECT_SOURCE_DIR}/engine/player/src)
target_link_libraries(illusion_tests PRIVATE
    illusion::core illusion::platform illusion::input illusion::assets illusion::animation illusion::camera illusion::audio
    illusion::physics illusion::particles illusion::render illusion::modules illusion::runtime illusion::player illusion::store
    illusion::save illusion::text illusion_text_tools illusion::ui illusion::nav illusion::ai illusion::doctest)
illusion_require_dependency(test_font)
target_compile_definitions(illusion_tests PRIVATE ILLUSION_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    ILLUSION_DOCS_DIR="${PROJECT_SOURCE_DIR}/docs/content"
    ILLUSION_TEST_FONT="${ILLUSION_TEST_FONT}")
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

# The material shader compiler (tests/assets/TestShaderCompiler.cpp) runs the pinned slangc on the engine's shaders.
target_compile_definitions(illusion_tests PRIVATE ILLUSION_TEST_SLANGC="${ILLUSION_SLANGC}"
    ILLUSION_TEST_SHADER_LIBRARY="${ILLUSION_SHADER_LIBRARY_DIR}")

# Child program of tests/platform/TestProcess.cpp.
add_executable(illusion_process_helper platform/ProcessHelper.cpp)
illusion_configure_target(illusion_process_helper)
target_compile_definitions(illusion_tests PRIVATE PROCESS_HELPER="$<TARGET_FILE:illusion_process_helper>")
add_dependencies(illusion_tests illusion_process_helper)

# Shared libraries loaded by tests/modules/TestModules.cpp (see TestModule.cpp for the variants).
foreach(variant 1 2 3 4 5 6)
    illusion_add_module(illusion_test_module_v${variant} SHARED SOURCES modules/TestModule.cpp
        USES physics audio animation nav ai camera input render runtime ui save)
    target_include_directories(illusion_test_module_v${variant} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules)
    target_compile_definitions(illusion_test_module_v${variant} PRIVATE TEST_MODULE_VARIANT=${variant})
    target_compile_definitions(illusion_tests PRIVATE
        TEST_MODULE_V${variant}="$<TARGET_FILE:illusion_test_module_v${variant}>")
    add_dependencies(illusion_tests illusion_test_module_v${variant})
endforeach()
# The showcase project's game module (samples/showcase/modules/showcase), loaded by tests/showcase.
illusion_add_module(illusion_showcase_module SHARED
    SOURCES ${PROJECT_SOURCE_DIR}/samples/showcase/modules/showcase/showcase.cpp
    USES physics audio particles animation nav ai camera input render runtime store ui save)
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
# The same for "A HUD and a pause menu".
set(tutorial_hud_page ${PROJECT_SOURCE_DIR}/docs/content/en/tutorials/hud-and-pause-menu.md)
set(tutorial_hud_source ${CMAKE_CURRENT_BINARY_DIR}/generated/tutorials/hud_and_pause_menu.cpp)
add_custom_command(OUTPUT ${tutorial_hud_source}
    COMMAND ${CMAKE_COMMAND} -D SOURCE=${tutorial_hud_page} -D OUTPUT=${tutorial_hud_source}
        -P ${PROJECT_SOURCE_DIR}/cmake/ExtractCppBlocks.cmake
    DEPENDS ${tutorial_hud_page} ${PROJECT_SOURCE_DIR}/cmake/ExtractCppBlocks.cmake
    COMMENT "Extracting the code of the HUD and pause menu tutorial"
    VERBATIM)
illusion_add_module(illusion_tutorial_hud_module SHARED SOURCES ${tutorial_hud_source} USES audio runtime save ui)
target_compile_definitions(illusion_tests PRIVATE
    TUTORIAL_HUD_MODULE="$<TARGET_FILE:illusion_tutorial_hud_module>")
add_dependencies(illusion_tests illusion_tutorial_hud_module)
# The same for "Animate a character".
set(tutorial_animation_page ${PROJECT_SOURCE_DIR}/docs/content/en/tutorials/animated-character.md)
set(tutorial_animation_source ${CMAKE_CURRENT_BINARY_DIR}/generated/tutorials/animated_character.cpp)
add_custom_command(OUTPUT ${tutorial_animation_source}
    COMMAND ${CMAKE_COMMAND} -D SOURCE=${tutorial_animation_page} -D OUTPUT=${tutorial_animation_source}
        -P ${PROJECT_SOURCE_DIR}/cmake/ExtractCppBlocks.cmake
    DEPENDS ${tutorial_animation_page} ${PROJECT_SOURCE_DIR}/cmake/ExtractCppBlocks.cmake
    COMMENT "Extracting the code of the Animate a character tutorial"
    VERBATIM)
illusion_add_module(illusion_tutorial_animation_module SHARED SOURCES ${tutorial_animation_source}
    USES animation audio input physics runtime)
target_compile_definitions(illusion_tests PRIVATE
    TUTORIAL_ANIMATION_MODULE="$<TARGET_FILE:illusion_tutorial_animation_module>")
add_dependencies(illusion_tests illusion_tutorial_animation_module)
# The same for "A creature that wanders and chases".
set(tutorial_creature_page ${PROJECT_SOURCE_DIR}/docs/content/en/tutorials/creature-ai.md)
set(tutorial_creature_source ${CMAKE_CURRENT_BINARY_DIR}/generated/tutorials/creature_ai.cpp)
add_custom_command(OUTPUT ${tutorial_creature_source}
    COMMAND ${CMAKE_COMMAND} -D SOURCE=${tutorial_creature_page} -D OUTPUT=${tutorial_creature_source}
        -P ${PROJECT_SOURCE_DIR}/cmake/ExtractCppBlocks.cmake
    DEPENDS ${tutorial_creature_page} ${PROJECT_SOURCE_DIR}/cmake/ExtractCppBlocks.cmake
    COMMENT "Extracting the code of the A creature that wanders and chases tutorial"
    VERBATIM)
illusion_add_module(illusion_tutorial_creature_module SHARED SOURCES ${tutorial_creature_source}
    USES ai input physics runtime)
target_compile_definitions(illusion_tests PRIVATE
    TUTORIAL_CREATURE_MODULE="$<TARGET_FILE:illusion_tutorial_creature_module>")
add_dependencies(illusion_tests illusion_tutorial_creature_module)
# The same for "A third-person camera".
set(tutorial_camera_page ${PROJECT_SOURCE_DIR}/docs/content/en/tutorials/third-person-camera.md)
set(tutorial_camera_source ${CMAKE_CURRENT_BINARY_DIR}/generated/tutorials/third_person_camera.cpp)
add_custom_command(OUTPUT ${tutorial_camera_source}
    COMMAND ${CMAKE_COMMAND} -D SOURCE=${tutorial_camera_page} -D OUTPUT=${tutorial_camera_source}
        -P ${PROJECT_SOURCE_DIR}/cmake/ExtractCppBlocks.cmake
    DEPENDS ${tutorial_camera_page} ${PROJECT_SOURCE_DIR}/cmake/ExtractCppBlocks.cmake
    COMMENT "Extracting the code of the A third-person camera tutorial"
    VERBATIM)
illusion_add_module(illusion_tutorial_camera_module SHARED SOURCES ${tutorial_camera_source}
    USES camera input physics runtime)
target_compile_definitions(illusion_tests PRIVATE
    TUTORIAL_CAMERA_MODULE="$<TARGET_FILE:illusion_tutorial_camera_module>")
add_dependencies(illusion_tests illusion_tutorial_camera_module)
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
    set(sdk_game_executable "sdk_game${CMAKE_EXECUTABLE_SUFFIX}")
    if(APPLE)
        set(sdk_game_executable "sdk_game.app/Contents/MacOS/sdk_game")
    endif()
    foreach(variant build_tree installed)
        add_test(NAME sdk.player.${variant}.run
            COMMAND "${CMAKE_COMMAND}" -DMODE=run "-DGAME=${sdk_work}/${variant}_game/build/${sdk_game_executable}"
                "-DUSER=${sdk_work}/${variant}_game/user" -P "${sdk_script}")
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
    gpu/TestMaterialShaders.cpp
    gpu/TestRendererExtensions.cpp
    gpu/TestResourceLifetime.cpp
    gpu/TestSaveThumbnail.cpp
    gpu/TestSkinning.cpp
    gpu/TestSwapchain.cpp
    gpu/TestTextureStreaming.cpp
    gpu/TestTextureUpload.cpp
    gpu/TestUiRenderer.cpp
    gpu/TestUploadQueue.cpp)
target_include_directories(illusion_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_gpu_tests PRIVATE illusion::render illusion::save illusion::doctest)
target_compile_definitions(illusion_gpu_tests PRIVATE ILLUSION_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    ILLUSION_TEST_FONT="${ILLUSION_TEST_FONT}")
illusion_configure_target(illusion_gpu_tests)
# Material shaders of TestMaterialShaders.cpp, compiled at build time like the engine's built-in ones.
illusion_add_material_shaders(illusion_gpu_tests
    NAMESPACE illusion::test_shaders
    SHADERS
        glowShader data/shaders/Glow.shader game:/shaders/glow.shader
        glowEditedShader data/shaders/GlowEdited.shader game:/shaders/glow.shader
        swayShader data/shaders/Sway.shader game:/shaders/sway.shader
        unlitShader data/shaders/Unlit.shader game:/shaders/unlit.shader
        skinShader data/shaders/Skin.shader game:/shaders/skin.shader
        coatShader data/shaders/Coat.shader game:/shaders/coat.shader)
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
