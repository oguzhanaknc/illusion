# Regression tests of illusion_pack: it packs and cooks a directory whose path is not ASCII (on Windows it once
# read its arguments in the ANSI code page as if they were UTF-8 and aborted), and it cooks material files
# (it once did not reflect the material types).
#   cmake -DPACK=<illusion_pack> -DWORK=<scratch directory> -DMATERIAL=<.material file> -P PackUnicodePath.cmake

set(root "${WORK}/Masaüstü ğış")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${root}/assets/data")
file(WRITE "${root}/assets/data/note.txt" "packed")
file(COPY "${MATERIAL}" DESTINATION "${root}/assets/materials")

execute_process(COMMAND "${PACK}" "${root}/assets" "${root}/assets.illpak" RESULT_VARIABLE packed TIMEOUT 120)
execute_process(COMMAND "${PACK}" --cook assets "${root}/assets" "${root}/cache.illpak" RESULT_VARIABLE cooked
    TIMEOUT 120)
if(NOT packed EQUAL 0 OR NOT EXISTS "${root}/assets.illpak")
    message(FATAL_ERROR "illusion_pack failed on a non-ASCII path (${packed})")
endif()
if(NOT cooked EQUAL 0 OR NOT EXISTS "${root}/cache.illpak")
    message(FATAL_ERROR "illusion_pack --cook failed on a non-ASCII path (${cooked})")
endif()
file(REMOVE_RECURSE "${WORK}")
