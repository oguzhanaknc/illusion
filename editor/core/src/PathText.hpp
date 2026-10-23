#pragma once

// UTF-8 text of file system paths for messages (path::string() can fail on Windows for names outside the
// ANSI code page).

#include <filesystem>
#include <string>

namespace illusion::editor::detail
{

inline std::string pathText(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace illusion::editor::detail
