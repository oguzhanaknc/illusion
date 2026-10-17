# illusion_pack: packs a directory into an archive, or cooks its assets into an import-cache archive.

add_executable(illusion_pack main.cpp)
target_link_libraries(illusion_pack PRIVATE illusion::assets)
illusion_configure_target(illusion_pack)
