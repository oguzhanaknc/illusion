// The Welcome window (recent projects, a new project from a template, the workspace folder browser), About
// and the third-party licence viewer.

#include "Panels.hpp"

#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <cstring>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

} // namespace

struct Projects::State
{
    std::array<char, 128> name{};
    std::array<char, 256> location{};
    int templateIndex = 0;
    std::vector<std::string> templates;  // read once
    std::string folder;                  // workspace-relative folder shown by the browser ("" for the root)
    std::vector<std::string> subfolders; // of folder
    bool listed = false;
    u64 recentVersion = 0;
    std::vector<std::string> recentRelative; // workspace-relative forms of the recent projects
    std::vector<std::string> licenses;       // file names in editor:/licenses, read once
    usize license = 0;
    std::string licenseText;
    bool licenseLoaded = false;

    void listFolder(const EditorContext& editor)
    {
        subfolders.clear();
        listed = true;
        const std::filesystem::path& workspace = editor.options().workspace;
        if (workspace.empty())
        {
            return;
        }
        VirtualFileSystem files;
        if (files.mount("workspace", workspace, MountAccess::ReadOnly) != FileError::None)
        {
            return;
        }
        std::vector<DirectoryEntry> entries;
        if (files.list("workspace:/" + folder, entries) != FileError::None)
        {
            return;
        }
        for (const DirectoryEntry& entry : entries)
        {
            if (entry.isDirectory && !entry.name.starts_with('.'))
            {
                subfolders.push_back(entry.name);
            }
        }
        std::sort(subfolders.begin(), subfolders.end());
    }

    void drawBrowser(Impl& shell)
    {
        ui::FrameText& text = shell.text;
        if (!listed)
        {
            listFolder(shell.editor);
        }
        if (ImGui::Button(text.iconLabel(ui::icons::kArrowUpward, s::kWelcomeUp, "welcome.browser.up")) &&
            !folder.empty())
        {
            const usize slash = folder.rfind('/');
            folder = slash == std::string::npos ? std::string() : folder.substr(0, slash);
            listed = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
                            text.copy(folder.empty() ? std::string_view("/") : std::string_view(folder)));
        if (ImGui::BeginChild(text.hidden("welcome.browser"), ImVec2(0.0f, 180.0f * shell.scale),
                              ImGuiChildFlags_Borders))
        {
            for (const std::string& subfolder : subfolders)
            {
                const char* label =
                    text.verbatim(text.concat(ui::icons::kFolder.c_str(), text.concat(" ", subfolder)),
                                  text.concat("welcome.browser.", subfolder));
                if (ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    folder = folder.empty() ? subfolder : folder + "/" + subfolder;
                    listed = false;
                    break;
                }
            }
        }
        ImGui::EndChild();
        ImGui::BeginDisabled(folder.empty());
        if (ImGui::Button(
                text.iconLabel(ui::icons::kFolderOpen, s::kWelcomeOpenFolder, "welcome.browser.open")))
        {
            if (shell.run("project.open", "{\"directory\":" + jsonQuote(folder) + "}", "welcome"))
            {
                shell.setOpen(Panel::Welcome, false);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(text.label(s::kWelcomeBrowse, "welcome.browser.use")))
        {
            const usize length = std::min(folder.size(), location.size() - 1);
            std::memcpy(location.data(), folder.data(), length);
            location[length] = '\0';
        }
    }
};

Projects::Projects()
    : m_state(std::make_unique<State>())
{
}

Projects::~Projects() = default;

void Projects::drawWelcome(Impl& shell)
{
    State& state = *m_state;
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    {
        const ScopedFont font(shell.options.fonts, ui::FontRole::Title);
        ImGui::TextUnformatted(text.format(s::kWelcomeTitle));
    }
    ImGui::Separator();
    if (!ImGui::BeginTable(text.hidden("welcome.columns"), 2, ImGuiTableFlags_SizingStretchSame))
    {
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ui::sectionHeader(kit, text.format(s::kWelcomeRecent));
    const std::vector<std::string>& recent = shell.editor.preferences().recentProjects;
    if (state.recentVersion != shell.editor.preferencesVersion())
    {
        state.recentVersion = shell.editor.preferencesVersion();
        state.recentRelative.clear();
        for (const std::string& project : recent)
        {
            state.recentRelative.push_back(workspaceRelative(shell.editor, project));
        }
    }
    if (recent.empty())
    {
        ImGui::TextDisabled("%s", text.format(s::kWelcomeNoRecent));
    }
    for (usize i = 0; i < recent.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        const std::string& relative = state.recentRelative[i];
        char id[32];
        std::snprintf(id, sizeof(id), "welcome.recent.%zu", i);
        ImGui::BeginDisabled(relative.empty());
        if (ImGui::Selectable(text.verbatim(recent[i], id)))
        {
            if (shell.run("project.open", "{\"directory\":" + jsonQuote(relative) + "}", "welcome"))
            {
                shell.setOpen(Panel::Welcome, false);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::iconButton(kit, ui::icons::kClose, text.concat(id, ".remove"),
                           text.format(s::kWelcomeRemove)))
        {
            std::string patch = "{\"recentProjects\":[";
            for (usize j = 0; j < recent.size(); ++j)
            {
                if (j != i)
                {
                    patch += patch.back() == '[' ? "" : ",";
                    patch += jsonQuote(recent[j]);
                }
            }
            patch += "]}";
            shell.setPreferences(patch);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::TableNextColumn();
    ui::sectionHeader(kit, text.format(s::kWelcomeNew));
    if (state.templates.empty() && shell.options.editorData != nullptr)
    {
        std::vector<DirectoryEntry> entries;
        if (shell.options.editorData->list("editor:/templates", entries) == FileError::None)
        {
            for (const DirectoryEntry& entry : entries)
            {
                if (entry.isDirectory)
                {
                    state.templates.push_back(entry.name);
                }
            }
            std::sort(state.templates.begin(), state.templates.end());
        }
    }
    const usize choice = static_cast<usize>(state.templateIndex);
    if (ui::beginPropertyTable(kit, "welcome.new"))
    {
        ui::propertyLabel(kit, text.format(s::kWelcomeName));
        ImGui::InputText(text.hidden("welcome.name"), state.name.data(), state.name.size());
        ui::propertyLabel(kit, text.format(s::kWelcomeLocation));
        ImGui::InputText(text.hidden("welcome.location"), state.location.data(), state.location.size());
        ui::propertyLabel(kit, text.format(s::kWelcomeTemplate));
        if (ImGui::BeginCombo(text.hidden("welcome.template"),
                              choice < state.templates.size() ? text.copy(state.templates[choice]) : ""))
        {
            for (usize i = 0; i < state.templates.size(); ++i)
            {
                if (ImGui::Selectable(text.verbatim(state.templates[i],
                                                    text.concat("welcome.template.", state.templates[i])),
                                      i == choice))
                {
                    state.templateIndex = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }
        ui::endPropertyTable();
    }
    const std::string_view name(state.name.data());
    ImGui::BeginDisabled(name.empty());
    if (ui::primaryButton(kit, text.label(s::kWelcomeCreate, "welcome.create")))
    {
        const std::string_view location(state.location.data());
        std::string directory =
            location.empty() ? std::string(name) : std::string(location) + "/" + std::string(name);
        std::string params = "{\"directory\":" + jsonQuote(directory) + ",\"name\":" + jsonQuote(name);
        if (choice < state.templates.size())
        {
            params += ",\"template\":" + jsonQuote(state.templates[choice]);
        }
        params += "}";
        if (shell.run("project.create", params, "welcome"))
        {
            shell.setOpen(Panel::Welcome, false);
        }
    }
    ImGui::EndDisabled();
    ui::sectionHeader(kit, text.format(s::kWelcomeOpen));
    state.drawBrowser(shell);
    ImGui::EndTable();
}

void Projects::drawAbout(Impl& shell)
{
    ui::FrameText& text = shell.text;
    {
        const ScopedFont font(shell.options.fonts, ui::FontRole::Title);
        ImGui::TextUnformatted("illusion");
    }
    ImGui::TextUnformatted(text.format(s::kAboutVersion, {{"version", shell.options.version}}));
    ImGui::TextDisabled("%s", text.format(s::kAboutTagline));
    ImGui::Separator();
    if (ImGui::Button(text.iconLabel(ui::icons::kDescription, s::kWindowLicenses, "about.licenses")))
    {
        (void)shell.run("ui.show_panel", R"({"panel":"window.licenses"})", "about");
    }
}

void Projects::drawLicenses(Impl& shell)
{
    State& state = *m_state;
    ui::FrameText& text = shell.text;
    if (state.licenses.empty() && shell.options.editorData != nullptr)
    {
        std::vector<DirectoryEntry> entries;
        if (shell.options.editorData->list("editor:/licenses", entries) == FileError::None)
        {
            for (const DirectoryEntry& entry : entries)
            {
                if (!entry.isDirectory)
                {
                    state.licenses.push_back(entry.name);
                }
            }
            std::sort(state.licenses.begin(), state.licenses.end());
        }
    }
    if (ImGui::BeginChild(text.hidden("licenses.files"), ImVec2(220.0f * shell.scale, 0.0f)))
    {
        for (usize i = 0; i < state.licenses.size(); ++i)
        {
            if (ImGui::Selectable(
                    text.verbatim(state.licenses[i], text.concat("licenses.file.", state.licenses[i])),
                    i == state.license))
            {
                state.license = i;
                state.licenseLoaded = false;
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (!state.licenseLoaded && state.license < state.licenses.size() && shell.options.editorData != nullptr)
    {
        state.licenseLoaded = true;
        state.licenseText.clear();
        (void)shell.options.editorData->readText("editor:/licenses/" + state.licenses[state.license],
                                                 state.licenseText);
    }
    if (ImGui::BeginChild(text.hidden("licenses.text"), ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        const ScopedFont font(shell.options.fonts, ui::FontRole::Mono);
        ImGui::TextUnformatted(state.licenseText.data(), state.licenseText.data() + state.licenseText.size());
    }
    ImGui::EndChild();
}

} // namespace illusion::editor::shell::detail
