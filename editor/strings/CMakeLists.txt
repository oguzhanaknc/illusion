# illusion::editor_strings — the English text of the editor (editor/data/locales/en.json) as a compiled
# string table with TextKey constants in illusion/editor/EditorStrings.hpp.

illusion_add_editor_library(illusion_editor_strings STATIC)
add_library(illusion::editor_strings ALIAS illusion_editor_strings)
illusion_add_string_table(illusion_editor_strings
    TABLE editor
    SOURCE ${PROJECT_SOURCE_DIR}/editor/data/locales/en.json
    NAMESPACE illusion::editor::strings
    HEADER illusion/editor/EditorStrings.hpp)
illusion_configure_target(illusion_editor_strings)
