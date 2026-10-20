# The UI gallery. The pages and the offscreen capture are a library shared with the editor UI tests and
# the gallery benchmark.

illusion_add_editor_library(illusion_ui_gallery_pages STATIC
    Gallery.cpp
    HeadlessUi.cpp
    OffscreenUi.cpp)
target_include_directories(illusion_ui_gallery_pages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_ui_gallery_pages PUBLIC
    illusion::editor_ui illusion::editor_strings illusion::assets)
illusion_configure_target(illusion_ui_gallery_pages)

illusion_add_editor_executable(illusion_ui_gallery main.cpp)
target_link_libraries(illusion_ui_gallery PRIVATE illusion_ui_gallery_pages)
add_dependencies(illusion_ui_gallery illusion_editor_data)
illusion_configure_target(illusion_ui_gallery)
