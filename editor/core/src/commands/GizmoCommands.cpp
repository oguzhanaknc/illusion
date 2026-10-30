#include "CommandSupport.hpp"

namespace illusion::editor::detail
{
namespace
{

// Enum params also accept raw numbers: reject values outside the enum.
template <class E>
Expected<void, CommandError> checkEnum(E value, E last, std::string pointer)
{
    if (static_cast<usize>(value) > static_cast<usize>(last))
    {
        return unexpected(CommandError::invalid(std::move(pointer), "is not one of the allowed values"));
    }
    return {};
}

Expected<GizmoSettings, CommandError> gizmoGet(CommandContext& context, const NoParams&)
{
    return context.editor().gizmo();
}

Expected<GizmoSettings, CommandError> gizmoSetTool(CommandContext& context, const GizmoToolParams& params)
{
    if (Expected<void, CommandError> valid = checkEnum(params.tool, GizmoTool::Measure, "/tool"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    GizmoSettings settings = context.editor().gizmo();
    settings.tool = params.tool;
    if (!context.dryRun())
    {
        context.editor().setGizmo(settings);
    }
    return settings;
}

Expected<GizmoSettings, CommandError> gizmoSetSpace(CommandContext& context, const GizmoSpaceParams& params)
{
    if (Expected<void, CommandError> valid = checkEnum(params.space, GizmoSpace::View, "/space"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    GizmoSettings settings = context.editor().gizmo();
    settings.space = params.space;
    if (!context.dryRun())
    {
        context.editor().setGizmo(settings);
    }
    return settings;
}

Expected<GizmoSettings, CommandError> gizmoSetPivot(CommandContext& context, const GizmoPivotParams& params)
{
    if (Expected<void, CommandError> valid = checkEnum(params.pivot, GizmoPivot::BoundsCenter, "/pivot");
        !valid)
    {
        return unexpected(std::move(valid).error());
    }
    GizmoSettings settings = context.editor().gizmo();
    settings.pivot = params.pivot;
    if (!context.dryRun())
    {
        context.editor().setGizmo(settings);
    }
    return settings;
}

Expected<GizmoSettings, CommandError> gizmoSetSnap(CommandContext& context, const GizmoSnapParams& params)
{
    if (!params.enabled && !params.translate && !params.rotate && !params.scale && !params.alignToNormal)
    {
        return unexpected(
            CommandError::invalid("", "must set enabled, translate, rotate, scale or alignToNormal"));
    }
    GizmoSettings settings = context.editor().gizmo();
    GizmoSnap& snap = settings.snap;
    snap.enabled = params.enabled.value_or(snap.enabled);
    snap.translate =
        std::clamp(params.translate.value_or(snap.translate), kGizmoMinTranslateSnap, kGizmoMaxTranslateSnap);
    snap.rotate = std::clamp(params.rotate.value_or(snap.rotate), kGizmoMinRotateSnap, kGizmoMaxRotateSnap);
    snap.scale = std::clamp(params.scale.value_or(snap.scale), kGizmoMinScaleSnap, kGizmoMaxScaleSnap);
    snap.alignToNormal = params.alignToNormal.value_or(snap.alignToNormal);
    if (!context.dryRun())
    {
        context.editor().setGizmo(settings);
    }
    return settings;
}

bool finite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Expected<Measurement, CommandError> gizmoMeasure(CommandContext& context, const GizmoMeasureParams& params)
{
    Vec3 from(0.0f);
    Vec3 to(0.0f);
    if (params.entities)
    {
        if (params.from || params.to)
        {
            return unexpected(CommandError::invalid("/entities", "give either from and to, or entities"));
        }
        Expected<std::vector<Entity>, CommandError> entities = resolveEntityList(context, *params.entities);
        if (!entities)
        {
            return unexpected(std::move(entities).error());
        }
        if (params.entities->size() != 2)
        {
            return unexpected(CommandError::invalid("/entities", "must name exactly two entities"));
        }
        World& world = context.world();
        from = worldTransformOf(world, (*entities)[0]).position;
        to = worldTransformOf(world, (*entities)[1]).position;
    }
    else if (params.from && params.to)
    {
        from = *params.from;
        to = *params.to;
    }
    else
    {
        return unexpected(CommandError::invalid("", "must give from and to, or entities"));
    }
    if (!finite(from) || !finite(to) ||
        glm::any(glm::greaterThan(glm::abs(from), Vec3(kViewportMaxExtent))) ||
        glm::any(glm::greaterThan(glm::abs(to), Vec3(kViewportMaxExtent))))
    {
        return unexpected(CommandError::invalid("", "the points must be finite and within 100,000 units"));
    }
    if (params.show.value_or(true) && !context.dryRun())
    {
        GizmoSettings settings = context.editor().gizmo();
        settings.ruler = GizmoRuler{from, to};
        context.editor().setGizmo(settings);
    }
    return measure(from, to);
}

Expected<GizmoSettings, CommandError> gizmoClearRuler(CommandContext& context, const NoParams&)
{
    GizmoSettings settings = context.editor().gizmo();
    settings.ruler.reset();
    if (!context.dryRun())
    {
        context.editor().setGizmo(settings);
    }
    return settings;
}

Expected<NoResult, CommandError> gizmoModal(CommandContext& context, const GizmoModalParams& params)
{
    if (Expected<void, CommandError> valid =
            checkEnum(params.operation, GizmoModalOperation::Scale, "/operation");
        !valid)
    {
        return unexpected(std::move(valid).error());
    }
    UiHost* ui = context.editor().ui();
    if (ui == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported,
                                             "this editor has no user interface (it runs with --no-gpu)"));
    }
    if (!context.dryRun())
    {
        if (Expected<void, CommandError> started = ui->beginModalTransform(params.operation); !started)
        {
            return unexpected(std::move(started).error());
        }
    }
    return NoResult{};
}

} // namespace

void registerGizmoCommands(CommandRegistry& registry)
{
    registry.add<&gizmoGet>({
        .name = "gizmo.get",
        .title = strings::kCommandGizmoGetTitle,
        .description = strings::kCommandGizmoGetDescription,
        .agentSummary = "Returns the transform gizmo's tool, space (axes), pivot and snap increments of the "
                        "Scene viewports. Example: {}.",
        .category = "Gizmo",
        .icon = "open_with",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&gizmoSetTool>({
        .name = "gizmo.set_tool",
        .title = strings::kCommandGizmoSetToolTitle,
        .description = strings::kCommandGizmoSetToolDescription,
        .agentSummary =
            "Chooses what a left drag in the Scene viewport does: \"select\" (box select), \"translate\", "
            "\"rotate\", \"scale\", \"universal\" or \"measure\" (draws a ruler). To move entities "
            "yourself, use transform.* instead. Example: {\"tool\": \"rotate\"}.",
        .category = "Gizmo",
        .icon = "open_with",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&gizmoSetSpace>({
        .name = "gizmo.set_space",
        .title = strings::kCommandGizmoSetSpaceTitle,
        .description = strings::kCommandGizmoSetSpaceDescription,
        .agentSummary =
            "Chooses the axes of the translate and rotate gizmos: \"world\", \"local\" (the active "
            "entity's) or \"view\" (the camera's). Example: {\"space\": \"local\"}.",
        .category = "Gizmo",
        .icon = "language",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&gizmoSetPivot>({
        .name = "gizmo.set_pivot",
        .title = strings::kCommandGizmoSetPivotTitle,
        .description = strings::kCommandGizmoSetPivotDescription,
        .agentSummary =
            "Chooses what gizmo drags turn and scale about: \"median\" (the mean position of the selection), "
            "\"active\" (the active entity) or \"individual\" (each entity's own origin). Example: "
            "{\"pivot\": \"individual\"}.",
        .category = "Gizmo",
        .icon = "filter_center_focus",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&gizmoSetSnap>({
        .name = "gizmo.set_snap",
        .title = strings::kCommandGizmoSetSnapTitle,
        .description = strings::kCommandGizmoSetSnapDescription,
        .agentSummary =
            "Turns gizmo snapping on or off (Ctrl inverts it during a drag) and sets its increments: "
            "translate in world units, rotate in degrees, scale as a factor step; alignToNormal makes "
            "surface "
            "snapping (Ctrl+Shift during a move) also turn the entities' +y axis onto the surface. Example: "
            "{\"enabled\": true, \"translate\": 0.25, \"rotate\": 15}.",
        .category = "Gizmo",
        .icon = "grid_4x4",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&gizmoMeasure>({
        .name = "gizmo.measure",
        .title = strings::kCommandGizmoMeasureTitle,
        .description = strings::kCommandGizmoMeasureDescription,
        .agentSummary =
            "Measures from one world point to another, or between the positions of two entities: the "
            "distance, the offset, its length in the ground plane and the elevation in degrees. The ruler "
            "stays in the Scene viewports until gizmo.clear_ruler (show false only measures). Example: "
            "{\"entities\": [\"/Game/Board\", \"/Game/Spawner\"]}.",
        .category = "Gizmo",
        .icon = "straighten",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&gizmoClearRuler>({
        .name = "gizmo.clear_ruler",
        .title = strings::kCommandGizmoClearRulerTitle,
        .description = strings::kCommandGizmoClearRulerDescription,
        .agentSummary = "Removes the ruler of gizmo.measure or the Measure tool from the Scene viewports. "
                        "Example: {}.",
        .category = "Gizmo",
        .icon = "straighten",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .palette = true,
    });
    registry.add<&gizmoModal>({
        .name = "gizmo.modal",
        .title = strings::kCommandGizmoModalTitle,
        .description = strings::kCommandGizmoModalDescription,
        .agentSummary =
            "Starts a Blender-style modal move, rotation or scale of the selection in the Scene view under "
            "the person's cursor (G, R and S); it follows their mouse until they confirm or cancel it, and "
            "does nothing without a selection. Meant for people at the editor: to move entities yourself, "
            "use transform.* instead. Example: {\"operation\": \"translate\"}.",
        .category = "Gizmo",
        .icon = "open_with",
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Edit,
    });
}

} // namespace illusion::editor::detail
