illusion_require_dependency(doctest)

add_executable(illusion_tests
    main.cpp
    AllocationCounter.cpp
    core/TestAssert.cpp
    core/TestEventBus.cpp
    core/TestExpected.cpp
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
    ecs/TestCommandBuffer.cpp
    ecs/TestComponent.cpp
    ecs/TestEntity.cpp
    ecs/TestQuery.cpp
    ecs/TestReflection.cpp
    ecs/TestSceneSerializer.cpp
    ecs/TestWorld.cpp
    platform/TestArchive.cpp
    platform/TestFileSystem.cpp
    platform/TestInput.cpp
    platform/TestWindow.cpp
    input/TestGamepadState.cpp
    input/TestGamepadSystem.cpp
    input/TestInputActions.cpp
    assets/TestAssetCooker.cpp
    assets/TestAssetDatabase.cpp
    assets/TestAssetFormat.cpp
    assets/TestAssetStreamer.cpp
    assets/TestEnvironment.cpp
    assets/TestGltf.cpp
    assets/TestImageMesh.cpp
    audio/TestAudioEffects.cpp
    audio/TestAudioWorld.cpp
    modules/TestModules.cpp
    physics/TestPhysicsExtensions.cpp
    physics/TestPhysicsWorld.cpp
    render/TestPostProcess.cpp
    render/TestRenderComponents.cpp
    render/TestShadows.cpp
    text/TestLocale.cpp
    text/TestLocalization.cpp
    text/TestMessageFormat.cpp
    text/TestPluralRules.cpp
    text/TestSearch.cpp
    text/TestTextTools.cpp
    text/TestUtf8.cpp)

target_include_directories(illusion_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_tests PRIVATE
    illusion::core illusion::platform illusion::input illusion::assets illusion::audio illusion::physics
    illusion::render illusion::modules illusion::text illusion_text_tools illusion::doctest)
target_compile_definitions(illusion_tests PRIVATE ILLUSION_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
illusion_configure_target(illusion_tests)

# A string table used by tests/text (its generated files are the golden output in data/text).
illusion_add_string_table(illusion_tests
    TABLE test
    SOURCE data/locales/en.json
    NAMESPACE illusion::test::strings
    HEADER TestStrings.hpp)

# Checks every string table and its translations (also part of the unit label, so every build job runs it).
get_property(locale_dirs GLOBAL PROPERTY ILLUSION_LOCALE_DIRECTORIES)
list(REMOVE_DUPLICATES locale_dirs)
add_test(NAME text.locales COMMAND illusion_textc check ${locale_dirs})
set_tests_properties(text.locales PROPERTIES LABELS "unit")

# Shared libraries loaded by tests/modules/TestModules.cpp (see TestModule.cpp for the variants).
foreach(variant 1 2 3 4)
    illusion_add_module(illusion_test_module_v${variant} SHARED SOURCES modules/TestModule.cpp)
    target_include_directories(illusion_test_module_v${variant} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules)
    target_compile_definitions(illusion_test_module_v${variant} PRIVATE TEST_MODULE_VARIANT=${variant})
    target_compile_definitions(illusion_tests PRIVATE
        TEST_MODULE_V${variant}="$<TARGET_FILE:illusion_test_module_v${variant}>")
    add_dependencies(illusion_tests illusion_test_module_v${variant})
endforeach()
target_compile_definitions(illusion_test_module_v3 PRIVATE TEST_MODULE_WIDE_COUNTER=1)
add_library(illusion_test_not_a_module MODULE modules/NotAModule.cpp)
illusion_configure_target(illusion_test_not_a_module)
target_compile_definitions(illusion_tests PRIVATE TEST_NOT_A_MODULE="$<TARGET_FILE:illusion_test_not_a_module>")
add_dependencies(illusion_tests illusion_test_not_a_module)

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
    gpu/TestRenderDevice.cpp
    gpu/TestSceneRenderer.cpp
    gpu/TestSwapchain.cpp
    gpu/TestUploadQueue.cpp)
target_include_directories(illusion_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_gpu_tests PRIVATE illusion::render illusion::doctest)
target_compile_definitions(illusion_gpu_tests PRIVATE ILLUSION_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
illusion_configure_target(illusion_gpu_tests)
doctest_discover_tests(illusion_gpu_tests
    PROPERTIES LABELS "gpu"
    ADD_LABELS 0)
