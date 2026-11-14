# The engine's own package (ROADMAP M8-7; docs/adr/0055-exported-games.md, "Releases of the engine itself").
#
# illusion_package_engine()
#
# Configures CPack for one archive per platform that holds every install component of the engine
# (include(CPack) runs here, so call it once, after every target and install rule exists):
#   - `editor`: illusion_editor in bin/<Config>/ with its editor-data/ (themes, locales, fonts, templates,
#     licences) next to it;
#   - `tools`: illusion_ctl, illusion_mcp and illusion_textc in bin/<Config>/;
#   - `sdk` and `player`: the CMake package (cmake/IllusionSdk.cmake), illusion_pack and illusion_icon in
#     bin/<Config>/; `sdk` also carries THIRD_PARTY_NOTICES.md, CHANGELOG.md and README.md at the top.
# The layout is relocatable: the editor finds editor-data/ and the installed SDK relative to its executable.
#
#   cpack --config <build>/CPackConfig.cmake -C Release -B <directory>
#
# writes <directory>/illusion-<version>-<platform>-<arch>.<zip|tar.gz> (ZIP on Windows, .tar.gz on Linux and
# macOS) with the directory of the same name inside, and its SHA-256 in <package>.sha256. The version is the
# project's (the root CMakeLists.txt).

include_guard(GLOBAL)

# The package's architecture name: x86_64 or arm64 (or the processor name CMake reports for others).
function(_illusion_release_architecture out)
    set(processor "${CMAKE_SYSTEM_PROCESSOR}")
    list(LENGTH CMAKE_OSX_ARCHITECTURES count)
    if(APPLE AND count EQUAL 1)
        set(processor "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    string(TOLOWER "${processor}" processor)
    if(processor MATCHES "^(amd64|x86_64|x64)$")
        set(processor x86_64)
    elseif(processor MATCHES "^(arm64|aarch64)$")
        set(processor arm64)
    endif()
    set(${out} "${processor}" PARENT_SCOPE)
endfunction()

function(illusion_package_engine)
    if(ILLUSION_PROFILING)
        # Profiling builds have no SDK (cmake/IllusionSdk.cmake), so there is nothing to release.
        return()
    endif()
    install(FILES "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
        "${PROJECT_SOURCE_DIR}/README.md" DESTINATION . COMPONENT sdk)

    set(components sdk)
    if(TARGET illusion_player)
        list(APPEND components player)
    endif()
    list(APPEND components tools)
    if(TARGET illusion_editor)
        list(APPEND components editor)
    endif()

    if(WIN32)
        set(platform windows)
        set(CPACK_GENERATOR ZIP)
    elseif(APPLE)
        set(platform macos)
        set(CPACK_GENERATOR TGZ)
    else()
        set(platform linux)
        set(CPACK_GENERATOR TGZ)
    endif()
    _illusion_release_architecture(architecture)

    set(CPACK_PACKAGE_NAME illusion)
    set(CPACK_PACKAGE_VENDOR illusion)
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_PACKAGE_FILE_NAME "illusion-${PROJECT_VERSION}-${platform}-${architecture}")
    set(CPACK_PACKAGE_CHECKSUM SHA256)
    set(CPACK_STRIP_FILES OFF)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
    set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_COMPONENTS_ALL ${components})
    include(CPack)
endfunction()
