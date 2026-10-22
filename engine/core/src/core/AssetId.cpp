#include <illusion/core/AssetId.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/VirtualPath.hpp>

#include <algorithm>
#include <array>
#include <charconv>

namespace illusion
{
namespace
{

constexpr std::array<std::string_view, 7> kBuiltinAssets = {
    "builtin:/material/default", "builtin:/mesh/capsule", "builtin:/mesh/cube",      "builtin:/mesh/cylinder",
    "builtin:/mesh/plane",       "builtin:/mesh/sphere",  "builtin:/sky/procedural",
};

constexpr std::string_view kMeshPrefix = "mesh/";
constexpr std::string_view kMaterialPrefix = "material/";
constexpr std::string_view kNodePrefix = "node/";

// Parses a decimal index; @p canonical is false if it has leading zeros.
bool parseIndex(std::string_view text, u32& index, bool& canonical) noexcept
{
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
    {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), index);
    canonical = text.size() == 1 || text.front() != '0';
    return error == std::errc() && end == text.data() + text.size();
}

bool isValidNodeName(std::string_view name) noexcept
{
    return !name.empty() && std::none_of(name.begin(), name.end(),
                                         [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

// Parses @p fragment (the text after '#') into @p parts; @p canonical is false if the index has leading
// zeros.
bool parseFragment(std::string_view fragment, AssetPathParts& parts, bool& canonical) noexcept
{
    canonical = true;
    if (fragment.starts_with(kMeshPrefix))
    {
        parts.fragment = AssetFragment::Mesh;
        return parseIndex(fragment.substr(kMeshPrefix.size()), parts.index, canonical);
    }
    if (fragment.starts_with(kMaterialPrefix))
    {
        parts.fragment = AssetFragment::Material;
        return parseIndex(fragment.substr(kMaterialPrefix.size()), parts.index, canonical);
    }
    if (fragment.starts_with(kNodePrefix))
    {
        parts.fragment = AssetFragment::Node;
        parts.node = fragment.substr(kNodePrefix.size());
        return isValidNodeName(parts.node);
    }
    return false;
}

} // namespace

AssetId AssetId::fromPath(std::string_view path)
{
    if (isNormalizedAssetPath(path))
    {
        return AssetId{fnv1a64(path)};
    }
    const std::optional<std::string> normalized = normalizeAssetPath(path);
    return normalized ? AssetId{fnv1a64(*normalized)} : AssetId{};
}

std::optional<std::string> normalizeAssetPath(std::string_view path)
{
    const usize hash = path.find('#');
    std::optional<std::string> file = normalizeVirtualPath(path.substr(0, hash));
    if (!file || hash == std::string_view::npos)
    {
        return file;
    }
    AssetPathParts parts;
    bool canonical = true;
    if (!parseFragment(path.substr(hash + 1), parts, canonical))
    {
        return std::nullopt;
    }
    file->push_back('#');
    switch (parts.fragment)
    {
    case AssetFragment::Mesh:
        file->append(kMeshPrefix).append(std::to_string(parts.index));
        break;
    case AssetFragment::Material:
        file->append(kMaterialPrefix).append(std::to_string(parts.index));
        break;
    case AssetFragment::Node:
    case AssetFragment::None:
        file->append(path.substr(hash + 1));
        break;
    }
    return file;
}

bool isNormalizedAssetPath(std::string_view path) noexcept
{
    return splitAssetPath(path).has_value();
}

std::optional<AssetPathParts> splitAssetPath(std::string_view path) noexcept
{
    const usize hash = path.find('#');
    AssetPathParts parts;
    parts.file = path.substr(0, hash);
    if (!isNormalizedVirtualPath(parts.file))
    {
        return std::nullopt;
    }
    if (hash == std::string_view::npos)
    {
        return parts;
    }
    bool canonical = true;
    if (!parseFragment(path.substr(hash + 1), parts, canonical) || !canonical)
    {
        return std::nullopt;
    }
    return parts;
}

std::span<const std::string_view> builtinAssetPaths() noexcept
{
    return kBuiltinAssets;
}

bool isOnBuiltinMount(std::string_view path) noexcept
{
    return path.size() > kBuiltinAssetMount.size() && path.starts_with(kBuiltinAssetMount) &&
           path.substr(kBuiltinAssetMount.size()).starts_with(":/");
}

bool isBuiltinAsset(std::string_view path) noexcept
{
    return std::binary_search(kBuiltinAssets.begin(), kBuiltinAssets.end(), path);
}

} // namespace illusion
