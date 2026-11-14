// The Export window: the export settings of the project ("illusion.ExportSettings", read and written with
// `project.settings_get` and `project.settings_set`), the target with the build options of `export.build`,
// and the export itself with its stage, progress, result and diagnostics.

#include "Panels.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/editor/core/Export.hpp>
#include <illusion/editor/core/ExportTools.hpp>
#include <illusion/editor/ui/Icons.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr std::string_view kSettingsType = "illusion.ExportSettings";
constexpr std::string_view kOrigin = "export";
constexpr usize kFieldCapacity = 4096;

// A field of ExportSettings.
struct SettingsFieldInfo
{
    std::string_view name; // reflected field name
    std::string_view id;   // stable item id
    text::TextKey label;
    text::TextKey hint{}; // shown while empty; {name} is the project's name
};

constexpr usize kExcludeField = 5;
constexpr std::array<SettingsFieldInfo, 6> kSettingsFields = {{
    {"productName", "export.settings.product_name", s::kExportProductName, s::kExportProductNameHint},
    {"organization", "export.settings.organization", s::kExportOrganization},
    {"identifier", "export.settings.identifier", s::kExportIdentifier, s::kExportIdentifierHint},
    {"copyright", "export.settings.copyright", s::kExportCopyright},
    {"icon", "export.settings.icon", s::kExportIcon, s::kExportIconHint},
    {"exclude", "export.settings.exclude", s::kExportExclude, s::kExportExcludeHint},
}};

// A param of `export.build` typed into the window.
struct BuildFieldInfo
{
    std::string_view param; // params member
    std::string_view id;
    text::TextKey label;
    std::string_view variable; // environment variable used when it is empty ("" for none)
};

enum BuildField : u8
{
    Output,
    Configuration,
    SignCommand,
    CodesignIdentity,
    NotaryProfile,
    BuildFieldCount,
};

constexpr std::array<BuildFieldInfo, BuildFieldCount> kBuildFields = {{
    {"output", "export.output", s::kExportOutput, ""},
    {"configuration", "export.configuration", s::kExportConfiguration, ""},
    {"signCommand", "export.sign_command", s::kExportSignCommand, "ILLUSION_SIGN_COMMAND"},
    {"codesignIdentity", "export.codesign_identity", s::kExportCodesignIdentity,
     "ILLUSION_CODESIGN_IDENTITY"},
    {"notaryProfile", "export.notary_profile", s::kExportNotaryProfile, "ILLUSION_NOTARY_PROFILE"},
}};

struct PackageChoice
{
    std::string_view name; // `package` param of export.build
    text::TextKey label;
};

using editor::detail::ExportPlatform;
constexpr ExportPlatform kHost = editor::detail::kExportPlatform;

constexpr std::array<PackageChoice, 3> kWindowsPackages = {{{"none", s::kExportPackageNone},
                                                            {"archive", s::kExportPackageArchiveZip},
                                                            {"installer", s::kExportPackageInstallerNsis}}};
constexpr std::array<PackageChoice, 3> kMacosPackages = {{{"none", s::kExportPackageNone},
                                                          {"archive", s::kExportPackageArchiveZip},
                                                          {"installer", s::kExportPackageInstallerDmg}}};
// On Linux the installer is the archive, so it is not offered twice.
constexpr std::array<PackageChoice, 2> kLinuxPackages = {
    {{"none", s::kExportPackageNone}, {"archive", s::kExportPackageArchiveTar}}};

constexpr std::span<const PackageChoice> hostPackages() noexcept
{
    if constexpr (kHost == ExportPlatform::Windows)
    {
        return kWindowsPackages;
    }
    else if constexpr (kHost == ExportPlatform::MacOS)
    {
        return kMacosPackages;
    }
    else
    {
        return kLinuxPackages;
    }
}

constexpr text::TextKey hostPlatformTitle() noexcept
{
    if constexpr (kHost == ExportPlatform::Windows)
    {
        return s::kExportPlatformWindows;
    }
    else if constexpr (kHost == ExportPlatform::MacOS)
    {
        return s::kExportPlatformMacos;
    }
    else
    {
        return s::kExportPlatformLinux;
    }
}

constexpr std::array<BuildField, 1> kWindowsSigning{SignCommand};
constexpr std::array<BuildField, 2> kMacosSigning{CodesignIdentity, NotaryProfile};

// The signing fields of this platform.
constexpr std::span<const BuildField> hostSigningFields() noexcept
{
    if constexpr (kHost == ExportPlatform::Windows)
    {
        return kWindowsSigning;
    }
    else if constexpr (kHost == ExportPlatform::MacOS)
    {
        return kMacosSigning;
    }
    else
    {
        return {};
    }
}

struct StageTitle
{
    std::string_view name; // exportStageName()
    text::TextKey title;
};

constexpr std::array<StageTitle, 9> kStages = {{
    {"validate", s::kExportStageValidate},
    {"generate", s::kExportStageGenerate},
    {"cook", s::kExportStageCook},
    {"pack", s::kExportStagePack},
    {"configure", s::kExportStageConfigure},
    {"build", s::kExportStageBuild},
    {"install", s::kExportStageInstall},
    {"package", s::kExportStagePackage},
    {"done", s::kExportStageDone},
}};

text::TextKey stageTitle(std::string_view name) noexcept
{
    for (const StageTitle& stage : kStages)
    {
        if (stage.name == name)
        {
            return stage.title;
        }
    }
    return s::kExportStageValidate;
}

// A u64 of a result: JSON writes them as decimal strings.
u64 readU64(JsonValue value) noexcept
{
    if (const std::optional<u64> number = value.asU64())
    {
        return *number;
    }
    const std::string_view digits = value.asString().value_or("");
    u64 parsed = 0;
    std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
    return parsed;
}

// True for a virtual path such as "assets:/a.png" (a file of the project), false for a native path.
bool isProjectFile(std::string_view file) noexcept
{
    const usize mount = file.find(":/");
    return mount != std::string_view::npos && mount > 1;
}

void copyText(std::span<char> buffer, std::string_view value) noexcept
{
    const usize length = std::min(value.size(), buffer.size() - 1);
    std::copy_n(value.data(), length, buffer.data());
    buffer[length] = '\0';
}

// Draws @p error under a field in the error colour; returns true when it is clicked.
bool fieldError(Impl& shell, const std::string& error, std::string_view id)
{
    if (error.empty())
    {
        return false;
    }
    ui::FrameText& text = shell.text;
    ImGui::PushStyleColor(ImGuiCol_TextLink, imvec(shell.theme.color(ui::ThemeToken::Error)));
    const bool clicked = ImGui::TextLink(text.verbatim(
        text.concat(ui::icons::kError.view(), text.concat(" ", error)), text.concat(id, ".error")));
    ImGui::PopStyleColor();
    return clicked;
}

} // namespace

struct ExportWindow::State
{
    struct Field
    {
        std::array<char, kFieldCapacity> buffer{};
        std::string error;
    };

    struct Diagnostic
    {
        std::string file;
        std::string location; // "file:line" as shown
        std::string severity;
        std::string message;
    };

    // The last export: its result, or why it could not run.
    struct Report
    {
        bool succeeded = false;
        bool cancelled = false;
        std::string stage;
        std::string error;
        std::string configuration;
        std::string directory;
        std::string package;
        std::string output;
        u64 files = 0;
        u64 totalBytes = 0;
        u64 skipped = 0;
        u32 errors = 0;
        u32 warnings = 0;
        bool signedBinaries = false;
        bool notarized = false;
        f64 seconds = 0.0;
        std::vector<Diagnostic> diagnostics;
    };

    std::array<Field, kSettingsFields.size()> settings;
    std::array<Field, BuildFieldCount> build;
    int package = 0;
    bool clean = false;
    i32 editing = -1; // the settings field being typed into
    i32 focus = -1;   // the settings field that takes the keyboard next frame

    const Project* project = nullptr;
    usize readPosition = ~usize{0}; // the undo history the settings were read at
    usize readSteps = ~usize{0};

    std::unique_ptr<RunningCommand> running;
    f64 startedAt = 0.0;
    std::optional<Report> report;

    // Reads the export settings (`project.settings_get`) when the undo history moved.
    void refresh(Impl& shell)
    {
        const UndoStack& history = shell.editor.history();
        if (readPosition == history.position() && readSteps == history.steps().size())
        {
            return;
        }
        readPosition = history.position();
        readSteps = history.steps().size();
        const std::optional<std::string> result =
            shell.run("project.settings_get", "{\"type\":" + jsonQuote(kSettingsType) + "}", kOrigin);
        if (!result)
        {
            return;
        }
        const Expected<JsonDocument, JsonError> json = JsonDocument::parse(*result);
        if (!json)
        {
            return;
        }
        const JsonValue value = json->root().pointer("/settings/0/value");
        for (usize i = 0; i < kSettingsFields.size(); ++i)
        {
            if (static_cast<i32>(i) == editing)
            {
                continue;
            }
            Field& field = settings[i];
            field.error.clear();
            const JsonValue member = value.find(kSettingsFields[i].name);
            if (i != kExcludeField)
            {
                const JsonValue path = member.isObject() ? member.find("path") : member;
                copyText(field.buffer, path.asString().value_or(""));
                continue;
            }
            std::string lines;
            for (const JsonValue pattern : member.elements())
            {
                lines += pattern.asString().value_or("");
                lines += '\n';
            }
            copyText(field.buffer, lines);
        }
    }

    // Writes the settings field @p index with `project.settings_set`; a failure shows under the field.
    void write(Impl& shell, usize index)
    {
        Field& field = settings[index];
        std::string value;
        if (index == kExcludeField)
        {
            value = "[";
            std::string_view rest(field.buffer.data());
            while (!rest.empty())
            {
                const usize end = rest.find('\n');
                std::string_view line = rest.substr(0, end);
                rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                {
                    line.remove_suffix(1);
                }
                while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
                {
                    line.remove_prefix(1);
                }
                if (!line.empty())
                {
                    value += (value.size() > 1 ? "," : "") + jsonQuote(line);
                }
            }
            value += "]";
        }
        else
        {
            value = jsonQuote(std::string_view(field.buffer.data()));
        }
        const std::string params = "{\"type\":" + jsonQuote(kSettingsType) + ",\"value\":{" +
                                   jsonQuote(kSettingsFields[index].name) + ":" + value + "}}";
        const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(params);
        if (!parsed)
        {
            return;
        }
        CommandOptions options;
        options.origin = kOrigin;
        const Expected<std::string, CommandError> result =
            shell.editor.executeJson("project.settings_set", parsed->root(), options);
        if (result)
        {
            field.error.clear();
            return;
        }
        const CommandError& error = result.error();
        field.error = error.errors.empty() ? error.message : error.errors.front().message;
    }

    // Starts `export.build` with the target and build options of the window.
    void start(Impl& shell)
    {
        for (Field& field : build)
        {
            field.error.clear();
        }
        report.reset();
        const std::span<const PackageChoice> packages = hostPackages();
        std::string params =
            "{\"package\":" + jsonQuote(packages[static_cast<usize>(package) % packages.size()].name);
        const auto add = [&](BuildField field) {
            const std::string_view value(build[field].buffer.data());
            if (!value.empty())
            {
                params += "," + jsonQuote(kBuildFields[field].param) + ":" + jsonQuote(value);
            }
        };
        add(Output);
        add(Configuration);
        for (const BuildField field : hostSigningFields())
        {
            add(field);
        }
        if (clean)
        {
            params += ",\"clean\":true";
        }
        params += "}";
        const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(params);
        if (!parsed)
        {
            return;
        }
        CommandOptions options;
        options.origin = kOrigin;
        Expected<std::unique_ptr<RunningCommand>, CommandError> started =
            shell.editor.startJson("export.build", parsed->root(), options);
        if (!started)
        {
            failed(started.error());
            return;
        }
        running = std::move(started).value();
        startedAt = shell.now;
    }

    // Shows @p error under the build field it names, or as the result.
    void failed(const CommandError& error)
    {
        if (!error.errors.empty())
        {
            for (usize i = 0; i < kBuildFields.size(); ++i)
            {
                if (error.errors.front().pointer.substr(1) == kBuildFields[i].param)
                {
                    build[i].error = error.errors.front().message;
                    return;
                }
            }
        }
        Report& failure = report.emplace();
        failure.stage = "validate";
        failure.error = error.message;
    }

    void finish(Impl& shell, std::string_view result)
    {
        const Expected<JsonDocument, JsonError> json = JsonDocument::parse(result);
        if (!json)
        {
            return;
        }
        const JsonValue root = json->root();
        Report& done = report.emplace();
        done.succeeded = root.find("succeeded").asBool().value_or(false);
        done.stage = std::string(root.find("stage").asString().value_or(""));
        done.error = std::string(root.find("error").asString().value_or(""));
        done.configuration = std::string(root.find("configuration").asString().value_or(""));
        done.directory = std::string(root.find("directory").asString().value_or(""));
        done.package = std::string(root.find("package").asString().value_or(""));
        done.output = std::string(root.find("output").asString().value_or(""));
        done.files = root.find("files").size();
        done.totalBytes = readU64(root.find("totalBytes"));
        done.skipped = root.find("skipped").size();
        done.errors = static_cast<u32>(readU64(root.find("errors")));
        done.warnings = static_cast<u32>(readU64(root.find("warnings")));
        done.signedBinaries = root.find("signedBinaries").asBool().value_or(false);
        done.notarized = root.find("notarized").asBool().value_or(false);
        done.seconds = std::round((shell.now - startedAt) * 10.0) / 10.0;
        for (const JsonValue entry : root.find("diagnostics").elements())
        {
            Diagnostic& diagnostic = done.diagnostics.emplace_back();
            diagnostic.file = std::string(entry.find("file").asString().value_or(""));
            diagnostic.severity = std::string(entry.find("severity").asString().value_or(""));
            diagnostic.message = std::string(entry.find("message").asString().value_or(""));
            diagnostic.location = diagnostic.file;
            if (const u64 line = readU64(entry.find("line")); line > 0 && !diagnostic.file.empty())
            {
                diagnostic.location += ":" + std::to_string(line);
            }
        }
    }

    void drawSettings(Impl& shell);
    void drawTarget(Impl& shell);
    void drawProgress(Impl& shell);
    void drawReport(Impl& shell);
};

ExportWindow::ExportWindow()
    : m_state(std::make_unique<State>())
{
}

ExportWindow::~ExportWindow() = default;

void ExportWindow::update(Impl& shell)
{
    State& state = *m_state;
    if (state.running == nullptr)
    {
        return;
    }
    Expected<std::optional<std::string>, CommandError> updated = state.running->update();
    if (updated && !updated->has_value())
    {
        return;
    }
    if (updated)
    {
        state.finish(shell, **updated);
    }
    else
    {
        state.failed(updated.error());
    }
    state.running.reset();
}

void ExportWindow::State::drawSettings(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    ui::sectionHeader(kit, text.format(s::kExportSettings));
    ImGui::TextDisabled("%s", text.format(s::kProjectSettingsFile, {{"file", kExportSettingsFile}}));
    if (!ui::beginPropertyTable(kit, "export.settings.table"))
    {
        return;
    }
    const ProjectManifest& manifest = shell.editor.project()->manifest();
    for (usize i = 0; i < kSettingsFields.size(); ++i)
    {
        const SettingsFieldInfo& info = kSettingsFields[i];
        Field& field = settings[i];
        ui::propertyLabel(kit, text.format(info.label));
        if (focus == static_cast<i32>(i))
        {
            ImGui::SetKeyboardFocusHere();
            focus = -1;
        }
        if (i == kExcludeField)
        {
            ImGui::InputTextMultiline(text.hidden(info.id), field.buffer.data(), field.buffer.size(),
                                      ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f +
                                                           ImGui::GetStyle().FramePadding.y * 2.0f));
            ImGui::SetItemTooltip("%s", text.format(info.hint));
        }
        else
        {
            const char* hint =
                info.hint.table != nullptr ? text.format(info.hint, {{"name", manifest.name}}) : "";
            ImGui::InputTextWithHint(text.hidden(info.id), hint, field.buffer.data(), field.buffer.size());
        }
        if (ImGui::IsItemActive())
        {
            editing = static_cast<i32>(i);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            write(shell, i);
        }
        if (ImGui::IsItemDeactivated() && editing == static_cast<i32>(i))
        {
            editing = -1;
        }
        if (fieldError(shell, field.error, info.id))
        {
            focus = static_cast<i32>(i);
        }
    }
    ui::propertyLabel(kit, text.format(s::kExportVersion), text.format(s::kExportVersionHint));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(manifest.version.c_str());
    ui::endPropertyTable();
}

void ExportWindow::State::drawTarget(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    ui::sectionHeader(kit, text.format(s::kExportTarget));
    if (!ui::beginPropertyTable(kit, "export.target.table"))
    {
        return;
    }
    ui::propertyLabel(kit, text.format(s::kExportPlatform), text.format(s::kExportPlatformHint));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text.format(hostPlatformTitle()));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ui::icons::kInfo.c_str());
    ImGui::SetItemTooltip("%s", text.format(s::kExportPlatformHint));

    const std::span<const PackageChoice> packages = hostPackages();
    package = std::clamp(package, 0, static_cast<int>(packages.size()) - 1);
    ui::propertyLabel(kit, text.format(s::kExportPackage));
    if (ImGui::BeginCombo(text.hidden("export.package"),
                          text.format(packages[static_cast<usize>(package)].label)))
    {
        for (usize i = 0; i < packages.size(); ++i)
        {
            if (ImGui::Selectable(
                    text.label(packages[i].label, text.concat("export.package.", packages[i].name)),
                    static_cast<usize>(package) == i))
            {
                package = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }

    const auto drawField = [&](BuildField index, const char* hint) {
        const BuildFieldInfo& info = kBuildFields[index];
        Field& field = build[index];
        ui::propertyLabel(kit, text.format(info.label));
        ImGui::InputTextWithHint(text.hidden(info.id), hint, field.buffer.data(), field.buffer.size());
        if (ImGui::IsItemEdited())
        {
            field.error.clear();
        }
        (void)fieldError(shell, field.error, info.id);
    };
    drawField(Output, text.format(s::kExportOutputHint));
    drawField(Configuration, text.format(s::kExportConfigurationHint));
    ui::propertyLabel(kit, text.format(s::kExportClean));
    ImGui::Checkbox(text.hidden("export.clean"), &clean);
    ui::endPropertyTable();

    // Signing is set up once per machine (usually through the environment), so it starts collapsed.
    if (!ImGui::CollapsingHeader(text.label(s::kExportSigning, "export.signing")))
    {
        return;
    }
    const std::span<const BuildField> signing = hostSigningFields();
    if (signing.empty())
    {
        ImGui::TextDisabled("%s", text.format(s::kExportSigningNone));
        return;
    }
    if (!ui::beginPropertyTable(kit, "export.signing.table"))
    {
        return;
    }
    for (const BuildField index : signing)
    {
        drawField(index,
                  text.format(s::kExportEnvironmentHint, {{"variable", kBuildFields[index].variable}}));
    }
    ui::endPropertyTable();
}

void ExportWindow::State::drawProgress(Impl& shell)
{
    ui::FrameText& text = shell.text;
    const CommandProgress& progress = running->progress();
    const f32 fraction = progress.total > 0.0 ? static_cast<f32>(progress.done / progress.total) : 0.0f;
    const char* stage = text.format(stageTitle(progress.message));
    ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f),
                       ImVec2(-(ImGui::GetFrameHeight() * 4.0f + ImGui::GetStyle().ItemSpacing.x), 0.0f),
                       stage);
    ImGui::SameLine();
    if (ImGui::Button(text.iconLabel(ui::icons::kCancel, s::kExportCancel, "export.cancel"),
                      ImVec2(-FLT_MIN, 0.0f)))
    {
        // Destroying the running command cancels it: the export ends its tool and leaves no partial game.
        running.reset();
        Report& cancelled = report.emplace();
        cancelled.cancelled = true;
    }
}

void ExportWindow::State::drawReport(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const Report& result = *report;
    ui::sectionHeader(kit, text.format(s::kExportReport));
    const auto status = [&](ui::ThemeToken token, const ui::Icon& icon, const char* message) {
        ImGui::TextColored(imvec(shell.theme.color(token)), "%s", icon.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(message);
    };
    if (result.cancelled)
    {
        status(ui::ThemeToken::Warning, ui::icons::kCancel, text.format(s::kExportCancelled));
        return;
    }
    if (!result.succeeded)
    {
        status(ui::ThemeToken::Error, ui::icons::kError,
               text.format(s::kExportFailed, {{"stage", text.format(stageTitle(result.stage))}}));
        ImGui::TextWrapped("%s", result.error.c_str());
    }
    else
    {
        status(ui::ThemeToken::Success, ui::icons::kCheckCircle, text.format(s::kExportSucceeded));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", text.format(s::kExportDuration, {{"seconds", result.seconds}}));
        if (ui::beginPropertyTable(kit, "export.report.table"))
        {
            ui::propertyLabel(kit, text.format(s::kExportGameFolder));
            ImGui::TextWrapped("%s", result.directory.c_str());
            if (!result.package.empty())
            {
                ui::propertyLabel(kit, text.format(s::kExportPackageFile));
                ImGui::TextWrapped("%s", result.package.c_str());
            }
            ui::propertyLabel(kit, text.format(s::kExportConfiguration));
            ImGui::TextUnformatted(result.configuration.c_str());
            const f64 megabytes =
                std::round(static_cast<f64>(result.totalBytes) / (1024.0 * 1024.0) * 100.0) / 100.0;
            ui::propertyLabel(kit, text.format(s::kExportSize));
            ImGui::TextUnformatted(
                text.format(s::kExportFiles, {{"count", result.files}, {"megabytes", megabytes}}));
            if (result.skipped > 0)
            {
                ImGui::TextDisabled("%s", text.format(s::kExportSkipped, {{"count", result.skipped}}));
            }
            if (result.signedBinaries || result.notarized)
            {
                ui::propertyLabel(kit, text.format(s::kExportSigning));
                if (result.signedBinaries)
                {
                    ui::badge(kit, text.format(s::kExportSigned), ui::ThemeToken::Success);
                }
                if (result.notarized)
                {
                    ImGui::SameLine();
                    ui::badge(kit, text.format(s::kExportNotarized), ui::ThemeToken::Success);
                }
            }
            ui::endPropertyTable();
        }
        if (ImGui::Button(text.iconLabel(ui::icons::kFolderOpen, s::kExportShowInFolder,
                                         "export.result.show_in_folder")))
        {
            (void)shell.run("editor.reveal", "{\"path\":" + jsonQuote(result.directory) + "}", kOrigin);
        }
    }

    if (!result.diagnostics.empty())
    {
        ui::sectionHeader(kit, text.format(s::kExportDiagnostics,
                                           {{"errors", result.errors}, {"warnings", result.warnings}}));
        char id[48];
        for (usize i = 0; i < result.diagnostics.size(); ++i)
        {
            const Diagnostic& diagnostic = result.diagnostics[i];
            const bool error = diagnostic.severity == "error";
            const bool warning = diagnostic.severity == "warning";
            ImGui::TextColored(imvec(shell.theme.color(error     ? ui::ThemeToken::Error
                                                       : warning ? ui::ThemeToken::Warning
                                                                 : ui::ThemeToken::Info)),
                               "%s",
                               (error     ? ui::icons::kError
                                : warning ? ui::icons::kWarning
                                          : ui::icons::kInfo)
                                   .c_str());
            ImGui::SameLine();
            if (isProjectFile(diagnostic.file))
            {
                std::snprintf(id, sizeof(id), "export.diagnostic.%zu", i);
                if (ImGui::TextLink(text.verbatim(diagnostic.location, id)))
                {
                    shell.assetBrowser->reveal(diagnostic.file);
                    (void)shell.run("ui.show_panel", R"({"panel":"panel.assets"})", kOrigin);
                }
                ImGui::SetItemTooltip("%s", text.format(s::kExportShowFile));
                ImGui::SameLine();
            }
            else if (!diagnostic.location.empty())
            {
                ImGui::TextDisabled("%s", diagnostic.location.c_str());
                ImGui::SameLine();
            }
            ImGui::TextWrapped("%s", diagnostic.message.c_str());
        }
    }

    if (!result.output.empty() &&
        ImGui::CollapsingHeader(text.label(s::kExportToolOutput, "export.result.output")))
    {
        if (ImGui::BeginChild(text.hidden("export.result.output_text"),
                              ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 10.0f),
                              ImGuiChildFlags_Borders))
        {
            ImGui::TextUnformatted(result.output.c_str(), result.output.c_str() + result.output.size());
        }
        ImGui::EndChild();
    }
}

void ExportWindow::draw(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    State& state = *m_state;
    const Project* project = shell.editor.project();
    if (project == nullptr)
    {
        ui::emptyState(kit, ui::icons::kRocketLaunch, text.format(s::kProjectSettingsNone),
                       text.format(s::kProjectSettingsNoneHint));
        return;
    }
    if (project != state.project)
    {
        state.project = project;
        state.readPosition = ~usize{0};
        state.editing = -1;
        state.report.reset();
    }
    state.refresh(shell);

    // Options on the left, the export and its result on the right; each side scrolls on its own.
    if (!ImGui::BeginTable(text.hidden("export.layout"), 2,
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
    {
        return;
    }
    setupStretchColumns({1.0f, 1.0f});
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const bool running = state.running != nullptr;
    if (ImGui::BeginChild(text.hidden("export.options")))
    {
        ImGui::BeginDisabled(running);
        state.drawSettings(shell);
        state.drawTarget(shell);
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::TableNextColumn();
    if (ImGui::BeginChild(text.hidden("export.run")))
    {
        if (running)
        {
            state.drawProgress(shell);
        }
        else if (ui::primaryButton(kit,
                                   text.iconLabel(ui::icons::kRocketLaunch, s::kExportStart, "export.start")))
        {
            state.start(shell);
        }
        if (state.report)
        {
            state.drawReport(shell);
        }
    }
    ImGui::EndChild();
    ImGui::EndTable();
}

} // namespace illusion::editor::shell::detail
