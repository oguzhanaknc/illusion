# Install rules and packages of an exported game (docs/adr/0055-exported-games.md).
#
# illusion_package_game(<target> [DATA <archives...>])
#
# Installs the game made by illusion_add_game(<target> ...) and configures CPack for it (include(CPack) runs
# here, so call it once per project, after the target's OUTPUT_NAME is final):
#   - Windows and Linux: <executable> at the install prefix and DATA in data/; on Linux also
#     <executable>.png and <executable>.desktop.
#   - macOS: the bundle <OUTPUT_NAME>.app at the install prefix, DATA in its Contents/Resources/data.
# Packages, by CPack generator: ZIP and TGZ hold <name>/ (<name>.app on macOS); NSIS installs into
# Program Files\<name> with a Start menu shortcut, an uninstaller and the game's icon; DragNDrop makes a DMG
# with the bundle and a link to /Applications. CPACK_PACKAGE_FILE_NAME is the game's name.
#
# A project that signs the executable in the build tree before packaging sets CMAKE_SKIP_INSTALL_ALL_DEPENDENCY
# so that `cmake --install` and CPack copy the signed files without building again.

include_guard(GLOBAL)

function(illusion_package_game target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "DATA")
    get_target_property(name ${target} ILLUSION_GAME_NAME)
    if(NOT name)
        message(FATAL_ERROR "illusion_package_game(${target}): not a game made by illusion_add_game()")
    endif()
    get_target_property(organization ${target} ILLUSION_GAME_ORGANIZATION)
    get_target_property(version ${target} ILLUSION_GAME_VERSION)
    get_target_property(copyright ${target} ILLUSION_GAME_COPYRIGHT)
    get_target_property(executable ${target} OUTPUT_NAME)
    if(NOT executable)
        set(executable ${target})
    endif()
    if(NOT version)
        set(version "0.0.0")
    endif()

    if(APPLE)
        foreach(file IN LISTS arg_DATA)
            cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
            target_sources(${target} PRIVATE "${file}")
            set_source_files_properties("${file}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources/data)
        endforeach()
        install(TARGETS ${target} BUNDLE DESTINATION .)
    else()
        install(TARGETS ${target} RUNTIME DESTINATION .)
        if(arg_DATA)
            install(FILES ${arg_DATA} DESTINATION data)
        endif()
        if(NOT WIN32)
            set(base "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>")
            install(FILES "${base}.png" "${base}.desktop" DESTINATION .)
        endif()
    endif()

    set(directory "${CMAKE_CURRENT_BINARY_DIR}/${target}_resources")
    set(license "${directory}/package-license.txt")
    set(license_text "${name} ${version}\n")
    if(copyright)
        string(APPEND license_text "${copyright}\n")
    endif()
    file(WRITE "${license}" "${license_text}")

    set(CPACK_PACKAGE_NAME "${name}")
    set(CPACK_PACKAGE_VENDOR "${organization}")
    set(CPACK_PACKAGE_VERSION "${version}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${name}")
    set(CPACK_PACKAGE_FILE_NAME "${name}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${name}")
    set(CPACK_RESOURCE_FILE_LICENSE "${license}")
    set(CPACK_STRIP_FILES OFF)
    if(WIN32)
        set(CPACK_GENERATOR ZIP)
        get_target_property(ico ${target} ILLUSION_GAME_ICO)
        set(CPACK_NSIS_MUI_ICON "${ico}")
        set(CPACK_NSIS_MUI_UNIICON "${ico}")
        set(CPACK_NSIS_INSTALLED_ICON_NAME "${executable}.exe")
        set(CPACK_NSIS_DISPLAY_NAME "${name}")
        set(CPACK_NSIS_PACKAGE_NAME "${name}")
        set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
        set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
        set(CPACK_PACKAGE_EXECUTABLES "${executable};${name}")
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
        set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
    elseif(APPLE)
        set(CPACK_GENERATOR ZIP)
        # The archive holds <name>.app itself, as the DMG does.
        set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
        set(CPACK_DMG_VOLUME_NAME "${name}")
    else()
        set(CPACK_GENERATOR TGZ)
    endif()
    include(CPack)
endfunction()
