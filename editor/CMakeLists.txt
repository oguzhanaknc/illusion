# The editor: never linked into games or exports (ADR 0030). Every target here is created with
# illusion_add_editor_library() or illusion_add_editor_executable(), which the boundary check relies on.

include(IllusionIcons)

add_subdirectory(thirdparty)
add_subdirectory(strings)
add_subdirectory(core)
add_subdirectory(ui)

# editor-data/ next to the editor executables (mounted read-only as "editor:"): themes, locales, fonts and
# the fonts' licences.
illusion_require_dependency(fonts)
set(editor_font_files ${ILLUSION_EDITOR_FONTS})
list(FILTER editor_font_files INCLUDE REGEX "\.ttf$")
set(editor_licence_files ${ILLUSION_EDITOR_FONTS})
list(FILTER editor_licence_files INCLUDE REGEX "LICENSE")
file(GLOB_RECURSE editor_data_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/*)
set(editor_data_dir ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/editor-data)
set(editor_data_stamp ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/editor-data/.stamp)
add_custom_command(
    OUTPUT ${editor_data_stamp}
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_CURRENT_SOURCE_DIR}/data ${editor_data_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${editor_data_dir}/fonts ${editor_data_dir}/licenses
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${editor_font_files} ${editor_data_dir}/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${editor_licence_files} ${editor_data_dir}/licenses
    COMMAND ${CMAKE_COMMAND} -E touch ${editor_data_stamp}
    DEPENDS ${editor_data_files} ${editor_font_files} ${editor_licence_files}
    COMMENT "Copying editor-data"
    VERBATIM)
add_custom_target(illusion_editor_data DEPENDS ${editor_data_stamp})
illusion_mark_editor_only(illusion_editor_data)

add_subdirectory(tools/ui_gallery)
add_subdirectory(tools/command_reference)

if(ILLUSION_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(ILLUSION_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
