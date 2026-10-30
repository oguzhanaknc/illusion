# Run in script mode by CTest: a module that USES an engine library without an API target must fail to
# configure.
include(${ILLUSION_SOURCE_DIR}/cmake/IllusionModules.cmake)
illusion_add_module(uses_fixture SHARED SOURCES Fixture.cpp USES scripting)
