# A project whose engine target reaches an editor-only target through another target. Its configure must
# fail (see tests/cmake/ExpectConfigureFailure.cmake).

cmake_minimum_required(VERSION 3.28)
project(editor_boundary_fixture NONE)

list(APPEND CMAKE_MODULE_PATH "${ILLUSION_CMAKE_DIR}")
include(IllusionEditor)

illusion_add_editor_library(fixture_editor_ui INTERFACE)
add_library(fixture_middle INTERFACE)
target_link_libraries(fixture_middle INTERFACE fixture_editor_ui)
add_library(fixture_engine INTERFACE)
target_link_libraries(fixture_engine INTERFACE fixture_middle)

# Editor targets may depend on anything.
illusion_add_editor_library(fixture_editor_app INTERFACE)
target_link_libraries(fixture_editor_app INTERFACE fixture_editor_ui fixture_engine)
