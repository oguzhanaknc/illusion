#include "Panels.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/editor/ui/EditorData.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/ui/UiPlatformBackend.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/text/LocaleId.hpp>
#include <illusion/text/Localization.hpp>

#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>

namespace illusion::editor::shell
{
namespace
{

namespace s = strings;
using detail::Panel;

constexpr std::string_view kSettingsType = "IllusionShell";
constexpr std::string_view kCurrentLayout = "current";
constexpr f32 kToolbarHeight = 40.0f;
constexpr f32 kStatusHeight = 24.0f;

// The value of the string member @p key of the flat JSON object @p json ("" if missing).
std::string_view stringParam(std::string_view json, std::string_view key) noexcept
{
    for (usize at = json.find(key); at != std::string_view::npos; at = json.find(key, at + 1))
    {
        if (at == 0 || json[at - 1] != '"' || at + key.size() + 2 >= json.size() ||
            json[at + key.size()] != '"')
        {
            continue;
        }
        usize start = json.find('"', at + key.size() + 1);
        if (start == std::string_view::npos)
        {
            return {};
        }
        ++start;
        const usize end = json.find('"', start);
        return end == std::string_view::npos ? std::string_view() : json.substr(start, end - start);
    }
    return {};
}

text::TextKey menuTitle(UiMenu menu) noexcept
{
    switch (menu)
    {
    case UiMenu::File:
        return s::kMenuFile;
    case UiMenu::Edit:
        return s::kMenuEdit;
    case UiMenu::View:
        return s::kMenuView;
    case UiMenu::Entity:
        return s::kMenuEntity;
    case UiMenu::Assets:
        return s::kMenuAssets;
    case UiMenu::Play:
        return s::kMenuPlay;
    case UiMenu::Modules:
        return s::kMenuModules;
    case UiMenu::Tools:
        return s::kMenuTools;
    case UiMenu::Window:
        return s::kMenuWindow;
    case UiMenu::Help:
        return s::kMenuHelp;
    case UiMenu::None:
        break;
    }
    return s::kMenuFile;
}

ImGuiKeyChord imguiChord(Chord chord) noexcept
{
    ImGuiKeyChord out = ui::imguiKey(chord.key);
    out |= (chord.modifiers & kChordCtrl) != 0 ? ImGuiMod_Ctrl : 0;
    out |= (chord.modifiers & kChordShift) != 0 ? ImGuiMod_Shift : 0;
    out |= (chord.modifiers & kChordAlt) != 0 ? ImGuiMod_Alt : 0;
    out |= (chord.modifiers & kChordSuper) != 0 ? ImGuiMod_Super : 0;
    return out;
}

ui::ThemeToken noticeToken(UiNotice notice) noexcept
{
    switch (notice)
    {
    case UiNotice::Success:
        return ui::ThemeToken::Success;
    case UiNotice::Warning:
        return ui::ThemeToken::Warning;
    case UiNotice::Error:
        return ui::ThemeToken::Error;
    case UiNotice::Info:
        break;
    }
    return ui::ThemeToken::Info;
}

ui::Icon noticeIcon(UiNotice notice) noexcept
{
    switch (notice)
    {
    case UiNotice::Success:
        return ui::icons::kCheckCircle;
    case UiNotice::Warning:
        return ui::icons::kWarning;
    case UiNotice::Error:
        return ui::icons::kError;
    case UiNotice::Info:
        break;
    }
    return ui::icons::kInfo;
}

ui::AccentPreset accentPreset(std::string_view name) noexcept
{
    if (name == "violet")
    {
        return ui::AccentPreset::Violet;
    }
    if (name == "teal")
    {
        return ui::AccentPreset::Teal;
    }
    if (name == "amber")
    {
        return ui::AccentPreset::Amber;
    }
    return ui::AccentPreset::Azure;
}

bool validLayoutFile(std::string_view name) noexcept
{
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
               c == '_';
    });
}

// The panels' open state is saved in the ini text of a layout as "[IllusionShell][Panels]".
void* readOpen(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name)
{
    if (std::strcmp(name, "Panels") != 0)
    {
        return nullptr;
    }
    // The layout decides which docked panels show; windows keep their state.
    auto* impl = static_cast<EditorShell::Impl*>(handler->UserData);
    for (usize i = 0; i < detail::kPanelCount; ++i)
    {
        if (detail::panelInfo(static_cast<Panel>(i)).docked)
        {
            impl->open[i] = false;
        }
    }
    return impl;
}

void readLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
{
    auto* impl = static_cast<EditorShell::Impl*>(entry);
    const std::string_view text(line);
    const usize equals = text.find('=');
    if (equals == std::string_view::npos)
    {
        return;
    }
    if (const std::optional<Panel> panel = detail::findPanel(text.substr(0, equals)))
    {
        impl->open[static_cast<usize>(*panel)] = text.substr(equals + 1) == "1";
    }
}

void writeAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out)
{
    const auto* impl = static_cast<const EditorShell::Impl*>(handler->UserData);
    out->appendf("[%s][Panels]\n", kSettingsType.data());
    for (usize i = 0; i < detail::kPanelCount; ++i)
    {
        const detail::PanelInfo& info = detail::panelInfo(static_cast<Panel>(i));
        if (info.docked)
        {
            out->appendf("%.*s=%d\n", static_cast<int>(info.id.size()), info.id.data(),
                         impl->open[i] ? 1 : 0);
        }
    }
    out->append("\n");
}

} // namespace

namespace detail
{

std::string jsonQuote(std::string_view text)
{
    JsonMutDocument doc;
    doc.setRoot(doc.makeString(text));
    std::string out;
    return doc.write(out, JsonWriteOptions{.pretty = false}) ? out : std::string("\"\"");
}

std::string workspaceRelative(const EditorContext& editor, std::string_view path)
{
    const std::filesystem::path& workspace = editor.options().workspace;
    if (workspace.empty())
    {
        return {};
    }
    std::error_code code;
    const std::filesystem::path relative = std::filesystem::relative(utf8Path(path), workspace, code);
    const std::u8string text = relative.generic_u8string();
    std::string out(reinterpret_cast<const char*>(text.data()), text.size());
    if (code || out.empty() || out == "." || out.starts_with("..") || relative.is_absolute())
    {
        return {};
    }
    return out;
}

} // namespace detail

EditorShell::Impl::Impl(EditorShell& owner, EditorContext& editorContext, ui::UiContext& uiContext,
                        ShellOptions shellOptions)
    : shell(owner)
    , editor(editorContext)
    , context(uiContext)
    , options(std::move(shellOptions))
    , text(editorContext.localization())
    , palette(std::make_unique<detail::Palette>())
    , outliner(std::make_unique<detail::Outliner>())
    , inspector(std::make_unique<detail::Inspector>())
    , sceneView(std::make_unique<detail::SceneView>("scene"))
    , gameView(std::make_unique<detail::SceneView>("game"))
    , console(std::make_unique<detail::Console>())
    , history(std::make_unique<detail::History>())
    , agentsPanel(std::make_unique<detail::Agents>())
    , preferences(std::make_unique<detail::PreferencesPage>())
    , projects(std::make_unique<detail::Projects>())
{
    context.makeCurrent();
    if (options.editorData != nullptr)
    {
        (void)ui::loadEditorLocales(*options.editorData, editor.localization());
    }
    ImGuiSettingsHandler handler;
    handler.TypeName = kSettingsType.data();
    handler.TypeHash = ImHashStr(kSettingsType.data());
    handler.ReadOpenFn = readOpen;
    handler.ReadLineFn = readLine;
    handler.WriteAllFn = writeAll;
    handler.UserData = this;
    ImGui::AddSettingsHandler(&handler);

    loadSavedLayouts();
    const auto current = savedLayouts.find(kCurrentLayout);
    const std::string& last = editor.preferences().layout;
    if (current != savedLayouts.end())
    {
        userArrangement = true;
        pendingIni = current->second;
        savedLayouts.erase(current);
        activeLayout = last;
    }
    else
    {
        const bool builtIn = std::find(detail::kBuiltInLayouts.begin(), detail::kBuiltInLayouts.end(),
                                       last) != detail::kBuiltInLayouts.end();
        activeLayout = builtIn ? last : std::string("default");
        pendingBuild = activeLayout;
        open = detail::builtInLayoutPanels(activeLayout);
    }
    // Without a project the Welcome window comes first.
    open[static_cast<usize>(Panel::Welcome)] = editor.project() == nullptr;
    applyPreferences();
}

EditorShell::Impl::~Impl()
{
    context.makeCurrent();
    ImGui::RemoveSettingsHandler(kSettingsType.data());
}

ui::UiKit EditorShell::Impl::kit()
{
    ui::UiKit out(theme, options.fonts, text);
    out.scale = scale;
    out.reduceMotion = editor.preferences().reduceMotion;
    out.colorBlindAxes = editor.preferences().colorBlindAxes;
    out.strings.clearSearch = text.format(s::kWidgetClearSearch);
    out.strings.moreOptions = text.format(s::kWidgetMoreOptions);
    out.strings.enabled = text.format(s::kWidgetEnabled);
    out.strings.intensity = text.format(s::kWidgetIntensity);
    return out;
}

std::optional<std::string> EditorShell::Impl::run(std::string_view command, std::string_view params,
                                                  std::string_view origin, std::string_view asOrigin,
                                                  std::string_view mergeKey)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(params.empty() ? "{}" : params);
    if (!parsed)
    {
        failed(command, CommandError::invalid("", "the params are not valid JSON"));
        return std::nullopt;
    }
    CommandOptions commandOptions;
    commandOptions.origin = asOrigin.empty() ? origin : asOrigin;
    commandOptions.mergeKey = mergeKey;
    const CommandDescriptor* descriptor = editor.commands().find(command);
    if (descriptor != nullptr && descriptor->info.flags.async)
    {
        Expected<std::unique_ptr<RunningCommand>, CommandError> started =
            editor.startJson(command, parsed->root(), commandOptions);
        if (!started)
        {
            failed(command, started.error());
            return std::nullopt;
        }
        running.push_back(std::move(started).value());
        return std::string("{}");
    }
    Expected<std::string, CommandError> result = editor.executeJson(command, parsed->root(), commandOptions);
    if (!result)
    {
        failed(command, result.error());
        return std::nullopt;
    }
    return std::move(result).value();
}

void EditorShell::Impl::runAction(usize index, std::string_view origin)
{
    const UiAction& action = uiActions()[index];
    if (action.id == "entity.rename" && open[static_cast<usize>(detail::Panel::Outliner)] &&
        editor.selection().size() == 1)
    {
        outliner->beginRename(editor.selection().primary());
        return;
    }
    const CommandDescriptor* descriptor = editor.commands().find(action.command);
    if (descriptor == nullptr)
    {
        return;
    }
    // Commands whose required params the action leaves open ask for them in the palette.
    if (descriptor->params != nullptr)
    {
        Expected<JsonDocument, JsonError> given =
            JsonDocument::parse(action.params.empty() ? "{}" : action.params);
        for (const FieldInfo& field : descriptor->params->fields)
        {
            if (field.type != FieldType::Optional && given && given->root().find(field.name).isAbsent())
            {
                palette->openForm(*this, action.command, action.params, origin);
                return;
            }
        }
    }
    (void)run(action.command, action.params, origin);
}

text::TextKey EditorShell::Impl::actionTitle(usize index) const
{
    const UiAction& action = uiActions()[index];
    if (action.title.table != nullptr)
    {
        return action.title;
    }
    const CommandDescriptor* descriptor = editor.commands().find(action.command);
    return descriptor != nullptr ? descriptor->info.title : s::kMenuFile;
}

ui::Icon EditorShell::Impl::actionIcon(usize index) const
{
    const UiAction& action = uiActions()[index];
    std::string_view name = action.icon;
    if (name.empty())
    {
        const CommandDescriptor* descriptor = editor.commands().find(action.command);
        name = descriptor != nullptr ? descriptor->info.icon : std::string_view();
    }
    return ui::findIcon(name).value_or(ui::icons::kCircle);
}

bool EditorShell::Impl::actionEnabled(usize index) const
{
    const UiAction& action = uiActions()[index];
    return action.milestone.empty() && editor.commands().find(action.command) != nullptr;
}

void EditorShell::Impl::toast(UiNotice level, std::string_view message, std::string_view action, u32 actionId)
{
    toasts.push(noticeIcon(level), noticeToken(level), message, now, level == UiNotice::Error ? 8.0 : 5.0,
                action, actionId);
    if (notices.size() == kMaxNotices)
    {
        notices.pop_front();
    }
    detail::Notice& notice = notices.emplace_back();
    notice.level = level;
    notice.time = now;
    const usize length = std::min(message.size(), notice.text.size() - 1);
    std::memcpy(notice.text.data(), message.data(), length);
    notice.text[length] = '\0';
}

void EditorShell::Impl::failed(std::string_view command, const CommandError& error)
{
    ILL_LOG_WARN("Editor", "{} failed: {}", command, error.message);
    const CommandDescriptor* descriptor = editor.commands().find(command);
    const char* title = descriptor != nullptr ? text.format(descriptor->info.title) : text.copy(command);
    toast(UiNotice::Error,
          text.format(s::kToastFailed, {{"title", title}, {"message", error.message.c_str()}}));
}

void EditorShell::Impl::setOpen(Panel panel, bool value)
{
    open[static_cast<usize>(panel)] = value;
    focus[static_cast<usize>(panel)] = value;
}

void EditorShell::Impl::setPreferences(std::string_view patch)
{
    (void)run("preferences.set", std::string("{\"value\":") + std::string(patch) + "}", "preferences");
}

void EditorShell::Impl::loadSavedLayouts()
{
    savedLayouts.clear();
    std::error_code code;
    const std::filesystem::path directory = options.userDirectory / "layouts";
    if (options.userDirectory.empty() || !std::filesystem::is_directory(directory, code))
    {
        return;
    }
    VirtualFileSystem files;
    if (files.mount("layouts", directory, MountAccess::ReadOnly) != FileError::None)
    {
        return;
    }
    std::vector<DirectoryEntry> entries;
    if (files.list("layouts:/", entries) != FileError::None)
    {
        return;
    }
    for (const DirectoryEntry& entry : entries)
    {
        if (entry.isDirectory || !entry.name.ends_with(".ini"))
        {
            continue;
        }
        const std::string name = entry.name.substr(0, entry.name.size() - 4);
        std::string ini;
        if (validLayoutFile(name) && files.readText("layouts:/" + entry.name, ini) == FileError::None)
        {
            savedLayouts[name] = std::move(ini);
        }
    }
}

std::string EditorShell::Impl::currentIni() const
{
    context.makeCurrent();
    size_t size = 0;
    const char* ini = ImGui::SaveIniSettingsToMemory(&size);
    return std::string(ini, size);
}

void EditorShell::Impl::applyPreferences()
{
    if (preferencesVersion == editor.preferencesVersion())
    {
        return;
    }
    preferencesVersion = editor.preferencesVersion();
    const Preferences& prefs = editor.preferences();
    if (options.editorData != nullptr)
    {
        if (Expected<ui::Theme, std::string> loaded = ui::loadTheme(*options.editorData, prefs.theme))
        {
            baseTheme = std::move(loaded).value();
        }
        else
        {
            ILL_LOG_WARN("Editor", "{}", loaded.error());
        }
    }
    theme = baseTheme.withAccent(accentPreset(prefs.accent));
    scale = prefs.uiScale * contentScale;
    if (options.fonts != nullptr)
    {
        options.fonts->setScale(prefs.uiScale, contentScale);
    }
    context.makeCurrent();
    theme.apply(ImGui::GetStyle(), scale);
    if (const std::optional<text::LocaleId> locale = text::LocaleId::parse(prefs.locale);
        locale && editor.localization().locale() != *locale)
    {
        editor.localization().setLocale(*locale);
        editor.localization().applyPendingLocale();
    }
    keymap = Keymap::build(prefs.keymap, prefs.shortcuts, &editor.commands());
    if (agents != nullptr && options.agentPermissions)
    {
        if (const std::optional<rpc::PermissionMode> mode = rpc::parsePermissionMode(prefs.agentPermissions))
        {
            agents->setPermissions(*mode);
        }
    }
}

EditorShell::EditorShell(EditorContext& editor, ui::UiContext& context, ShellOptions options)
    : m_impl(std::make_unique<Impl>(*this, editor, context, std::move(options)))
{
    editor.setUi(this);
}

EditorShell::~EditorShell()
{
    Impl& impl = *m_impl;
    if (impl.editor.ui() == this)
    {
        impl.editor.setUi(nullptr);
    }
    if (!impl.options.userDirectory.empty())
    {
        // The arrangement comes back at the next start.
        std::error_code code;
        const std::filesystem::path directory = impl.options.userDirectory / "layouts";
        std::filesystem::create_directories(directory, code);
        VirtualFileSystem files;
        if (files.mount("layouts", directory, MountAccess::ReadWrite) == FileError::None)
        {
            (void)files.writeText("layouts:/current.ini", impl.currentIni());
        }
    }
}

void EditorShell::setAgents(rpc::RpcServer* server) noexcept
{
    m_impl->agents = server;
    if (server != nullptr && m_impl->options.agentPermissions)
    {
        if (const std::optional<rpc::PermissionMode> mode =
                rpc::parsePermissionMode(m_impl->editor.preferences().agentPermissions))
        {
            server->setPermissions(*mode);
        }
    }
}

void EditorShell::beginFrame(f32 contentScale)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.context.makeCurrent();
    if (contentScale != impl.contentScale)
    {
        impl.contentScale = contentScale;
        impl.preferencesVersion = 0;
    }
    impl.applyPreferences();
    // A project can ship a default arrangement, used when the user has none of their own.
    if (impl.editor.project() != impl.lastProject)
    {
        impl.lastProject = impl.editor.project();
        std::string shipped;
        if (impl.lastProject != nullptr && !impl.userArrangement &&
            impl.editor.files().readText("settings:/editor_layout.ini", shipped) == FileError::None)
        {
            impl.pendingIni = std::move(shipped);
        }
    }
    if (impl.pendingIni)
    {
        ImGui::LoadIniSettingsFromMemory(impl.pendingIni->data(), impl.pendingIni->size());
        impl.pendingIni.reset();
    }
}

void EditorShell::draw(f64 now, f64 deltaSeconds)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.text.reset();
    impl.now = now;
    impl.frameMs = deltaSeconds * 1000.0;
    ui::UiKit kit = impl.kit();
    ui::FrameText& text = impl.text;
    const std::span<const UiAction> actions = uiActions();

    // Async commands the UI started.
    for (auto it = impl.running.begin(); it != impl.running.end();)
    {
        Expected<std::optional<std::string>, CommandError> updated = (*it)->update();
        if (updated && !updated->has_value())
        {
            ++it;
            continue;
        }
        if (!updated)
        {
            impl.failed((*it)->command().info.name, updated.error());
        }
        it = impl.running.erase(it);
    }
    impl.console->update(impl);
    impl.agentsPanel->update(impl);
    detail::updateEyedropper(impl);
    detail::followAgent(impl);

    // Shortcuts, unless a text field or the shortcut capture has the keyboard.
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !impl.preferences->capturing() && !impl.palette->isOpen() &&
        !impl.viewportNavigating)
    {
        for (usize i = 0; i < actions.size(); ++i)
        {
            const Chord chord = impl.keymap.chord(i);
            if (chord.valid() && impl.actionEnabled(i) && ImGui::IsKeyChordPressed(imguiChord(chord)))
            {
                impl.runAction(i, "shortcut");
                break;
            }
        }
    }

    // Menu bar: every menu lists its actions; disabled ones arrive in a later milestone.
    if (ImGui::BeginMainMenuBar())
    {
        for (usize m = 1; m <= kUiMenuCount; ++m)
        {
            const UiMenu menu = static_cast<UiMenu>(m);
            if (!ImGui::BeginMenu(text.label(menuTitle(menu), text.concat("menu.", uiMenuName(menu)))))
            {
                continue;
            }
            std::optional<u8> group;
            std::string_view submenu;
            bool submenuOpen = false;
            const auto closeSubmenu = [&] {
                if (submenuOpen && submenu == "layouts")
                {
                    // Saved layouts follow the built-in ones.
                    for (const auto& [name, ini] : impl.savedLayouts)
                    {
                        if (ImGui::MenuItem(text.verbatim(name, text.concat("menu.window.layout.", name)),
                                            nullptr, impl.activeLayout == name))
                        {
                            (void)impl.run("ui.layout_apply", "{\"name\":" + detail::jsonQuote(name) + "}",
                                           "menu");
                        }
                    }
                }
                if (submenuOpen)
                {
                    ImGui::EndMenu();
                }
                submenuOpen = false;
            };
            for (usize i = 0; i < actions.size(); ++i)
            {
                const UiAction& action = actions[i];
                if (action.menu != menu)
                {
                    continue;
                }
                if (action.submenu != submenu)
                {
                    closeSubmenu();
                    submenu = action.submenu;
                    if (!submenu.empty() && group)
                    {
                        ImGui::Separator();
                    }
                    group.reset();
                    if (!submenu.empty())
                    {
                        submenuOpen = ImGui::BeginMenu(
                            text.label(s::kMenuLayouts,
                                       text.concat("menu.", text.concat(uiMenuName(menu), ".layouts"))));
                    }
                }
                if (!submenu.empty() && !submenuOpen)
                {
                    continue;
                }
                if (group && *group != action.group)
                {
                    ImGui::Separator();
                }
                group = action.group;
                const bool enabled = impl.actionEnabled(i);
                bool selected = false;
                std::optional<Panel> panel;
                if (action.command == "ui.show_panel")
                {
                    panel = detail::findPanel(stringParam(action.params, "panel"));
                    selected = panel && impl.open[static_cast<usize>(*panel)];
                }
                else if (action.command == "ui.layout_apply")
                {
                    selected = impl.activeLayout == stringParam(action.params, "name");
                }
                const std::string_view chord = impl.keymap.text(i);
                const char* label =
                    text.iconLabel(impl.actionIcon(i), impl.actionTitle(i), text.concat("menu.", action.id));
                if (ImGui::MenuItem(label, chord.empty() ? nullptr : text.copy(chord), selected, enabled))
                {
                    if (panel && selected)
                    {
                        (void)impl.run("ui.show_panel",
                                       "{\"panel\":" + detail::jsonQuote(detail::panelInfo(*panel).id) +
                                           ",\"open\":false}",
                                       "menu");
                    }
                    else
                    {
                        impl.runAction(i, "menu");
                    }
                }
                if (!enabled && !action.milestone.empty() &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("%s",
                                      text.format(s::kPaletteDisabled, {{"milestone", action.milestone}}));
                }
            }
            closeSubmenu();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr ImGuiWindowFlags kBarFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;

    const u64 agentCount = impl.agentsPanel->clientCount();
    const bool waiting = impl.agents != nullptr && !impl.agents->approvals().empty();

    // Toolbar: tools on the left, play controls in the centre, layout, agents, notifications and search on
    // the right.
    if (ImGui::BeginViewportSideBar("###shell.toolbar", viewport, ImGuiDir_Up, kToolbarHeight * kit.scale,
                                    kBarFlags))
    {
        const auto button = [&](std::string_view id, bool selected = false) {
            const UiAction* action = findUiAction(id);
            const usize index = static_cast<usize>(action - actions.data());
            const bool enabled = impl.actionEnabled(index);
            ImGui::BeginDisabled(!enabled);
            const std::string_view chord = impl.keymap.text(index);
            if (ui::iconButton(kit, impl.actionIcon(index), text.concat("toolbar.", id),
                               text.format(impl.actionTitle(index)),
                               chord.empty() ? nullptr : text.copy(chord), selected))
            {
                impl.runAction(index, "toolbar");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
        };
        ImGui::SetCursorPosY((ImGui::GetWindowHeight() - ImGui::GetFrameHeight()) * 0.5f);
        constexpr std::array<std::pair<std::string_view, GizmoTool>, 5> kTools = {
            std::pair{"view.tool_select", GizmoTool::Select},
            std::pair{"view.tool_move", GizmoTool::Translate},
            std::pair{"view.tool_rotate", GizmoTool::Rotate}, std::pair{"view.tool_scale", GizmoTool::Scale},
            std::pair{"view.tool_universal", GizmoTool::Universal}};
        for (const auto& [id, tool] : kTools)
        {
            button(id, impl.editor.gizmo().tool == tool);
        }
        detail::drawGizmoToolbar(impl);
        const f32 width = ImGui::GetWindowWidth();
        const f32 buttonWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (width - 4.0f * buttonWidth) * 0.5f));
        for (const std::string_view id : {"play.play", "play.pause", "play.step", "play.simulate"})
        {
            button(id);
        }
        const f32 comboWidth = 150.0f * kit.scale;
        const f32 chipWidth = 110.0f * kit.scale;
        const f32 right = comboWidth + chipWidth + 2.0f * buttonWidth +
                          3.0f * ImGui::GetStyle().ItemSpacing.x + ImGui::GetStyle().WindowPadding.x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), width - right));
        const text::TextKey activeTitle = detail::builtInLayoutTitle(impl.activeLayout);
        const char* preview =
            activeTitle.table != nullptr ? text.format(activeTitle) : text.copy(impl.activeLayout);
        if (ImGui::Button(text.verbatim(text.concat(ui::icons::kDashboard.c_str(), text.concat(" ", preview)),
                                        "toolbar.layout"),
                          ImVec2(comboWidth, 0.0f)))
        {
            ImGui::OpenPopup("###toolbar.layout.menu");
        }
        const bool layoutHovered = ImGui::IsItemHovered();
        if (ImGui::BeginPopup("###toolbar.layout.menu"))
        {
            for (const std::string_view name : detail::kBuiltInLayouts)
            {
                if (ImGui::Selectable(
                        text.label(detail::builtInLayoutTitle(name), text.concat("toolbar.layout.", name)),
                        impl.activeLayout == name))
                {
                    (void)impl.run("ui.layout_apply", "{\"name\":" + detail::jsonQuote(name) + "}",
                                   "toolbar");
                }
            }
            for (const auto& [name, ini] : impl.savedLayouts)
            {
                if (ImGui::Selectable(text.verbatim(name, text.concat("toolbar.layout.", name)),
                                      impl.activeLayout == name))
                {
                    (void)impl.run("ui.layout_apply", "{\"name\":" + detail::jsonQuote(name) + "}",
                                   "toolbar");
                }
            }
            ImGui::EndPopup();
        }
        if (layoutHovered)
        {
            ImGui::SetTooltip("%s", text.format(s::kToolbarLayout));
        }
        ImGui::SameLine();
        if (waiting)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, detail::imvec(impl.theme.color(ui::ThemeToken::Warning)));
        }
        const text::MessageArgument agentArguments[] = {{"count", agentCount}};
        if (ImGui::Button(
                text.iconLabel(ui::icons::kSmartToy, s::kToolbarAgents, "toolbar.agents", agentArguments),
                ImVec2(chipWidth, 0.0f)))
        {
            (void)impl.run("ui.show_panel", R"({"panel":"panel.ai_activity"})", "toolbar");
        }
        if (waiting)
        {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ui::iconButton(kit, ui::icons::kNotifications, "toolbar.notifications",
                           text.format(s::kToolbarNotifications), nullptr,
                           impl.open[static_cast<usize>(Panel::Notifications)]))
        {
            (void)impl.run("ui.show_panel",
                           impl.open[static_cast<usize>(Panel::Notifications)]
                               ? R"({"panel":"window.notifications","open":false})"
                               : R"({"panel":"window.notifications"})",
                           "toolbar");
        }
        ImGui::SameLine();
        const UiAction* search = findUiAction("edit.search");
        const usize searchIndex = static_cast<usize>(search - actions.data());
        const std::string_view searchChord = impl.keymap.text(searchIndex);
        if (ui::iconButton(kit, ui::icons::kSearch, "toolbar.search", text.format(s::kToolbarSearch),
                           searchChord.empty() ? nullptr : text.copy(searchChord)))
        {
            impl.runAction(searchIndex, "toolbar");
        }
    }
    ImGui::End();

    // Status bar: selection, revision and saved state, frame time, language, agents, play state.
    // The status bar's text is centred in its 24 px.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().WindowPadding.x, 0.0f));
    const bool statusOpen = ImGui::BeginViewportSideBar("###shell.status", viewport, ImGuiDir_Down,
                                                        kStatusHeight * kit.scale, kBarFlags);
    ImGui::PopStyleVar();
    if (statusOpen)
    {
        ImGui::SetCursorPosY((ImGui::GetWindowHeight() - ImGui::GetFrameHeight()) * 0.5f);
        const detail::ScopedFont font(impl.options.fonts, ui::FontRole::Caption);
        ImGui::AlignTextToFramePadding();
        const auto separator = [&] {
            ImGui::SameLine();
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "|");
            ImGui::SameLine();
        };
        ImGui::TextUnformatted(
            text.format(s::kStatusSelection, {{"count", static_cast<u64>(impl.editor.selection().size())}}));
        separator();
        ImGui::TextUnformatted(text.format(s::kStatusRevision, {{"revision", impl.editor.revision()}}));
        ImGui::SameLine();
        if (impl.editor.dirty())
        {
            ImGui::TextColored(detail::imvec(impl.theme.color(ui::ThemeToken::Warning)), "%s",
                               text.format(s::kStatusUnsaved));
        }
        else
        {
            ImGui::TextDisabled("%s", text.format(s::kStatusSaved));
        }
        separator();
        ImGui::TextUnformatted(text.format(s::kStatusFrameTime, {{"ms", impl.frameMs}}));
        separator();
        ImGui::TextUnformatted(text.copy(impl.editor.localization().locale().tag()));
        separator();
        ImGui::TextUnformatted(text.format(s::kStatusAgents, {{"count", static_cast<u64>(agentCount)}}));
        if (waiting)
        {
            ImGui::SameLine();
            ImGui::TextColored(detail::imvec(impl.theme.color(ui::ThemeToken::Warning)), "%s",
                               text.format(s::kStatusWaiting,
                                           {{"count", static_cast<u64>(impl.agents->approvals().size())}}));
        }
        separator();
        ImGui::TextUnformatted(text.format(s::kStatusEditMode));
    }
    ImGui::End();

    // Dock space and panels.
    const ImGuiID dockspace = ImHashStr("shell.dockspace");
    if (impl.pendingBuild)
    {
        detail::buildLayout(*impl.pendingBuild, dockspace, Vec2(viewport->WorkSize.x, viewport->WorkSize.y));
        impl.pendingBuild.reset();
        impl.settleFrames = 2;
    }
    else if (impl.settleFrames > 0)
    {
        --impl.settleFrames;
    }
    ImGui::DockSpaceOverViewport(dockspace, viewport);
    impl.viewportNavigating = false;
    std::swap(impl.lastHoveredSceneView, impl.hoveredSceneView);
    impl.hoveredSceneView.clear();
    for (usize i = 0; i < detail::kPanelCount; ++i)
    {
        const Panel panel = static_cast<Panel>(i);
        const detail::PanelInfo& info = detail::panelInfo(panel);
        if (!impl.open[i])
        {
            continue;
        }
        // Focus requests wait until a new layout has docked its windows.
        if (impl.focus[i] && impl.settleFrames == 0)
        {
            ImGui::SetNextWindowFocus();
            impl.focus[i] = false;
        }
        ImGuiWindowFlags flags = 0;
        if (!info.milestone.empty() && panel != Panel::Scene)
        {
            flags |= ImGuiWindowFlags_NoFocusOnAppearing;
        }
        if (!info.docked)
        {
            const ImVec2 size = panel == Panel::Preferences     ? ImVec2(760.0f, 520.0f)
                                : panel == Panel::Notifications ? ImVec2(380.0f, 420.0f)
                                                                : ImVec2(640.0f, 460.0f);
            ImGui::SetNextWindowSize(ImVec2(size.x * kit.scale, size.y * kit.scale), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            flags |= ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
        }
        bool keep = true;
        if (ImGui::Begin(
                text.iconLabel(ui::findIcon(info.icon).value_or(ui::icons::kCircle), info.title, info.id),
                &keep, flags))
        {
            switch (panel)
            {
            case Panel::Outliner:
                impl.outliner->draw(impl);
                break;
            case Panel::Inspector:
                impl.inspector->draw(impl);
                break;
            case Panel::Scene:
                impl.sceneView->draw(impl);
                break;
            case Panel::Game:
                impl.gameView->draw(impl);
                break;
            case Panel::Console:
                impl.console->draw(impl);
                break;
            case Panel::History:
                impl.history->draw(impl);
                break;
            case Panel::AiActivity:
                impl.agentsPanel->draw(impl);
                break;
            case Panel::Preferences:
                impl.preferences->draw(impl);
                break;
            case Panel::Welcome:
                impl.projects->drawWelcome(impl);
                break;
            case Panel::About:
                impl.projects->drawAbout(impl);
                break;
            case Panel::Licenses:
                impl.projects->drawLicenses(impl);
                break;
            case Panel::Notifications:
            {
                if (ImGui::Button(text.label(s::kNotificationsClear, "notifications.clear")))
                {
                    impl.notices.clear();
                }
                ImGui::Separator();
                if (impl.notices.empty())
                {
                    ImGui::TextDisabled("%s", text.format(s::kNotificationsEmpty));
                }
                for (auto it = impl.notices.rbegin(); it != impl.notices.rend(); ++it)
                {
                    ImGui::TextColored(detail::imvec(impl.theme.color(noticeToken(it->level))), "%s",
                                       noticeIcon(it->level).c_str());
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", it->text.data());
                }
                break;
            }
            default:
                ui::emptyState(kit, ui::findIcon(info.icon).value_or(ui::icons::kCircle),
                               text.format(info.title),
                               text.format(s::kPlaceholderHint, {{"milestone", info.milestone}}));
                break;
            }
        }
        ImGui::End();
        if (!keep)
        {
            (void)impl.run("ui.show_panel", "{\"panel\":" + detail::jsonQuote(info.id) + ",\"open\":false}",
                           "ui");
        }
    }

    detail::drawSecondaryViewports(impl);
    // A modal transform no Scene view took (its panel is closed) is dropped.
    impl.pendingModal.reset();
    impl.palette->draw(impl);
    impl.agentsPanel->drawPrompt(impl);
    if (const std::optional<u32> clicked = impl.toasts.draw(kit, now);
        clicked && *clicked == detail::kToastShowAgents)
    {
        (void)impl.run("ui.show_panel", R"({"panel":"panel.ai_activity"})", "toast");
    }
}

bool EditorShell::quitRequested() const noexcept
{
    return m_impl->quit;
}

std::optional<ScreenshotRequest> EditorShell::pendingScreenshot() const noexcept
{
    for (const Impl::Capture& capture : m_impl->captures)
    {
        if (!capture.result)
        {
            return ScreenshotRequest{capture.ticket, capture.size};
        }
    }
    return std::nullopt;
}

void EditorShell::completeScreenshot(u64 ticket, Expected<UiScreenshot, CommandError> result)
{
    for (Impl::Capture& capture : m_impl->captures)
    {
        if (capture.ticket == ticket && !capture.result)
        {
            capture.result = std::move(result);
            return;
        }
    }
}

const ui::Theme& EditorShell::theme() const noexcept
{
    return m_impl->theme;
}

ui::FrameText& EditorShell::text() noexcept
{
    return m_impl->text;
}

Vec4 EditorShell::clearColor() const noexcept
{
    return m_impl->theme.color(ui::ThemeToken::BgBase);
}

bool EditorShell::paletteOpen() const noexcept
{
    return m_impl->palette->isOpen();
}

usize EditorShell::toastCount() const noexcept
{
    return m_impl->toasts.size();
}

std::vector<UiPanelInfo> EditorShell::panels() const
{
    std::vector<UiPanelInfo> out;
    for (usize i = 0; i < detail::kPanelCount; ++i)
    {
        const detail::PanelInfo& info = detail::panelInfo(static_cast<Panel>(i));
        out.push_back(
            UiPanelInfo{std::string(info.id), m_impl->text.format(info.title), m_impl->open[i], info.docked});
    }
    return out;
}

Expected<void, CommandError> EditorShell::showPanel(std::string_view id, bool open)
{
    const std::optional<Panel> panel = detail::findPanel(id);
    if (!panel)
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "there is no panel '" + std::string(id) + "'"));
    }
    m_impl->setOpen(*panel, open);
    return {};
}

std::vector<UiLayoutInfo> EditorShell::layouts() const
{
    std::vector<UiLayoutInfo> out;
    for (const std::string_view name : detail::kBuiltInLayouts)
    {
        out.push_back(UiLayoutInfo{std::string(name), true, m_impl->activeLayout == name});
    }
    for (const auto& [name, ini] : m_impl->savedLayouts)
    {
        out.push_back(UiLayoutInfo{name, false, m_impl->activeLayout == name});
    }
    return out;
}

Expected<void, CommandError> EditorShell::applyLayout(std::string_view name)
{
    Impl& impl = *m_impl;
    if (const auto saved = impl.savedLayouts.find(name); saved != impl.savedLayouts.end())
    {
        impl.pendingIni = saved->second;
    }
    else if (std::find(detail::kBuiltInLayouts.begin(), detail::kBuiltInLayouts.end(), name) !=
             detail::kBuiltInLayouts.end())
    {
        impl.pendingBuild = std::string(name);
        impl.open = detail::builtInLayoutPanels(name);
    }
    else
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "there is no layout '" + std::string(name) + "'"));
    }
    impl.activeLayout = std::string(name);
    if (impl.editor.preferences().layout != name)
    {
        Preferences prefs = impl.editor.preferences();
        prefs.layout = std::string(name);
        if (Expected<void, CommandError> saved = impl.editor.setPreferences(std::move(prefs)); !saved)
        {
            return saved;
        }
    }
    return {};
}

Expected<void, CommandError> EditorShell::saveLayout(std::string_view name)
{
    Impl& impl = *m_impl;
    std::string ini = impl.currentIni();
    if (!impl.options.userDirectory.empty())
    {
        std::error_code code;
        const std::filesystem::path directory = impl.options.userDirectory / "layouts";
        std::filesystem::create_directories(directory, code);
        VirtualFileSystem files;
        FileError error = files.mount("layouts", directory, MountAccess::ReadWrite);
        if (error == FileError::None)
        {
            error = files.writeText("layouts:/" + std::string(name) + ".ini", ini);
        }
        if (error != FileError::None)
        {
            return unexpected(CommandError::make(
                ErrorKind::IoError, "cannot save the layout (" + std::string(fileErrorName(error)) + ")"));
        }
    }
    impl.savedLayouts[std::string(name)] = std::move(ini);
    impl.activeLayout = std::string(name);
    return {};
}

Expected<void, CommandError> EditorShell::deleteLayout(std::string_view name)
{
    Impl& impl = *m_impl;
    const auto saved = impl.savedLayouts.find(name);
    if (saved == impl.savedLayouts.end())
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "there is no saved layout '" + std::string(name) + "'"));
    }
    if (!impl.options.userDirectory.empty())
    {
        std::error_code code;
        std::filesystem::remove(impl.options.userDirectory / "layouts" / utf8Path(std::string(name) + ".ini"),
                                code);
        if (code)
        {
            return unexpected(CommandError::make(ErrorKind::IoError, "cannot delete the layout file"));
        }
    }
    impl.savedLayouts.erase(saved);
    if (impl.activeLayout == name)
    {
        impl.activeLayout = "default";
    }
    return {};
}

void EditorShell::resetLayout()
{
    Impl& impl = *m_impl;
    const bool builtIn = std::find(detail::kBuiltInLayouts.begin(), detail::kBuiltInLayouts.end(),
                                   impl.activeLayout) != detail::kBuiltInLayouts.end();
    (void)applyLayout(builtIn ? std::string_view(impl.activeLayout) : std::string_view("default"));
}

Expected<void, CommandError> EditorShell::openPalette(std::string_view mode, std::string_view query)
{
    m_impl->palette->open(mode, query);
    return {};
}

void EditorShell::notify(UiNotice notice, std::string_view message)
{
    m_impl->toast(notice, message);
}

void EditorShell::requestQuit()
{
    m_impl->quit = true;
}

Expected<void, CommandError> EditorShell::beginModalTransform(GizmoModalOperation operation)
{
    Impl& impl = *m_impl;
    if (!impl.open[static_cast<usize>(Panel::Scene)] && impl.secondaryViews.empty())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no Scene view is open"));
    }
    impl.pendingModal = operation;
    return {};
}

Expected<u64, CommandError> EditorShell::requestScreenshot(UVec2 size)
{
    Impl& impl = *m_impl;
    if (!impl.options.canCapture)
    {
        return unexpected(
            CommandError::make(ErrorKind::Unsupported, "this editor cannot capture frames (no GPU)"));
    }
    constexpr usize kMaxPending = 8;
    if (impl.captures.size() >= kMaxPending)
    {
        CommandError error = CommandError::make(ErrorKind::Busy, "too many screenshots are pending");
        error.retryable = true;
        return unexpected(std::move(error));
    }
    const u64 ticket = impl.nextTicket++;
    impl.captures.push_back(Impl::Capture{ticket, size, std::nullopt});
    return ticket;
}

std::optional<Expected<UiScreenshot, CommandError>> EditorShell::screenshot(u64 ticket)
{
    std::vector<Impl::Capture>& captures = m_impl->captures;
    const auto capture = std::find_if(captures.begin(), captures.end(),
                                      [&](const Impl::Capture& entry) { return entry.ticket == ticket; });
    if (capture == captures.end())
    {
        return Expected<UiScreenshot, CommandError>(
            unexpected(CommandError::make(ErrorKind::NotFound, "the screenshot was already taken")));
    }
    if (!capture->result)
    {
        return std::nullopt;
    }
    std::optional<Expected<UiScreenshot, CommandError>> out = std::move(capture->result);
    captures.erase(capture);
    return out;
}

} // namespace illusion::editor::shell
