# Packs the data of a game for the player (script mode, run by illusion_add_game(... PROJECT ...)):
#   cmake -DPACK=<illusion_pack> -DPROJECT=<project directory> -DOUTPUT=<data directory> [-DSTAMP=<file>]
#         -P PackGameData.cmake
# writes OUTPUT/assets.illpak, OUTPUT/settings.illpak and OUTPUT/cache.illpak (the assets cooked for the
# `assets` mount). A project without a settings directory gets an empty settings archive.

foreach(variable PACK PROJECT OUTPUT)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "PackGameData.cmake: ${variable} is not set")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${PROJECT}/assets")
    message(FATAL_ERROR "PackGameData.cmake: ${PROJECT} has no assets directory")
endif()

file(MAKE_DIRECTORY "${OUTPUT}")
set(settings "${PROJECT}/settings")
if(NOT IS_DIRECTORY "${settings}")
    set(settings "${OUTPUT}/.empty-settings")
    file(REMOVE_RECURSE "${settings}")
    file(MAKE_DIRECTORY "${settings}")
endif()

function(pack)
    execute_process(COMMAND "${PACK}" ${ARGN} RESULT_VARIABLE result TIMEOUT 1200)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "PackGameData.cmake: illusion_pack ${ARGN} failed (${result})")
    endif()
endfunction()

pack("${PROJECT}/assets" "${OUTPUT}/assets.illpak")
pack("${settings}" "${OUTPUT}/settings.illpak")
pack(--cook assets "${PROJECT}/assets" "${OUTPUT}/cache.illpak")
if(settings STREQUAL "${OUTPUT}/.empty-settings")
    file(REMOVE_RECURSE "${settings}")
endif()
if(DEFINED STAMP)
    file(TOUCH "${STAMP}")
endif()
