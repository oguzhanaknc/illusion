// The camera.* commands (ADR 0064): list the virtual cameras and brains of a World, show what the brains
// show (camera.status, play sessions), raise a virtual camera above the others (camera.activate, undoable)
// and preview one in the Game views of the edit World (camera.preview). The stages themselves run in the
// CameraWorld of a play session; the edit World has none, so its commands read saved fields only.

#include "CommandSupport.hpp"

#include <illusion/camera/CameraBlend.hpp>
#include <illusion/camera/CameraComponents.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/ViewState.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace illusion::editor::detail
{

namespace
{

namespace strings = illusion::editor::strings;

constexpr usize kMaxListedCameras = 200;
constexpr usize kMaxListedBrains = 50;

std::string_view blendStyleName(CameraBlendStyle style) noexcept
{
    switch (style)
    {
    case CameraBlendStyle::Cut:
        return "Cut";
    case CameraBlendStyle::Linear:
        return "Linear";
    case CameraBlendStyle::EaseInOut:
        return "EaseInOut";
    case CameraBlendStyle::Curve:
        return "Curve";
    }
    return "Cut";
}

std::string_view bodyOf(const World& world, Entity entity) noexcept
{
    if (world.has<CameraFollow>(entity))
    {
        return "Follow";
    }
    if (world.has<CameraOrbit>(entity))
    {
        return "Orbit";
    }
    if (world.has<CameraSpringArm>(entity))
    {
        return "SpringArm";
    }
    if (world.has<CameraDolly>(entity))
    {
        return "Dolly";
    }
    return "None";
}

std::string_view aimOf(const World& world, Entity entity) noexcept
{
    if (world.has<CameraLookAt>(entity))
    {
        return "LookAt";
    }
    if (world.has<CameraComposer>(entity))
    {
        return "Composer";
    }
    return "None";
}

std::string pathOf(CommandContext& context, Entity entity)
{
    return !entity.isNull() && context.world().isAlive(entity) ? context.summary(entity).path : std::string();
}

template <typename T>
std::vector<Entity> sortedEntities(World& world)
{
    std::vector<Entity> out;
    world.query<const T>().each([&](Entity entity, const T&) { out.push_back(entity); });
    std::ranges::sort(out, [](Entity a, Entity b) { return a.toBits() < b.toBits(); });
    return out;
}

Expected<Entity, CommandError> virtualCameraEntity(CommandContext& context, std::string_view reference,
                                                   std::string_view pointer)
{
    Expected<Entity, CommandError> entity = context.entity(reference, pointer);
    if (!entity)
    {
        return entity;
    }
    if (!context.world().has<VirtualCamera>(*entity))
    {
        return unexpected(
            CommandError::invalid(std::string(pointer), "the entity has no illusion.VirtualCamera"));
    }
    return entity;
}

Expected<CameraListResult, CommandError> cameraList(CommandContext& context, const NoParams&)
{
    World& world = context.world();
    CameraListResult result;
    result.world = context.onPlayWorld() ? "play" : "edit";
    std::vector<Entity> live;
    world.query<const CameraBrainState>().each([&](Entity, const CameraBrainState& state) {
        if (!state.live.isNull())
        {
            live.push_back(state.live);
        }
    });
    for (const Entity entity : sortedEntities<VirtualCamera>(world))
    {
        ++result.totalCameras;
        if (result.cameras.size() >= kMaxListedCameras)
        {
            continue;
        }
        const VirtualCamera& camera = world.get<VirtualCamera>(entity);
        result.cameras.push_back(CameraListCamera{
            .entity = context.summary(entity),
            .priority = camera.priority,
            .enabled = camera.enabled,
            .inactive = world.has<Inactive>(entity),
            .fieldOfView = glm::degrees(camera.fovY),
            .nearPlane = camera.nearPlane,
            .dutch = glm::degrees(camera.dutch),
            .body = std::string(bodyOf(world, entity)),
            .aim = std::string(aimOf(world, entity)),
            .noise = world.has<CameraNoise>(entity),
            .follow = pathOf(context, camera.follow),
            .lookAt = pathOf(context, camera.lookAt),
            .customBlend = camera.customBlend,
            .live = std::ranges::find(live, entity) != live.end(),
        });
    }
    for (const Entity entity : sortedEntities<CameraBrain>(world))
    {
        ++result.totalBrains;
        if (result.brains.size() >= kMaxListedBrains)
        {
            continue;
        }
        const CameraBrain& brain = world.get<CameraBrain>(entity);
        result.brains.push_back(
            CameraListBrain{.entity = context.summary(entity),
                            .defaultBlend = std::string(blendStyleName(brain.defaultBlend.style)),
                            .defaultDuration = brain.defaultBlend.duration,
                            .overrides = static_cast<u32>(brain.blends.size())});
    }
    world.query<const CameraImpulse>().each([&](Entity, const CameraImpulse&) { ++result.impulses; });
    return result;
}

Expected<CameraStatusResult, CommandError> cameraStatus(CommandContext& context, const NoParams&)
{
    World& world = context.world();
    CameraStatusResult result;
    result.world = context.onPlayWorld() ? "play" : "edit";
    u64 lastUpdate = 0;
    world.query<const VirtualCameraState>().each(
        [&](Entity, const VirtualCameraState& state) { lastUpdate = std::max(lastUpdate, state.evaluated); });
    world.query<const VirtualCamera>().each([&](Entity entity, const VirtualCamera&) {
        ++result.cameras;
        const VirtualCameraState* state = world.tryGet<VirtualCameraState>(entity);
        if (state != nullptr && lastUpdate != 0 && state->evaluated == lastUpdate)
        {
            ++result.evaluated;
        }
    });
    for (const Entity entity : sortedEntities<CameraBrainState>(world))
    {
        const CameraBrainState& state = world.get<CameraBrainState>(entity);
        if (!state.hasPose)
        {
            continue;
        }
        const f32 progress = state.blending && state.blend.duration > 0.0f
                                 ? std::clamp(state.elapsed / state.blend.duration, 0.0f, 1.0f)
                                 : 1.0f;
        result.brains.push_back(
            CameraStatusBrain{.entity = context.summary(entity),
                              .live = pathOf(context, state.live),
                              .from = pathOf(context, state.from),
                              .blending = state.blending,
                              .progress = progress,
                              .weight = state.weight,
                              .position = state.pose.position,
                              .rotation = glm::degrees(glm::eulerAngles(state.pose.rotation)),
                              .fieldOfView = glm::degrees(state.pose.fovY)});
    }
    result.live = !result.brains.empty();
    if (!result.live)
    {
        result.note =
            context.onPlayWorld()
                ? "no CameraBrain has shown a virtual camera yet: the scene needs a CameraBrain on its "
                  "Camera and an enabled VirtualCamera"
                : "the edit World does not run its cameras: start a play session (play.start) to see "
                  "what the brains show";
    }
    return result;
}

Expected<CameraActivateResult, CommandError> cameraActivate(CommandContext& context,
                                                            const CameraActivateParams& params)
{
    Expected<Entity, CommandError> entity = virtualCameraEntity(context, params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    World& world = context.world();
    VirtualCamera camera = world.get<VirtualCamera>(*entity);
    if (!camera.enabled)
    {
        return unexpected(
            CommandError::make(ErrorKind::PreconditionFailed,
                               "'" + context.summary(*entity).path +
                                   "' is disabled: enable it first (component.set_field enabled)"));
    }
    if (world.has<Inactive>(*entity))
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "'" + context.summary(*entity).path +
                                                 "' is inactive (it or an ancestor is disabled)"));
    }
    bool others = false;
    i64 highest = std::numeric_limits<i64>::min();
    world.query<const VirtualCamera>().without<Inactive>().each(
        [&](Entity other, const VirtualCamera& value) {
            if (other != *entity && value.enabled)
            {
                others = true;
                highest = std::max<i64>(highest, value.priority);
            }
        });
    i64 target = camera.priority;
    if (others && highest >= target)
    {
        target = highest + 1;
    }
    if (target > std::numeric_limits<i32>::max())
    {
        return unexpected(
            CommandError::make(ErrorKind::PreconditionFailed,
                               "another virtual camera has the highest possible priority; lower it first"));
    }
    Expected<ComponentId, CommandError> type = context.componentType("illusion.VirtualCamera", "/entity");
    if (!type)
    {
        return unexpected(std::move(type).error());
    }
    const i32 previous = camera.priority;
    camera.priority = static_cast<i32>(target);
    Expected<bool, CommandError> changed = context.edit().assignComponent(*entity, *type, &camera);
    if (!changed)
    {
        return unexpected(std::move(changed).error());
    }
    return CameraActivateResult{.entity = context.summary(*entity),
                                .previous = previous,
                                .priority = camera.priority,
                                .changed = *changed};
}

Expected<CameraPreviewResult, CommandError> cameraPreview(CommandContext& context,
                                                          const CameraPreviewParams& params)
{
    const bool clear = params.clear.value_or(false);
    if (params.entity && clear)
    {
        return unexpected(CommandError::invalid("/clear", "cannot be given together with entity"));
    }
    ViewState& view = context.editor().view();
    EntityGuid shown = view.previewCamera();
    if (params.entity)
    {
        Expected<Entity, CommandError> entity = virtualCameraEntity(context, *params.entity, "/entity");
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        shown = context.index().guidOf(*entity);
    }
    else if (clear)
    {
        shown = EntityGuid{};
    }
    if (!context.dryRun())
    {
        view.setPreviewCamera(shown);
    }
    CameraPreviewResult result;
    if (const Entity entity = shown.isNull() ? Entity{} : context.index().find(shown); !entity.isNull())
    {
        result.entity = context.summary(entity);
    }
    return result;
}

} // namespace

void registerCameraCommands(CommandRegistry& registry)
{
    registry.add<&cameraList>({
        .name = "camera.list",
        .title = strings::kCommandCameraListTitle,
        .description = strings::kCommandCameraListDescription,
        .agentSummary =
            "Lists the VirtualCameras of the scene (or of the running game) with their priority, enabled "
            "and inactive flags, lens in degrees and metres, body stage (Follow, Orbit, SpringArm, Dolly or "
            "None), aim stage (LookAt, Composer or None), noise, follow and look-at targets and whether a "
            "brain shows them now (live), and the CameraBrains with their default blend. The brain shows "
            "the enabled camera with the highest priority; ties go to the one that became capable last. "
            "Use it to check a rig before play.start. Example: {}.",
        .category = "Camera",
        .icon = "videocam",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Read,
    });
    registry.add<&cameraStatus>({
        .name = "camera.status",
        .title = strings::kCommandCameraStatusTitle,
        .description = strings::kCommandCameraStatusDescription,
        .agentSummary =
            "Shows what each CameraBrain shows in the running game: the live virtual camera, the one it "
            "blends from, the blend progress (0 to 1) and the weight of the live camera, and the world "
            "position, rotation (pitch, yaw, roll in degrees) and field of view it wrote to the Camera. "
            "Only a play session evaluates cameras: in the edit World the result says so (live false). "
            "Use play.step, then camera.status, to test a blend or a spring arm deterministically. "
            "Example: {}.",
        .category = "Camera",
        .icon = "monitoring",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Read,
    });
    registry.add<&cameraActivate>({
        .name = "camera.activate",
        .title = strings::kCommandCameraActivateTitle,
        .description = strings::kCommandCameraActivateDescription,
        .agentSummary =
            "Makes a VirtualCamera the one the brain shows by giving it a priority one above every other "
            "enabled camera (unchanged when it is above them already). It edits VirtualCamera.priority, so "
            "it is undoable and saved with the scene; the game blends to the camera when it starts. The "
            "camera must be enabled and not inactive. In a running game CameraService.activate does the "
            "same for modules. The result has the priority before and now. Example: {\"entity\": "
            "\"/Rigs/Shoulder\"}.",
        .category = "Camera",
        .icon = "switch_video",
        .flags = {.idempotent = true, .undoable = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Edit,
    });
    registry.add<&cameraPreview>({
        .name = "camera.preview",
        .title = strings::kCommandCameraPreviewTitle,
        .description = strings::kCommandCameraPreviewDescription,
        .agentSummary =
            "Makes the Game views of the edited scene look through a VirtualCamera (entity) at the "
            "camera's own Transform and lens instead of through the scene's main Camera; clear true goes "
            "back to the main Camera, and neither reports the state. Stages need a play session: in the "
            "edit World the view shows where the Entity stands, and while playing the Game view shows what "
            "the brain writes. Not undoable: it is editor state. Example: {\"entity\": \"/Rigs/Shoulder\"} "
            "or {\"clear\": true}.",
        .category = "Camera",
        .icon = "preview",
        .flags = {.idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
}

} // namespace illusion::editor::detail
