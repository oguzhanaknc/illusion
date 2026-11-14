# illusion_icon: the application icons of exported games (Windows .ico, PNG, macOS .iconset).

add_executable(illusion_icon main.cpp)
# The name illusion_add_game() uses, in the build tree and in the package (cmake/IllusionSdk.cmake).
add_executable(illusion::icon ALIAS illusion_icon)
target_link_libraries(illusion_icon PRIVATE illusion::assets)
illusion_configure_target(illusion_icon)
