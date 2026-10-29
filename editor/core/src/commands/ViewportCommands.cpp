#include "CommandSupport.hpp"

#include <cmath>

namespace illusion::editor::detail
{
namespace
{

constexpr std::string_view kDefaultViewport = "scene";

Expected<ViewportHost*, CommandError> requireViewports(CommandContext& context)
{
    ViewportHost* viewports = context.editor().viewports();
    if (viewports == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported,
                                             "this editor has no viewports (it runs without a GPU)"));
    }
    return viewports;
}

// The viewports and the one the params name.
struct Target
{
    ViewportHost* host = nullptr;
    ViewportInfo info;
};

Expected<Target, CommandError> findViewport(CommandContext& context, const std::optional<std::string>& id)
{
    Expected<ViewportHost*, CommandError> host = requireViewports(context);
    if (!host)
    {
        return unexpected(std::move(host).error());
    }
    const std::string_view wanted = id ? std::string_view(*id) : kDefaultViewport;
    for (ViewportInfo& info : (*host)->viewports())
    {
        if (info.id == wanted)
        {
            return Target{*host, std::move(info)};
        }
    }
    return unexpected(CommandError::make(ErrorKind::NotFound, "no viewport '" + std::string(wanted) + "'"));
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

ViewportCameraResult cameraResult(const std::string& id, const ViewportCamera& camera)
{
    return ViewportCameraResult{id, camera, viewportEye(camera), viewportForward(camera)};
}

// Moves the camera of @p target (unless this is a dry run) and reports where it goes.
Expected<ViewportCameraResult, CommandError> moveCamera(CommandContext& context, const Target& target,
                                                        const ViewportCamera& camera,
                                                        std::optional<bool> animate)
{
    if (!context.dryRun())
    {
        if (Expected<void, CommandError> set =
                target.host->setCamera(target.info.id, camera, animate.value_or(true));
            !set)
        {
            return unexpected(std::move(set).error());
        }
    }
    return cameraResult(target.info.id, camera);
}

Expected<ViewportListResult, CommandError> viewportList(CommandContext& context, const NoParams&)
{
    Expected<ViewportHost*, CommandError> host = requireViewports(context);
    if (!host)
    {
        return unexpected(std::move(host).error());
    }
    ViewportListResult result;
    for (ViewportInfo& info : (*host)->viewports())
    {
        result.viewports.push_back(ViewportEntry{std::move(info.id), info.size.x, info.size.y, info.visible,
                                                 info.camera, viewportEye(info.camera), info.settings});
    }
    return result;
}

Expected<ViewportCameraResult, CommandError> cameraGet(CommandContext& context, const ViewportParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    return cameraResult(target->info.id, target->info.camera);
}

Expected<ViewportCameraResult, CommandError> cameraSet(CommandContext& context,
                                                       const ViewportCameraSetParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (Expected<void, CommandError> projection =
            checkEnum(params.projection, ViewportProjection::Orthographic, "/projection");
        !projection)
    {
        return unexpected(std::move(projection).error());
    }
    if (params.eye && (params.yaw || params.pitch || params.distance))
    {
        return unexpected(CommandError::invalid("/eye", "cannot be combined with yaw, pitch or distance"));
    }
    ViewportCamera camera = target->info.camera;
    camera.pivot = params.pivot.value_or(camera.pivot);
    camera.distance = params.distance.value_or(camera.distance);
    camera.yaw = params.yaw.value_or(camera.yaw);
    camera.pitch = params.pitch.value_or(camera.pitch);
    camera.fieldOfView = params.fieldOfView.value_or(camera.fieldOfView);
    camera.projection = params.projection.value_or(camera.projection);
    camera.orthographicHeight = params.orthographicHeight.value_or(camera.orthographicHeight);
    if (!std::isfinite(camera.pivot.x) || !std::isfinite(camera.pivot.y) || !std::isfinite(camera.pivot.z) ||
        std::abs(camera.pivot.x) > kViewportMaxExtent || std::abs(camera.pivot.y) > kViewportMaxExtent ||
        std::abs(camera.pivot.z) > kViewportMaxExtent)
    {
        return unexpected(CommandError::invalid("/pivot", "must be within 100,000 units of the origin"));
    }
    if (params.eye)
    {
        const Vec3 eye = *params.eye;
        if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z))
        {
            return unexpected(CommandError::invalid("/eye", "must be finite"));
        }
        std::optional<ViewportCamera> aimed = aimViewportCamera(camera, eye);
        if (!aimed)
        {
            return unexpected(CommandError::invalid("/eye", "must not be at the pivot"));
        }
        camera = *aimed;
    }
    return moveCamera(context, *target, camera, params.animate);
}

Expected<ViewportCameraResult, CommandError> viewportView(CommandContext& context,
                                                          const ViewportViewParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (Expected<void, CommandError> view =
            checkEnum(std::optional<ViewportView>(params.view), ViewportView::Right, "/view");
        !view)
    {
        return unexpected(std::move(view).error());
    }
    return moveCamera(context, *target, viewportPreset(target->info.camera, params.view), params.animate);
}

Expected<ViewportCameraResult, CommandError> viewportFrame(CommandContext& context,
                                                           const ViewportFrameParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    std::vector<Entity> entities;
    if (params.entities && !params.entities->empty())
    {
        Expected<std::vector<Entity>, CommandError> resolved = resolveEntityList(context, *params.entities);
        if (!resolved)
        {
            return unexpected(std::move(resolved).error());
        }
        entities = std::move(resolved).value();
    }
    else if (!params.entities)
    {
        for (const EntityGuid guid : context.editor().selection().items())
        {
            if (const Entity entity = context.editor().entities().find(guid); !entity.isNull())
            {
                entities.push_back(entity);
            }
        }
    }
    Expected<ViewportCamera, CommandError> framed = target->host->framing(target->info.id, entities);
    if (!framed)
    {
        return unexpected(std::move(framed).error());
    }
    return moveCamera(context, *target, *framed, params.animate);
}

Expected<ViewportSettingsResult, CommandError> applySettings(CommandContext& context, const Target& target,
                                                             const ViewportSettings& settings)
{
    if (!context.dryRun())
    {
        if (Expected<void, CommandError> set = target.host->setSettings(target.info.id, settings); !set)
        {
            return unexpected(std::move(set).error());
        }
    }
    return ViewportSettingsResult{target.info.id, settings};
}

Expected<ViewportSettingsResult, CommandError> setMode(CommandContext& context,
                                                       const ViewportModeParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (Expected<void, CommandError> shading =
            checkEnum(params.shading, ViewportShading::MotionVectors, "/shading");
        !shading)
    {
        return unexpected(std::move(shading).error());
    }
    ViewportSettings settings = target->info.settings;
    settings.shading = params.shading.value_or(settings.shading);
    settings.wireframe = params.wireframe.value_or(settings.wireframe);
    return applySettings(context, *target, settings);
}

Expected<ViewportSettingsResult, CommandError> setOverlays(CommandContext& context,
                                                           const ViewportOverlaysParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    ViewportSettings settings = target->info.settings;
    settings.grid = params.grid.value_or(settings.grid);
    settings.selectionOutline = params.selectionOutline.value_or(settings.selectionOutline);
    return applySettings(context, *target, settings);
}

Expected<ViewportProjectResult, CommandError> viewportProject(CommandContext& context,
                                                              const ViewportProjectParams& params)
{
    if (params.entity.has_value() == params.point.has_value())
    {
        return unexpected(CommandError::invalid("", "must give either entity or point"));
    }
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    Vec3 point{0.0f};
    if (params.entity)
    {
        Expected<Entity, CommandError> entity =
            context.editor().entities().resolve(*params.entity, context.editor().selection(), "/entity");
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        point = worldTransformOf(context.editor().world(), *entity).position;
    }
    else
    {
        point = *params.point;
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        {
            return unexpected(CommandError::invalid("/point", "must be finite"));
        }
    }
    const ViewportPoint projected = projectToViewport(target->info.camera, target->info.size, point);
    return ViewportProjectResult{projected.pixel.x, projected.pixel.y, projected.depth, projected.inFront,
                                 projected.inside};
}

// Waits for the viewport to read the entity under the cursor, then changes the selection if asked.
class PickTask final : public CommandTask<ViewportPickResult>
{
public:
    PickTask(ViewportHost& host, u64 ticket, PickSelect select) noexcept
        : m_host(&host)
        , m_ticket(ticket)
        , m_select(select)
    {
    }

    Expected<std::optional<ViewportPickResult>, CommandError> update(CommandContext& context,
                                                                     CommandProgress& progress) override
    {
        if (context.editor().viewports() != m_host)
        {
            return unexpected(CommandError::make(ErrorKind::Unsupported, "the viewports closed"));
        }
        std::optional<Expected<std::optional<ViewportHit>, CommandError>> done = m_host->pick(m_ticket);
        if (!done)
        {
            progress.message = "waiting for the GPU";
            return std::optional<ViewportPickResult>{};
        }
        if (!*done)
        {
            return unexpected(std::move(*done).error());
        }
        const std::optional<ViewportHit>& hit = done->value();
        EditorContext& editor = context.editor();
        const bool alive = hit && editor.world().isAlive(hit->entity);
        ViewportPickResult result;
        if (alive)
        {
            result.entity = editor.entities().summary(hit->entity);
            result.index = hit->index;
            result.candidates = hit->candidates;
        }
        if (context.dryRun())
        {
            return std::optional<ViewportPickResult>(std::move(result));
        }
        const EntityGuid guid = alive ? editor.entities().guidOf(hit->entity) : EntityGuid{};
        Selection& selection = editor.selection();
        switch (m_select)
        {
        case PickSelect::None:
            break;
        case PickSelect::Replace:
            if (alive)
            {
                selection.set(std::span<const EntityGuid>(&guid, 1));
            }
            else
            {
                selection.clear();
            }
            break;
        case PickSelect::Add:
            if (alive)
            {
                selection.add(guid);
            }
            break;
        case PickSelect::Toggle:
            if (alive && !selection.remove(guid))
            {
                selection.add(guid);
            }
            break;
        }
        return std::optional<ViewportPickResult>(std::move(result));
    }

private:
    ViewportHost* m_host;
    u64 m_ticket;
    PickSelect m_select;
};

Expected<std::unique_ptr<CommandTask<ViewportPickResult>>, CommandError>
viewportPick(CommandContext& context, const ViewportPickParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (Expected<void, CommandError> select = checkEnum(params.select, PickSelect::Toggle, "/select");
        !select)
    {
        return unexpected(std::move(select).error());
    }
    if (target->info.size.x == 0 || target->info.size.y == 0)
    {
        return unexpected(CommandError::make(
            ErrorKind::PreconditionFailed, "the viewport has no image yet: show its panel (ui.show_panel)"));
    }
    if (!std::isfinite(params.x) || !std::isfinite(params.y) || params.x < 0.0f || params.y < 0.0f ||
        params.x >= static_cast<f32>(target->info.size.x) ||
        params.y >= static_cast<f32>(target->info.size.y))
    {
        return unexpected(CommandError::invalid("", "x and y must lie within the viewport (" +
                                                        std::to_string(target->info.size.x) + "x" +
                                                        std::to_string(target->info.size.y) + " pixels)"));
    }
    Expected<u64, CommandError> ticket =
        target->host->requestPick(target->info.id, Vec2(params.x, params.y), params.cycle.value_or(false));
    if (!ticket)
    {
        return unexpected(std::move(ticket).error());
    }
    return std::unique_ptr<CommandTask<ViewportPickResult>>(
        std::make_unique<PickTask>(*target->host, *ticket, params.select.value_or(PickSelect::None)));
}

} // namespace

void registerViewportCommands(CommandRegistry& registry)
{
    registry.add<&viewportList>({
        .name = "viewport.list",
        .title = strings::kCommandViewportListTitle,
        .description = strings::kCommandViewportListDescription,
        .agentSummary = "Lists the viewports (\"scene\" is the Scene panel) with their image size in pixels, "
                        "orbit camera, eye position and what they draw. Fails with code 1400 without a GPU. "
                        "Example: {}.",
        .category = "Viewport",
        .icon = "view_in_ar",
        .flags = {.readOnly = true, .idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
    });
    registry.add<&cameraGet>({
        .name = "viewport.camera_get",
        .title = strings::kCommandViewportCameraGetTitle,
        .description = strings::kCommandViewportCameraGetDescription,
        .agentSummary =
            "Returns a viewport's orbit camera (pivot, distance, yaw and pitch in degrees, field of "
            "view, projection), its eye position and view direction. Example: {\"viewport\": "
            "\"scene\"}.",
        .category = "Viewport",
        .icon = "photo_camera",
        .flags = {.readOnly = true, .idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
    });
    registry.add<&cameraSet>({
        .name = "viewport.camera_set",
        .title = strings::kCommandViewportCameraSetTitle,
        .description = strings::kCommandViewportCameraSetDescription,
        .agentSummary = "Moves a viewport's editor camera (not a scene Camera): set any of pivot, distance, "
                        "yaw, pitch (degrees, negative looks down), fieldOfView, projection and "
                        "orthographicHeight, or put the eye at a point looking at the pivot. Animated unless "
                        "animate is false; not undoable. Example: {\"pivot\": [0, 0, 0], \"eye\": [8, 6, 8], "
                        "\"animate\": false}.",
        .category = "Viewport",
        .icon = "videocam",
        .flags = {.idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&viewportView>({
        .name = "viewport.view",
        .title = strings::kCommandViewportViewTitle,
        .description = strings::kCommandViewportViewDescription,
        .agentSummary =
            "Turns a viewport to a standard view around its pivot: top, bottom, front, back, left "
            "and right are orthographic; perspective goes back. Example: {\"view\": \"top\"}.",
        .category = "Viewport",
        .icon = "3d_rotation",
        .flags = {.idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&viewportFrame>({
        .name = "viewport.frame",
        .title = strings::kCommandViewportFrameTitle,
        .description = strings::kCommandViewportFrameDescription,
        .agentSummary = "Moves a viewport's camera so that entities fill the image: the given ones, else the "
                        "selection, else (or with entities: []) everything visible. Example: {\"entities\": "
                        "[\"/Game/Board\"]}.",
        .category = "Viewport",
        .icon = "center_focus_strong",
        .flags = {.idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&setMode>({
        .name = "viewport.set_mode",
        .title = strings::kCommandViewportSetModeTitle,
        .description = strings::kCommandViewportSetModeDescription,
        .agentSummary = "Changes how a viewport shades the scene: lit, unlit, albedo, normals, roughness, "
                        "metallic, ambientOcclusion, shadowCascades or motionVectors, and the wireframe "
                        "overlay. Example: {\"shading\": \"normals\", \"wireframe\": true}.",
        .category = "Viewport",
        .icon = "contrast",
        .flags = {.idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&setOverlays>({
        .name = "viewport.set_overlays",
        .title = strings::kCommandViewportSetOverlaysTitle,
        .description = strings::kCommandViewportSetOverlaysDescription,
        .agentSummary = "Turns a viewport's overlays on or off: the ground grid and the selection outline. "
                        "Example: {\"grid\": false}.",
        .category = "Viewport",
        .icon = "layers",
        .flags = {.idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&viewportProject>({
        .name = "viewport.project",
        .title = strings::kCommandViewportProjectTitle,
        .description = strings::kCommandViewportProjectDescription,
        .agentSummary = "Returns the pixel (origin top left, y down) where an entity's position or a world "
                        "point appears in a viewport, its depth and whether it is in view; pass the pixel to "
                        "viewport.pick. Example: {\"entity\": \"/Game/Board\"}.",
        .category = "Viewport",
        .icon = "my_location",
        .flags = {.readOnly = true, .idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
    });
    registry.addAsync<&viewportPick>({
        .name = "viewport.pick",
        .title = strings::kCommandViewportPickTitle,
        .description = strings::kCommandViewportPickDescription,
        .agentSummary = "Asynchronous: returns the entity drawn at a viewport pixel (within 2 pixels; hidden "
                        "and locked entities are skipped) and optionally selects it. With cycle, a pick at "
                        "the previous pixel returns the next entity under the cursor, front to back. "
                        "Example: {\"x\": 640, \"y\": 360, \"select\": \"replace\"}.",
        .category = "Viewport",
        .icon = "ads_click",
        .flags = {.needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
}

} // namespace illusion::editor::detail
