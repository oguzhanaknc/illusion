# The Steam store (docs/adr/0056-store-service-and-steamworks.md).
#
# illusion_add_steam_library(<steam directory>)
#
# Builds illusion::steam (SteamStoreService) from <steam directory>/src and <steam directory>/include against the
# Steamworks SDK that ILLUSION_STEAMWORKS_SDK names. The SDK is supplied by the user and never committed or
# downloaded; a path that is not a Steamworks SDK with this platform's redistributable library fails the configure
# step. The engine's build calls it from engine/steam; a game's build gets it with
# find_package(illusion CONFIG COMPONENTS player steam), which uses the sources the package carries.
#
# The Steam headers are a private, system include directory of illusion::steam. The target property
# ILLUSION_STEAM_RUNTIME holds the redistributable library that a game ships next to its executable.

include_guard(GLOBAL)

set(ILLUSION_STEAMWORKS_SDK "" CACHE PATH
    "Directory of the Steamworks SDK (user-supplied, never committed); builds illusion::steam")

# The redistributable library of the SDK at <sdk> for this platform in <runtime>, and the file to link in <link>.
function(_illusion_steamworks_files sdk runtime link)
    set(bin "${sdk}/redistributable_bin")
    if(WIN32)
        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            message(FATAL_ERROR "illusion::steam: the engine builds 64-bit games only")
        endif()
        set(found_runtime "${bin}/win64/steam_api64.dll")
        set(found_link "${bin}/win64/steam_api64.lib")
    elseif(APPLE)
        set(found_runtime "${bin}/osx/libsteam_api.dylib")
        set(found_link "${found_runtime}")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(found_runtime "${bin}/linuxarm64/libsteam_api.so")
        set(found_link "${found_runtime}")
    else()
        set(found_runtime "${bin}/linux64/libsteam_api.so")
        set(found_link "${found_runtime}")
    endif()
    set(${runtime} "${found_runtime}" PARENT_SCOPE)
    set(${link} "${found_link}" PARENT_SCOPE)
endfunction()

function(illusion_add_steam_library directory)
    if(TARGET illusion_steam)
        return()
    endif()
    if(NOT ILLUSION_STEAMWORKS_SDK)
        message(FATAL_ERROR "illusion::steam needs the Steamworks SDK: set ILLUSION_STEAMWORKS_SDK to its directory "
            "(the one with public/steam/steam_api.h)")
    endif()
    cmake_path(ABSOLUTE_PATH ILLUSION_STEAMWORKS_SDK NORMALIZE OUTPUT_VARIABLE sdk)
    if(NOT EXISTS "${sdk}/public/steam/steam_api.h")
        message(FATAL_ERROR "ILLUSION_STEAMWORKS_SDK (${ILLUSION_STEAMWORKS_SDK}) is not a Steamworks SDK: "
            "${sdk}/public/steam/steam_api.h does not exist")
    endif()
    _illusion_steamworks_files("${sdk}" runtime link)
    foreach(file IN ITEMS "${runtime}" "${link}")
        if(NOT EXISTS "${file}")
            message(FATAL_ERROR "ILLUSION_STEAMWORKS_SDK (${ILLUSION_STEAMWORKS_SDK}) has no redistributable library "
                "for this platform: ${file} does not exist")
        endif()
    endforeach()

    add_library(illusion_steamworks SHARED IMPORTED GLOBAL)
    set_target_properties(illusion_steamworks PROPERTIES
        IMPORTED_LOCATION "${runtime}"
        INTERFACE_INCLUDE_DIRECTORIES "${sdk}/public")
    if(WIN32)
        set_target_properties(illusion_steamworks PROPERTIES IMPORTED_IMPLIB "${link}")
    endif()

    add_library(illusion_steam STATIC "${directory}/src/SteamStoreService.cpp")
    add_library(illusion::steam ALIAS illusion_steam)
    target_include_directories(illusion_steam PUBLIC "$<BUILD_INTERFACE:${directory}/include>")
    target_link_libraries(illusion_steam PUBLIC illusion::store PRIVATE illusion_steamworks)
    set_target_properties(illusion_steam PROPERTIES ILLUSION_STEAM_RUNTIME "${runtime}")
    illusion_configure_target(illusion_steam)
endfunction()
