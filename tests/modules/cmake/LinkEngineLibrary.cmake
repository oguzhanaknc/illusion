# Run in script mode by CTest: a SHARED module that links an engine library must fail to configure
# (ADR 0041). The check runs before any target is created.
include(${ILLUSION_SOURCE_DIR}/cmake/IllusionModules.cmake)
illusion_add_module(link_fixture SHARED SOURCES Fixture.cpp LINK illusion::physics)
