# A recording stand-in for a Windows sign command (ADR 0055), in script mode:
#
#   cmake -DLOG=<file> -DFILE=<file to sign> -P RecordSign.cmake
#
# appends FILE to LOG and the marker ILLUSION-TEST-SIGNATURE to FILE (unless it ends with it), so that a test can
# tell what was signed, in which order, and whether the installed and packaged copies are the signed ones. Fails
# if FILE is missing.

cmake_minimum_required(VERSION 3.28)

foreach(variable LOG FILE)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "RecordSign.cmake: ${variable} is not set")
    endif()
endforeach()
file(TO_CMAKE_PATH "${FILE}" file)
if(NOT EXISTS "${file}")
    message(FATAL_ERROR "RecordSign.cmake: ${file} does not exist")
endif()
file(APPEND "${LOG}" "${file}\n")
# Signing a signed file replaces its signature, as signtool does: a file never carries the marker twice.
set(marker "ILLUSION-TEST-SIGNATURE")
string(LENGTH "${marker}" length)
file(SIZE "${file}" size)
set(tail "")
if(size GREATER_EQUAL length)
    math(EXPR offset "${size} - ${length}")
    file(READ "${file}" tail OFFSET ${offset} LIMIT ${length})
endif()
if(NOT tail STREQUAL marker)
    file(APPEND "${file}" "${marker}")
endif()
