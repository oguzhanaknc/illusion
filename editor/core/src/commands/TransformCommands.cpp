#include "CommandSupport.hpp"

#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace illusion::editor::detail
{
namespace
{

// The parent whose space the entity's LocalTransform is in, or null when the entity is placed in world space.
Entity placingParent(World& world, Entity entity)
{
    if (!world.has<LocalTransform>(entity) || !world.has<Hierarchy>(entity))
    {
        return Entity{};
    }
    return parentOf(world, entity);
}

} // namespace

Transform worldTransformOf(World& world, Entity entity)
{
    std::vector<Entity> chain;
    for (Entity current = entity; !current.isNull() && chain.size() <= world.aliveCount();
         current = placingParent(world, current))
    {
        chain.push_back(current);
    }
    const Transform* top = world.tryGet<Transform>(chain.back());
    Transform placed = top != nullptr ? *top : Transform{};
    for (auto it = chain.rbegin() + 1; it != chain.rend(); ++it)
    {
        placed = combine(placed, world.get<LocalTransform>(*it).asTransform());
    }
    return placed;
}

void dropDescendants(World& world, std::vector<Entity>& entities)
{
    std::unordered_set<u64> listed;
    for (const Entity entity : entities)
    {
        listed.insert(entity.toBits());
    }
    std::erase_if(entities, [&](Entity entity) {
        usize steps = 0;
        for (Entity parent = world.has<Hierarchy>(entity) ? parentOf(world, entity) : Entity{};
             !parent.isNull() && steps <= world.aliveCount();
             parent = world.has<Hierarchy>(parent) ? parentOf(world, parent) : Entity{}, ++steps)
        {
            if (listed.contains(parent.toBits()))
            {
                return true;
            }
        }
        return false;
    });
}

namespace
{

Transform parentWorldOf(World& world, Entity entity)
{
    const Entity parent = placingParent(world, entity);
    return parent.isNull() ? Transform{} : worldTransformOf(world, parent);
}

bool finite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const Transform& transform) noexcept
{
    return finite(transform.position) && finite(transform.scale) && std::isfinite(transform.rotation.x) &&
           std::isfinite(transform.rotation.y) && std::isfinite(transform.rotation.z) &&
           std::isfinite(transform.rotation.w);
}

Expected<void, CommandError> assign(CommandContext& context, Entity entity, ComponentId type,
                                    const void* value)
{
    Expected<bool, CommandError> assigned = context.edit().assignComponent(entity, type, value);
    if (!assigned)
    {
        return unexpected(std::move(assigned).error());
    }
    return {};
}

// Places @p entity at @p local in its parent's space (world space for entities placed in world space).
Expected<void, CommandError> placeLocal(CommandContext& context, Entity entity, const Transform& local,
                                        std::string_view pointer)
{
    if (!finite(local))
    {
        return unexpected(
            CommandError::invalid(std::string(pointer), "gives a placement that is not finite"));
    }
    World& world = context.world();
    const Entity parent = placingParent(world, entity);
    if (!parent.isNull())
    {
        const LocalTransform value = LocalTransform::from(local);
        if (Expected<void, CommandError> done =
                assign(context, entity, componentId<LocalTransform>(), &value);
            !done)
        {
            return done;
        }
        if (world.has<Transform>(entity))
        {
            const Transform placed = combine(worldTransformOf(world, parent), local);
            return assign(context, entity, componentId<Transform>(), &placed);
        }
        return {};
    }
    if (!world.has<Transform>(entity))
    {
        Expected<std::vector<std::string>, CommandError> added =
            context.edit().addComponent(entity, componentId<Transform>());
        if (!added)
        {
            return unexpected(std::move(added).error());
        }
    }
    if (Expected<void, CommandError> done = assign(context, entity, componentId<Transform>(), &local); !done)
    {
        return done;
    }
    if (world.has<LocalTransform>(entity))
    {
        // A root keeps its LocalTransform equal to its Transform, as updateTransformHierarchy() does.
        const LocalTransform value = LocalTransform::from(local);
        return assign(context, entity, componentId<LocalTransform>(), &value);
    }
    return {};
}

// Places @p entity at @p placed in world space.
Expected<void, CommandError> placeWorld(CommandContext& context, Entity entity, const Transform& placed,
                                        std::string_view pointer)
{
    World& world = context.world();
    const Entity parent = placingParent(world, entity);
    if (parent.isNull())
    {
        return placeLocal(context, entity, placed, pointer);
    }
    Transform local = relativeTo(placed, worldTransformOf(world, parent));
    local.rotation = glm::normalize(local.rotation);
    return placeLocal(context, entity, local, pointer);
}

// The local placement of @p entity: its LocalTransform for children, its world placement otherwise.
Transform localOf(World& world, Entity entity)
{
    return placingParent(world, entity).isNull() ? worldTransformOf(world, entity)
                                                 : world.get<LocalTransform>(entity).asTransform();
}

// Entities in hierarchy pre-order (parents before children), the others after them in the given order.
void sortParentsFirst(CommandContext& context, std::vector<Entity>& entities)
{
    const HierarchyIndex& hierarchy = context.editor().entities().hierarchy();
    std::unordered_map<u64, usize> position;
    const std::span<const Entity> order = hierarchy.preOrder();
    for (usize i = 0; i < order.size(); ++i)
    {
        position.emplace(order[i].toBits(), i);
    }
    std::stable_sort(entities.begin(), entities.end(), [&](Entity a, Entity b) {
        const auto pa = position.find(a.toBits());
        const auto pb = position.find(b.toBits());
        const usize ia = pa != position.end() ? pa->second : order.size();
        const usize ib = pb != position.end() ? pb->second : order.size();
        return ia < ib;
    });
}

Expected<Quat, CommandError> rotationParam(const std::optional<Quat>& rotation,
                                           const std::optional<Vec3>& euler, bool required)
{
    if (rotation && euler)
    {
        return unexpected(CommandError::invalid("/euler", "cannot be given together with rotation"));
    }
    if (euler)
    {
        return quatFromEuler(glm::radians(*euler));
    }
    if (!rotation)
    {
        if (required)
        {
            return unexpected(CommandError::invalid("/rotation", "is required (or euler)"));
        }
        return Quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const f32 length = glm::length(*rotation);
    if (!(length > 1e-6f) || !std::isfinite(length))
    {
        return unexpected(CommandError::invalid("/rotation", "must be a quaternion of non-zero length"));
    }
    return *rotation / length;
}

Expected<Vec3, CommandError> pivotOf(World& world, const std::vector<Entity>& entities,
                                     const std::optional<TransformPivot>& pivot,
                                     const std::optional<Vec3>& point, bool& individual)
{
    if (point && pivot)
    {
        return unexpected(CommandError::invalid("/point", "cannot be given together with pivot"));
    }
    individual = false;
    if (point)
    {
        return *point;
    }
    if (pivot.value_or(TransformPivot::Individual) == TransformPivot::Individual)
    {
        individual = true;
        return Vec3(0.0f);
    }
    Vec3 sum(0.0f);
    for (const Entity entity : entities)
    {
        sum += worldTransformOf(world, entity).position;
    }
    return sum / static_cast<f32>(entities.size());
}

Expected<TransformGetResult, CommandError> transformGet(CommandContext& context,
                                                        const TransformGetParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    const TransformSpace space = params.space.value_or(TransformSpace::World);
    if (space == TransformSpace::Self)
    {
        return unexpected(CommandError::invalid("/space", "must be \"world\" or \"local\""));
    }
    const Transform placed = space == TransformSpace::World ? worldTransformOf(context.world(), *entity)
                                                            : localOf(context.world(), *entity);
    TransformGetResult result;
    result.entity = context.summary(*entity);
    result.position = placed.position;
    result.rotation = placed.rotation;
    result.euler = glm::degrees(eulerFromQuat(placed.rotation));
    result.scale = placed.scale;
    return result;
}

Expected<EntitiesResult, CommandError> transformSet(CommandContext& context, const TransformSetParams& params)
{
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, params.entities);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    const TransformSpace space = params.space.value_or(TransformSpace::World);
    if (space == TransformSpace::Self)
    {
        return unexpected(CommandError::invalid("/space", "must be \"world\" or \"local\""));
    }
    if (!params.position && !params.rotation && !params.euler && !params.scale)
    {
        return unexpected(CommandError::invalid("/position", "or rotation, euler or scale is required"));
    }
    std::optional<Quat> rotation;
    if (params.rotation || params.euler)
    {
        Expected<Quat, CommandError> parsed = rotationParam(params.rotation, params.euler, true);
        if (!parsed)
        {
            return unexpected(std::move(parsed).error());
        }
        rotation = *parsed;
    }
    sortParentsFirst(context, *entities);
    World& world = context.world();
    EntitiesResult result;
    for (const Entity entity : *entities)
    {
        Transform placed =
            space == TransformSpace::World ? worldTransformOf(world, entity) : localOf(world, entity);
        placed.position = params.position.value_or(placed.position);
        placed.rotation = rotation.value_or(placed.rotation);
        placed.scale = params.scale.value_or(placed.scale);
        Expected<void, CommandError> done = space == TransformSpace::World
                                                ? placeWorld(context, entity, placed, "/position")
                                                : placeLocal(context, entity, placed, "/position");
        if (!done)
        {
            return unexpected(std::move(done).error());
        }
        result.entities.push_back(context.summary(entity));
    }
    return result;
}

Expected<EntitiesResult, CommandError> transformTranslate(CommandContext& context,
                                                          const TransformTranslateParams& params)
{
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, params.entities);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    World& world = context.world();
    dropDescendants(world, *entities);
    const TransformSpace space = params.space.value_or(TransformSpace::World);
    EntitiesResult result;
    for (const Entity entity : *entities)
    {
        Expected<void, CommandError> done;
        if (space == TransformSpace::Local)
        {
            Transform local = localOf(world, entity);
            local.position += params.offset;
            done = placeLocal(context, entity, local, "/offset");
        }
        else
        {
            Transform placed = worldTransformOf(world, entity);
            placed.position +=
                space == TransformSpace::Self ? placed.rotation * params.offset : params.offset;
            done = placeWorld(context, entity, placed, "/offset");
        }
        if (!done)
        {
            return unexpected(std::move(done).error());
        }
        result.entities.push_back(context.summary(entity));
    }
    return result;
}

Expected<EntitiesResult, CommandError> transformRotate(CommandContext& context,
                                                       const TransformRotateParams& params)
{
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, params.entities);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    Expected<Quat, CommandError> rotation = rotationParam(params.rotation, params.euler, true);
    if (!rotation)
    {
        return unexpected(std::move(rotation).error());
    }
    World& world = context.world();
    dropDescendants(world, *entities);
    bool individual = true;
    Expected<Vec3, CommandError> pivot = pivotOf(world, *entities, params.pivot, params.point, individual);
    if (!pivot)
    {
        return unexpected(std::move(pivot).error());
    }
    const TransformSpace space = params.space.value_or(TransformSpace::World);
    EntitiesResult result;
    for (const Entity entity : *entities)
    {
        Transform placed = worldTransformOf(world, entity);
        // The rotation in world space: about the world axes, the parent's axes or the entity's own.
        Quat turn = *rotation;
        if (space == TransformSpace::Local)
        {
            const Quat parent = parentWorldOf(world, entity).rotation;
            turn = parent * turn * glm::inverse(parent);
        }
        else if (space == TransformSpace::Self)
        {
            turn = placed.rotation * turn * glm::inverse(placed.rotation);
        }
        const Vec3 center = individual ? placed.position : *pivot;
        placed.position = center + turn * (placed.position - center);
        placed.rotation = glm::normalize(turn * placed.rotation);
        if (Expected<void, CommandError> done = placeWorld(context, entity, placed, "/rotation"); !done)
        {
            return unexpected(std::move(done).error());
        }
        result.entities.push_back(context.summary(entity));
    }
    return result;
}

Expected<EntitiesResult, CommandError> transformScale(CommandContext& context,
                                                      const TransformScaleParams& params)
{
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, params.entities);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    World& world = context.world();
    dropDescendants(world, *entities);
    bool individual = true;
    Expected<Vec3, CommandError> pivot = pivotOf(world, *entities, params.pivot, params.point, individual);
    if (!pivot)
    {
        return unexpected(std::move(pivot).error());
    }
    EntitiesResult result;
    for (const Entity entity : *entities)
    {
        Expected<void, CommandError> done;
        if (individual)
        {
            // The entity's own scale changes; its position stays (exact for children).
            Transform local = localOf(world, entity);
            local.scale *= params.factor;
            done = placeLocal(context, entity, local, "/factor");
        }
        else
        {
            Transform placed = worldTransformOf(world, entity);
            placed.position = *pivot + (placed.position - *pivot) * params.factor;
            placed.scale *= params.factor;
            done = placeWorld(context, entity, placed, "/factor");
        }
        if (!done)
        {
            return unexpected(std::move(done).error());
        }
        result.entities.push_back(context.summary(entity));
    }
    return result;
}

Expected<EntitiesResult, CommandError> transformReset(CommandContext& context,
                                                      const TransformResetParams& params)
{
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, params.entities);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    sortParentsFirst(context, *entities);
    World& world = context.world();
    const Transform identity;
    EntitiesResult result;
    for (const Entity entity : *entities)
    {
        Transform local = localOf(world, entity);
        if (params.position.value_or(true))
        {
            local.position = identity.position;
        }
        if (params.rotation.value_or(true))
        {
            local.rotation = identity.rotation;
        }
        if (params.scale.value_or(true))
        {
            local.scale = identity.scale;
        }
        if (Expected<void, CommandError> done = placeLocal(context, entity, local, "/entities"); !done)
        {
            return unexpected(std::move(done).error());
        }
        result.entities.push_back(context.summary(entity));
    }
    return result;
}

Expected<EntitiesResult, CommandError> transformSnapToGrid(CommandContext& context,
                                                           const TransformSnapParams& params)
{
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, params.entities);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    World& world = context.world();
    dropDescendants(world, *entities);
    const f32 increment = params.increment.value_or(context.editor().gizmo().snap.translate);
    EntitiesResult result;
    for (const Entity entity : *entities)
    {
        Transform placed = worldTransformOf(world, entity);
        placed.position =
            Vec3(snapToIncrement(placed.position.x, increment), snapToIncrement(placed.position.y, increment),
                 snapToIncrement(placed.position.z, increment));
        if (Expected<void, CommandError> done = placeWorld(context, entity, placed, "/entities"); !done)
        {
            return unexpected(std::move(done).error());
        }
        result.entities.push_back(context.summary(entity));
    }
    return result;
}

// Enum params also accept raw numbers: reject values outside the enum.
template <class E>
Expected<void, CommandError> checkEnum(const std::optional<E>& value, E last, std::string pointer)
{
    if (value && static_cast<usize>(*value) > static_cast<usize>(last))
    {
        return unexpected(CommandError::invalid(std::move(pointer), "is not one of the allowed values"));
    }
    return {};
}

// The entities a placing command moves (descendants of others dropped) with the world-space box around what
// each draws with its descendants: the Mesh boxes of the viewports, or its position when it draws nothing
// (or the editor has no viewports).
struct Arranged
{
    std::vector<Entity> entities;
    std::vector<Aabb> boxes;
    std::vector<Aabb> others; // Mesh boxes of the entities that do not move
};

Expected<Arranged, CommandError> arranged(CommandContext& context, const std::vector<std::string>& references)
{
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, references);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    World& world = context.world();
    dropDescendants(world, *entities);
    std::vector<ViewportBounds> meshes;
    if (ViewportHost* host = context.editor().viewports())
    {
        host->meshBounds(meshes);
    }
    std::unordered_map<u64, usize> owner; // entity bits -> index in entities of the moved root
    const HierarchyIndex& hierarchy = context.editor().entities().hierarchy();
    for (usize i = 0; i < entities->size(); ++i)
    {
        const Entity root = (*entities)[i];
        const std::span<const Entity> members =
            hierarchy.contains(root) ? hierarchy.subtree(root) : std::span<const Entity>(&root, 1);
        for (const Entity member : members)
        {
            owner.emplace(member.toBits(), i);
        }
    }
    Arranged out;
    out.boxes.resize(entities->size());
    for (const ViewportBounds& mesh : meshes)
    {
        const auto found = owner.find(mesh.entity.toBits());
        if (found != owner.end())
        {
            out.boxes[found->second].expand(mesh.bounds);
        }
        else
        {
            out.others.push_back(mesh.bounds);
        }
    }
    for (usize i = 0; i < entities->size(); ++i)
    {
        if (!out.boxes[i].isValid())
        {
            out.boxes[i].expand(worldTransformOf(world, (*entities)[i]).position);
        }
    }
    out.entities = std::move(*entities);
    return out;
}

// Moves each arranged entity by offsets[i] (world units) and lists them.
Expected<EntitiesResult, CommandError> moveArranged(CommandContext& context, const Arranged& arranged,
                                                    std::span<const Vec3> offsets)
{
    World& world = context.world();
    EntitiesResult result;
    for (usize i = 0; i < arranged.entities.size(); ++i)
    {
        const Entity entity = arranged.entities[i];
        if (offsets[i] != Vec3(0.0f))
        {
            Transform placed = worldTransformOf(world, entity);
            placed.position += offsets[i];
            if (Expected<void, CommandError> done = placeWorld(context, entity, placed, "/entities"); !done)
            {
                return unexpected(std::move(done).error());
            }
        }
        result.entities.push_back(context.summary(entity));
    }
    return result;
}

Expected<EntitiesResult, CommandError> transformAlign(CommandContext& context,
                                                      const TransformAlignParams& params)
{
    if (Expected<void, CommandError> valid = checkEnum(std::optional(params.axis), TransformAxis::Z, "/axis");
        !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<void, CommandError> valid = checkEnum(params.edge, AlignEdge::Max, "/edge"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<void, CommandError> valid = checkEnum(params.to, AlignTarget::Active, "/to"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<Arranged, CommandError> found = arranged(context, params.entities);
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    Aabb target;
    if (params.to.value_or(AlignTarget::Selection) == AlignTarget::Active)
    {
        target = found->boxes.back();
    }
    else
    {
        for (const Aabb& box : found->boxes)
        {
            target.expand(box);
        }
    }
    const u32 axis = static_cast<u32>(params.axis);
    std::vector<f32> along(found->boxes.size());
    alignOffsets(found->boxes, axis, params.edge.value_or(AlignEdge::Center), target, along);
    std::vector<Vec3> offsets(along.size(), Vec3(0.0f));
    for (usize i = 0; i < along.size(); ++i)
    {
        offsets[i][static_cast<glm::length_t>(axis)] = along[i];
    }
    return moveArranged(context, *found, offsets);
}

Expected<EntitiesResult, CommandError> transformDistribute(CommandContext& context,
                                                           const TransformDistributeParams& params)
{
    if (Expected<void, CommandError> valid = checkEnum(std::optional(params.axis), TransformAxis::Z, "/axis");
        !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<void, CommandError> valid = checkEnum(params.mode, DistributeMode::Gaps, "/mode"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<Arranged, CommandError> found = arranged(context, params.entities);
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    if (found->entities.size() < 3)
    {
        return unexpected(CommandError::invalid(
            "/entities", "must name at least three entities that are not descendants of each other"));
    }
    const u32 axis = static_cast<u32>(params.axis);
    std::vector<f32> along(found->boxes.size());
    distributeOffsets(found->boxes, axis, params.mode.value_or(DistributeMode::Centers), along);
    std::vector<Vec3> offsets(along.size(), Vec3(0.0f));
    for (usize i = 0; i < along.size(); ++i)
    {
        offsets[i][static_cast<glm::length_t>(axis)] = along[i];
    }
    return moveArranged(context, *found, offsets);
}

Expected<EntitiesResult, CommandError> transformDropToGround(CommandContext& context,
                                                             const TransformDropParams& params)
{
    if (Expected<void, CommandError> valid = checkEnum(params.onto, DropTarget::Plane, "/onto"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<Arranged, CommandError> found = arranged(context, params.entities);
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    const bool plane = params.onto.value_or(DropTarget::Surfaces) == DropTarget::Plane;
    const std::span<const Aabb> surfaces =
        plane ? std::span<const Aabb>() : std::span<const Aabb>(found->others);
    std::vector<Vec3> offsets(found->boxes.size(), Vec3(0.0f));
    for (usize i = 0; i < offsets.size(); ++i)
    {
        offsets[i].y = restingHeight(found->boxes[i], surfaces, 0.0f) - found->boxes[i].min.y;
    }
    return moveArranged(context, *found, offsets);
}

Expected<EntitiesResult, CommandError> transformAlignToView(CommandContext& context,
                                                            const TransformAlignViewParams& params)
{
    ViewportHost* host = context.editor().viewports();
    if (host == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported,
                                             "this editor has no viewports (it runs without a GPU)"));
    }
    const std::string_view wanted = params.viewport ? std::string_view(*params.viewport) : "scene";
    std::optional<ViewportInfo> view;
    for (ViewportInfo& info : host->viewports())
    {
        if (info.id == wanted)
        {
            view = std::move(info);
            break;
        }
    }
    if (!view)
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "no viewport '" + std::string(wanted) + "'"));
    }
    Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, params.entities);
    if (!entities)
    {
        return unexpected(std::move(entities).error());
    }
    World& world = context.world();
    dropDescendants(world, *entities);
    sortParentsFirst(context, *entities);
    const Quat rotation = glm::normalize(glm::quat_cast(Mat3(glm::inverse(view->view))));
    EntitiesResult result;
    for (const Entity entity : *entities)
    {
        Transform placed = worldTransformOf(world, entity);
        placed.rotation = rotation;
        if (params.move.value_or(false))
        {
            placed.position = view->eye;
        }
        if (Expected<void, CommandError> done = placeWorld(context, entity, placed, "/entities"); !done)
        {
            return unexpected(std::move(done).error());
        }
        result.entities.push_back(context.summary(entity));
    }
    return result;
}

} // namespace

void registerTransformCommands(CommandRegistry& registry)
{
    registry.add<&transformGet>({
        .name = "transform.get",
        .title = strings::kCommandTransformGetTitle,
        .description = strings::kCommandTransformGetDescription,
        .agentSummary =
            "Returns an entity's placement in world space (default) or relative to its parent, with "
            "the rotation also as Euler angles in degrees. Example: {\"entity\": \"/Game/Board\", "
            "\"space\": \"local\"}.",
        .category = "Transform",
        .icon = "my_location",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&transformSet>({
        .name = "transform.set",
        .title = strings::kCommandTransformSetTitle,
        .description = strings::kCommandTransformSetDescription,
        .agentSummary =
            "Sets the position, rotation (quaternion [x, y, z, w] or euler in degrees) and scale of entities "
            "in world space (default) or relative to their parents; children stay in place relative to them. "
            "Example: {\"entities\": [\"/Game/Board\"], \"position\": [0, 1, 0], \"euler\": [0, 90, 0]}.",
        .category = "Transform",
        .icon = "open_with",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&transformTranslate>({
        .name = "transform.translate",
        .title = strings::kCommandTransformTranslateTitle,
        .description = strings::kCommandTransformTranslateDescription,
        .agentSummary = "Moves entities by an offset along the world axes (default), the parent's axes "
                        "(\"local\") or their own (\"self\"). Example: {\"entities\": [\"$selection\"], "
                        "\"offset\": [1, 0, 0], \"space\": \"self\"}.",
        .category = "Transform",
        .icon = "drag_pan",
        .flags = {.undoable = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&transformRotate>({
        .name = "transform.rotate",
        .title = strings::kCommandTransformRotateTitle,
        .description = strings::kCommandTransformRotateDescription,
        .agentSummary =
            "Turns entities by a rotation (quaternion or euler in degrees) about the world, parent or own "
            "axes, each about its own position or about their center or a point. Example: {\"entities\": "
            "[\"/Game/Board\"], \"euler\": [0, 45, 0], \"pivot\": \"center\"}.",
        .category = "Transform",
        .icon = "rotate_right",
        .flags = {.undoable = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&transformScale>({
        .name = "transform.scale",
        .title = strings::kCommandTransformScaleTitle,
        .description = strings::kCommandTransformScaleDescription,
        .agentSummary =
            "Multiplies the scale of entities per axis; with pivot \"center\" or a point their "
            "positions spread from it too. Example: {\"entities\": [\"/Game/Board\"], \"factor\": "
            "[2, 2, 2]}.",
        .category = "Transform",
        .icon = "zoom_out_map",
        .flags = {.undoable = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&transformReset>({
        .name = "transform.reset",
        .title = strings::kCommandTransformResetTitle,
        .description = strings::kCommandTransformResetDescription,
        .agentSummary =
            "Resets the position, rotation and scale (all by default) of entities relative to their "
            "parents. Example: {\"entities\": [\"/Game/Board\"], \"scale\": false}.",
        .category = "Transform",
        .icon = "restart_alt",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&transformSnapToGrid>({
        .name = "transform.snap_to_grid",
        .title = strings::kCommandTransformSnapToGridTitle,
        .description = strings::kCommandTransformSnapToGridDescription,
        .agentSummary =
            "Moves entities so that their world positions lie on a grid: each coordinate is rounded to the "
            "nearest multiple of increment (default: the gizmo's translation snap). Children follow their "
            "parents. Example: {\"entities\": [\"$selection\"], \"increment\": 1}.",
        .category = "Transform",
        .icon = "grid_4x4",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&transformAlign>({
        .name = "transform.align",
        .title = strings::kCommandTransformAlignTitle,
        .description = strings::kCommandTransformAlignDescription,
        .agentSummary = "Lines entities up along one world axis: moves each so that the min, center or max "
                        "edge of the box "
                        "around what it draws matches that edge of the box around all of them (to "
                        "\"selection\") or of the "
                        "last listed entity (to \"active\"). Entities that draw nothing count as their "
                        "position. Example: "
                        "{\"entities\": [\"/A\", \"/B\"], \"axis\": \"x\", \"edge\": \"min\"}.",
        .category = "Transform",
        .icon = "align_horizontal_left",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&transformDistribute>({
        .name = "transform.distribute",
        .title = strings::kCommandTransformDistributeTitle,
        .description = strings::kCommandTransformDistributeDescription,
        .agentSummary =
            "Spaces three or more entities evenly along one world axis between the first and the last (by "
            "the "
            "centres of their boxes, which stay put): equal distances between \"centers\" (default) or equal "
            "\"gaps\" between the boxes. Example: {\"entities\": [\"/A\", \"/B\", \"/C\"], \"axis\": \"x\", "
            "\"mode\": \"gaps\"}.",
        .category = "Transform",
        .icon = "horizontal_distribute",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&transformDropToGround>({
        .name = "transform.drop_to_ground",
        .title = strings::kCommandTransformDropToGroundTitle,
        .description = strings::kCommandTransformDropToGroundDescription,
        .agentSummary =
            "Moves entities straight down (or up) until the bottom of the box around what they draw rests on "
            "the highest box of another entity below them, or on the ground plane y = 0 when there is none "
            "(onto \"surfaces\", default), or always on the plane (onto \"plane\"). Render bounds, not "
            "physics. "
            "Example: {\"entities\": [\"$selection\"]}.",
        .category = "Transform",
        .icon = "vertical_align_bottom",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&transformAlignToView>({
        .name = "transform.align_to_view",
        .title = strings::kCommandTransformAlignToViewTitle,
        .description = strings::kCommandTransformAlignToViewDescription,
        .agentSummary = "Turns entities to the rotation of a viewport's camera (they look where it looks, "
                        "along their -z) "
                        "and, with move, also places them at its eye, e.g. to aim a Camera or a light from "
                        "the current view. "
                        "Example: {\"entities\": [\"/Main Camera\"], \"move\": true}.",
        .category = "Transform",
        .icon = "switch_video",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Edit,
        .palette = true,
    });
}

} // namespace illusion::editor::detail
