# Tests of exported games (ADR 0055), in script mode:
#
#   -DMODE=copy    -DSOURCE=<project> -DDESTINATION=<directory>
#       copies a project without its .illusion directory, so that the export starts from a clean state.
#   -DMODE=run     -DGAME=<executable> -DUSER=<directory> [-DFRAMES=<count>]
#       runs the exported game for FRAMES frames (default 300) with a fixed step and its user data in USER, and
#       fails if it fails or its player.log holds an error.
#   -DMODE=symbols -DGAME=<executable>
#       fails if the executable names Dear ImGui, ImPlot or the editor's namespace (ADR 0030): in symbols,
#       RTTI names or strings, mangled the MSVC or the Itanium way.

cmake_minimum_required(VERSION 3.28)

function(require)
    foreach(variable IN LISTS ARGN)
        if(NOT DEFINED ${variable})
            message(FATAL_ERROR "ExportGame.cmake: ${variable} is not set")
        endif()
    endforeach()
endfunction()

require(MODE)
if(MODE STREQUAL "copy")
    require(SOURCE DESTINATION)
    file(REMOVE_RECURSE "${DESTINATION}")
    file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" PATTERN ".illusion" EXCLUDE)
elseif(MODE STREQUAL "run")
    require(GAME USER)
    if(NOT DEFINED FRAMES)
        set(FRAMES 300)
    endif()
    if(NOT EXISTS "${GAME}")
        message(FATAL_ERROR "the exported game ${GAME} does not exist")
    endif()
    file(REMOVE_RECURSE "${USER}")
    execute_process(COMMAND "${GAME}" --fixed-step --frames ${FRAMES} --no-dialogs --user-data "${USER}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output TIMEOUT 600)
    set(log "")
    if(EXISTS "${USER}/player.log")
        file(READ "${USER}/player.log" log)
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${GAME} failed (${result}):\n${output}\n${log}")
    endif()
    string(REGEX MATCHALL "[^\n]*\\[ERROR\\][^\n]*" errors "${log}")
    # A machine without a sound device (CI) still runs the game; the audio backend reports the device.
    list(FILTER errors EXCLUDE REGEX "\\[Audio\\].*device")
    if(errors)
        list(JOIN errors "\n" errors)
        message(FATAL_ERROR "${GAME} logged errors:\n${errors}")
    endif()
    if(NOT log MATCHES "ended after ${FRAMES} frames")
        message(FATAL_ERROR "${GAME} did not run ${FRAMES} frames:\n${log}")
    endif()
elseif(MODE STREQUAL "symbols")
    require(GAME)
    if(NOT EXISTS "${GAME}")
        message(FATAL_ERROR "the exported game ${GAME} does not exist")
    endif()
    file(STRINGS "${GAME}" found REGEX "ImGui|ImPlot|illusion::[e]ditor|editor@illusion@@|8illusion6editor")
    if(found)
        list(REMOVE_DUPLICATES found)
        list(SUBLIST found 0 20 found)
        list(JOIN found "\n  " found)
        message(FATAL_ERROR "${GAME} holds editor code (ADR 0030):\n  ${found}")
    endif()
else()
    message(FATAL_ERROR "ExportGame.cmake: unknown MODE '${MODE}'")
endif()
