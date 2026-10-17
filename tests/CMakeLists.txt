illusion_require_dependency(doctest)

add_executable(illusion_tests
    main.cpp
    core/TestAssert.cpp
    core/TestEventBus.cpp
    core/TestHash.cpp
    core/TestInplaceFunction.cpp
    core/TestJobSystem.cpp
    core/TestLog.cpp
    core/TestMath.cpp
    core/TestMemory.cpp
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
    physics/TestPhysicsWorld.cpp
    render/TestPostProcess.cpp
    render/TestRenderComponents.cpp
    render/TestShadows.cpp)

target_include_directories(illusion_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_tests PRIVATE
    illusion::core illusion::platform illusion::input illusion::assets illusion::physics illusion::render
    illusion::doctest)
target_compile_definitions(illusion_tests PRIVATE ILLUSION_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
illusion_configure_target(illusion_tests)

include(${ILLUSION_DOCTEST_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(illusion_tests
    PROPERTIES LABELS "unit"
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
