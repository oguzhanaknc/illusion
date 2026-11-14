# illusion_pack: packs a directory into an archive, or cooks its assets into an import-cache archive.

add_executable(illusion_pack main.cpp)
# The name illusion_add_game() uses, in the build tree and in the package (cmake/IllusionSdk.cmake).
add_executable(illusion::pack ALIAS illusion_pack)
target_link_libraries(illusion_pack PRIVATE illusion::assets)
illusion_configure_target(illusion_pack)
