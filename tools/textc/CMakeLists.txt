# illusion_textc: generates string table code (illusion_add_string_table) and checks locale files
# (CTest text.locales). It is a build tool, so it is built even without ILLUSION_BUILD_TOOLS.

add_library(illusion_text_tools STATIC TextTools.cpp)
target_include_directories(illusion_text_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(illusion_text_tools PUBLIC illusion::text)
illusion_configure_target(illusion_text_tools)

add_executable(illusion_textc main.cpp)
target_link_libraries(illusion_textc PRIVATE illusion_text_tools)
illusion_configure_target(illusion_textc)
