// The Inspector panel: entity header, lock toggle, Local/World Transform card with Euler continuity cache,
// metadata-driven and custom Component cards, multi-entity editing, inline validation, and Add Component.

#include "Panels.hpp"

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/text/FuzzyMatcher.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui_internal.h>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr const char* kEntityPayload = "ILLUSION_ENTITY";
constexpr const char* kAssetPayload = "ILLUSION_ASSET";

// Writes an integer into FrameText without heap allocation.
std::string_view formatUint(ui::FrameText& text, u64 value)
{
    char buffer[24];
    const auto [end, _] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    const char* copied = text.copy(std::string_view(buffer, static_cast<usize>(end - buffer)));
    return std::string_view(copied, static_cast<usize>(end - buffer));
}

// Formats a float into a stack buffer for JSON serialization.
std::string formatFloatJson(f64 value)
{
    if (!std::isfinite(value))
    {
        return "0";
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.7g", value);
    return std::string(buffer);
}

std::string formatFloatJson(f32 value)
{
    return formatFloatJson(static_cast<f64>(value));
}

std::string formatVec3Json(Vec3 value)
{
    return "[" + formatFloatJson(value.x) + "," + formatFloatJson(value.y) + "," + formatFloatJson(value.z) +
           "]";
}

// Formats a display name into FrameText without allocating a std::string on idle frames.
const char* displayLabel(ui::FrameText& text, std::string_view explicitDisplayName,
                         std::string_view identifier)
{
    if (!explicitDisplayName.empty())
    {
        return text.copy(explicitDisplayName);
    }
    const usize dot = identifier.rfind('.');
    if (dot != std::string_view::npos && dot + 1 < identifier.size())
    {
        identifier.remove_prefix(dot + 1);
    }
    char buffer[128];
    usize out = 0;
    for (usize i = 0; i < identifier.size() && out + 2 < sizeof(buffer); ++i)
    {
        const char c = identifier[i];
        if (c == '_')
        {
            if (out > 0 && buffer[out - 1] != ' ')
            {
                buffer[out++] = ' ';
            }
            continue;
        }
        const bool isUpper = c >= 'A' && c <= 'Z';
        const bool prevLower = i > 0 && identifier[i - 1] >= 'a' && identifier[i - 1] <= 'z';
        const bool nextLower =
            i + 1 < identifier.size() && identifier[i + 1] >= 'a' && identifier[i + 1] <= 'z';
        const bool prevUpper = i > 0 && identifier[i - 1] >= 'A' && identifier[i - 1] <= 'Z';
        if (i > 0 && isUpper && (prevLower || (prevUpper && nextLower)) && buffer[out - 1] != ' ')
        {
            buffer[out++] = ' ';
        }
        if (out == 0 && c >= 'a' && c <= 'z')
        {
            buffer[out++] = static_cast<char>(c - 'a' + 'A');
        }
        else if (out > 0 && isUpper && !prevUpper && !nextLower)
        {
            buffer[out++] = static_cast<char>(c - 'A' + 'a');
        }
        else
        {
            buffer[out++] = c;
        }
    }
    return text.copy(std::string_view(buffer, out));
}

// Modifier-scaled drag speed: Dear ImGui already multiplies by 10x with Shift and 0.01x with Alt, so multiply
// by 10x under Alt to make Alt 0.1x.
f32 dragSpeed(f32 baseSpeed) noexcept
{
    return ImGui::GetIO().KeyAlt ? baseSpeed * 10.0f : baseSpeed;
}

// True for built-in components that are shown in the entity header, Transform card, or Outliner instead of a
// generic component card.
bool isIntrinsicComponent(ComponentId id, std::string_view stableName) noexcept
{
    if (id == componentId<Transform>() || id == componentId<LocalTransform>() ||
        id == componentId<Hierarchy>() || id == componentId<Name>() || id == componentId<EntityGuid>() ||
        id == componentId<Disabled>())
    {
        return true;
    }
    return stableName == "illusion.Transform" || stableName == "illusion.LocalTransform" ||
           stableName == "illusion.Hierarchy" || stableName == "illusion.Name" ||
           stableName == "illusion.EntityGuid" || stableName == "illusion.Disabled";
}

// Lock-free, allocation-free lookup of a reflected ComponentId by stable name.
ComponentId findComponentIdLockFree(const ComponentRegistry& registry, std::string_view stableName) noexcept
{
    const u32 total = registry.count();
    for (u32 id = 0; id < total; ++id)
    {
        if (const TypeReflection* refl = registry.reflection(static_cast<ComponentId>(id));
            refl != nullptr && refl->name == stableName)
        {
            return static_cast<ComponentId>(id);
        }
    }
    return kInvalidComponentId;
}

// Local-space parent of @p entity when it has both LocalTransform and Hierarchy.
Entity placingParent(const World& world, Entity entity)
{
    if (!world.has<LocalTransform>(entity) || !world.has<Hierarchy>(entity))
    {
        return Entity{};
    }
    return parentOf(world, entity);
}

Transform computeWorldTransform(const World& world, Entity entity)
{
    Entity chain[64];
    usize depth = 0;
    for (Entity current = entity; !current.isNull() && depth < 64; current = placingParent(world, current))
    {
        chain[depth++] = current;
    }
    if (depth == 0)
    {
        return Transform{};
    }
    const Transform* top = world.tryGet<Transform>(chain[depth - 1]);
    Transform placed = top != nullptr ? *top : Transform{};
    for (usize i = depth - 1; i > 0; --i)
    {
        const Entity child = chain[i - 1];
        if (const LocalTransform* local = world.tryGet<LocalTransform>(child))
        {
            placed = combine(placed, local->asTransform());
        }
    }
    return placed;
}

Transform computeLocalTransform(const World& world, Entity entity)
{
    if (!placingParent(world, entity).isNull())
    {
        if (const LocalTransform* local = world.tryGet<LocalTransform>(entity))
        {
            return local->asTransform();
        }
    }
    if (const Transform* worldTr = world.tryGet<Transform>(entity))
    {
        return *worldTr;
    }
    if (const LocalTransform* local = world.tryGet<LocalTransform>(entity))
    {
        return local->asTransform();
    }
    return Transform{};
}

// Resolves Entity references to 16-hex GUID strings when writing a reflected value to JSON.
class GuidEntityResolver final : public EntityResolver
{
public:
    explicit GuidEntityResolver(const EntityIndex& index) noexcept
        : m_index(&index)
    {
    }

    JsonMutValue writeEntity(JsonMutDocument& doc, Entity entity) const override
    {
        const EntityGuid guid = m_index->guidOf(entity);
        if (guid.isNull())
        {
            return doc.makeNull();
        }
        const auto hex = formatGuid(guid);
        return doc.makeString(std::string_view(hex.data(), hex.size()));
    }

    Expected<Entity, std::string> readEntity(JsonValue value) const override
    {
        if (value.isNull())
        {
            return Entity{};
        }
        if (const auto text = value.asString())
        {
            if (const auto guid = parseGuid(*text))
            {
                return m_index->lookup(*guid);
            }
        }
        return Entity{};
    }

private:
    const EntityIndex* m_index;
};

// Serializes one reflected field or sub-value at @p dotPath on @p componentData to compact JSON.
std::string serializeComponentPath(const EntityIndex& index, const TypeReflection& reflection,
                                   const void* componentData, std::string_view slashPath)
{
    std::string dotPath(slashPath);
    std::replace(dotPath.begin(), dotPath.end(), '/', '.');
    JsonMutDocument doc;
    const GuidEntityResolver resolver(index);
    const Expected<JsonMutValue, ValueError> val =
        getPath(doc, reflection, componentData, dotPath, {.entities = &resolver});
    if (!val)
    {
        return "null";
    }
    doc.setRoot(*val);
    std::string out;
    if (!doc.write(out, JsonWriteOptions{.pretty = false}))
    {
        return "null";
    }
    return out;
}

// Draws a 3-axis vector field supporting per-axis mixed values ("—") and returning a bitmask of changed axes.
u8 drawVec3Mixed(ui::UiKit& kit, std::string_view id, Vec3& value, std::array<bool, 3> mixed, f32 speed,
                 const char* format = "%.3f")
{
    if (!mixed[0] && !mixed[1] && !mixed[2])
    {
        const Vec3 before = value;
        if (!ui::vec3Field(kit, id, value, speed, format))
        {
            return 0;
        }
        u8 mask = 0;
        for (usize axis = 0; axis < 3; ++axis)
        {
            if (value[static_cast<int>(axis)] != before[static_cast<int>(axis)])
            {
                mask |= static_cast<u8>(1u << axis);
            }
        }
        return mask != 0 ? mask : 0x7u;
    }

    const f32 scaledSpeed = dragSpeed(speed);

    static constexpr std::array<const char*, 3> kLetters = {"X", "Y", "Z"};
    static constexpr std::array<std::string_view, 3> kSuffixes = {".x", ".y", ".z"};
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 spacing = style.ItemInnerSpacing.x;
    const f32 total = ImGui::CalcItemWidth();
    const f32 fieldWidth = std::max(1.0f, std::floor((total - spacing * 2.0f) / 3.0f));
    const f32 chipWidth = std::round(ImGui::GetFontSize() * 1.1f);
    const char* mixedFormat = kit.text.format(s::kInspectorMixed);
    u8 changedMask = 0;

    for (usize axis = 0; axis < 3; ++axis)
    {
        if (axis > 0)
        {
            ImGui::SameLine(0.0f, spacing);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(style.FramePadding.x + chipWidth, style.FramePadding.y));
        ImGui::SetNextItemWidth(fieldWidth);
        const char* axisFormat = mixed[axis] ? mixedFormat : format;
        if (ImGui::DragFloat(kit.text.hidden(kit.text.concat(id, kSuffixes[axis])),
                             &value[static_cast<int>(axis)], scaledSpeed, 0.0f, 0.0f, axisFormat))
        {
            changedMask |= static_cast<u8>(1u << axis);
        }
        ui::requestInfiniteDrag();
        ImGui::PopStyleVar();

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        static constexpr std::array<ui::ThemeToken, 3> kAxis = {ui::ThemeToken::AxisX, ui::ThemeToken::AxisY,
                                                                ui::ThemeToken::AxisZ};
        static constexpr std::array<ui::ThemeToken, 3> kAxisCb = {
            ui::ThemeToken::AxisCbX, ui::ThemeToken::AxisCbY, ui::ThemeToken::AxisCbZ};
        const Vec4 color = kit.theme.color((kit.colorBlindAxes ? kAxisCb : kAxis)[axis]);
        const ImVec2 chipMax(min.x + chipWidth, max.y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const f32 luma = 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
        const ImU32 textColor = luma > 0.55f ? IM_COL32(20, 22, 26, 255) : IM_COL32(250, 250, 252, 255);
        drawList->AddRectFilled(min, chipMax,
                                ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, 1.0f)),
                                style.FrameRounding, ImDrawFlags_RoundLeft);
        const ImVec2 letter = ImGui::CalcTextSize(kLetters[axis]);
        drawList->AddText(ImVec2(std::floor((min.x + chipMax.x - letter.x) * 0.5f),
                                 std::floor((min.y + max.y - letter.y) * 0.5f)),
                          textColor, kLetters[axis]);
    }
    return changedMask;
}

std::string_view enumValueName(const ValueType& type, i64 numericValue) noexcept
{
    for (const EnumValue& entry : type.enumValues)
    {
        if (entry.value == numericValue)
        {
            return entry.name;
        }
    }
    return {};
}

// Custom drawer field filter for illusion.Collider, illusion.Joint, and illusion.AudioSource.
bool shouldShowComponentField(const TypeReflection& reflection, const void* primaryData,
                              const FieldInfo& field) noexcept
{
    if (field.meta.hidden)
    {
        return false;
    }
    if (primaryData == nullptr)
    {
        return true;
    }
    if (reflection.name == "illusion.Collider")
    {
        const FieldInfo* shapeField = reflection.field("shape");
        if (shapeField == nullptr || shapeField->type != FieldType::Enum)
        {
            return true;
        }
        const std::string_view shape = enumValueName(*shapeField, readEnumField(primaryData, *shapeField));
        if (shape == "Box")
        {
            return field.name != "radius" && field.name != "halfHeight" && field.name != "mesh" &&
                   field.name != "meshIndex";
        }
        if (shape == "Sphere")
        {
            return field.name != "halfExtents" && field.name != "halfHeight" && field.name != "mesh" &&
                   field.name != "meshIndex";
        }
        if (shape == "Capsule" || shape == "Cylinder")
        {
            return field.name != "halfExtents" && field.name != "mesh" && field.name != "meshIndex";
        }
        if (shape == "ConvexHull" || shape == "Mesh")
        {
            return field.name != "halfExtents" && field.name != "radius" && field.name != "halfHeight";
        }
    }
    else if (reflection.name == "illusion.Joint")
    {
        const FieldInfo* typeField = reflection.field("type");
        if (typeField == nullptr || typeField->type != FieldType::Enum)
        {
            return true;
        }
        const std::string_view jointType = enumValueName(*typeField, readEnumField(primaryData, *typeField));
        if (jointType == "Fixed")
        {
            return field.name != "connectedAnchor" && field.name != "axis" && field.name != "limited" &&
                   field.name != "minLimit" && field.name != "maxLimit" && field.name != "motorSpeed" &&
                   field.name != "motorMaxForce";
        }
        if (jointType == "Distance")
        {
            return field.name != "axis" && field.name != "limited" && field.name != "motorSpeed" &&
                   field.name != "motorMaxForce";
        }
        if (jointType == "Hinge" || jointType == "Slider")
        {
            if (field.name == "connectedAnchor")
            {
                return false;
            }
            if (field.name == "minLimit" || field.name == "maxLimit")
            {
                const FieldInfo* limitedField = reflection.field("limited");
                if (limitedField != nullptr && limitedField->type == FieldType::Bool)
                {
                    const bool limited = *static_cast<const bool*>(fieldData(primaryData, *limitedField));
                    return limited;
                }
            }
        }
    }
    else if (reflection.name == "illusion.AudioSource")
    {
        const FieldInfo* spatialField = reflection.field("spatial");
        if (spatialField != nullptr && spatialField->type == FieldType::Bool)
        {
            const bool isSpatial = *static_cast<const bool*>(fieldData(primaryData, *spatialField));
            if (field.meta.category == "3D sound")
            {
                return isSpatial;
            }
            if (field.name == "pan")
            {
                return !isSpatial;
            }
        }
    }
    return true;
}

} // namespace

struct Inspector::State
{
    struct InspectedEntity
    {
        Entity entity;
        EntityGuid guid;
    };

    struct EulerCache
    {
        u64 guid = 0;
        int worldSpace = 0;
        Quat lastRotation{1.0f, 0.0f, 0.0f, 0.0f};
        Vec3 eulerDegrees{0.0f, 0.0f, 0.0f};
    };

    struct CandidateComponent
    {
        ComponentId id = kInvalidComponentId;
        const TypeReflection* reflection = nullptr;
        i32 score = 0;
    };

    bool locked = false;
    std::vector<EntityGuid> lockedGuids;
    std::vector<InspectedEntity> inspected;
    std::vector<ComponentId> commonComponents;
    std::vector<const void*> componentPtrs;
    std::vector<EulerCache> eulerCache;
    std::vector<CandidateComponent> addCandidates;

    int worldSpace = 0; // 0 = Local, 1 = World
    u64 dragSerial = 1;

    u64 lastNameGuid = 0;
    usize lastInspectedCount = 0;
    std::string lastNameSynced;
    std::array<char, 256> nameBuffer{};
    std::array<char, 256> addSearchBuffer{};
    std::array<char, 512> textFieldBuffer{};
    bool focusAddSearch = false;

    State()
    {
        lockedGuids.reserve(16);
        inspected.reserve(16);
        commonComponents.reserve(32);
        componentPtrs.reserve(16);
        eulerCache.reserve(16);
        addCandidates.reserve(64);
    }

    const char* mergeKey(ui::FrameText& text, std::string_view component, std::string_view path) const
    {
        const std::string_view serial = formatUint(text, dragSerial);
        return text.concat(
            "inspector.drag.",
            text.concat(serial, text.concat(".", text.concat(component, text.concat(".", path)))));
    }

    std::string entitiesJsonArray() const
    {
        std::string out = "[";
        for (usize i = 0; i < inspected.size(); ++i)
        {
            if (i > 0)
            {
                out.push_back(',');
            }
            const auto hex = formatGuid(inspected[i].guid);
            out.push_back('"');
            out.append(hex.data(), hex.size());
            out.push_back('"');
        }
        out.push_back(']');
        return out;
    }

    Vec3& cachedEuler(EntityGuid guid, Quat currentRotation)
    {
        const Quat normalized = glm::length(currentRotation) > 1e-6f ? glm::normalize(currentRotation)
                                                                     : Quat(1.0f, 0.0f, 0.0f, 0.0f);
        for (EulerCache& entry : eulerCache)
        {
            if (entry.guid == guid.value)
            {
                if (entry.worldSpace != worldSpace ||
                    std::abs(glm::dot(normalized, entry.lastRotation)) <= 0.99999f)
                {
                    entry.worldSpace = worldSpace;
                    entry.lastRotation = normalized;
                    entry.eulerDegrees =
                        glm::degrees(eulerFromQuat(normalized, glm::radians(entry.eulerDegrees)));
                }
                return entry.eulerDegrees;
            }
        }
        if (eulerCache.size() >= 64)
        {
            eulerCache.erase(eulerCache.begin());
        }
        EulerCache& added = eulerCache.emplace_back();
        added.guid = guid.value;
        added.worldSpace = worldSpace;
        added.lastRotation = normalized;
        added.eulerDegrees = glm::degrees(eulerFromQuat(normalized));
        return added.eulerDegrees;
    }

    void setCachedEuler(EntityGuid guid, Vec3 degrees)
    {
        const Quat q = glm::normalize(quatFromEuler(glm::radians(degrees)));
        for (EulerCache& entry : eulerCache)
        {
            if (entry.guid == guid.value)
            {
                entry.worldSpace = worldSpace;
                entry.lastRotation = q;
                entry.eulerDegrees = degrees;
                return;
            }
        }
        EulerCache& added = eulerCache.emplace_back();
        added.guid = guid.value;
        added.worldSpace = worldSpace;
        added.lastRotation = q;
        added.eulerDegrees = degrees;
    }

    void applyFieldJson(Impl& shell, std::string_view typeName, std::string_view path,
                        std::string_view valueJson, bool merge)
    {
        const std::string_view key = merge ? mergeKey(shell.text, typeName, path) : std::string_view{};
        if (inspected.size() == 1)
        {
            const auto hex = formatGuid(inspected[0].guid);
            const std::string params = "{\"entity\":" + jsonQuote(std::string_view(hex.data(), hex.size())) +
                                       ",\"type\":" + jsonQuote(typeName) + ",\"path\":" + jsonQuote(path) +
                                       ",\"value\":" + std::string(valueJson) + "}";
            (void)shell.run("component.set_field", params, "inspector", {}, key);
            return;
        }
        std::string steps = "{\"steps\":[";
        for (usize i = 0; i < inspected.size(); ++i)
        {
            if (i > 0)
            {
                steps.push_back(',');
            }
            const auto hex = formatGuid(inspected[i].guid);
            steps += "{\"method\":\"component.set_field\",\"params\":{\"entity\":" +
                     jsonQuote(std::string_view(hex.data(), hex.size())) +
                     ",\"type\":" + jsonQuote(typeName) + ",\"path\":" + jsonQuote(path) +
                     ",\"value\":" + std::string(valueJson) + "}}";
        }
        steps += "]}";
        (void)shell.run("editor.batch", steps, "inspector", {}, key);
    }

    void resetFieldPath(Impl& shell, std::string_view typeName, std::string_view path)
    {
        if (inspected.size() == 1)
        {
            const auto hex = formatGuid(inspected[0].guid);
            const std::string params = "{\"entity\":" + jsonQuote(std::string_view(hex.data(), hex.size())) +
                                       ",\"type\":" + jsonQuote(typeName) + ",\"path\":" + jsonQuote(path) +
                                       "}";
            (void)shell.run("component.reset", params, "inspector");
            return;
        }
        std::string steps = "{\"steps\":[";
        for (usize i = 0; i < inspected.size(); ++i)
        {
            if (i > 0)
            {
                steps.push_back(',');
            }
            const auto hex = formatGuid(inspected[i].guid);
            steps += "{\"method\":\"component.reset\",\"params\":{\"entity\":" +
                     jsonQuote(std::string_view(hex.data(), hex.size())) +
                     ",\"type\":" + jsonQuote(typeName) + ",\"path\":" + jsonQuote(path) + "}}";
        }
        steps += "]}";
        (void)shell.run("editor.batch", steps, "inspector");
    }

    void drawHeader(Impl& shell, ui::UiKit& kit);
    void drawTransformCard(Impl& shell, ui::UiKit& kit);
    void drawComponentCard(Impl& shell, ui::UiKit& kit, ComponentId compId, const TypeReflection& reflection);
    void drawValidationWarnings(Impl& shell, ui::UiKit& kit, const TypeReflection& reflection,
                                std::string_view cardId);
    void drawFieldRow(Impl& shell, ui::UiKit& kit, const TypeReflection& compReflection,
                      const FieldInfo& field, std::string_view fieldPath, std::string_view fieldId,
                      usize baseOffset, bool readOnly);
    void drawValueWidget(Impl& shell, ui::UiKit& kit, const TypeReflection& compReflection,
                         const ValueType& valueType, const FieldMeta& meta, std::string_view fieldPath,
                         std::string_view fieldId, std::span<const void* const> values, bool mixed,
                         bool readOnly);
    void drawAddComponent(Impl& shell, ui::UiKit& kit);
};

void Inspector::State::drawHeader(Impl& shell, ui::UiKit& kit)
{
    EditorContext& editor = shell.editor;
    const World& world = editor.world();
    ui::FrameText& text = shell.text;
    const ImGuiStyle& style = ImGui::GetStyle();

    usize enabledCount = 0;
    for (const InspectedEntity& item : inspected)
    {
        if (!world.has<Disabled>(item.entity))
        {
            ++enabledCount;
        }
    }
    bool allEnabled = enabledCount == inspected.size();
    const bool mixedEnabled = enabledCount > 0 && enabledCount < inspected.size();
    if (mixedEnabled)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
    }
    if (ImGui::Checkbox(text.hidden("inspector.enabled"), &allEnabled))
    {
        const bool targetEnabled = mixedEnabled ? true : allEnabled;
        const char* enabledJson = targetEnabled ? "true" : "false";
        if (inspected.size() == 1)
        {
            const auto hex = formatGuid(inspected[0].guid);
            const std::string params = "{\"entity\":" + jsonQuote(std::string_view(hex.data(), hex.size())) +
                                       ",\"enabled\":" + enabledJson + "}";
            (void)shell.run("entity.set_enabled", params, "inspector");
        }
        else
        {
            std::string steps = "{\"steps\":[";
            for (usize i = 0; i < inspected.size(); ++i)
            {
                if (i > 0)
                {
                    steps.push_back(',');
                }
                const auto hex = formatGuid(inspected[i].guid);
                steps += "{\"method\":\"entity.set_enabled\",\"params\":{\"entity\":" +
                         jsonQuote(std::string_view(hex.data(), hex.size())) + ",\"enabled\":" + enabledJson +
                         "}}";
            }
            steps += "]}";
            (void)shell.run("editor.batch", steps, "inspector");
        }
    }
    if (mixedEnabled)
    {
        ImGui::PopItemFlag();
    }
    if (kit.strings.enabled[0] != '\0')
    {
        ImGui::SetItemTooltip("%s", kit.strings.enabled);
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

    // Sync the name buffer when the inspected entity or its Name changed.
    bool mixedNames = false;
    std::string_view primaryName{};
    if (const Name* name = world.tryGet<Name>(inspected[0].entity))
    {
        primaryName = name->value;
    }
    for (usize i = 1; i < inspected.size(); ++i)
    {
        std::string_view otherName{};
        if (const Name* name = world.tryGet<Name>(inspected[i].entity))
        {
            otherName = name->value;
        }
        if (otherName != primaryName)
        {
            mixedNames = true;
            break;
        }
    }

    const u64 primaryGuid = inspected[0].guid.value;
    const std::string_view shownName = mixedNames ? std::string_view{} : primaryName;
    if (lastNameGuid != primaryGuid || lastInspectedCount != inspected.size() || lastNameSynced != shownName)
    {
        lastNameGuid = primaryGuid;
        lastInspectedCount = inspected.size();
        lastNameSynced.assign(shownName);
        const usize len = std::min(shownName.size(), nameBuffer.size() - 1);
        if (len > 0)
        {
            std::memcpy(nameBuffer.data(), shownName.data(), len);
        }
        nameBuffer[len] = '\0';
    }

    const f32 buttonSize = ImGui::GetFrameHeight();
    const f32 availWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::max(40.0f, availWidth - buttonSize - style.ItemInnerSpacing.x));
    const char* nameHint = mixedNames ? text.format(s::kInspectorMixed) : text.format(s::kOutlinerUnnamed);
    const bool enterRename =
        ImGui::InputTextWithHint(text.hidden("inspector.name"), nameHint, nameBuffer.data(),
                                 nameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (enterRename || ImGui::IsItemDeactivatedAfterEdit())
    {
        const std::string_view newName(nameBuffer.data());
        lastNameSynced.assign(newName);
        if (inspected.size() == 1)
        {
            const auto hex = formatGuid(inspected[0].guid);
            const std::string params = "{\"entity\":" + jsonQuote(std::string_view(hex.data(), hex.size())) +
                                       ",\"name\":" + jsonQuote(newName) + "}";
            (void)shell.run("entity.rename", params, "inspector");
        }
        else
        {
            std::string steps = "{\"steps\":[";
            for (usize i = 0; i < inspected.size(); ++i)
            {
                if (i > 0)
                {
                    steps.push_back(',');
                }
                const auto hex = formatGuid(inspected[i].guid);
                steps += "{\"method\":\"entity.rename\",\"params\":{\"entity\":" +
                         jsonQuote(std::string_view(hex.data(), hex.size())) +
                         ",\"name\":" + jsonQuote(newName) + "}}";
            }
            steps += "]}";
            (void)shell.run("editor.batch", steps, "inspector");
        }
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (ui::iconButton(kit, locked ? ui::icons::kLock : ui::icons::kLockOpen, "inspector.lock",
                       text.format(s::kInspectorLock), nullptr, locked))
    {
        locked = !locked;
        lockedGuids.clear();
        if (locked)
        {
            for (const InspectedEntity& item : inspected)
            {
                lockedGuids.push_back(item.guid);
            }
        }
    }

    // Second line: GUID + Copy ID for a single entity, or selected count for multi-selection.
    if (inspected.size() == 1)
    {
        const auto hex = formatGuid(inspected[0].guid);
        const std::string_view hexView(hex.data(), hex.size());
        ImGui::AlignTextToFramePadding();
        {
            const ScopedFont mono(shell.options.fonts, ui::FontRole::Mono);
            ImGui::PushStyleColor(ImGuiCol_Text, imvec(kit.theme.color(ui::ThemeToken::TextSecondary)));
            ImGui::TextUnformatted(text.copy(hexView));
            ImGui::PopStyleColor();
        }
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        if (ui::iconButton(kit, ui::icons::kContentCopy, "inspector.copy_id",
                           text.format(s::kInspectorCopyId)))
        {
            ImGui::SetClipboardText(text.copy(hexView));
        }
    }
    else
    {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, imvec(kit.theme.color(ui::ThemeToken::TextSecondary)));
        ImGui::TextUnformatted(
            text.format(s::kInspectorMultiSelected, {{"count", static_cast<i64>(inspected.size())}}));
        ImGui::PopStyleColor();
    }
    ImGui::Separator();
}

void Inspector::State::drawTransformCard(Impl& shell, ui::UiKit& kit)
{
    const World& world = shell.editor.world();
    ui::FrameText& text = shell.text;

    for (const InspectedEntity& item : inspected)
    {
        if (!world.has<Transform>(item.entity) && !world.has<LocalTransform>(item.entity))
        {
            return;
        }
    }

    const bool open = ui::beginComponentCard(kit, "inspector.transform", ui::icons::kTransform,
                                             text.format(s::kComponentTransform), nullptr);
    if (ui::beginComponentCardMenu(kit, "inspector.transform"))
    {
        if (ImGui::MenuItem(
                text.iconLabel(ui::icons::kRestartAlt, s::kInspectorReset, "inspector.transform.menu.reset")))
        {
            (void)shell.run("transform.reset", "{\"entities\":" + entitiesJsonArray() + "}", "inspector");
        }
        if (ImGui::MenuItem(
                text.iconLabel(ui::icons::kContentCopy, s::kInspectorCopy, "inspector.transform.menu.copy")))
        {
            const auto hex = formatGuid(inspected[0].guid);
            const char* typeName =
                world.has<Transform>(inspected[0].entity) ? "illusion.Transform" : "illusion.LocalTransform";
            const std::string params = "{\"entity\":" + jsonQuote(std::string_view(hex.data(), hex.size())) +
                                       ",\"type\":\"" + typeName + "\"}";
            (void)shell.run("component.copy", params, "inspector");
        }
        const std::string_view clipType = shell.editor.componentClipboard().type;
        const bool canPaste = clipType == "illusion.Transform" || clipType == "illusion.LocalTransform";
        ImGui::BeginDisabled(!canPaste);
        if (ImGui::MenuItem(text.iconLabel(ui::icons::kContentPaste, s::kInspectorPasteValues,
                                           "inspector.transform.menu.paste")))
        {
            (void)shell.run("component.paste", "{\"entities\":" + entitiesJsonArray() + "}", "inspector");
        }
        ImGui::EndDisabled();
        ui::endComponentCardMenu();
    }

    if (!open)
    {
        return;
    }

    const std::array<const char*, 2> spaceLabels = {
        text.format(s::kGallerySpaceLocal),
        text.format(s::kGallerySpaceWorld),
    };
    (void)ui::segmentedControl(kit, "inspector.transform.space", spaceLabels, worldSpace);
    const char* spaceJson = worldSpace == 1 ? "\"world\"" : "\"local\"";

    const Transform primaryTr = worldSpace == 1 ? computeWorldTransform(world, inspected[0].entity)
                                                : computeLocalTransform(world, inspected[0].entity);
    Vec3 position = primaryTr.position;
    Vec3 euler = cachedEuler(inspected[0].guid, primaryTr.rotation);
    Vec3 scale = primaryTr.scale;

    std::array<bool, 3> mixedPos = {false, false, false};
    std::array<bool, 3> mixedRot = {false, false, false};
    std::array<bool, 3> mixedScale = {false, false, false};

    for (usize i = 1; i < inspected.size(); ++i)
    {
        const Transform otherTr = worldSpace == 1 ? computeWorldTransform(world, inspected[i].entity)
                                                  : computeLocalTransform(world, inspected[i].entity);
        const Vec3 otherEuler = cachedEuler(inspected[i].guid, otherTr.rotation);
        for (usize axis = 0; axis < 3; ++axis)
        {
            const int a = static_cast<int>(axis);
            if (std::abs(otherTr.position[a] - position[a]) > 1e-5f)
            {
                mixedPos[axis] = true;
            }
            if (std::abs(otherEuler[a] - euler[a]) > 1e-4f)
            {
                mixedRot[axis] = true;
            }
            if (std::abs(otherTr.scale[a] - scale[a]) > 1e-5f)
            {
                mixedScale[axis] = true;
            }
        }
    }

    if (ui::beginPropertyTable(kit, "inspector.transform.table"))
    {
        // Position
        ui::propertyLabel(kit, text.format(s::kPropertyPosition));
        if (ImGui::BeginPopupContextItem(text.hidden("inspector.transform.position.context")))
        {
            if (ImGui::MenuItem(text.label(s::kInspectorReset, "inspector.transform.position.reset")))
            {
                const std::string params = "{\"entities\":" + entitiesJsonArray() +
                                           ",\"space\":" + spaceJson + ",\"position\":[0,0,0]}";
                (void)shell.run("transform.set", params, "inspector");
            }
            ImGui::EndPopup();
        }
        if (const u8 posMask =
                drawVec3Mixed(kit, "inspector.transform.position", position, mixedPos, 0.05f, "%.3f"))
        {
            const std::string_view key = mergeKey(text, "illusion.Transform", "position");
            if (inspected.size() == 1 || posMask == 0x7u || (!mixedPos[0] && !mixedPos[1] && !mixedPos[2]))
            {
                const std::string params = "{\"entities\":" + entitiesJsonArray() +
                                           ",\"space\":" + spaceJson +
                                           ",\"position\":" + formatVec3Json(position) + "}";
                (void)shell.run("transform.set", params, "inspector", {}, key);
            }
            else
            {
                std::string steps = "{\"steps\":[";
                for (usize i = 0; i < inspected.size(); ++i)
                {
                    if (i > 0)
                    {
                        steps.push_back(',');
                    }
                    const Transform cur = worldSpace == 1 ? computeWorldTransform(world, inspected[i].entity)
                                                          : computeLocalTransform(world, inspected[i].entity);
                    Vec3 target = cur.position;
                    for (usize axis = 0; axis < 3; ++axis)
                    {
                        if ((posMask & (1u << axis)) != 0)
                        {
                            target[static_cast<int>(axis)] = position[static_cast<int>(axis)];
                        }
                    }
                    const auto hex = formatGuid(inspected[i].guid);
                    steps += "{\"method\":\"transform.set\",\"params\":{\"entities\":[" +
                             jsonQuote(std::string_view(hex.data(), hex.size())) +
                             "],\"space\":" + spaceJson + ",\"position\":" + formatVec3Json(target) + "}}";
                }
                steps += "]}";
                (void)shell.run("editor.batch", steps, "inspector", {}, key);
            }
        }

        // Rotation (Euler degrees with continuity cache)
        ui::propertyLabel(kit, text.format(s::kPropertyRotation));
        if (ImGui::BeginPopupContextItem(text.hidden("inspector.transform.rotation.context")))
        {
            if (ImGui::MenuItem(text.label(s::kInspectorReset, "inspector.transform.rotation.reset")))
            {
                for (const InspectedEntity& item : inspected)
                {
                    setCachedEuler(item.guid, Vec3(0.0f));
                }
                const std::string params = "{\"entities\":" + entitiesJsonArray() +
                                           ",\"space\":" + spaceJson + ",\"euler\":[0,0,0]}";
                (void)shell.run("transform.set", params, "inspector");
            }
            ImGui::EndPopup();
        }
        if (const u8 rotMask =
                drawVec3Mixed(kit, "inspector.transform.rotation", euler, mixedRot, 0.5f, "%.2f°"))
        {
            const std::string_view key = mergeKey(text, "illusion.Transform", "rotation");
            if (inspected.size() == 1 || rotMask == 0x7u || (!mixedRot[0] && !mixedRot[1] && !mixedRot[2]))
            {
                for (const InspectedEntity& item : inspected)
                {
                    setCachedEuler(item.guid, euler);
                }
                const std::string params = "{\"entities\":" + entitiesJsonArray() +
                                           ",\"space\":" + spaceJson + ",\"euler\":" + formatVec3Json(euler) +
                                           "}";
                (void)shell.run("transform.set", params, "inspector", {}, key);
            }
            else
            {
                std::string steps = "{\"steps\":[";
                for (usize i = 0; i < inspected.size(); ++i)
                {
                    if (i > 0)
                    {
                        steps.push_back(',');
                    }
                    const Transform cur = worldSpace == 1 ? computeWorldTransform(world, inspected[i].entity)
                                                          : computeLocalTransform(world, inspected[i].entity);
                    Vec3 target = cachedEuler(inspected[i].guid, cur.rotation);
                    for (usize axis = 0; axis < 3; ++axis)
                    {
                        if ((rotMask & (1u << axis)) != 0)
                        {
                            target[static_cast<int>(axis)] = euler[static_cast<int>(axis)];
                        }
                    }
                    setCachedEuler(inspected[i].guid, target);
                    const auto hex = formatGuid(inspected[i].guid);
                    steps += "{\"method\":\"transform.set\",\"params\":{\"entities\":[" +
                             jsonQuote(std::string_view(hex.data(), hex.size())) +
                             "],\"space\":" + spaceJson + ",\"euler\":" + formatVec3Json(target) + "}}";
                }
                steps += "]}";
                (void)shell.run("editor.batch", steps, "inspector", {}, key);
            }
        }

        // Scale
        ui::propertyLabel(kit, text.format(s::kPropertyScale));
        if (ImGui::BeginPopupContextItem(text.hidden("inspector.transform.scale.context")))
        {
            if (ImGui::MenuItem(text.label(s::kInspectorReset, "inspector.transform.scale.reset")))
            {
                const std::string params = "{\"entities\":" + entitiesJsonArray() +
                                           ",\"space\":" + spaceJson + ",\"scale\":[1,1,1]}";
                (void)shell.run("transform.set", params, "inspector");
            }
            ImGui::EndPopup();
        }
        if (const u8 scaleMask =
                drawVec3Mixed(kit, "inspector.transform.scale", scale, mixedScale, 0.02f, "%.3f"))
        {
            const std::string_view key = mergeKey(text, "illusion.Transform", "scale");
            if (inspected.size() == 1 || scaleMask == 0x7u ||
                (!mixedScale[0] && !mixedScale[1] && !mixedScale[2]))
            {
                const std::string params = "{\"entities\":" + entitiesJsonArray() +
                                           ",\"space\":" + spaceJson + ",\"scale\":" + formatVec3Json(scale) +
                                           "}";
                (void)shell.run("transform.set", params, "inspector", {}, key);
            }
            else
            {
                std::string steps = "{\"steps\":[";
                for (usize i = 0; i < inspected.size(); ++i)
                {
                    if (i > 0)
                    {
                        steps.push_back(',');
                    }
                    const Transform cur = worldSpace == 1 ? computeWorldTransform(world, inspected[i].entity)
                                                          : computeLocalTransform(world, inspected[i].entity);
                    Vec3 target = cur.scale;
                    for (usize axis = 0; axis < 3; ++axis)
                    {
                        if ((scaleMask & (1u << axis)) != 0)
                        {
                            target[static_cast<int>(axis)] = scale[static_cast<int>(axis)];
                        }
                    }
                    const auto hex = formatGuid(inspected[i].guid);
                    steps += "{\"method\":\"transform.set\",\"params\":{\"entities\":[" +
                             jsonQuote(std::string_view(hex.data(), hex.size())) +
                             "],\"space\":" + spaceJson + ",\"scale\":" + formatVec3Json(target) + "}}";
                }
                steps += "]}";
                (void)shell.run("editor.batch", steps, "inspector", {}, key);
            }
        }

        ui::endPropertyTable();
    }

    ui::endComponentCard();
}

void Inspector::State::drawValidationWarnings(Impl& shell, ui::UiKit& kit, const TypeReflection& reflection,
                                              std::string_view cardId)
{
    const World& world = shell.editor.world();
    const ComponentRegistry& registry = world.registry();
    ui::FrameText& text = shell.text;
    const ImGuiStyle& style = ImGui::GetStyle();

    // 1. Missing requiredComponents check.
    for (const std::string_view reqName : reflection.meta.requiredComponents)
    {
        const ComponentId reqId = findComponentIdLockFree(registry, reqName);
        if (reqId == kInvalidComponentId)
        {
            continue;
        }
        bool anyMissing = false;
        for (const InspectedEntity& item : inspected)
        {
            const std::span<const ComponentId> comps = world.componentsOf(item.entity);
            if (std::find(comps.begin(), comps.end(), reqId) == comps.end())
            {
                anyMissing = true;
                break;
            }
        }
        if (!anyMissing)
        {
            continue;
        }
        const TypeReflection* reqRefl = registry.reflection(reqId);
        const char* reqLabel = reqRefl != nullptr
                                   ? displayLabel(text, reqRefl->meta.displayName, reqRefl->name)
                                   : text.copy(reqName);
        const char* warningText = text.format(s::kInspectorFixRequires, {{"component", reqLabel}});
        const char* fixId = text.concat(cardId, text.concat(".fix.req.", reqName));
        const char* buttonText = text.format(s::kInspectorFixAdd);
        const f32 buttonWidth = ImGui::CalcTextSize(buttonText).x + style.FramePadding.x * 2.0f;
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, imvec(kit.theme.color(ui::ThemeToken::Warning)));
        ImGui::TextUnformatted(ui::icons::kWarning.c_str());
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        const bool sameLine = ImGui::CalcTextSize(warningText).x + style.ItemInnerSpacing.x + buttonWidth <=
                              ImGui::GetContentRegionAvail().x;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(warningText);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (sameLine)
        {
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        }
        if (ImGui::SmallButton(text.label(s::kInspectorFixAdd, fixId)))
        {
            if (inspected.size() == 1)
            {
                const auto hex = formatGuid(inspected[0].guid);
                const std::string params =
                    "{\"entity\":" + jsonQuote(std::string_view(hex.data(), hex.size())) +
                    ",\"type\":" + jsonQuote(reqName) + "}";
                (void)shell.run("component.add", params, "inspector");
            }
            else
            {
                std::string steps = "{\"steps\":[";
                bool first = true;
                for (const InspectedEntity& item : inspected)
                {
                    const std::span<const ComponentId> comps = world.componentsOf(item.entity);
                    if (std::find(comps.begin(), comps.end(), reqId) != comps.end())
                    {
                        continue;
                    }
                    if (!first)
                    {
                        steps.push_back(',');
                    }
                    first = false;
                    const auto hex = formatGuid(item.guid);
                    steps += "{\"method\":\"component.add\",\"params\":{\"entity\":" +
                             jsonQuote(std::string_view(hex.data(), hex.size())) +
                             ",\"type\":" + jsonQuote(reqName) + "}}";
                }
                steps += "]}";
                (void)shell.run("editor.batch", steps, "inspector");
            }
        }
    }

    // 2. Mesh Collider on a Dynamic RigidBody check.
    if (reflection.name == "illusion.Collider" && !componentPtrs.empty() && componentPtrs[0] != nullptr)
    {
        const FieldInfo* shapeField = reflection.field("shape");
        const ComponentId rbId = findComponentIdLockFree(registry, "illusion.RigidBody");
        const TypeReflection* rbRefl = rbId != kInvalidComponentId ? registry.reflection(rbId) : nullptr;
        const FieldInfo* motionField = rbRefl != nullptr ? rbRefl->field("motionType") : nullptr;
        if (shapeField != nullptr && motionField != nullptr)
        {
            bool meshOnDynamic = false;
            for (usize i = 0; i < inspected.size(); ++i)
            {
                const void* colData = componentPtrs[i];
                const void* rbData = world.componentData(inspected[i].entity, rbId);
                if (colData != nullptr && rbData != nullptr)
                {
                    const std::string_view shape =
                        enumValueName(*shapeField, readEnumField(colData, *shapeField));
                    const std::string_view motion =
                        enumValueName(*motionField, readEnumField(rbData, *motionField));
                    if (shape == "Mesh" && motion == "Dynamic")
                    {
                        meshOnDynamic = true;
                        break;
                    }
                }
            }
            if (meshOnDynamic)
            {
                const char* warningText = text.format(s::kInspectorFixMeshDynamic);
                const char* fixId = text.concat(cardId, ".fix.convex_hull");
                const char* buttonText = text.format(s::kInspectorFixConvexHull);
                const f32 buttonWidth = ImGui::CalcTextSize(buttonText).x + style.FramePadding.x * 2.0f;
                ImGui::AlignTextToFramePadding();
                ImGui::PushStyleColor(ImGuiCol_Text, imvec(kit.theme.color(ui::ThemeToken::Warning)));
                ImGui::TextUnformatted(ui::icons::kWarning.c_str());
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                const bool sameLine =
                    ImGui::CalcTextSize(warningText).x + style.ItemInnerSpacing.x + buttonWidth <=
                    ImGui::GetContentRegionAvail().x;
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(warningText);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                if (sameLine)
                {
                    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                }
                if (ImGui::SmallButton(text.label(s::kInspectorFixConvexHull, fixId)))
                {
                    applyFieldJson(shell, reflection.name, "shape", "\"ConvexHull\"", false);
                }
            }
        }
    }
}

void Inspector::State::drawValueWidget(Impl& shell, ui::UiKit& kit, const TypeReflection& compReflection,
                                       const ValueType& valueType, const FieldMeta& meta,
                                       std::string_view fieldPath, std::string_view fieldId,
                                       std::span<const void* const> values, bool mixed, bool readOnly)
{
    ui::FrameText& text = shell.text;
    const ImGuiStyle& style = ImGui::GetStyle();
    const void* primary = values.empty() ? nullptr : values[0];
    if (primary == nullptr)
    {
        return;
    }

    ImGui::BeginDisabled(readOnly);

    switch (valueType.type)
    {
    case FieldType::Bool:
    {
        bool val = *static_cast<const bool*>(primary);
        if (mixed)
        {
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        }
        if (ImGui::Checkbox(text.hidden(fieldId), &val))
        {
            const bool next = mixed ? true : val;
            applyFieldJson(shell, compReflection.name, fieldPath, next ? "true" : "false", false);
        }
        if (mixed)
        {
            ImGui::PopItemFlag();
        }
        break;
    }
    case FieldType::I8:
    case FieldType::I16:
    case FieldType::I32:
    case FieldType::I64:
    case FieldType::U8:
    case FieldType::U16:
    case FieldType::U32:
    case FieldType::U64:
    {
        i64 signedVal = 0;
        if (valueType.type == FieldType::I8)
        {
            signedVal = *static_cast<const i8*>(primary);
        }
        else if (valueType.type == FieldType::I16)
        {
            signedVal = *static_cast<const i16*>(primary);
        }
        else if (valueType.type == FieldType::I32)
        {
            signedVal = *static_cast<const i32*>(primary);
        }
        else if (valueType.type == FieldType::I64)
        {
            signedVal = *static_cast<const i64*>(primary);
        }
        else if (valueType.type == FieldType::U8)
        {
            signedVal = static_cast<i64>(*static_cast<const u8*>(primary));
        }
        else if (valueType.type == FieldType::U16)
        {
            signedVal = static_cast<i64>(*static_cast<const u16*>(primary));
        }
        else if (valueType.type == FieldType::U32)
        {
            signedVal = static_cast<i64>(*static_cast<const u32*>(primary));
        }
        else
        {
            signedVal = static_cast<i64>(*static_cast<const u64*>(primary));
        }
        int val = static_cast<int>(signedVal);
        const f32 speed = dragSpeed(static_cast<f32>(meta.step.value_or(1.0)));
        const int minVal = meta.min ? static_cast<int>(*meta.min) : 0;
        const int maxVal = meta.max ? static_cast<int>(*meta.max) : 0;
        const char* fmt = mixed ? text.format(s::kInspectorMixed) : "%d";
        bool edited = false;
        if (meta.hint == FieldHint::Slider && (meta.softMin || meta.min) && (meta.softMax || meta.max))
        {
            const int sMin = static_cast<int>(meta.softMin.value_or(meta.min.value_or(0.0)));
            const int sMax = static_cast<int>(meta.softMax.value_or(meta.max.value_or(100.0)));
            edited = ImGui::SliderInt(text.hidden(fieldId), &val, sMin, sMax, fmt);
        }
        else
        {
            edited = ImGui::DragInt(text.hidden(fieldId), &val, speed, minVal, maxVal, fmt);
            ui::requestInfiniteDrag();
        }
        if (edited)
        {
            if (meta.min && val < static_cast<int>(*meta.min))
            {
                val = static_cast<int>(*meta.min);
            }
            if (meta.max && val > static_cast<int>(*meta.max))
            {
                val = static_cast<int>(*meta.max);
            }
            char numBuf[32];
            const auto [end, _] = std::to_chars(numBuf, numBuf + sizeof(numBuf), val);
            applyFieldJson(shell, compReflection.name, fieldPath,
                           std::string_view(numBuf, static_cast<usize>(end - numBuf)), true);
        }
        break;
    }
    case FieldType::F32:
    case FieldType::F64:
    {
        const f64 stored = valueType.type == FieldType::F32
                               ? static_cast<f64>(*static_cast<const f32*>(primary))
                               : *static_cast<const f64*>(primary);
        const Unit storedUnit = meta.unit;
        const Unit shownUnit = meta.displayUnit != Unit::None ? meta.displayUnit : storedUnit;
        const auto toShown = [&](f64 v) noexcept {
            return convertUnit(v, storedUnit, shownUnit).value_or(v);
        };
        const auto toStored = [&](f64 v) noexcept {
            return convertUnit(v, shownUnit, storedUnit).value_or(v);
        };

        f32 shown = static_cast<f32>(toShown(stored));
        const std::string_view unitSym = unitSymbol(shownUnit);
        const char* fmt = "%.3f";
        if (mixed)
        {
            fmt = text.format(s::kInspectorMixed);
        }
        else if (!unitSym.empty())
        {
            fmt = text.concat("%.2f ", unitSym);
        }

        const f32 baseStep = static_cast<f32>(meta.step ? toShown(*meta.step) : 0.05);
        const f32 speed = dragSpeed(std::max(1e-5f, baseStep));
        const bool hasSliderBounds = (meta.softMin || meta.min) && (meta.softMax || meta.max);
        bool edited = false;
        if ((meta.hint == FieldHint::Slider || meta.hint == FieldHint::LogSlider) && hasSliderBounds)
        {
            const f32 sMin = static_cast<f32>(toShown(meta.softMin.value_or(meta.min.value_or(0.0))));
            const f32 sMax = static_cast<f32>(toShown(meta.softMax.value_or(meta.max.value_or(1.0))));
            const ImGuiSliderFlags flags =
                meta.hint == FieldHint::LogSlider ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None;
            edited = ImGui::SliderFloat(text.hidden(fieldId), &shown, sMin, sMax, fmt, flags);
        }
        else
        {
            const f32 dMin = meta.min ? static_cast<f32>(toShown(*meta.min)) : 0.0f;
            const f32 dMax = meta.max ? static_cast<f32>(toShown(*meta.max)) : 0.0f;
            const ImGuiSliderFlags flags =
                (meta.min && meta.max) ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
            edited = ImGui::DragFloat(text.hidden(fieldId), &shown, speed, dMin, dMax, fmt, flags);
            ui::requestInfiniteDrag();
        }
        if (edited && std::isfinite(shown))
        {
            f64 newStored = toStored(static_cast<f64>(shown));
            if (meta.min && newStored < *meta.min)
            {
                newStored = *meta.min;
            }
            if (meta.max && newStored > *meta.max)
            {
                newStored = *meta.max;
            }
            applyFieldJson(shell, compReflection.name, fieldPath, formatFloatJson(newStored), true);
        }
        break;
    }
    case FieldType::Vector2:
    {
        Vec2 val = *static_cast<const Vec2*>(primary);
        const f32 speed = dragSpeed(static_cast<f32>(meta.step.value_or(0.05)));
        const char* fmt = mixed ? text.format(s::kInspectorMixed) : "%.3f";
        if (ImGui::DragFloat2(text.hidden(fieldId), &val.x, speed, 0.0f, 0.0f, fmt))
        {
            const std::string json = "[" + formatFloatJson(val.x) + "," + formatFloatJson(val.y) + "]";
            applyFieldJson(shell, compReflection.name, fieldPath, json, true);
        }
        ui::requestInfiniteDrag();
        break;
    }
    case FieldType::Vector3:
    {
        Vec3 val = *static_cast<const Vec3*>(primary);
        if (meta.hint == FieldHint::Color)
        {
            if (ImGui::ColorEdit3(text.hidden(fieldId), &val.x, ImGuiColorEditFlags_Float))
            {
                val = glm::clamp(val, Vec3(0.0f), Vec3(1.0f));
                applyFieldJson(shell, compReflection.name, fieldPath, formatVec3Json(val), true);
            }
        }
        else if (meta.hint == FieldHint::HdrColor)
        {
            f32 intensity = std::max({val.x, val.y, val.z, 1.0f});
            Vec3 baseColor = intensity > 0.0f ? val / intensity : Vec3(1.0f);
            if (ui::colorHdr(kit, fieldId, baseColor, intensity))
            {
                const Vec3 combined = glm::max(baseColor * std::max(0.0f, intensity), Vec3(0.0f));
                applyFieldJson(shell, compReflection.name, fieldPath, formatVec3Json(combined), true);
            }
        }
        else
        {
            std::array<bool, 3> axisMixed = {false, false, false};
            for (usize i = 1; i < values.size(); ++i)
            {
                const Vec3 other = *static_cast<const Vec3*>(values[i]);
                for (usize axis = 0; axis < 3; ++axis)
                {
                    if (other[static_cast<int>(axis)] != val[static_cast<int>(axis)])
                    {
                        axisMixed[axis] = true;
                    }
                }
            }
            const f32 baseStep = static_cast<f32>(meta.step.value_or(0.05));
            if (const u8 mask = drawVec3Mixed(kit, fieldId, val, axisMixed, baseStep, "%.3f"))
            {
                static constexpr std::array<std::string_view, 3> kAxisPath = {"/x", "/y", "/z"};
                if (values.size() == 1 || mask == 0x7u || (!axisMixed[0] && !axisMixed[1] && !axisMixed[2]))
                {
                    applyFieldJson(shell, compReflection.name, fieldPath, formatVec3Json(val), true);
                }
                else
                {
                    for (usize axis = 0; axis < 3; ++axis)
                    {
                        if ((mask & (1u << axis)) != 0)
                        {
                            const char* subPath = text.concat(fieldPath, kAxisPath[axis]);
                            applyFieldJson(shell, compReflection.name, subPath,
                                           formatFloatJson(val[static_cast<int>(axis)]), true);
                        }
                    }
                }
            }
        }
        break;
    }
    case FieldType::Vector4:
    {
        Vec4 val = *static_cast<const Vec4*>(primary);
        if (meta.hint == FieldHint::Color || meta.hint == FieldHint::HdrColor)
        {
            if (ImGui::ColorEdit4(text.hidden(fieldId), &val.x, ImGuiColorEditFlags_Float))
            {
                const std::string json = "[" + formatFloatJson(val.x) + "," + formatFloatJson(val.y) + "," +
                                         formatFloatJson(val.z) + "," + formatFloatJson(val.w) + "]";
                applyFieldJson(shell, compReflection.name, fieldPath, json, true);
            }
        }
        else
        {
            const f32 speed = dragSpeed(static_cast<f32>(meta.step.value_or(0.05)));
            const char* fmt = mixed ? text.format(s::kInspectorMixed) : "%.3f";
            if (ImGui::DragFloat4(text.hidden(fieldId), &val.x, speed, 0.0f, 0.0f, fmt))
            {
                const std::string json = "[" + formatFloatJson(val.x) + "," + formatFloatJson(val.y) + "," +
                                         formatFloatJson(val.z) + "," + formatFloatJson(val.w) + "]";
                applyFieldJson(shell, compReflection.name, fieldPath, json, true);
            }
            ui::requestInfiniteDrag();
        }
        break;
    }
    case FieldType::Quaternion:
    {
        const Quat q = *static_cast<const Quat*>(primary);
        Vec3 euler = glm::degrees(eulerFromQuat(q));
        const std::array<bool, 3> axisMixed = {mixed, mixed, mixed};
        if (drawVec3Mixed(kit, fieldId, euler, axisMixed, 0.5f, "%.2f°") != 0)
        {
            const Quat updated = glm::normalize(quatFromEuler(glm::radians(euler)));
            const std::string json = "[" + formatFloatJson(updated.x) + "," + formatFloatJson(updated.y) +
                                     "," + formatFloatJson(updated.z) + "," + formatFloatJson(updated.w) +
                                     "]";
            applyFieldJson(shell, compReflection.name, fieldPath, json, true);
        }
        break;
    }
    case FieldType::String:
    {
        const std::string& str = *static_cast<const std::string*>(primary);
        if (!ImGui::IsAnyItemActive())
        {
            const std::string_view shown = mixed ? std::string_view{} : std::string_view(str);
            const usize len = std::min(shown.size(), textFieldBuffer.size() - 1);
            if (len > 0)
            {
                std::memcpy(textFieldBuffer.data(), shown.data(), len);
            }
            textFieldBuffer[len] = '\0';
        }
        const char* hint = mixed ? text.format(s::kInspectorMixed) : "";
        const bool enter =
            ImGui::InputTextWithHint(text.hidden(fieldId), hint, textFieldBuffer.data(),
                                     textFieldBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::IsItemDeactivatedAfterEdit())
        {
            applyFieldJson(shell, compReflection.name, fieldPath,
                           jsonQuote(std::string_view(textFieldBuffer.data())), false);
        }
        break;
    }
    case FieldType::Enum:
    {
        const i64 cur = readEnumValue(primary, valueType);
        const std::string_view curName = enumValueName(valueType, cur);
        const char* preview =
            mixed ? text.format(s::kInspectorMixed) : (!curName.empty() ? text.copy(curName) : "?");
        const char* popupId = text.concat(fieldId, ".popup");
        if (ImGui::Button(text.verbatim(preview, fieldId), ImVec2(-FLT_MIN, 0.0f)))
        {
            ImGui::OpenPopup(popupId);
        }
        if (ImGui::BeginPopup(popupId))
        {
            for (usize i = 0; i < valueType.enumValues.size(); ++i)
            {
                const EnumValue& ev = valueType.enumValues[i];
                const bool isSelected = !mixed && ev.value == cur;
                const char* itemId = text.concat(fieldId, text.concat(".opt.", ev.name));
                if (ImGui::Selectable(text.verbatim(ev.name, itemId), isSelected))
                {
                    applyFieldJson(shell, compReflection.name, fieldPath, jsonQuote(ev.name), false);
                }
            }
            ImGui::EndPopup();
        }
        break;
    }
    case FieldType::Flags:
    {
        const i64 cur = readEnumValue(primary, valueType);
        char previewBuf[256];
        usize previewLen = 0;
        const char* preview = nullptr;
        if (mixed)
        {
            preview = text.format(s::kInspectorMixed);
        }
        else
        {
            for (const EnumValue& ev : valueType.enumValues)
            {
                if (ev.value != 0 && (cur & ev.value) == ev.value)
                {
                    if (previewLen > 0 && previewLen + 3 < sizeof(previewBuf))
                    {
                        std::memcpy(previewBuf + previewLen, " | ", 3);
                        previewLen += 3;
                    }
                    const usize copyLen = std::min(ev.name.size(), sizeof(previewBuf) - 1 - previewLen);
                    if (copyLen > 0)
                    {
                        std::memcpy(previewBuf + previewLen, ev.name.data(), copyLen);
                    }
                    previewLen += copyLen;
                }
            }
            previewBuf[previewLen] = '\0';
            preview = previewLen > 0 ? text.copy(std::string_view(previewBuf, previewLen))
                                     : text.format(s::kInspectorNone);
        }
        const char* popupId = text.concat(fieldId, ".popup");
        if (ImGui::Button(text.verbatim(preview, fieldId), ImVec2(-FLT_MIN, 0.0f)))
        {
            ImGui::OpenPopup(popupId);
        }
        if (ImGui::BeginPopup(popupId))
        {
            for (usize i = 0; i < valueType.enumValues.size(); ++i)
            {
                const EnumValue& ev = valueType.enumValues[i];
                if (ev.value == 0)
                {
                    continue;
                }
                bool bitSet = !mixed && (cur & ev.value) == ev.value;
                const char* itemId = text.concat(fieldId, text.concat(".flag.", ev.name));
                if (ImGui::Checkbox(text.verbatim(ev.name, itemId), &bitSet))
                {
                    const i64 next = bitSet ? (cur | ev.value) : (cur & ~ev.value);
                    std::string json = "[";
                    bool first = true;
                    for (const EnumValue& flag : valueType.enumValues)
                    {
                        if (flag.value != 0 && (next & flag.value) == flag.value)
                        {
                            if (!first)
                            {
                                json.push_back(',');
                            }
                            first = false;
                            json += jsonQuote(flag.name);
                        }
                    }
                    json.push_back(']');
                    applyFieldJson(shell, compReflection.name, fieldPath, json, false);
                }
            }
            ImGui::EndPopup();
        }
        break;
    }
    case FieldType::Entity:
    {
        const Entity target = *static_cast<const Entity*>(primary);
        const World& world = shell.editor.world();
        const EntityIndex& entityIndex = shell.editor.entities();
        const char* buttonText = text.format(s::kInspectorNone);
        if (mixed)
        {
            buttonText = text.format(s::kInspectorMixed);
        }
        else if (!target.isNull() && world.isAlive(target))
        {
            if (const Name* name = world.tryGet<Name>(target); name != nullptr && !name->value.empty())
            {
                buttonText = text.copy(name->value);
            }
            else
            {
                const EntityGuid guid = entityIndex.guidOf(target);
                const auto hex = formatGuid(guid);
                buttonText = text.copy(std::string_view(hex.data(), hex.size()));
            }
        }

        const bool hasClear = !mixed && !target.isNull();
        const f32 buttonSize = ImGui::GetFrameHeight();
        const f32 avail = ImGui::GetContentRegionAvail().x;
        const f32 pickWidth =
            hasClear ? std::max(40.0f, avail - buttonSize - style.ItemInnerSpacing.x) : avail;
        const char* popupId = text.concat(fieldId, ".popup");
        if (ImGui::Button(text.verbatim(buttonText, fieldId), ImVec2(pickWidth, 0.0f)))
        {
            ImGui::OpenPopup(popupId);
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntityPayload))
            {
                const char* raw = static_cast<const char*>(payload->Data);
                usize len = static_cast<usize>(payload->DataSize > 0 ? payload->DataSize : 0);
                if (len > 0 && raw[len - 1] == '\0')
                {
                    --len;
                }
                const std::string_view guidStr(raw, len);
                if (parseGuid(guidStr))
                {
                    applyFieldJson(shell, compReflection.name, fieldPath, jsonQuote(guidStr), false);
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (hasClear)
        {
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            if (ui::iconButton(kit, ui::icons::kClose, text.concat(fieldId, ".clear"),
                               text.format(s::kInspectorClear)))
            {
                applyFieldJson(shell, compReflection.name, fieldPath, "null", false);
            }
        }
        if (ImGui::BeginPopup(popupId))
        {
            if (ImGui::Selectable(text.label(s::kInspectorNone, text.concat(fieldId, ".pick.none")),
                                  !mixed && target.isNull()))
            {
                applyFieldJson(shell, compReflection.name, fieldPath, "null", false);
            }
            const std::span<const Entity> treeOrder = shell.editor.entities().hierarchy().preOrder();
            for (usize i = 0; i < treeOrder.size(); ++i)
            {
                const Entity candidate = treeOrder[i];
                if (!world.isAlive(candidate))
                {
                    continue;
                }
                const EntityGuid guid = entityIndex.guidOf(candidate);
                if (guid.isNull())
                {
                    continue;
                }
                const auto hex = formatGuid(guid);
                const std::string_view hexView(hex.data(), hex.size());
                const Name* candName = world.tryGet<Name>(candidate);
                const std::string_view labelView = (candName != nullptr && !candName->value.empty())
                                                       ? std::string_view(candName->value)
                                                       : hexView;
                const char* itemId = text.concat(fieldId, text.concat(".pick.", formatUint(text, i)));
                if (ImGui::Selectable(text.verbatim(labelView, itemId), !mixed && candidate == target))
                {
                    applyFieldJson(shell, compReflection.name, fieldPath, jsonQuote(hexView), false);
                }
            }
            ImGui::EndPopup();
        }
        break;
    }
    case FieldType::Asset:
    {
        const AssetRef ref = *static_cast<const AssetRef*>(primary);
        const std::string_view assetPath = ref.path();
        if (!ImGui::IsAnyItemActive())
        {
            const std::string_view shown = mixed ? std::string_view{} : assetPath;
            const usize len = std::min(shown.size(), textFieldBuffer.size() - 1);
            if (len > 0)
            {
                std::memcpy(textFieldBuffer.data(), shown.data(), len);
            }
            textFieldBuffer[len] = '\0';
        }
        const bool hasClear = !mixed && !assetPath.empty();
        const f32 buttonSize = ImGui::GetFrameHeight();
        const f32 avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(hasClear ? std::max(40.0f, avail - buttonSize - style.ItemInnerSpacing.x)
                                         : avail);
        const char* hint = mixed ? text.format(s::kInspectorMixed) : text.format(s::kInspectorNone);
        const bool enter =
            ImGui::InputTextWithHint(text.hidden(fieldId), hint, textFieldBuffer.data(),
                                     textFieldBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::IsItemDeactivatedAfterEdit())
        {
            applyFieldJson(shell, compReflection.name, fieldPath,
                           jsonQuote(std::string_view(textFieldBuffer.data())), false);
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayload))
            {
                const char* raw = static_cast<const char*>(payload->Data);
                usize len = static_cast<usize>(payload->DataSize > 0 ? payload->DataSize : 0);
                if (len > 0 && raw[len - 1] == '\0')
                {
                    --len;
                }
                const std::string_view dropped(raw, len);
                applyFieldJson(shell, compReflection.name, fieldPath, jsonQuote(dropped), false);
            }
            ImGui::EndDragDropTarget();
        }
        if (hasClear)
        {
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            if (ui::iconButton(kit, ui::icons::kClose, text.concat(fieldId, ".clear"),
                               text.format(s::kInspectorClear)))
            {
                applyFieldJson(shell, compReflection.name, fieldPath, "\"\"", false);
            }
        }
        if (!mixed && !assetPath.empty() && !assetPath.starts_with("builtin:") &&
            shell.editor.project() != nullptr && !shell.editor.files().exists(assetPath))
        {
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, imvec(kit.theme.color(ui::ThemeToken::Warning)));
            ImGui::TextUnformatted(ui::icons::kWarning.c_str());
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            ImGui::TextUnformatted(text.format(s::kInspectorFixMissingAsset, {{"path", assetPath}}));
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            if (ImGui::SmallButton(text.label(s::kInspectorFixClear, text.concat(fieldId, ".fix.clear"))))
            {
                applyFieldJson(shell, compReflection.name, fieldPath, "\"\"", false);
            }
        }
        break;
    }
    case FieldType::Json:
    {
        const JsonText& jsonVal = *static_cast<const JsonText*>(primary);
        if (!ImGui::IsAnyItemActive())
        {
            const std::string_view shown = mixed ? std::string_view{} : std::string_view(jsonVal.text);
            const usize len = std::min(shown.size(), textFieldBuffer.size() - 1);
            if (len > 0)
            {
                std::memcpy(textFieldBuffer.data(), shown.data(), len);
            }
            textFieldBuffer[len] = '\0';
        }
        const bool enter = ImGui::InputText(text.hidden(fieldId), textFieldBuffer.data(),
                                            textFieldBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::IsItemDeactivatedAfterEdit())
        {
            applyFieldJson(shell, compReflection.name, fieldPath, std::string_view(textFieldBuffer.data()),
                           false);
        }
        break;
    }
    default:
        break;
    }

    ImGui::EndDisabled();
}

void Inspector::State::drawFieldRow(Impl& shell, ui::UiKit& kit, const TypeReflection& compReflection,
                                    const FieldInfo& field, std::string_view fieldPath,
                                    std::string_view fieldId, usize baseOffset, bool readOnly)
{
    ui::FrameText& text = shell.text;
    const bool fieldReadOnly = readOnly || field.meta.readOnly;
    const char* label = displayLabel(text, field.meta.displayName, field.name);
    const char* tooltip = field.meta.description.empty() ? nullptr : text.copy(field.meta.description);

    // Nested struct: recurse into its fields with slash-separated path.
    if (field.type == FieldType::Struct && field.structType != nullptr)
    {
        for (const FieldInfo& subField : field.structType->fields)
        {
            if (subField.meta.hidden)
            {
                continue;
            }
            const char* subPath = text.concat(fieldPath, text.concat("/", subField.name));
            const char* subId = text.concat(fieldId, text.concat(".", subField.name));
            drawFieldRow(shell, kit, compReflection, subField, subPath, subId, baseOffset + field.offset,
                         fieldReadOnly);
        }
        return;
    }

    // Gather pointers to this field across all inspected entities and check if values are mixed.
    const usize count = componentPtrs.size();
    const void* stackPtrs[16];
    std::unique_ptr<const void*[]> heapPtrs;
    const void** ptrs = stackPtrs;
    if (count > 16)
    {
        heapPtrs = std::make_unique<const void*[]>(count);
        ptrs = heapPtrs.get();
    }
    bool mixed = false;
    for (usize i = 0; i < count; ++i)
    {
        ptrs[i] = static_cast<const std::byte*>(componentPtrs[i]) + baseOffset + field.offset;
        if (i > 0 && !valuesEqual(field, ptrs[0], ptrs[i]))
        {
            mixed = true;
        }
    }

    // Optional<T>: checkbox to toggle presence, plus inner widget when present.
    if (field.type == FieldType::Optional && field.element != nullptr && field.container != nullptr)
    {
        ui::propertyLabel(kit, label, tooltip);
        if (ImGui::BeginPopupContextItem(text.hidden(text.concat(fieldId, ".context"))))
        {
            if (ImGui::MenuItem(text.label(s::kInspectorReset, text.concat(fieldId, ".reset"))))
            {
                resetFieldPath(shell, compReflection.name, fieldPath);
            }
            ImGui::EndPopup();
        }
        bool hasValue = field.container->size(ptrs[0]) > 0;
        ImGui::BeginDisabled(fieldReadOnly);
        if (ImGui::Checkbox(text.hidden(text.concat(fieldId, ".has")), &hasValue))
        {
            if (!hasValue)
            {
                applyFieldJson(shell, compReflection.name, fieldPath, "null", false);
            }
            else
            {
                FieldValue temp(field);
                field.container->resize(temp.data(), 1);
                JsonMutDocument doc;
                GuidEntityResolver resolver(shell.editor.entities());
                TypeReflection wrapper{};
                FieldInfo fakeField = field;
                fakeField.offset = 0;
                wrapper.fields = std::span<const FieldInfo>(&fakeField, 1);
                wrapper.size = field.size;
                wrapper.alignment = field.alignment;
                const auto written = getPath(doc, wrapper, temp.data(), field.name, {.entities = &resolver});
                std::string json = "null";
                if (written)
                {
                    doc.setRoot(*written);
                    (void)doc.write(json, JsonWriteOptions{.pretty = false});
                }
                applyFieldJson(shell, compReflection.name, fieldPath, json, false);
            }
        }
        ImGui::EndDisabled();
        if (hasValue && !mixed)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            const void* elemPtr = field.container->constElement(ptrs[0], 0);
            drawValueWidget(shell, kit, compReflection, *field.element, field.meta, fieldPath,
                            text.concat(fieldId, ".value"), std::span<const void* const>(&elemPtr, 1), false,
                            fieldReadOnly);
        }
        return;
    }

    // Array<T> / FixedArray<T>: header row with element count (and + button for Array), then element rows.
    if ((field.type == FieldType::Array || field.type == FieldType::FixedArray) && field.element != nullptr)
    {
        ui::propertyLabel(kit, label, tooltip);
        if (ImGui::BeginPopupContextItem(text.hidden(text.concat(fieldId, ".context"))))
        {
            if (ImGui::MenuItem(text.label(s::kInspectorReset, text.concat(fieldId, ".reset"))))
            {
                resetFieldPath(shell, compReflection.name, fieldPath);
            }
            ImGui::EndPopup();
        }
        const usize elemCount = field.type == FieldType::FixedArray ? static_cast<usize>(field.count)
                                                                    : field.container->size(ptrs[0]);
        ImGui::BeginDisabled(fieldReadOnly);
        if (field.type == FieldType::Array && !mixed)
        {
            if (ui::iconButton(kit, ui::icons::kAdd, text.concat(fieldId, ".add")))
            {
                FieldValue copy(field, ptrs[0]);
                field.container->resize(copy.data(), elemCount + 1);
                TypeReflection wrapper{};
                FieldInfo fakeField = field;
                fakeField.offset = 0;
                wrapper.fields = std::span<const FieldInfo>(&fakeField, 1);
                wrapper.size = field.size;
                wrapper.alignment = field.alignment;
                const std::string json =
                    serializeComponentPath(shell.editor.entities(), wrapper, copy.data(), field.name);
                applyFieldJson(shell, compReflection.name, fieldPath, json, false);
            }
        }
        ImGui::EndDisabled();

        if (!mixed)
        {
            for (usize idx = 0; idx < elemCount; ++idx)
            {
                const std::string_view idxText = formatUint(text, idx);
                const char* elemPath = text.concat(fieldPath, text.concat("/", idxText));
                const char* elemId = text.concat(fieldId, text.concat(".", idxText));
                ui::propertyLabel(kit, text.concat("  [", text.concat(idxText, "]")));
                const void* elemPtr = field.type == FieldType::FixedArray
                                          ? static_cast<const std::byte*>(ptrs[0]) + idx * field.element->size
                                          : field.container->constElement(ptrs[0], idx);
                if (field.type == FieldType::Array && !fieldReadOnly)
                {
                    const f32 buttonSize = ImGui::GetFrameHeight();
                    const f32 avail = ImGui::GetContentRegionAvail().x;
                    ImGui::SetNextItemWidth(
                        std::max(40.0f, avail - buttonSize - ImGui::GetStyle().ItemInnerSpacing.x));
                }
                drawValueWidget(shell, kit, compReflection, *field.element, field.meta, elemPath, elemId,
                                std::span<const void* const>(&elemPtr, 1), false, fieldReadOnly);
                if (field.type == FieldType::Array && !fieldReadOnly)
                {
                    ImGui::SameLine();
                    if (ui::iconButton(kit, ui::icons::kRemove, text.concat(elemId, ".remove")))
                    {
                        FieldValue copy(field, ptrs[0]);
                        for (usize moveIdx = idx; moveIdx + 1 < elemCount; ++moveIdx)
                        {
                            field.element->ops->assign(
                                field.container->element(copy.data(), moveIdx),
                                field.container->constElement(copy.data(), moveIdx + 1));
                        }
                        field.container->resize(copy.data(), elemCount - 1);
                        TypeReflection wrapper{};
                        FieldInfo fakeField = field;
                        fakeField.offset = 0;
                        wrapper.fields = std::span<const FieldInfo>(&fakeField, 1);
                        wrapper.size = field.size;
                        wrapper.alignment = field.alignment;
                        const std::string json =
                            serializeComponentPath(shell.editor.entities(), wrapper, copy.data(), field.name);
                        applyFieldJson(shell, compReflection.name, fieldPath, json, false);
                        break;
                    }
                }
            }
        }
        return;
    }

    ui::propertyLabel(kit, label, tooltip);
    if (!fieldReadOnly && ImGui::BeginPopupContextItem(text.hidden(text.concat(fieldId, ".context"))))
    {
        if (ImGui::MenuItem(text.label(s::kInspectorReset, text.concat(fieldId, ".reset"))))
        {
            resetFieldPath(shell, compReflection.name, fieldPath);
        }
        ImGui::EndPopup();
    }
    drawValueWidget(shell, kit, compReflection, field, field.meta, fieldPath, fieldId,
                    std::span<const void* const>(ptrs, count), mixed, fieldReadOnly);
}

void Inspector::State::drawComponentCard(Impl& shell, ui::UiKit& kit, ComponentId compId,
                                         const TypeReflection& reflection)
{
    const World& world = shell.editor.world();
    ui::FrameText& text = shell.text;

    componentPtrs.clear();
    for (const InspectedEntity& item : inspected)
    {
        const void* ptr = world.componentData(item.entity, compId);
        if (ptr == nullptr && reflection.size > 0)
        {
            return;
        }
        componentPtrs.push_back(ptr);
    }

    const char* cardId = text.concat("inspector.card.", reflection.name);
    const ui::Icon icon = ui::findIcon(reflection.meta.icon).value_or(ui::icons::kExtension);
    const char* title = displayLabel(text, reflection.meta.displayName, reflection.name);
    const bool open = ui::beginComponentCard(kit, cardId, icon, title, nullptr);

    if (ui::beginComponentCardMenu(kit, cardId))
    {
        const bool isTransient = reflection.meta.transient;
        ImGui::BeginDisabled(isTransient);
        if (ImGui::MenuItem(text.iconLabel(ui::icons::kRestartAlt, s::kInspectorReset,
                                           text.concat(cardId, ".menu.reset"))))
        {
            if (inspected.size() == 1)
            {
                const auto hex = formatGuid(inspected[0].guid);
                const std::string params =
                    "{\"entity\":" + jsonQuote(std::string_view(hex.data(), hex.size())) +
                    ",\"type\":" + jsonQuote(reflection.name) + "}";
                (void)shell.run("component.reset", params, "inspector");
            }
            else
            {
                std::string steps = "{\"steps\":[";
                for (usize i = 0; i < inspected.size(); ++i)
                {
                    if (i > 0)
                    {
                        steps.push_back(',');
                    }
                    const auto hex = formatGuid(inspected[i].guid);
                    steps += "{\"method\":\"component.reset\",\"params\":{\"entity\":" +
                             jsonQuote(std::string_view(hex.data(), hex.size())) +
                             ",\"type\":" + jsonQuote(reflection.name) + "}}";
                }
                steps += "]}";
                (void)shell.run("editor.batch", steps, "inspector");
            }
        }
        if (ImGui::MenuItem(text.iconLabel(ui::icons::kContentCopy, s::kInspectorCopy,
                                           text.concat(cardId, ".menu.copy"))))
        {
            const auto hex = formatGuid(inspected[0].guid);
            const std::string params = "{\"entity\":" + jsonQuote(std::string_view(hex.data(), hex.size())) +
                                       ",\"type\":" + jsonQuote(reflection.name) + "}";
            (void)shell.run("component.copy", params, "inspector");
        }
        const bool canPaste = shell.editor.componentClipboard().type == reflection.name;
        ImGui::BeginDisabled(!canPaste);
        if (ImGui::MenuItem(text.iconLabel(ui::icons::kContentPaste, s::kInspectorPasteValues,
                                           text.concat(cardId, ".menu.paste"))))
        {
            (void)shell.run("component.paste", "{\"entities\":" + entitiesJsonArray() + "}", "inspector");
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem(
                text.iconLabel(ui::icons::kDelete, s::kInspectorRemove, text.concat(cardId, ".menu.remove"))))
        {
            if (inspected.size() == 1)
            {
                const auto hex = formatGuid(inspected[0].guid);
                const std::string params =
                    "{\"entity\":" + jsonQuote(std::string_view(hex.data(), hex.size())) +
                    ",\"type\":" + jsonQuote(reflection.name) + "}";
                (void)shell.run("component.remove", params, "inspector");
            }
            else
            {
                std::string steps = "{\"steps\":[";
                for (usize i = 0; i < inspected.size(); ++i)
                {
                    if (i > 0)
                    {
                        steps.push_back(',');
                    }
                    const auto hex = formatGuid(inspected[i].guid);
                    steps += "{\"method\":\"component.remove\",\"params\":{\"entity\":" +
                             jsonQuote(std::string_view(hex.data(), hex.size())) +
                             ",\"type\":" + jsonQuote(reflection.name) + "}}";
                }
                steps += "]}";
                (void)shell.run("editor.batch", steps, "inspector");
            }
            ui::endComponentCardMenu();
            if (open)
            {
                ui::endComponentCard();
            }
            return;
        }
        ImGui::EndDisabled();
        if (!reflection.meta.docsAnchor.empty())
        {
            ImGui::Separator();
            if (ImGui::MenuItem(
                    text.iconLabel(ui::icons::kHelp, s::kInspectorDocs, text.concat(cardId, ".menu.docs"))))
            {
                ImGui::SetClipboardText(text.copy(reflection.meta.docsAnchor));
            }
        }
        ui::endComponentCardMenu();
    }

    if (!open)
    {
        return;
    }

    drawValidationWarnings(shell, kit, reflection, cardId);

    const void* primaryData = componentPtrs.empty() ? nullptr : componentPtrs[0];
    bool hasVisibleFields = false;
    for (const FieldInfo& field : reflection.fields)
    {
        if (shouldShowComponentField(reflection, primaryData, field))
        {
            hasVisibleFields = true;
            break;
        }
    }

    if (hasVisibleFields && ui::beginPropertyTable(kit, text.concat(cardId, ".table")))
    {
        for (const FieldInfo& field : reflection.fields)
        {
            if (!shouldShowComponentField(reflection, primaryData, field))
            {
                continue;
            }
            const char* fieldId = text.concat(cardId, text.concat(".", field.name));
            drawFieldRow(shell, kit, reflection, field, field.name, fieldId, 0, reflection.meta.transient);
        }
        ui::endPropertyTable();
    }

    ui::endComponentCard();
}

void Inspector::State::drawAddComponent(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    const ComponentRegistry& registry = shell.editor.world().registry();

    ImGui::Spacing();
    if (ui::primaryButton(
            kit, text.iconLabel(ui::icons::kAdd, s::kInspectorAddComponent, "inspector.add_component")))
    {
        addSearchBuffer[0] = '\0';
        focusAddSearch = true;
        ImGui::OpenPopup("###inspector.add_component.popup");
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(260.0f * shell.scale, 120.0f * shell.scale),
                                        ImVec2(420.0f * shell.scale, 360.0f * shell.scale));
    if (!ImGui::BeginPopup("###inspector.add_component.popup"))
    {
        return;
    }

    if (focusAddSearch)
    {
        ImGui::SetKeyboardFocusHere();
        focusAddSearch = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    (void)ui::searchField(kit, "inspector.add_component.search", addSearchBuffer,
                          text.format(s::kInspectorAddComponentSearch));
    const bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter, false, ImGuiKeyOwner_Any) ||
                              ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false, ImGuiKeyOwner_Any);

    const std::string_view query(addSearchBuffer.data());
    text::FuzzyMatcher matcher(query);
    addCandidates.clear();

    for (ComponentId id = 0; id < registry.count(); ++id)
    {
        const TypeReflection* reflection = registry.reflection(id);
        if (reflection == nullptr || reflection->meta.transient || isIntrinsicComponent(id, reflection->name))
        {
            continue;
        }
        const char* display = displayLabel(text, reflection->meta.displayName, reflection->name);
        i32 score = 0;
        if (!query.empty())
        {
            const std::optional<i32> s1 = matcher.score(display);
            const std::optional<i32> s2 = matcher.score(reflection->name);
            const std::optional<i32> s3 = matcher.score(reflection->meta.category);
            if (!s1 && !s2 && !s3)
            {
                continue;
            }
            score = std::max({s1.value_or(-100000), s2.value_or(-100000), s3.value_or(-100000)});
        }
        addCandidates.push_back(CandidateComponent{id, reflection, score});
    }

    std::stable_sort(addCandidates.begin(), addCandidates.end(),
                     [](const CandidateComponent& a, const CandidateComponent& b) {
                         if (a.score != b.score)
                         {
                             return a.score > b.score;
                         }
                         return a.reflection->name < b.reflection->name;
                     });

    const auto triggerAdd = [&](const TypeReflection& chosen) {
        if (inspected.size() == 1)
        {
            const auto hex = formatGuid(inspected[0].guid);
            const std::string params = "{\"entity\":" + jsonQuote(std::string_view(hex.data(), hex.size())) +
                                       ",\"type\":" + jsonQuote(chosen.name) + "}";
            (void)shell.run("component.add", params, "inspector");
        }
        else
        {
            std::string steps = "{\"steps\":[";
            for (usize i = 0; i < inspected.size(); ++i)
            {
                if (i > 0)
                {
                    steps.push_back(',');
                }
                const auto hex = formatGuid(inspected[i].guid);
                steps += "{\"method\":\"component.add\",\"params\":{\"entity\":" +
                         jsonQuote(std::string_view(hex.data(), hex.size())) +
                         ",\"type\":" + jsonQuote(chosen.name) + "}}";
            }
            steps += "]}";
            (void)shell.run("editor.batch", steps, "inspector");
        }
        ImGui::CloseCurrentPopup();
    };

    ImGui::Separator();
    if (enterPressed && !addCandidates.empty())
    {
        triggerAdd(*addCandidates[0].reflection);
        ImGui::EndPopup();
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 catColor =
        ImGui::ColorConvertFloat4ToU32(imvec(kit.theme.color(ui::ThemeToken::TextDisabled)));
    const f32 padX = ImGui::GetStyle().FramePadding.x;
    for (usize i = 0; i < addCandidates.size(); ++i)
    {
        const TypeReflection& refl = *addCandidates[i].reflection;
        const ui::Icon icon = ui::findIcon(refl.meta.icon).value_or(ui::icons::kExtension);
        const char* display = displayLabel(text, refl.meta.displayName, refl.name);
        const char* itemId = text.concat("inspector.add_component.item.", refl.name);
        const char* label = text.verbatim(text.concat(icon.view(), text.concat("  ", display)), itemId);
        if (ImGui::Selectable(label, i == 0 && !query.empty()))
        {
            triggerAdd(refl);
            break;
        }
        if (!refl.meta.category.empty())
        {
            const char* catText = text.copy(refl.meta.category);
            const ImVec2 catSize = ImGui::CalcTextSize(catText);
            const ImVec2 rMin = ImGui::GetItemRectMin();
            const ImVec2 rMax = ImGui::GetItemRectMax();
            const ImVec2 pos(rMax.x - catSize.x - padX, rMin.y + (rMax.y - rMin.y - catSize.y) * 0.5f);
            drawList->AddText(pos, catColor, catText);
        }
    }

    ImGui::EndPopup();
}

Inspector::Inspector()
    : m_state(std::make_unique<State>())
{
}

Inspector::~Inspector() = default;

void Inspector::draw(Impl& shell)
{
    ILL_PROFILE_SCOPE();
    State& state = *m_state;
    EditorContext& editor = shell.editor;
    const World& world = editor.world();
    EntityIndex& entityIndex = editor.entities();
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;

    // Resolve the list of alive entities to inspect (either locked or current selection).
    state.inspected.clear();
    const std::span<const EntityGuid> sourceGuids =
        state.locked ? std::span<const EntityGuid>(state.lockedGuids) : editor.selection().items();
    for (const EntityGuid guid : sourceGuids)
    {
        const Entity entity = entityIndex.find(guid);
        if (!entity.isNull() && world.isAlive(entity))
        {
            state.inspected.push_back(State::InspectedEntity{entity, guid});
        }
    }

    if (state.inspected.empty())
    {
        if (state.locked)
        {
            if (ui::iconButton(kit, ui::icons::kLock, "inspector.lock", text.format(s::kInspectorLock),
                               nullptr, true))
            {
                state.locked = false;
                state.lockedGuids.clear();
            }
        }
        ui::emptyState(kit, ui::icons::kTune, text.format(s::kInspectorEmptyTitle),
                       text.format(s::kInspectorEmptyHint));
        if (!ImGui::IsAnyItemActive())
        {
            ++state.dragSerial;
        }
        return;
    }

    state.drawHeader(shell, kit);
    state.drawTransformCard(shell, kit);

    // Find non-intrinsic reflected components present on all inspected entities, sorted by stable name.
    state.commonComponents.clear();
    const ComponentRegistry& registry = world.registry();
    const std::span<const ComponentId> firstComps = world.componentsOf(state.inspected[0].entity);
    for (const ComponentId compId : firstComps)
    {
        const TypeReflection* reflection = registry.reflection(compId);
        if (reflection == nullptr || isIntrinsicComponent(compId, reflection->name))
        {
            continue;
        }
        bool presentOnAll = true;
        for (usize i = 1; i < state.inspected.size(); ++i)
        {
            const std::span<const ComponentId> otherComps = world.componentsOf(state.inspected[i].entity);
            if (std::find(otherComps.begin(), otherComps.end(), compId) == otherComps.end())
            {
                presentOnAll = false;
                break;
            }
        }
        if (presentOnAll)
        {
            state.commonComponents.push_back(compId);
        }
    }
    std::sort(state.commonComponents.begin(), state.commonComponents.end(),
              [&](ComponentId a, ComponentId b) {
                  return registry.reflection(a)->name < registry.reflection(b)->name;
              });

    for (const ComponentId compId : state.commonComponents)
    {
        if (const TypeReflection* reflection = registry.reflection(compId))
        {
            state.drawComponentCard(shell, kit, compId, *reflection);
        }
    }

    state.drawAddComponent(shell, kit);

    if (!ImGui::IsAnyItemActive())
    {
        ++state.dragSerial;
    }
}

} // namespace illusion::editor::shell::detail
