#include <illusion/core/Json.hpp>
#include <illusion/core/TypeName.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/editor/core/Preferences.hpp>
#include <illusion/editor/core/UiActions.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/text/LocaleId.hpp>

#include <algorithm>
#include <array>
#include <mutex>

namespace illusion::editor
{
namespace
{

constexpr std::array<std::string_view, 3> kThemes = {"dark", "light", "high-contrast"};
constexpr std::array<std::string_view, 4> kAccents = {"azure", "violet", "teal", "amber"};
constexpr std::array<std::string_view, 3> kPermissionModes = {"read-only", "ask", "auto"};
constexpr std::array<std::string_view, 5> kNavigationPresets = {"keymap", "illusion", "unity", "unreal",
                                                                "blender"};
constexpr std::string_view kFileName = "preferences.json";

bool oneOf(std::span<const std::string_view> names, std::string_view value) noexcept
{
    return std::find(names.begin(), names.end(), value) != names.end();
}

std::string quoted(std::span<const std::string_view> names)
{
    std::string out;
    for (const std::string_view name : names)
    {
        out += out.empty() ? "" : ", ";
        out += "\"" + std::string(name) + "\"";
    }
    return out;
}

} // namespace

void reflectPreferences(StructRegistry& structs)
{
    static std::once_flag once;
    std::call_once(once, [&structs] {
        reflectStruct<ShortcutBinding>("editor.ShortcutBinding", 1, structs)
            .field("action", &ShortcutBinding::action)
            .field("chord", &ShortcutBinding::chord);
        reflectStruct<ViewportQuality>("editor.ViewportQuality", 1, structs)
            .field("shadows", &ViewportQuality::shadows)
            .field("shadowCascades", &ViewportQuality::shadowCascades, {.min = 1.0, .max = 4.0})
            .field("shadowResolution", &ViewportQuality::shadowResolution, {.min = 512.0, .max = 4096.0})
            .field("shadowDistance", &ViewportQuality::shadowDistance,
                   {.unit = Unit::Meters, .min = 5.0, .max = 1000.0})
            .field("ambientOcclusion", &ViewportQuality::ambientOcclusion)
            .field("bloom", &ViewportQuality::bloom)
            .field("bloomIntensity", &ViewportQuality::bloomIntensity,
                   {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
            .field("temporalAntiAliasing", &ViewportQuality::temporalAntiAliasing)
            .field("occlusionCulling", &ViewportQuality::occlusionCulling);
        reflectStruct<Preferences>("editor.Preferences", 1, structs)
            .field("theme", &Preferences::theme)
            .field("accent", &Preferences::accent)
            .field("uiScale", &Preferences::uiScale, {.min = 0.75, .max = 2.0, .step = 0.05})
            .field("reduceMotion", &Preferences::reduceMotion)
            .field("colorBlindAxes", &Preferences::colorBlindAxes)
            .field("locale", &Preferences::locale)
            .field("keymap", &Preferences::keymap)
            .field("shortcuts", &Preferences::shortcuts)
            .field("viewportNavigation", &Preferences::viewportNavigation)
            .field("viewportQuality", &Preferences::viewportQuality)
            .field("agentPermissions", &Preferences::agentPermissions)
            .field("approvalPopups", &Preferences::approvalPopups)
            .field("followAgent", &Preferences::followAgent)
            .field("playModeTint", &Preferences::playModeTint)
            .field("maximizeOnPlay", &Preferences::maximizeOnPlay)
            .field("autosaveInterval", &Preferences::autosaveInterval,
                   {.unit = Unit::Seconds, .min = 0.0, .max = 3600.0})
            .field("autosaveCopies", &Preferences::autosaveCopies, {.min = 1.0, .max = 20.0})
            .field("codeEditor", &Preferences::codeEditor)
            .field("fileBrowser", &Preferences::fileBrowser)
            .field("assetFavourites", &Preferences::assetFavourites)
            .field("recentProjects", &Preferences::recentProjects)
            .field("layout", &Preferences::layout);
    });
}

const StructReflection& preferencesReflection()
{
    StructRegistry& structs = ComponentRegistry::instance().structs();
    reflectPreferences(structs);
    const StructReflection* type = structs.reflectionOf(typeHash<Preferences>());
    ILL_CHECK(type != nullptr, "editor.Preferences is not reflected");
    return *type;
}

Expected<void, std::string> validatePreferences(const Preferences& preferences)
{
    if (!oneOf(kThemes, preferences.theme))
    {
        return unexpected("/theme must be one of " + quoted(kThemes));
    }
    if (!oneOf(kAccents, preferences.accent))
    {
        return unexpected("/accent must be one of " + quoted(kAccents));
    }
    if (!(preferences.uiScale >= 0.75f && preferences.uiScale <= 2.0f))
    {
        return unexpected(std::string("/uiScale must be between 0.75 and 2"));
    }
    if (!text::LocaleId::parse(preferences.locale))
    {
        return unexpected(std::string("/locale must be a BCP 47 language tag, e.g. \"en\" or \"en-XA\""));
    }
    if (!oneOf(keymapPresets(), preferences.keymap))
    {
        return unexpected("/keymap must be one of " + quoted(keymapPresets()));
    }
    if (!oneOf(kNavigationPresets, preferences.viewportNavigation))
    {
        return unexpected("/viewportNavigation must be one of " + quoted(kNavigationPresets));
    }
    const ViewportQuality& quality = preferences.viewportQuality;
    if (quality.shadowCascades < 1 || quality.shadowCascades > 4)
    {
        return unexpected(std::string("/viewportQuality/shadowCascades must be between 1 and 4"));
    }
    if (quality.shadowResolution < 512 || quality.shadowResolution > 4096)
    {
        return unexpected(std::string("/viewportQuality/shadowResolution must be between 512 and 4096"));
    }
    if (!(quality.shadowDistance >= 5.0f && quality.shadowDistance <= 1000.0f))
    {
        return unexpected(std::string("/viewportQuality/shadowDistance must be between 5 and 1000"));
    }
    if (!(quality.bloomIntensity >= 0.0f && quality.bloomIntensity <= 1.0f))
    {
        return unexpected(std::string("/viewportQuality/bloomIntensity must be between 0 and 1"));
    }
    for (usize i = 0; i < preferences.shortcuts.size(); ++i)
    {
        const ShortcutBinding& binding = preferences.shortcuts[i];
        const std::string pointer = "/shortcuts/" + std::to_string(i);
        if (findUiAction(binding.action) == nullptr)
        {
            return unexpected(pointer + "/action is not a UI action");
        }
        if (!binding.chord.empty() && !parseChord(binding.chord))
        {
            return unexpected(pointer + "/chord must be a chord such as \"Ctrl+Shift+P\", or \"\"");
        }
        for (usize j = 0; j < i; ++j)
        {
            if (preferences.shortcuts[j].action == binding.action)
            {
                return unexpected(pointer + "/action is changed twice");
            }
        }
    }
    if (!oneOf(kPermissionModes, preferences.agentPermissions))
    {
        return unexpected("/agentPermissions must be one of " + quoted(kPermissionModes));
    }
    if (!(preferences.autosaveInterval >= 0.0 && preferences.autosaveInterval <= 3600.0))
    {
        return unexpected(std::string("/autosaveInterval must be between 0 and 3600 seconds"));
    }
    if (preferences.autosaveCopies < 1 || preferences.autosaveCopies > 20)
    {
        return unexpected(std::string("/autosaveCopies must be between 1 and 20"));
    }
    if (preferences.assetFavourites.size() > kMaxAssetFavourites)
    {
        return unexpected("/assetFavourites holds at most " + std::to_string(kMaxAssetFavourites) +
                          " assets");
    }
    for (usize i = 0; i < preferences.assetFavourites.size(); ++i)
    {
        const std::string& favourite = preferences.assetFavourites[i];
        const std::string pointer = "/assetFavourites/" + std::to_string(i);
        if (!favourite.starts_with("assets:/") || favourite.find("..") != std::string::npos)
        {
            return unexpected(pointer + " must be an asset path such as \"assets:/models/ship.glb\"");
        }
        for (usize j = 0; j < i; ++j)
        {
            if (preferences.assetFavourites[j] == favourite)
            {
                return unexpected(pointer + " is a duplicate");
            }
        }
    }
    if (preferences.recentProjects.size() > kMaxRecentProjects)
    {
        return unexpected("/recentProjects holds at most " + std::to_string(kMaxRecentProjects) +
                          " projects");
    }
    if (preferences.layout.empty())
    {
        return unexpected(std::string("/layout must name a layout"));
    }
    return {};
}

std::string writePreferences(const Preferences& preferences)
{
    const StructReflection& type = preferencesReflection();
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    root.add("format", doc.makeString(kPreferencesFormat));
    root.add("version", doc.makeU64(type.version));
    Expected<JsonMutValue, ValueError> written = writeValue(doc, type, &preferences);
    // Preferences hold strings, numbers and flags only, which always convert.
    ILL_CHECK(written.hasValue(), "cannot convert the preferences to JSON");
    root.add("data", *written);
    doc.setRoot(root);
    std::string text;
    ILL_CHECK(doc.write(text).hasValue(), "cannot write the preferences");
    text += '\n';
    return text;
}

Expected<Preferences, std::string> readPreferences(std::string_view json)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(json);
    if (!parsed)
    {
        return unexpected(std::string("is not valid JSON"));
    }
    const JsonValue root = parsed->root();
    if (root.find("format").asString() != kPreferencesFormat)
    {
        return unexpected("is not a preferences file (\"format\" must be \"" +
                          std::string(kPreferencesFormat) + "\")");
    }
    const std::optional<u64> version = root.find("version").asU64();
    if (!version || *version == 0 || *version > ~u32{0})
    {
        return unexpected(std::string("needs a \"version\" number"));
    }
    Preferences preferences;
    ValueReadOptions options;
    options.mode = JsonReadMode::Merge;
    options.savedVersion = static_cast<u32>(*version);
    const ValueReadResult read = readValue(preferencesReflection(), &preferences, root.find("data"), options);
    if (!read.ok())
    {
        return unexpected("'/data" + read.errors.front().pointer + "' " + read.errors.front().message);
    }
    if (Expected<void, std::string> valid = validatePreferences(preferences); !valid)
    {
        return unexpected("'/data" + valid.error());
    }
    return preferences;
}

Preferences loadPreferences(const std::filesystem::path& directory, std::string& warning)
{
    warning.clear();
    VirtualFileSystem files;
    std::error_code code;
    if (directory.empty() || !std::filesystem::exists(directory / kFileName, code) ||
        files.mount("user", directory, MountAccess::ReadOnly) != FileError::None)
    {
        return {};
    }
    std::string json;
    if (const FileError error = files.readText("user:/preferences.json", json); error != FileError::None)
    {
        warning = "cannot read preferences.json (" + std::string(fileErrorName(error)) + ")";
        return {};
    }
    Expected<Preferences, std::string> read = readPreferences(json);
    if (!read)
    {
        warning = "preferences.json " + read.error() + "; using the defaults";
        return {};
    }
    return std::move(read).value();
}

Expected<void, std::string> savePreferences(const std::filesystem::path& directory,
                                            const Preferences& preferences)
{
    std::error_code code;
    std::filesystem::create_directories(directory, code);
    VirtualFileSystem files;
    if (files.mount("user", directory, MountAccess::ReadWrite) != FileError::None)
    {
        return unexpected(std::string("cannot open the user directory"));
    }
    if (const FileError error = files.writeText("user:/preferences.json", writePreferences(preferences));
        error != FileError::None)
    {
        return unexpected("cannot write preferences.json (" + std::string(fileErrorName(error)) + ")");
    }
    return {};
}

void addRecentProject(std::vector<std::string>& recent, std::string_view project)
{
    // A new list (the project first, then the others in order) instead of inserting at the front, which
    // GCC 13 reports as a null dereference inside std::string (a false positive).
    std::vector<std::string> next;
    next.reserve(kMaxRecentProjects);
    next.emplace_back(project);
    for (std::string& entry : recent)
    {
        if (next.size() < kMaxRecentProjects && entry != project)
        {
            next.push_back(std::move(entry));
        }
    }
    recent = std::move(next);
}

std::span<const std::string_view> preferenceThemes() noexcept
{
    return kThemes;
}

std::span<const std::string_view> preferenceAccents() noexcept
{
    return kAccents;
}

std::span<const std::string_view> viewportNavigationPresets() noexcept
{
    return kNavigationPresets;
}

std::string_view viewportNavigationOf(const Preferences& preferences) noexcept
{
    return preferences.viewportNavigation == "keymap" ? std::string_view(preferences.keymap)
                                                      : std::string_view(preferences.viewportNavigation);
}

} // namespace illusion::editor
