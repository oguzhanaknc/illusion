#include "CommandSupport.hpp"

#include <illusion/assets/Image.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

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

// Which viewports a command accepts.
enum class Needs : u8
{
    Any,          // every viewport
    EditorCamera, // Scene viewports (the Game view looks through a scene Camera)
};

Expected<Target, CommandError> findViewport(CommandContext& context, const std::optional<std::string>& id,
                                            Needs needs = Needs::Any)
{
    Expected<ViewportHost*, CommandError> host = requireViewports(context);
    if (!host)
    {
        return unexpected(std::move(host).error());
    }
    const std::string_view wanted = id ? std::string_view(*id) : kDefaultViewport;
    for (ViewportInfo& info : (*host)->viewports())
    {
        if (info.id != wanted)
        {
            continue;
        }
        if (needs == Needs::EditorCamera && info.kind == ViewportKind::Game)
        {
            return unexpected(CommandError::make(
                ErrorKind::PreconditionFailed,
                "the game view looks through the scene's highest-priority Camera: move that entity instead"));
        }
        return Target{*host, std::move(info)};
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

ViewportEntry entryOf(CommandContext& context, ViewportInfo info)
{
    EditorContext& editor = context.editor();
    std::optional<EntitySummary> sceneCamera;
    if (!info.sceneCamera.isNull() && editor.world().isAlive(info.sceneCamera))
    {
        sceneCamera = editor.entities().summary(info.sceneCamera);
    }
    return ViewportEntry{std::move(info.id), info.kind, info.size.x,   info.size.y,           info.visible,
                         info.camera,        info.eye,  info.settings, std::move(sceneCamera)};
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
        result.viewports.push_back(entryOf(context, std::move(info)));
    }
    return result;
}

Expected<ViewportCameraResult, CommandError> cameraGet(CommandContext& context, const ViewportParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport, Needs::EditorCamera);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    return cameraResult(target->info.id, target->info.camera);
}

Expected<ViewportCameraResult, CommandError> cameraSet(CommandContext& context,
                                                       const ViewportCameraSetParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport, Needs::EditorCamera);
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
    Expected<Target, CommandError> target = findViewport(context, params.viewport, Needs::EditorCamera);
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
    Expected<Target, CommandError> target = findViewport(context, params.viewport, Needs::EditorCamera);
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
    if (Expected<void, CommandError> scope =
            checkEnum(params.visualiserScope, ViewportVisualiserScope::All, "/visualiserScope");
        !scope)
    {
        return unexpected(std::move(scope).error());
    }
    ViewportSettings settings = target->info.settings;
    settings.grid = params.grid.value_or(settings.grid);
    settings.selectionOutline = params.selectionOutline.value_or(settings.selectionOutline);
    settings.hover = params.hover.value_or(settings.hover);
    settings.icons = params.icons.value_or(settings.icons);
    settings.viewCube = params.viewCube.value_or(settings.viewCube);
    settings.stats = params.stats.value_or(settings.stats);
    settings.cameraPreview = params.cameraPreview.value_or(settings.cameraPreview);
    settings.visualiserScope = params.visualiserScope.value_or(settings.visualiserScope);
    if (params.visualisers)
    {
        const ViewportVisualisersParams& kinds = *params.visualisers;
        ViewportVisualisers& visualisers = settings.visualisers;
        visualisers.lights = kinds.lights.value_or(visualisers.lights);
        visualisers.cameras = kinds.cameras.value_or(visualisers.cameras);
        visualisers.colliders = kinds.colliders.value_or(visualisers.colliders);
        visualisers.characters = kinds.characters.value_or(visualisers.characters);
        visualisers.joints = kinds.joints.value_or(visualisers.joints);
        visualisers.audio = kinds.audio.value_or(visualisers.audio);
    }
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
    const ViewportPoint projected =
        projectToViewport(target->info.projection * target->info.view, target->info.size, point);
    return ViewportProjectResult{projected.pixel.x, projected.pixel.y, projected.depth, projected.inFront,
                                 projected.inside};
}

// Changes the selection by @p select with the entities a pick hit (none: the background).
void applySelection(Selection& selection, PickSelect select, std::span<const EntityGuid> hits)
{
    switch (select)
    {
    case PickSelect::None:
        break;
    case PickSelect::Replace:
        if (hits.empty())
        {
            selection.clear();
        }
        else
        {
            selection.set(hits);
        }
        break;
    case PickSelect::Add:
        for (const EntityGuid guid : hits)
        {
            selection.add(guid);
        }
        break;
    case PickSelect::Toggle:
        for (const EntityGuid guid : hits)
        {
            if (!selection.remove(guid))
            {
                selection.add(guid);
            }
        }
        break;
    }
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
        applySelection(editor.selection(), m_select,
                       alive ? std::span<const EntityGuid>(&guid, 1) : std::span<const EntityGuid>());
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

// The viewport of @p target has an image to read: its panel was shown or it has a fixed resolution.
Expected<void, CommandError> requireImage(const Target& target)
{
    if (target.info.size.x == 0 || target.info.size.y == 0)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the viewport has no image yet: show its panel (ui.show_panel) "
                                             "or give it a fixed resolution (viewport.set_aspect)"));
    }
    if (target.info.kind == ViewportKind::Game && target.info.sceneCamera.isNull())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the scene has no active Camera for the game view"));
    }
    return {};
}

Expected<void, CommandError> checkPixel(const Target& target, f32 x, f32 y)
{
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || y < 0.0f ||
        x >= static_cast<f32>(target.info.size.x) || y >= static_cast<f32>(target.info.size.y))
    {
        return unexpected(CommandError::invalid("", "x and y must lie within the viewport (" +
                                                        std::to_string(target.info.size.x) + "x" +
                                                        std::to_string(target.info.size.y) + " pixels)"));
    }
    return {};
}

// Waits for the viewport to find a vertex or a surface under a pixel.
class SnapTask final : public CommandTask<ViewportSnapResult>
{
public:
    SnapTask(ViewportHost& host, u64 ticket) noexcept
        : m_host(&host)
        , m_ticket(ticket)
    {
    }

    Expected<std::optional<ViewportSnapResult>, CommandError> update(CommandContext& context,
                                                                     CommandProgress& progress) override
    {
        if (context.editor().viewports() != m_host)
        {
            return unexpected(CommandError::make(ErrorKind::Unsupported, "the viewports closed"));
        }
        std::optional<Expected<std::optional<ViewportSnap>, CommandError>> done = m_host->snap(m_ticket);
        if (!done)
        {
            progress.message = "waiting for the meshes and the GPU";
            return std::optional<ViewportSnapResult>{};
        }
        if (!*done)
        {
            return unexpected(std::move(*done).error());
        }
        ViewportSnapResult result;
        if (const std::optional<ViewportSnap>& found = done->value())
        {
            result.hit = true;
            result.kind = found->kind;
            result.position = found->position;
            if (found->kind == ViewportSnapTarget::Surface)
            {
                result.normal = found->normal;
                result.rotation = alignToNormal(Quat(1.0f, 0.0f, 0.0f, 0.0f), found->normal);
            }
            if (context.editor().world().isAlive(found->entity))
            {
                result.entity = context.editor().entities().summary(found->entity);
            }
        }
        return std::optional<ViewportSnapResult>(std::move(result));
    }

private:
    ViewportHost* m_host;
    u64 m_ticket;
};

Expected<std::unique_ptr<CommandTask<ViewportSnapResult>>, CommandError>
viewportSnap(CommandContext& context, const ViewportSnapParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (Expected<void, CommandError> valid = checkEnum(params.to, ViewportSnapTarget::Surface, "/to"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    const f32 tolerance = params.tolerance.value_or(kVertexSnapTolerance);
    if (!(tolerance >= kMinSnapTolerance && tolerance <= kMaxSnapTolerance))
    {
        return unexpected(CommandError::invalid("/tolerance", "must be from 1 to 64 pixels"));
    }
    if (Expected<void, CommandError> image = requireImage(*target); !image)
    {
        return unexpected(std::move(image).error());
    }
    if (Expected<void, CommandError> pixel = checkPixel(*target, params.x, params.y); !pixel)
    {
        return unexpected(std::move(pixel).error());
    }
    std::vector<Entity> exclude;
    if (params.exclude && !params.exclude->empty())
    {
        Expected<std::vector<Entity>, CommandError> resolved = resolveEntityList(context, *params.exclude);
        if (!resolved)
        {
            return unexpected(std::move(resolved).error());
        }
        exclude = std::move(resolved).value();
    }
    ViewportSnapRequest request;
    request.pixel = Vec2(params.x, params.y);
    request.target = params.to.value_or(ViewportSnapTarget::Any);
    request.tolerance = tolerance;
    request.exclude = exclude;
    Expected<u64, CommandError> ticket = target->host->requestSnap(target->info.id, request);
    if (!ticket)
    {
        return unexpected(std::move(ticket).error());
    }
    return std::unique_ptr<CommandTask<ViewportSnapResult>>(
        std::make_unique<SnapTask>(*target->host, *ticket));
}

// Waits for a viewport readback, then turns it into the command's result.
template <class R>
class ReadbackTask final : public CommandTask<R>
{
public:
    using Finish = Expected<R, CommandError> (*)(CommandContext&, ViewportReadback&, PickSelect);

    ReadbackTask(ViewportHost& host, u64 ticket, Finish finish, PickSelect select) noexcept
        : m_host(&host)
        , m_ticket(ticket)
        , m_finish(finish)
        , m_select(select)
    {
    }

    Expected<std::optional<R>, CommandError> update(CommandContext& context,
                                                    CommandProgress& progress) override
    {
        if (context.editor().viewports() != m_host)
        {
            return unexpected(CommandError::make(ErrorKind::Unsupported, "the viewports closed"));
        }
        std::optional<Expected<ViewportReadback, CommandError>> done = m_host->readback(m_ticket);
        if (!done)
        {
            progress.message = "waiting for the GPU";
            return std::optional<R>{};
        }
        if (!*done)
        {
            return unexpected(std::move(*done).error());
        }
        Expected<R, CommandError> result = m_finish(context, done->value(), m_select);
        if (!result)
        {
            return unexpected(std::move(result).error());
        }
        return std::optional<R>(std::move(result).value());
    }

private:
    ViewportHost* m_host;
    u64 m_ticket;
    Finish m_finish;
    PickSelect m_select;
};

template <class R>
Expected<std::unique_ptr<CommandTask<R>>, CommandError>
startReadback(const Target& target, ViewportChannel channel, UVec2 origin, UVec2 extent,
              typename ReadbackTask<R>::Finish finish, PickSelect select = PickSelect::None)
{
    Expected<u64, CommandError> ticket =
        target.host->requestReadback(target.info.id, channel, origin, extent);
    if (!ticket)
    {
        return unexpected(std::move(ticket).error());
    }
    return std::unique_ptr<CommandTask<R>>(
        std::make_unique<ReadbackTask<R>>(*target.host, *ticket, finish, select));
}

Expected<ScreenshotResult, CommandError> finishScreenshot(CommandContext&, ViewportReadback& readback,
                                                          PickSelect)
{
    Image image;
    image.format = ImageFormat::RGBA8Srgb;
    image.data.resize(readback.color.size());
    std::memcpy(image.data.data(), readback.color.data(), readback.color.size());
    image.mips.push_back(ImageMip{readback.extent.x, readback.extent.y, 0, image.data.size()});
    const std::vector<std::byte> png = encodePng(image);
    if (png.empty())
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "the image could not be encoded"));
    }
    ScreenshotResult result;
    result.width = readback.extent.x;
    result.height = readback.extent.y;
    result._images.push_back(ResultImage{
        "image/png", encodeBase64(std::string_view(reinterpret_cast<const char*>(png.data()), png.size()))});
    return result;
}

Expected<std::unique_ptr<CommandTask<ScreenshotResult>>, CommandError>
viewportScreenshot(CommandContext& context, const ViewportParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (Expected<void, CommandError> image = requireImage(*target); !image)
    {
        return unexpected(std::move(image).error());
    }
    return startReadback<ScreenshotResult>(*target, ViewportChannel::Color, UVec2(0), target->info.size,
                                           &finishScreenshot);
}

Expected<ViewportSampleResult, CommandError> finishSample(CommandContext&, ViewportReadback& readback,
                                                          PickSelect)
{
    ViewportSampleResult result;
    const usize pixels = readback.color.size() / 4;
    if (pixels == 0)
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "the sample read no pixels"));
    }
    Vec4 sum{0.0f};
    for (usize i = 0; i < pixels; ++i)
    {
        for (u32 c = 0; c < 4; ++c)
        {
            sum[static_cast<int>(c)] += static_cast<f32>(readback.color[i * 4 + c]) / 255.0f;
        }
    }
    result.color = sum / static_cast<f32>(pixels);
    result.pixels = static_cast<u32>(pixels);
    const f32 high = std::max({result.color.r, result.color.g, result.color.b});
    const f32 low = std::min({result.color.r, result.color.g, result.color.b});
    const f32 range = high - low;
    result.value = high;
    result.saturation = high > 0.0f ? range / high : 0.0f;
    if (range > 0.0f)
    {
        f32 hue = 0.0f;
        if (high == result.color.r)
        {
            hue = std::fmod((result.color.g - result.color.b) / range, 6.0f);
        }
        else if (high == result.color.g)
        {
            hue = (result.color.b - result.color.r) / range + 2.0f;
        }
        else
        {
            hue = (result.color.r - result.color.g) / range + 4.0f;
        }
        hue *= 60.0f;
        result.hue = hue < 0.0f ? hue + 360.0f : hue;
    }
    return result;
}

Expected<std::unique_ptr<CommandTask<ViewportSampleResult>>, CommandError>
viewportSample(CommandContext& context, const ViewportSampleParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (Expected<void, CommandError> image = requireImage(*target); !image)
    {
        return unexpected(std::move(image).error());
    }
    if (Expected<void, CommandError> pixel = checkPixel(*target, params.x, params.y); !pixel)
    {
        return unexpected(std::move(pixel).error());
    }
    const u32 radius = params.radius.value_or(0);
    if (radius > 8)
    {
        return unexpected(CommandError::invalid("/radius", "must be 0 to 8"));
    }
    const UVec2 center(static_cast<u32>(params.x), static_cast<u32>(params.y));
    const UVec2 low(center.x > radius ? center.x - radius : 0, center.y > radius ? center.y - radius : 0);
    const UVec2 high = glm::min(center + radius, target->info.size - 1u);
    return startReadback<ViewportSampleResult>(*target, ViewportChannel::Color, low, high - low + 1u,
                                               &finishSample);
}

Expected<ViewportPickRectResult, CommandError> finishPickRect(CommandContext& context,
                                                              ViewportReadback& readback, PickSelect select)
{
    EditorContext& editor = context.editor();
    ViewportPickRectResult result;
    std::vector<EntityGuid> guids;
    for (const Entity entity : readback.entities)
    {
        if (editor.world().isAlive(entity))
        {
            result.entities.push_back(editor.entities().summary(entity));
            guids.push_back(editor.entities().guidOf(entity));
        }
    }
    if (!context.dryRun())
    {
        applySelection(editor.selection(), select, guids);
    }
    return result;
}

Expected<std::unique_ptr<CommandTask<ViewportPickRectResult>>, CommandError>
viewportPickRect(CommandContext& context, const ViewportPickRectParams& params)
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
    if (Expected<void, CommandError> image = requireImage(*target); !image)
    {
        return unexpected(std::move(image).error());
    }
    if (Expected<void, CommandError> pixel = checkPixel(*target, params.x, params.y); !pixel)
    {
        return unexpected(std::move(pixel).error());
    }
    if (!std::isfinite(params.width) || !std::isfinite(params.height) || params.width < 1.0f ||
        params.height < 1.0f)
    {
        return unexpected(CommandError::invalid("", "width and height must be at least 1 pixel"));
    }
    // The rectangle is clipped to the image.
    const UVec2 origin(static_cast<u32>(params.x), static_cast<u32>(params.y));
    const Vec2 end =
        glm::min(Vec2(params.x + params.width, params.y + params.height), Vec2(target->info.size));
    const UVec2 extent = glm::max(UVec2(glm::ceil(end)) - origin, UVec2(1u));
    return startReadback<ViewportPickRectResult>(*target, ViewportChannel::Entities, origin, extent,
                                                 &finishPickRect, params.select.value_or(PickSelect::None));
}

Expected<ViewportEntry, CommandError> viewportCreate(CommandContext& context, const NoParams&)
{
    Expected<ViewportHost*, CommandError> host = requireViewports(context);
    if (!host)
    {
        return unexpected(std::move(host).error());
    }
    if (context.dryRun())
    {
        const std::vector<ViewportInfo> viewports = (*host)->viewports();
        const auto secondary =
            std::count_if(viewports.begin(), viewports.end(), [](const ViewportInfo& info) {
                return info.id != "scene" && info.kind == ViewportKind::Scene;
            });
        if (static_cast<u32>(secondary) >= kMaxSecondaryViewports)
        {
            return unexpected(CommandError::make(ErrorKind::LimitExceeded, "at most 3 extra viewports"));
        }
        Expected<Target, CommandError> scene = findViewport(context, std::nullopt);
        if (!scene)
        {
            return unexpected(std::move(scene).error());
        }
        return entryOf(context, std::move(scene->info));
    }
    Expected<std::string, CommandError> id = (*host)->createViewport();
    if (!id)
    {
        return unexpected(std::move(id).error());
    }
    Expected<Target, CommandError> created = findViewport(context, *id);
    if (!created)
    {
        return unexpected(std::move(created).error());
    }
    return entryOf(context, std::move(created->info));
}

Expected<ViewportIdResult, CommandError> viewportClose(CommandContext& context, const ViewportParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (target->info.id == "scene" || target->info.id == "game")
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the \"scene\" and \"game\" viewports cannot be closed"));
    }
    if (!context.dryRun())
    {
        if (Expected<void, CommandError> closed = target->host->closeViewport(target->info.id); !closed)
        {
            return unexpected(std::move(closed).error());
        }
    }
    return ViewportIdResult{target->info.id};
}

Expected<ViewportSettingsResult, CommandError> setAspect(CommandContext& context,
                                                         const ViewportAspectParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (Expected<void, CommandError> aspect =
            checkEnum(std::optional<ViewportAspect>(params.aspect), ViewportAspect::Fixed, "/aspect");
        !aspect)
    {
        return unexpected(std::move(aspect).error());
    }
    ViewportSettings settings = target->info.settings;
    settings.aspect = params.aspect;
    settings.width = 0;
    settings.height = 0;
    if (params.aspect == ViewportAspect::Fixed)
    {
        if (!params.width || !params.height)
        {
            return unexpected(CommandError::invalid("", "\"fixed\" needs width and height"));
        }
        if (*params.width < kViewportMinResolution || *params.width > kViewportMaxWidth)
        {
            return unexpected(CommandError::invalid("/width", "must be 16 to 7,680"));
        }
        if (*params.height < kViewportMinResolution || *params.height > kViewportMaxHeight)
        {
            return unexpected(CommandError::invalid("/height", "must be 16 to 4,320"));
        }
        settings.width = *params.width;
        settings.height = *params.height;
    }
    else if (params.width || params.height)
    {
        return unexpected(CommandError::invalid("", "width and height need the aspect \"fixed\""));
    }
    return applySettings(context, *target, settings);
}

Expected<void, CommandError> checkSlot(u32 slot)
{
    if (slot < 1 || slot > kCameraBookmarkSlots)
    {
        return unexpected(CommandError::invalid("/slot", "must be 1 to 9"));
    }
    return {};
}

Expected<ViewportCameraResult, CommandError> bookmarkSet(CommandContext& context,
                                                         const CameraBookmarkParams& params)
{
    const u32 slot = params.slot.value_or(1);
    if (Expected<void, CommandError> valid = checkSlot(slot); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<Target, CommandError> target = findViewport(context, params.viewport, Needs::EditorCamera);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    if (!context.dryRun())
    {
        context.editor().view().setBookmark(slot, target->info.camera);
    }
    return cameraResult(target->info.id, target->info.camera);
}

Expected<ViewportCameraResult, CommandError> bookmarkGo(CommandContext& context,
                                                        const CameraBookmarkParams& params)
{
    const u32 slot = params.slot.value_or(1);
    if (Expected<void, CommandError> valid = checkSlot(slot); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<Target, CommandError> target = findViewport(context, params.viewport, Needs::EditorCamera);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    const std::optional<ViewportCamera> camera = context.editor().view().bookmark(slot);
    if (!camera)
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "bookmark " + std::to_string(slot) + " is empty"));
    }
    return moveCamera(context, *target, *camera, params.animate);
}

Expected<RenderStatsResult, CommandError> renderStats(CommandContext& context, const ViewportParams& params)
{
    Expected<Target, CommandError> target = findViewport(context, params.viewport);
    if (!target)
    {
        return unexpected(std::move(target).error());
    }
    Expected<ViewportRenderStats, CommandError> stats = target->host->renderStats(target->info.id);
    if (!stats)
    {
        return unexpected(std::move(stats).error());
    }
    return RenderStatsResult{target->info.id, target->info.size.x, target->info.size.y, *stats};
}

Expected<RenderGpuInfoResult, CommandError> renderGpuInfo(CommandContext& context, const NoParams&)
{
    ViewportHost* host = context.editor().viewports();
    if (host == nullptr)
    {
        return RenderGpuInfoResult{};
    }
    return RenderGpuInfoResult{true, host->gpuInfo()};
}

Expected<RenderDiagnosticsResult, CommandError> renderDiagnostics(CommandContext& context, const NoParams&)
{
    ViewportHost* host = context.editor().viewports();
    if (host == nullptr)
    {
        return RenderDiagnosticsResult{};
    }
    const ViewportGpuInfo gpu = host->gpuInfo();
    return RenderDiagnosticsResult{true, gpu.validation, gpu.validationErrors};
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
        .agentSummary = "Turns a viewport's overlays on or off: the ground grid, the selection outline, the "
                        "hover highlight, icon billboards, the view cube, the stats overlay, the "
                        "picture-in-picture camera preview and the DebugDraw visualisers (lights, cameras, "
                        "colliders, characters, joints, audio) of the selected or of all entities. Only the "
                        "given fields change. Example: {\"visualisers\": {\"colliders\": true}, "
                        "\"visualiserScope\": \"all\", \"stats\": true}.",
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
    registry.addAsync<&viewportPickRect>({
        .name = "viewport.pick_rect",
        .title = strings::kCommandViewportPickRectTitle,
        .description = strings::kCommandViewportPickRectDescription,
        .agentSummary = "Asynchronous: returns every entity drawn in a rectangle of viewport pixels (box "
                        "select; hidden and locked entities are skipped, the rectangle is clipped to the "
                        "image) and optionally changes the selection with them. Example: {\"x\": 100, \"y\": "
                        "80, \"width\": 400, \"height\": 300, \"select\": \"replace\"}.",
        .category = "Viewport",
        .icon = "select",
        .flags = {.needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.addAsync<&viewportSnap>({
        .name = "viewport.snap",
        .title = strings::kCommandViewportSnapTitle,
        .description = strings::kCommandViewportSnapDescription,
        .agentSummary = "Asynchronous: finds the point to snap to at a viewport pixel: the mesh vertex that "
                        "appears nearest within tolerance pixels (to \"vertex\"), the opaque surface under "
                        "the pixel with its normal (to \"surface\"), or a vertex if one is near and the "
                        "surface otherwise (\"any\", default). Exclude the entities you are about to move; "
                        "then place them with transform.set (position, and rotation to align +y to the "
                        "surface). Example: {\"x\": 640, \"y\": 360, \"to\": \"surface\", "
                        "\"exclude\": [\"$selection\"]}.",
        .category = "Viewport",
        .icon = "my_location",
        .flags = {.readOnly = true, .idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
    });
    registry.addAsync<&viewportSample>({
        .name = "viewport.sample",
        .title = strings::kCommandViewportSampleTitle,
        .description = strings::kCommandViewportSampleDescription,
        .agentSummary = "Asynchronous: returns the displayed colour at a viewport pixel (sRGB RGBA from 0 to "
                        "1, averaged over radius pixels around it) with its hue in degrees, saturation and "
                        "value, to check what the scene looks like. Example: {\"viewport\": \"game\", \"x\": "
                        "320, \"y\": 180, \"radius\": 2}.",
        .category = "Viewport",
        .icon = "colorize",
        .flags = {.readOnly = true, .idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
    });
    registry.addAsync<&viewportScreenshot>({
        .name = "viewport.screenshot",
        .title = strings::kCommandViewportScreenshotTitle,
        .description = strings::kCommandViewportScreenshotDescription,
        .agentSummary = "Asynchronous: captures a viewport's image (the rendered scene with its overlays, "
                        "without the editor UI) as a PNG at the viewport's size; give it a fixed resolution "
                        "with viewport.set_aspect first for a repeatable size. Example: {\"viewport\": "
                        "\"game\"}.",
        .category = "Viewport",
        .icon = "screenshot_monitor",
        .flags = {.readOnly = true, .idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
    });
    registry.add<&viewportCreate>({
        .name = "viewport.create",
        .title = strings::kCommandViewportCreateTitle,
        .description = strings::kCommandViewportCreateDescription,
        .agentSummary = "Opens an extra Scene viewport (\"scene-2\" to \"scene-4\", at most three) with the "
                        "camera and settings of \"scene\" and returns it; it renders fewer, smaller shadow "
                        "cascades. Show it with its panel or give it a fixed resolution. Example: {}.",
        .category = "Viewport",
        .icon = "add_to_queue",
        .flags = {.needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&viewportClose>({
        .name = "viewport.close",
        .title = strings::kCommandViewportCloseTitle,
        .description = strings::kCommandViewportCloseDescription,
        .agentSummary =
            "Closes an extra Scene viewport opened by viewport.create (\"scene\" and \"game\" stay). "
            "Example: {\"viewport\": \"scene-2\"}.",
        .category = "Viewport",
        .icon = "remove_from_queue",
        .flags = {.idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&setAspect>({
        .name = "viewport.set_aspect",
        .title = strings::kCommandViewportSetAspectTitle,
        .description = strings::kCommandViewportSetAspectDescription,
        .agentSummary =
            "Sets the shape of a viewport's image inside its panel (letterboxed): free, 16:9, "
            "16:10, 4:3, 21:9, 1:1, or fixed with width and height in pixels, which also renders "
            "the viewport while its panel is hidden. Example: {\"viewport\": \"game\", \"aspect\": "
            "\"fixed\", \"width\": 640, \"height\": 360}.",
        .category = "Viewport",
        .icon = "aspect_ratio",
        .flags = {.idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&bookmarkSet>({
        .name = "camera.bookmark_set",
        .title = strings::kCommandCameraBookmarkSetTitle,
        .description = strings::kCommandCameraBookmarkSetDescription,
        .agentSummary = "Stores a Scene viewport's editor camera in bookmark slot 1 to 9 (Ctrl+Shift+1..9 in "
                        "a viewport); bookmarks last until the scene is replaced. Example: {\"slot\": 1}.",
        .category = "Viewport",
        .icon = "bookmark_add",
        .flags = {.idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&bookmarkGo>({
        .name = "camera.bookmark_go",
        .title = strings::kCommandCameraBookmarkGoTitle,
        .description = strings::kCommandCameraBookmarkGoDescription,
        .agentSummary = "Moves a Scene viewport's editor camera to bookmark slot 1 to 9 (Ctrl+1..9 in a "
                        "viewport), animated unless animate is false; error 1200 for an empty slot. Example: "
                        "{\"slot\": 1, \"animate\": false}.",
        .category = "Viewport",
        .icon = "bookmark",
        .flags = {.idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&renderStats>({
        .name = "render.stats",
        .title = strings::kCommandRenderStatsTitle,
        .description = strings::kCommandRenderStatsDescription,
        .agentSummary =
            "Returns what the renderer did in a viewport's last frame: frames rendered, RenderMesh "
            "candidates, visible entities after culling, draw calls, triangles, shadow casters "
            "and dropped lights. Render it first (viewport.screenshot or a shown panel). Example: "
            "{\"viewport\": \"game\"}.",
        .category = "Render",
        .icon = "monitoring",
        .flags = {.readOnly = true, .idempotent = true, .needsGpu = true},
        .mode = CommandMode::Any,
    });
    registry.add<&renderGpuInfo>({
        .name = "render.gpu_info",
        .title = strings::kCommandRenderGpuInfoTitle,
        .description = strings::kCommandRenderGpuInfoDescription,
        .agentSummary = "Returns the GPU the editor renders with (name, driver, Vulkan version, software or "
                        "discrete, bindless, validation), or available: false without one. Example: {}.",
        .category = "Render",
        .icon = "memory",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&renderDiagnostics>({
        .name = "render.diagnostics",
        .title = strings::kCommandRenderDiagnosticsTitle,
        .description = strings::kCommandRenderDiagnosticsDescription,
        .agentSummary = "Returns whether Vulkan validation is on and how many validation errors the renderer "
                        "caused (0 is required), or available: false without a GPU. Example: {}.",
        .category = "Render",
        .icon = "bug_report",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
}

} // namespace illusion::editor::detail
