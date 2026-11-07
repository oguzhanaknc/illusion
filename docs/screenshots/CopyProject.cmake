# Copies the showcase project for the manual screenshots (CTest docs.screenshots.project):
#   cmake -D SOURCE=<samples/showcase> -D DESTINATION=<directory> -P CopyProject.cmake
# The copy leaves out the editor's cache directory (.illusion) so that every run starts the same way.
file(REMOVE_RECURSE "${DESTINATION}")
file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" PATTERN ".illusion" EXCLUDE)
