# Exported games (docs/adr/0055-exported-games.md).
#
# illusion_add_game(<target> MODULES <module targets...>
#                   NAME <product name> ORGANIZATION <organization> VERSION <version>
#                   STARTUP_SCENE <virtual path> [DATA_DIRECTORY <directory>] [PROJECT <project directory>]
#                   [ICON <square png>] [IDENTIFIER <reverse-DNS id>] [COPYRIGHT <line>])
#
# Builds the executable <target> of a game: a generated main.cpp passes a PlayerDesc with the game's name,
# organization, version, startup scene and static modules to illusion::runPlayer(), and links illusion::player
# and the modules. Every module must be a static library made by illusion_add_module(... STATIC); its entry
# point is illusionModuleEntry_<id>, with the id of its ILLUSION_MODULE_ID property. DATA_DIRECTORY is where
# the player looks for its archives, relative to the executable (default "data"; on macOS relative to
# Contents/Resources of the bundle).
#
# With PROJECT, a build step packs the project's data next to the executable with illusion::pack (the target
# illusion_pack in the engine's build, an imported executable in the package):
# <DATA_DIRECTORY>/assets.illpak from <project>/assets, settings.illpak from <project>/settings (empty when the
# project has none) and cache.illpak with the assets cooked for the `assets` mount.
#
# The executable gets the resources of its platform, made by illusion::icon from ICON (the engine's icon
# without it):
#   - Windows: an .ico with every size from 16 to 256 pixels and a VERSIONINFO (product name and version,
#     company = ORGANIZATION, COPYRIGHT).
#   - macOS: an application bundle with a generated Info.plist (bundle id IDENTIFIER, default
#     "com.<organization>.<name>"), an .icns made with iconutil, and the Vulkan loader with MoltenVK and its
#     driver manifest from the build machine (the Vulkan SDK or Homebrew) in Contents/Frameworks, so that the
#     bundle runs without a Vulkan SDK. Without them configuring fails when ILLUSION_GAME_REQUIRE_VULKAN is ON
#     (as in exports) and warns otherwise.
#   - Linux: <executable>.png (512 pixels) and <executable>.desktop next to the executable.
# illusion_package_game() (IllusionGamePackage.cmake) installs and packages them.

include_guard(GLOBAL)

set(ILLUSION_PACK_GAME_DATA_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/PackGameData.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/IllusionGamePackage.cmake")

# @p text as the body of a C++ string literal.
function(_illusion_cpp_string out text)
    string(REPLACE "\\" "\\\\" text "${text}")
    string(REPLACE "\"" "\\\"" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# @p text as the body of a Windows resource string.
function(_illusion_rc_string out text)
    string(REPLACE "\\" "\\\\" text "${text}")
    string(REPLACE "\"" "\"\"" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# @p text as XML character data that configure_file() leaves alone (no `$` or `@`).
function(_illusion_xml_string out text)
    string(REPLACE "&" "&amp;" text "${text}")
    string(REPLACE "<" "&lt;" text "${text}")
    string(REPLACE ">" "&gt;" text "${text}")
    string(REPLACE "\"" "&quot;" text "${text}")
    string(REPLACE "$" "&#36;" text "${text}")
    string(REPLACE "@" "&#64;" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# The four numbers of a version resource from the leading "a.b.c.d" of @p version (missing ones are 0).
function(_illusion_numeric_version out version)
    string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" numeric "${version}")
    string(REPLACE "." ";" parts "${numeric}")
    list(APPEND parts 0 0 0 0)
    list(SUBLIST parts 0 4 parts)
    set(result "")
    foreach(part IN LISTS parts)
        string(REGEX REPLACE "^0+([0-9])" "\\1" part "${part}")
        string(LENGTH "${part}" digits)
        if(digits GREATER 5 OR part GREATER 65535)
            set(part 65535)
        endif()
        list(APPEND result "${part}")
    endforeach()
    set(${out} "${result}" PARENT_SCOPE)
endfunction()

# A reverse-DNS label made of @p text: ASCII letters, digits and hyphens.
function(_illusion_dns_label out text)
    string(TOLOWER "${text}" text)
    string(REGEX REPLACE "[^a-z0-9-]+" "-" text "${text}")
    string(REGEX REPLACE "^-+|-+$" "" text "${text}")
    if(text STREQUAL "")
        set(text "game")
    endif()
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(_illusion_game_windows_resources target name organization version copyright icon_input icon_depends)
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/${target}_resources")
    set(ico "${directory}/${target}.ico")
    add_custom_command(
        OUTPUT "${ico}"
        COMMAND "$<TARGET_FILE:illusion::icon>" "${icon_input}" --ico "${ico}"
        DEPENDS "$<TARGET_FILE:illusion::icon>" ${icon_depends}
        COMMENT "Making the icon of ${target}"
        VERBATIM)
    _illusion_numeric_version(numbers "${version}")
    list(JOIN numbers "," file_version)
    foreach(field name organization version copyright)
        _illusion_rc_string(rc_${field} "${${field}}")
    endforeach()
    set(rc_ico "${ico}")
    set(rc "${directory}/${target}.rc")
    file(CONFIGURE OUTPUT "${rc}" CONTENT [[
// Generated by illusion_add_game(); do not edit.
#pragma code_page(65001)
#include <winver.h>

1 ICON "@rc_ico@"

VS_VERSION_INFO VERSIONINFO
 FILEVERSION @file_version@
 PRODUCTVERSION @file_version@
 FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
 FILEFLAGS 0x0L
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_APP
 FILESUBTYPE VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName", "@rc_organization@"
            VALUE "FileDescription", "@rc_name@"
            VALUE "FileVersion", "@rc_version@"
            VALUE "LegalCopyright", "@rc_copyright@"
            VALUE "ProductName", "@rc_name@"
            VALUE "ProductVersion", "@rc_version@"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
]] @ONLY)
    target_sources(${target} PRIVATE "${rc}" "${ico}")
    set_source_files_properties("${rc}" PROPERTIES OBJECT_DEPENDS "${ico}")
    set_target_properties(${target} PROPERTIES ILLUSION_GAME_ICO "${ico}")
endfunction()

# The Vulkan loader and MoltenVK of the build machine, copied into the bundle with a driver manifest that
# points at Contents/Frameworks (the loader looks for drivers in Contents/Resources/vulkan/icd.d).
#
# The Vulkan SDK (VULKAN_SDK) is searched first, then Homebrew (`brew --prefix`, which is /opt/homebrew on Apple
# Silicon and /usr/local on Intel). A missing library is a configure error when ILLUSION_GAME_REQUIRE_VULKAN is ON
# (exports set it: a shipped game must run without a Vulkan SDK), a warning otherwise.
function(_illusion_game_bundle_vulkan target directory)
    set(prefixes "")
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND prefixes "$ENV{VULKAN_SDK}")
    endif()
    find_program(ILLUSION_BREW_EXECUTABLE brew)
    if(ILLUSION_BREW_EXECUTABLE)
        foreach(formula "" vulkan-loader molten-vk)
            execute_process(COMMAND "${ILLUSION_BREW_EXECUTABLE}" --prefix ${formula} OUTPUT_VARIABLE prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE result)
            if(result EQUAL 0 AND IS_DIRECTORY "${prefix}")
                list(APPEND prefixes "${prefix}")
            endif()
        endforeach()
    endif()
    list(APPEND prefixes "/opt/homebrew" "/usr/local")
    list(REMOVE_DUPLICATES prefixes)
    set(hints "")
    foreach(prefix IN LISTS prefixes)
        list(APPEND hints "${prefix}/lib")
    endforeach()
    find_library(ILLUSION_VULKAN_LOADER_LIBRARY NAMES vulkan.1 vulkan HINTS ${hints})
    find_library(ILLUSION_MOLTENVK_LIBRARY NAMES MoltenVK HINTS ${hints})
    set(missing "")
    if(NOT ILLUSION_VULKAN_LOADER_LIBRARY)
        list(APPEND missing "the Vulkan loader (libvulkan.1.dylib)")
    endif()
    if(NOT ILLUSION_MOLTENVK_LIBRARY)
        list(APPEND missing "MoltenVK (libMoltenVK.dylib)")
    endif()
    if(missing)
        list(JOIN missing " and " missing)
        set(text "illusion_add_game(${target}): ${missing} not found in the Vulkan SDK (VULKAN_SDK) or under the "
            "Homebrew prefix; install the Vulkan SDK for macOS and set VULKAN_SDK, or run "
            "`brew install vulkan-loader molten-vk`, on the machine that builds the game")
        if(ILLUSION_GAME_REQUIRE_VULKAN)
            message(FATAL_ERROR "${text}")
        endif()
        get_property(warned GLOBAL PROPERTY _ILLUSION_MOLTENVK_WARNED)
        if(NOT warned)
            set_property(GLOBAL PROPERTY _ILLUSION_MOLTENVK_WARNED TRUE)
            message(WARNING "${text}; the bundle then needs a Vulkan loader on the player's Mac")
        endif()
        return()
    endif()
    set(api_version "1.2.0")
    set(manifest_dirs "")
    foreach(prefix IN LISTS prefixes)
        if(prefix)
            list(APPEND manifest_dirs "${prefix}/share/vulkan/icd.d" "${prefix}/etc/vulkan/icd.d")
        endif()
    endforeach()
    find_file(ILLUSION_MOLTENVK_MANIFEST MoltenVK_icd.json HINTS ${manifest_dirs} NO_CACHE)
    if(ILLUSION_MOLTENVK_MANIFEST)
        file(READ "${ILLUSION_MOLTENVK_MANIFEST}" manifest)
        string(JSON found ERROR_VARIABLE error GET "${manifest}" ICD api_version)
        if(NOT error)
            set(api_version "${found}")
        endif()
    endif()
    set(manifest "${directory}/MoltenVK_icd.json")
    file(CONFIGURE OUTPUT "${manifest}" CONTENT [[
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../../Frameworks/libMoltenVK.dylib",
        "api_version": "@api_version@",
        "is_portability_driver": true
    }
}
]] @ONLY)
    file(REAL_PATH "${ILLUSION_VULKAN_LOADER_LIBRARY}" loader)
    file(REAL_PATH "${ILLUSION_MOLTENVK_LIBRARY}" moltenvk)
    set(contents "$<TARGET_BUNDLE_CONTENT_DIR:${target}>")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${contents}/Frameworks" "${contents}/Resources/vulkan/icd.d"
        COMMAND "${CMAKE_COMMAND}" -E copy "${loader}" "${contents}/Frameworks/libvulkan.1.dylib"
        COMMAND "${CMAKE_COMMAND}" -E copy "${moltenvk}" "${contents}/Frameworks/libMoltenVK.dylib"
        COMMAND "${CMAKE_COMMAND}" -E copy "${manifest}" "${contents}/Resources/vulkan/icd.d/MoltenVK_icd.json"
        VERBATIM)
endfunction()

function(_illusion_game_macos_resources target name organization version copyright identifier icon_input
        icon_depends)
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/${target}_resources")
    set(iconset "${directory}/${target}.iconset")
    set(icns "${directory}/${target}.icns")
    add_custom_command(
        OUTPUT "${icns}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${iconset}"
        COMMAND "$<TARGET_FILE:illusion::icon>" "${icon_input}" --iconset "${iconset}"
        COMMAND iconutil -c icns -o "${icns}" "${iconset}"
        DEPENDS "$<TARGET_FILE:illusion::icon>" ${icon_depends}
        COMMENT "Making the icon of ${target}"
        VERBATIM)
    target_sources(${target} PRIVATE "${icns}")
    set_source_files_properties("${icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    if(NOT identifier)
        _illusion_dns_label(organization_label "${organization}")
        _illusion_dns_label(name_label "${name}")
        set(identifier "com.${organization_label}.${name_label}")
    endif()
    set(minimum "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    if(NOT minimum)
        execute_process(COMMAND xcrun --sdk macosx --show-sdk-version OUTPUT_VARIABLE minimum
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    if(NOT minimum)
        set(minimum "11.0")
    endif()
    _illusion_numeric_version(numbers "${version}")
    list(SUBLIST numbers 0 3 numbers)
    list(JOIN numbers "." bundle_version)
    foreach(field name identifier version copyright minimum bundle_version)
        _illusion_xml_string(xml_${field} "${${field}}")
    endforeach()
    set(plist "${directory}/Info.plist.in")
    # CMake configures this file again for the bundle, filling in MACOSX_BUNDLE_EXECUTABLE_NAME.
    file(WRITE "${plist}"
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "    <key>CFBundleDevelopmentRegion</key>\n    <string>en</string>\n"
        "    <key>CFBundleExecutable</key>\n    <string>\${MACOSX_BUNDLE_EXECUTABLE_NAME}</string>\n"
        "    <key>CFBundleIconFile</key>\n    <string>${target}.icns</string>\n"
        "    <key>CFBundleIdentifier</key>\n    <string>${xml_identifier}</string>\n"
        "    <key>CFBundleInfoDictionaryVersion</key>\n    <string>6.0</string>\n"
        "    <key>CFBundleName</key>\n    <string>${xml_name}</string>\n"
        "    <key>CFBundleDisplayName</key>\n    <string>${xml_name}</string>\n"
        "    <key>CFBundlePackageType</key>\n    <string>APPL</string>\n"
        "    <key>CFBundleShortVersionString</key>\n    <string>${xml_bundle_version}</string>\n"
        "    <key>CFBundleVersion</key>\n    <string>${xml_bundle_version}</string>\n"
        "    <key>LSApplicationCategoryType</key>\n    <string>public.app-category.games</string>\n"
        "    <key>LSMinimumSystemVersion</key>\n    <string>${xml_minimum}</string>\n"
        "    <key>NSHighResolutionCapable</key>\n    <true/>\n"
        "    <key>NSHumanReadableCopyright</key>\n    <string>${xml_copyright}</string>\n"
        "</dict>\n</plist>\n")
    # The install of the bundle must not change the executable after it was signed.
    set_target_properties(${target} PROPERTIES MACOSX_BUNDLE ON MACOSX_BUNDLE_INFO_PLIST "${plist}"
        BUILD_WITH_INSTALL_RPATH ON)
    _illusion_game_bundle_vulkan(${target} "${directory}")
endfunction()

function(_illusion_game_linux_resources target name icon_input icon_depends)
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/${target}_resources")
    set(png "${directory}/${target}.png")
    add_custom_command(
        OUTPUT "${png}"
        COMMAND "$<TARGET_FILE:illusion::icon>" "${icon_input}" --png 512 "${png}"
        DEPENDS "$<TARGET_FILE:illusion::icon>" ${icon_depends}
        COMMENT "Making the icon of ${target}"
        VERBATIM)
    target_sources(${target} PRIVATE "${png}")
    string(REPLACE "\\" "\\\\" desktop_name "${name}")
    set(desktop "${directory}/${target}.desktop")
    string(CONCAT desktop_text
        "[Desktop Entry]\nType=Application\nVersion=1.5\nName=${desktop_name}\nExec=$<TARGET_FILE_NAME:${target}>\n"
        "Icon=$<TARGET_FILE_BASE_NAME:${target}>\nTerminal=false\nCategories=Game;\n")
    file(GENERATE OUTPUT "${desktop}" CONTENT "${desktop_text}")
    # Next to the executable under its name, as the install and the packages ship them.
    set(base "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy "${png}" "${base}.png"
        COMMAND "${CMAKE_COMMAND}" -E copy "${desktop}" "${base}.desktop"
        VERBATIM)
endfunction()

function(illusion_add_game target)
    cmake_parse_arguments(PARSE_ARGV 1 arg ""
        "NAME;ORGANIZATION;VERSION;STARTUP_SCENE;DATA_DIRECTORY;PROJECT;ICON;IDENTIFIER;COPYRIGHT" "MODULES")
    foreach(required NAME ORGANIZATION STARTUP_SCENE)
        if(NOT arg_${required})
            message(FATAL_ERROR "illusion_add_game(${target}): ${required} is required")
        endif()
    endforeach()
    if(NOT arg_DATA_DIRECTORY)
        set(arg_DATA_DIRECTORY data)
    endif()

    set(declarations "")
    set(entries "")
    foreach(module IN LISTS arg_MODULES)
        if(NOT TARGET ${module})
            message(FATAL_ERROR "illusion_add_game(${target}): no module target '${module}'")
        endif()
        get_target_property(type ${module} TYPE)
        get_target_property(id ${module} ILLUSION_MODULE_ID)
        if(NOT type STREQUAL "STATIC_LIBRARY" OR NOT id)
            message(FATAL_ERROR "illusion_add_game(${target}): '${module}' is not a static module; build it with "
                "illusion_add_module(${module} STATIC ...)")
        endif()
        string(APPEND declarations "ILL_DECLARE_STATIC_MODULE(${id});\n")
        list(APPEND entries "illusionModuleEntry_${id}")
    endforeach()
    if(entries)
        list(JOIN entries ", " entry_list)
        set(modules_code "    static constexpr illusion::PlayerModuleEntry kModules[] = {${entry_list}};\n    desc.modules = kModules;\n")
    else()
        set(modules_code "")
    endif()
    _illusion_cpp_string(name "${arg_NAME}")
    _illusion_cpp_string(organization "${arg_ORGANIZATION}")
    _illusion_cpp_string(version "${arg_VERSION}")
    _illusion_cpp_string(scene "${arg_STARTUP_SCENE}")
    _illusion_cpp_string(data "${arg_DATA_DIRECTORY}")

    set(main "${CMAKE_CURRENT_BINARY_DIR}/${target}_main.cpp")
    file(CONFIGURE OUTPUT "${main}" CONTENT [[
// Generated by illusion_add_game(); do not edit.

#include <illusion/core/Module.hpp>
#include <illusion/player/Player.hpp>

@declarations@
int main(int argc, char** argv)
{
    illusion::PlayerDesc desc;
    desc.name = "@name@";
    desc.organization = "@organization@";
    desc.version = "@version@";
    desc.startupScene = "@scene@";
    desc.dataDirectory = "@data@";
@modules_code@    return illusion::runPlayer(desc, argc, argv);
}
]] @ONLY)

    add_executable(${target} "${main}")
    target_link_libraries(${target} PRIVATE illusion::player ${arg_MODULES})
    if(WIN32)
        # A game opens no console window; main() stays the entry point.
        set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE ON)
        if(MSVC)
            target_link_options(${target} PRIVATE "/ENTRY:mainCRTStartup")
        endif()
    endif()
    illusion_configure_target(${target})

    if(arg_ICON)
        cmake_path(ABSOLUTE_PATH arg_ICON BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        if(NOT EXISTS "${arg_ICON}")
            message(FATAL_ERROR "illusion_add_game(${target}): the icon ${arg_ICON} does not exist")
        endif()
        set(icon_input "${arg_ICON}")
        set(icon_depends "${arg_ICON}")
    else()
        set(icon_input "-")
        set(icon_depends "")
    endif()
    set_target_properties(${target} PROPERTIES ILLUSION_GAME_NAME "${arg_NAME}"
        ILLUSION_GAME_ORGANIZATION "${arg_ORGANIZATION}" ILLUSION_GAME_VERSION "${arg_VERSION}"
        ILLUSION_GAME_COPYRIGHT "${arg_COPYRIGHT}")
    if(WIN32)
        _illusion_game_windows_resources(${target} "${arg_NAME}" "${arg_ORGANIZATION}" "${arg_VERSION}"
            "${arg_COPYRIGHT}" "${icon_input}" "${icon_depends}")
    elseif(APPLE)
        _illusion_game_macos_resources(${target} "${arg_NAME}" "${arg_ORGANIZATION}" "${arg_VERSION}"
            "${arg_COPYRIGHT}" "${arg_IDENTIFIER}" "${icon_input}" "${icon_depends}")
    else()
        _illusion_game_linux_resources(${target} "${arg_NAME}" "${icon_input}" "${icon_depends}")
    endif()

    if(arg_PROJECT)
        cmake_path(ABSOLUTE_PATH arg_PROJECT BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        file(GLOB_RECURSE inputs CONFIGURE_DEPENDS "${arg_PROJECT}/assets/*" "${arg_PROJECT}/settings/*")
        set(stamp "${CMAKE_CURRENT_BINARY_DIR}/${target}_data_$<CONFIG>.stamp")
        if(APPLE)
            set(data_output "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/${arg_DATA_DIRECTORY}")
        else()
            set(data_output "$<TARGET_FILE_DIR:${target}>/${arg_DATA_DIRECTORY}")
        endif()
        add_custom_command(
            OUTPUT "${stamp}"
            COMMAND "${CMAKE_COMMAND}" "-DPACK=$<TARGET_FILE:illusion::pack>" "-DPROJECT=${arg_PROJECT}"
                    "-DOUTPUT=${data_output}" "-DSTAMP=${stamp}"
                    -P "${ILLUSION_PACK_GAME_DATA_SCRIPT}"
            DEPENDS "$<TARGET_FILE:illusion::pack>" ${inputs} "${ILLUSION_PACK_GAME_DATA_SCRIPT}"
            COMMENT "Packing the data of ${target}"
            VERBATIM)
        add_custom_target(${target}_data ALL DEPENDS "${stamp}")
        add_dependencies(${target} ${target}_data)
    endif()
endfunction()
