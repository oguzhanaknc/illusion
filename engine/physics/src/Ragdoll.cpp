#include <illusion/assets/Animation.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/physics/Ragdoll.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <initializer_list>
#include <numbers>
#include <optional>

namespace illusion
{
namespace
{

constexpr std::string_view kFormatName = "illusion.ragdoll";
constexpr u64 kRagdollVersion = 1;
constexpr f32 kPi = std::numbers::pi_v<f32>;
constexpr f32 kEpsilon = 1e-6f;

std::string member(const std::string& path, std::string_view key)
{
    return path.empty() ? std::string(key) : std::format("{}.{}", path, key);
}

class Parser
{
public:
    std::string error;

    bool fail(const std::string& path, std::string_view message)
    {
        if (error.empty())
        {
            error = path.empty() ? std::string(message) : std::format("{}: {}", path, message);
        }
        return false;
    }

    bool object(JsonValue value, const std::string& path, std::initializer_list<std::string_view> allowed)
    {
        if (!value.isObject())
        {
            return fail(path, std::format("expected an object, found {}", jsonTypeName(value.type())));
        }
        for (const JsonMember& entry : value.members())
        {
            if (std::find(allowed.begin(), allowed.end(), entry.key) == allowed.end())
            {
                return fail(path, std::format("unknown key '{}'", entry.key));
            }
        }
        return true;
    }

    bool string(JsonValue parent, const std::string& path, std::string_view key, std::string& out)
    {
        const JsonValue value = parent.find(key);
        if (value.isAbsent())
        {
            return fail(member(path, key), "is required");
        }
        const std::optional<std::string_view> text = value.asString();
        if (!text)
        {
            return fail(member(path, key),
                        std::format("expected a string, found {}", jsonTypeName(value.type())));
        }
        if (text->empty())
        {
            return fail(member(path, key), "must not be empty");
        }
        out = *text;
        return true;
    }

    bool numberValue(JsonValue value, const std::string& path, f32& out, f32 low, f32 high)
    {
        const std::optional<f64> number = value.asF64();
        if (!number)
        {
            return fail(path, std::format("expected a number, found {}", jsonTypeName(value.type())));
        }
        const auto single = static_cast<f32>(*number);
        if (!std::isfinite(single))
        {
            return fail(path, "must be a finite number");
        }
        if (single < low || single > high)
        {
            return fail(path, high == HUGE_VALF ? std::format("must be at least {}", low)
                                                : std::format("must be in [{}, {}]", low, high));
        }
        out = single;
        return true;
    }

    // An optional number in [@p low, @p high]; @p out keeps its value when the key is absent.
    bool number(JsonValue parent, const std::string& path, std::string_view key, f32& out, f32 low, f32 high)
    {
        const JsonValue value = parent.find(key);
        return value.isAbsent() || numberValue(value, member(path, key), out, low, high);
    }

    // An optional array of exactly @p count finite numbers.
    template <usize N>
    bool numbers(JsonValue parent, const std::string& path, std::string_view key, std::array<f32, N>& out)
    {
        const JsonValue value = parent.find(key);
        if (value.isAbsent())
        {
            return true;
        }
        const std::string at = member(path, key);
        if (!value.isArray() || value.size() != N)
        {
            return fail(at, std::format("expected an array of {} numbers", N));
        }
        for (usize i = 0; i < N; ++i)
        {
            if (!numberValue(value[i], std::format("{}[{}]", at, i), out[i], -HUGE_VALF, HUGE_VALF))
            {
                return false;
            }
        }
        return true;
    }

    bool part(JsonValue entry, const std::string& path, RagdollPart& out)
    {
        if (!object(entry, path,
                    {"joint", "radius", "halfHeight", "offset", "rotation", "massShare", "swingLimit",
                     "twistMin", "twistMax"}) ||
            !string(entry, path, "joint", out.joint) ||
            !number(entry, path, "radius", out.radius, 0.0f, HUGE_VALF) ||
            !number(entry, path, "halfHeight", out.halfHeight, 0.0f, HUGE_VALF) ||
            !number(entry, path, "massShare", out.massShare, 0.0f, 1.0f) ||
            !number(entry, path, "swingLimit", out.swingLimit, 0.0f, kPi) ||
            !number(entry, path, "twistMin", out.twistMin, -kPi, 0.0f) ||
            !number(entry, path, "twistMax", out.twistMax, 0.0f, kPi))
        {
            return false;
        }
        if (!(out.radius > 0.0f))
        {
            return fail(member(path, "radius"), "must be positive");
        }
        std::array<f32, 3> offset{out.offset.x, out.offset.y, out.offset.z};
        std::array<f32, 4> rotation{out.rotation.x, out.rotation.y, out.rotation.z, out.rotation.w};
        if (!numbers(entry, path, "offset", offset) || !numbers(entry, path, "rotation", rotation))
        {
            return false;
        }
        out.offset = Vec3(offset[0], offset[1], offset[2]);
        const Quat turn{rotation[3], rotation[0], rotation[1], rotation[2]};
        const f32 length = glm::length(turn);
        if (!(length > kEpsilon) || !std::isfinite(length))
        {
            return fail(member(path, "rotation"), "must not be zero");
        }
        out.rotation = turn / length;
        return true;
    }
};

// The rotation that turns +y onto the unit vector @p to.
[[nodiscard]] Quat fromUp(Vec3 to) noexcept
{
    const f32 cosine = to.y;
    if (cosine > 1.0f - kEpsilon)
    {
        return Quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    if (cosine < -1.0f + kEpsilon)
    {
        return Quat{0.0f, 1.0f, 0.0f, 0.0f}; // half a turn about x
    }
    const Vec3 axis = glm::cross(Vec3(0.0f, 1.0f, 0.0f), to);
    return glm::normalize(Quat(1.0f + cosine, axis.x, axis.y, axis.z));
}

[[nodiscard]] f32 matrixScale(const Mat4& matrix) noexcept
{
    const f32 scale =
        (glm::length(Vec3(matrix[0])) + glm::length(Vec3(matrix[1])) + glm::length(Vec3(matrix[2]))) / 3.0f;
    return std::isfinite(scale) && scale > kEpsilon ? scale : 1.0f;
}

} // namespace

Expected<RagdollProfile, std::string> parseRagdollProfile(std::string_view json)
{
    Expected<JsonDocument, JsonError> doc = JsonDocument::parse(json);
    if (!doc)
    {
        return unexpected(std::format("invalid JSON at line {}, column {}: {}", doc.error().line,
                                      doc.error().column, doc.error().message));
    }
    const JsonValue root = doc->root();
    if (!root.isObject() || root.find("format").asString() != kFormatName)
    {
        return unexpected(std::string("not a ragdoll profile (missing \"format\": \"illusion.ragdoll\")"));
    }
    const std::optional<u64> version = root.find("version").asU64();
    if (!version || *version == 0 || *version > kRagdollVersion)
    {
        return unexpected(std::format(
            "version: unsupported ragdoll profile version (this build reads 1 to {})", kRagdollVersion));
    }
    Parser parser;
    if (!parser.object(root, "", {"format", "version", "parts"}))
    {
        return unexpected(std::move(parser.error));
    }
    const JsonValue parts = root.find("parts");
    if (parts.isAbsent())
    {
        return unexpected(std::string("parts: is required"));
    }
    if (!parts.isArray() || parts.size() == 0)
    {
        return unexpected(std::string("parts: expected an array of at least one part"));
    }
    RagdollProfile profile;
    profile.parts.reserve(parts.size());
    f32 shares = 0.0f;
    usize index = 0;
    for (const JsonValue entry : parts.elements())
    {
        const std::string path = std::format("parts[{}]", index++);
        RagdollPart part;
        if (!parser.part(entry, path, part))
        {
            return unexpected(std::move(parser.error));
        }
        for (const RagdollPart& other : profile.parts)
        {
            if (other.joint == part.joint)
            {
                return unexpected(std::format("{}.joint: duplicate joint '{}'", path, part.joint));
            }
        }
        shares += part.massShare;
        if (shares > 1.0f + 1e-4f)
        {
            return unexpected(std::format("{}.massShare: the shares add up to more than 1", path));
        }
        profile.parts.push_back(std::move(part));
    }
    return profile;
}

RagdollProfile makeRagdollProfile(const Skeleton& skeleton, std::span<const Aabb> jointBounds, f32 minSize)
{
    RagdollProfile profile;
    const usize count = std::min(skeleton.joints.size(), jointBounds.size());
    std::vector<Mat4> bind(count);
    for (usize j = 0; j < count; ++j)
    {
        const f32 determinant = glm::determinant(skeleton.joints[j].inverseBind);
        bind[j] = determinant != 0.0f && std::isfinite(determinant)
                      ? glm::inverse(skeleton.joints[j].inverseBind)
                      : Mat4(1.0f);
    }
    for (usize j = 0; j < count; ++j)
    {
        const Aabb& bounds = jointBounds[j];
        if (!bounds.isValid())
        {
            continue;
        }
        const f32 scale = matrixScale(bind[j]);
        const Vec3 half = bounds.extents();
        if (!(2.0f * std::max({half.x, half.y, half.z}) * scale > minSize))
        {
            continue;
        }
        // The farthest child, in this joint's bind space, gives the bone.
        Vec3 bone(0.0f);
        for (usize c = j + 1; c < count; ++c)
        {
            if (skeleton.joints[c].parent != static_cast<i32>(j))
            {
                continue;
            }
            const Vec3 child(skeleton.joints[j].inverseBind * bind[c][3]);
            if (glm::length(child) > glm::length(bone))
            {
                bone = child;
            }
        }
        RagdollPart part;
        part.joint = skeleton.joints[j].name;
        const f32 length = glm::length(bone);
        if (length > kEpsilon && std::isfinite(length))
        {
            const Vec3 axis = bone / length;
            // Half-extents across the bone, weighted by how perpendicular each axis is to it.
            const Vec3 across = Vec3(1.0f) - axis * axis;
            const f32 radius = glm::dot(half, across) / std::max(across.x + across.y + across.z, kEpsilon);
            part.radius = std::max(radius, 0.5f * minSize / scale) * scale;
            part.halfHeight = std::max(0.5f * length * scale - part.radius, 0.0f);
            part.offset = 0.5f * bone;
            part.rotation = fromUp(axis);
        }
        else
        {
            // A leaf: a capsule along the longest axis of its bounds.
            const glm::length_t longest =
                half.x >= half.y && half.x >= half.z ? 0 : (half.y >= half.z ? 1 : 2);
            Vec3 axis(0.0f);
            axis[longest] = 1.0f;
            const f32 radius = 0.5f * (half.x + half.y + half.z - half[longest]);
            part.radius = std::max(radius, 0.5f * minSize / scale) * scale;
            part.halfHeight = std::max(half[longest] * scale - part.radius, 0.0f);
            part.offset = bounds.center();
            part.rotation = fromUp(axis);
        }
        profile.parts.push_back(std::move(part));
    }
    return profile;
}

} // namespace illusion
