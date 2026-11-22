# illusion_shaderc: compiles the engine's built-in material shaders at build time (engine/render). It is a build
# tool, so it is built even without ILLUSION_BUILD_TOOLS.

add_executable(illusion_shaderc main.cpp)
target_link_libraries(illusion_shaderc PRIVATE illusion::assets)
illusion_configure_target(illusion_shaderc)
