#pragma once

/// @file
/// Import settings sidecars: `<asset>.import.json` files next to a texture or model that change how it is
/// imported. A sidecar exists only while a setting differs from its default, and holds only those settings:
///
/// ```json
/// {
///   "format": "illusion.import",
///   "version": 1,
///   "texture": {"colorSpace": "linear", "maxSize": 1024}
/// }
/// ```
///
/// AssetDatabase, AssetStreamer and so cookAssets() read the sidecar of every image and model they import,
/// and record it as a source of the cooked file: editing, adding or removing a sidecar invalidates the
/// import cache. Cache entries stay keyed by the settings the caller asked for, so a game that ships only
/// the cooked cache finds them without the sidecars.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <string>
#include <string_view>

namespace illusion
{

class JsonValue;
struct ImageImportSettings;

/// Suffix of import settings sidecars: the sidecar of "assets:/a.png" is "assets:/a.png.import.json".
inline constexpr std::string_view kImportSettingsSuffix = ".import.json";

/// Version of the sidecar format written by writeImportSettings().
inline constexpr u32 kImportSettingsVersion = 1;

/// Which settings an asset has, decided by its extension.
enum class ImportSettingsKind : u8
{
    None,    ///< Not imported (scenes, materials, audio, ...): no sidecar.
    Texture, ///< Images (.png, .jpg, .jpeg, .tga, .bmp, .hdr).
    Model,   ///< glTF models (.gltf, .glb).
};

/// How the colour data of an 8-bit image is decoded.
enum class ImportColorSpace : u8
{
    Auto,   ///< As the user of the image asks (sRGB for base colour and emissive, linear for the rest).
    Srgb,   ///< Always sRGB.
    Linear, ///< Always linear.
};

/// Settings of a texture sidecar ("texture" member).
struct TextureImportSettings
{
    ImportColorSpace colorSpace = ImportColorSpace::Auto; ///< Overrides the colour space users ask for.
    bool generateMips = true; ///< false imports only the top level, even when a user asks for mips.
    u32 maxSize = 0;          ///< Halve the image until no side exceeds this (0: no limit; else 1 to 16384).

    /// Compares every setting.
    friend bool operator==(const TextureImportSettings&, const TextureImportSettings&) = default;
};

/// Settings of a model sidecar ("model" member).
struct ModelImportSettings
{
    f32 scale = 1.0f; ///< Uniform scale of positions and node translations (greater than 0, at most 1000).

    /// Compares every setting.
    friend bool operator==(const ModelImportSettings&, const ModelImportSettings&) = default;
};

/// The settings of one sidecar; only the member that matches the asset's kind is used.
struct ImportSettings
{
    TextureImportSettings texture{}; ///< Used for ImportSettingsKind::Texture.
    ModelImportSettings model{};     ///< Used for ImportSettingsKind::Model.

    /// True if every setting has its default (the asset needs no sidecar).
    [[nodiscard]] bool isDefault() const noexcept { return *this == ImportSettings{}; }

    /// Compares every setting.
    friend bool operator==(const ImportSettings&, const ImportSettings&) = default;
};

/// Why a sidecar or a settings patch was rejected.
struct ImportSettingsError
{
    std::string pointer; ///< JSON pointer of the offending value ("" for the whole document).
    std::string message; ///< What is wrong with it.
};

/// The settings kind of the asset at @p path (by extension, in any case).
[[nodiscard]] ImportSettingsKind importSettingsKindOf(std::string_view path) noexcept;

/// True if @p path names an import settings sidecar (ends with kImportSettingsSuffix).
[[nodiscard]] bool isImportSettingsPath(std::string_view path) noexcept;

/// The sidecar path of the asset at @p assetPath.
[[nodiscard]] std::string importSettingsPath(std::string_view assetPath);

/// The asset a sidecar at @p sidecarPath belongs to (the path without the suffix); @p sidecarPath must be a
/// sidecar path.
[[nodiscard]] std::string_view importSettingsOwner(std::string_view sidecarPath) noexcept;

/// Merges the JSON merge patch @p patch (RFC 7386) into @p settings: an object whose members name the
/// settings of @p kind ("texture" or "model"), with null resetting a member or a whole section to its
/// defaults. A null @p patch resets everything. Unknown members, members of another kind, wrong types and
/// out-of-range values are rejected and leave @p settings unchanged.
[[nodiscard]] Expected<void, ImportSettingsError>
mergeImportSettings(ImportSettings& settings, const JsonValue& patch, ImportSettingsKind kind);

/// Parses the text of a sidecar of an asset of @p kind ("format": "illusion.import", versions 1 to
/// kImportSettingsVersion); validates like mergeImportSettings().
[[nodiscard]] Expected<ImportSettings, ImportSettingsError> parseImportSettings(std::string_view text,
                                                                                ImportSettingsKind kind);

/// The sidecar text of @p settings for an asset of @p kind, with only the settings that differ from their
/// defaults; empty if every setting is a default (the sidecar should not exist).
[[nodiscard]] std::string writeImportSettings(const ImportSettings& settings, ImportSettingsKind kind);

/// Every setting of @p kind as compact JSON, defaults included, e.g.
/// `{"texture":{"colorSpace":"auto","generateMips":true,"maxSize":0}}`; "{}" for ImportSettingsKind::None.
[[nodiscard]] std::string importSettingsValue(const ImportSettings& settings, ImportSettingsKind kind);

/// What an image is imported with: @p requested (what its user asks for) changed by @p settings.
[[nodiscard]] ImageImportSettings effectiveImageImport(const ImageImportSettings& requested,
                                                       const TextureImportSettings& settings) noexcept;

} // namespace illusion
