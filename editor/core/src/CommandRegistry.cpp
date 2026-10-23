#include <illusion/editor/core/CommandRegistry.hpp>

#include <algorithm>

namespace illusion::editor
{

std::string_view commandModeName(CommandMode mode) noexcept
{
    switch (mode)
    {
    case CommandMode::Edit:
        return "edit";
    case CommandMode::Play:
        return "play";
    case CommandMode::Any:
        return "any";
    }
    return "edit";
}

std::string_view permissionClassName(PermissionClass permission) noexcept
{
    switch (permission)
    {
    case PermissionClass::Read:
        return "read";
    case PermissionClass::Edit:
        return "edit";
    case PermissionClass::Destructive:
        return "destructive";
    case PermissionClass::Files:
        return "files";
    case PermissionClass::Build:
        return "build";
    case PermissionClass::System:
        return "system";
    }
    return "read";
}

void CommandRegistry::addDescriptor(const CommandDescriptor& descriptor)
{
    const std::string_view name = descriptor.info.name;
    ILL_ASSERT(!name.empty() && name.find('.') != std::string_view::npos,
               "command names are domain.verb, not '{}'", name);
    ILL_ASSERT(!m_entries.contains(name), "command {} is registered twice", name);
    Entry& entry = m_entries[std::string(name)];
    entry.descriptor = descriptor;
    entry.name = std::string(name);
    entry.summary = std::string(descriptor.info.agentSummary);
    entry.category = std::string(descriptor.info.category);
    entry.icon = std::string(descriptor.info.icon);
    entry.shortcut = std::string(descriptor.info.defaultShortcut);
    entry.descriptor.info.name = entry.name;
    entry.descriptor.info.agentSummary = entry.summary;
    entry.descriptor.info.category = entry.category;
    entry.descriptor.info.icon = entry.icon;
    entry.descriptor.info.defaultShortcut = entry.shortcut;
    m_sorted.clear();
    for (const auto& [key, value] : m_entries)
    {
        m_sorted.push_back(&value.descriptor);
    }
}

const CommandDescriptor* CommandRegistry::find(std::string_view name) const
{
    const auto it = m_entries.find(name);
    return it != m_entries.end() ? &it->second.descriptor : nullptr;
}

std::vector<std::string> CommandRegistry::suggest(std::string_view name) const
{
    std::vector<std::string_view> names;
    names.reserve(m_sorted.size());
    for (const CommandDescriptor* descriptor : m_sorted)
    {
        names.push_back(descriptor->info.name);
    }
    return closestMatches(name, names);
}

} // namespace illusion::editor
