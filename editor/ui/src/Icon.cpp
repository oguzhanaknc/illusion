#include <illusion/editor/ui/Icon.hpp>

#include <algorithm>

namespace illusion::editor::ui
{

std::optional<Icon> findIcon(std::string_view name) noexcept
{
    const std::span<const IconEntry> icons = allIcons();
    const auto it = std::ranges::lower_bound(icons, name, {}, &IconEntry::name);
    if (it == icons.end() || it->name != name)
    {
        return std::nullopt;
    }
    return it->icon;
}

} // namespace illusion::editor::ui
