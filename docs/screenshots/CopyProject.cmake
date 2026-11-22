# Copies the showcase project for the manual screenshots (CTest docs.screenshots.project):
#   cmake -D SOURCE=<samples/showcase> -D DESTINATION=<directory> -P CopyProject.cmake
# The copy leaves out the editor's cache directory (.illusion) so that every run starts the same way, and the
# directory names in the optional list EXCLUDE (the tech demo's downloaded assets/external).
file(REMOVE_RECURSE "${DESTINATION}")
set(excluded PATTERN ".illusion" EXCLUDE)
foreach(directory IN LISTS EXCLUDE)
    list(APPEND excluded PATTERN "${directory}" EXCLUDE)
endforeach()
file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" ${excluded})
