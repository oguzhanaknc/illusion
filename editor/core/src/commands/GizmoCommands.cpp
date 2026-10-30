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
    if (Expected<void, CommandError> valid = checkEnum(params.tool, GizmoTool::Universal, "/tool"); !valid)
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
    if (!params.enabled && !params.translate && !params.rotate && !params.scale)
    {
        return unexpected(CommandError::invalid("", "must set enabled, translate, rotate or scale"));
    }
    GizmoSettings settings = context.editor().gizmo();
    GizmoSnap& snap = settings.snap;
    snap.enabled = params.enabled.value_or(snap.enabled);
    snap.translate =
        std::clamp(params.translate.value_or(snap.translate), kGizmoMinTranslateSnap, kGizmoMaxTranslateSnap);
    snap.rotate = std::clamp(params.rotate.value_or(snap.rotate), kGizmoMinRotateSnap, kGizmoMaxRotateSnap);
    snap.scale = std::clamp(params.scale.value_or(snap.scale), kGizmoMinScaleSnap, kGizmoMaxScaleSnap);
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
            "\"rotate\", \"scale\" or \"universal\". To move entities yourself, use transform.* instead. "
            "Example: {\"tool\": \"rotate\"}.",
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
            "translate in world units, rotate in degrees, scale as a factor step. Example: {\"enabled\": "
            "true, \"translate\": 0.25, \"rotate\": 15}.",
        .category = "Gizmo",
        .icon = "grid_4x4",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
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
