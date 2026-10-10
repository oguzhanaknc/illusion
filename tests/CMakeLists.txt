illusion_require_dependency(doctest)

add_executable(illusion_tests
    main.cpp
    core/TestAssert.cpp
    core/TestHash.cpp
    core/TestInplaceFunction.cpp
    core/TestJobSystem.cpp
    core/TestLog.cpp
    core/TestMemory.cpp
    core/TestThread.cpp
    core/TestTypeName.cpp
    ecs/TestCommandBuffer.cpp
    ecs/TestComponent.cpp
    ecs/TestEntity.cpp
    ecs/TestQuery.cpp
    ecs/TestWorld.cpp)

target_include_directories(illusion_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_tests PRIVATE illusion::core illusion::doctest)
illusion_configure_target(illusion_tests)

include(${ILLUSION_DOCTEST_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(illusion_tests
    PROPERTIES LABELS "unit"
    ADD_LABELS 0)
