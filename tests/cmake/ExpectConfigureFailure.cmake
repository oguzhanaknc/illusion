# Configures the fixture project FIXTURE in BINARY_DIR and succeeds only if the configure fails with a
# message matching EXPECTED.
#
#   cmake -DFIXTURE=<dir> -DBINARY_DIR=<dir> -DEXPECTED=<regex> -DILLUSION_CMAKE_DIR=<dir> -P ExpectConfigureFailure.cmake

file(REMOVE_RECURSE "${BINARY_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${FIXTURE}" -B "${BINARY_DIR}" "-DILLUSION_CMAKE_DIR=${ILLUSION_CMAKE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "the configure of ${FIXTURE} succeeded but must fail")
endif()
if(NOT "${output}${error}" MATCHES "${EXPECTED}")
    message(FATAL_ERROR "the configure failed without the expected message '${EXPECTED}':\n${output}${error}")
endif()
message(STATUS "the configure failed as expected")
