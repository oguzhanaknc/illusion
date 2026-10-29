#include "ViewportOverlays.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/core/Preferences.hpp>
#include <illusion/editor/ui/UiRenderer.hpp>
#include <illusion/editor/viewport/EditorCamera.hpp>
#include <illusion/editor/viewport/PickCandidates.hpp>
#include <illusion/editor/viewport/ResizePolicy.hpp>
#include <illusion/editor/viewport/ViewportService.hpp>
#include <illusion/editor/viewport/Visualisers.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/DebugDraw.hpp>
#include <illusion/render/ReadbackRing.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace illusion::editor::viewport
{
namespace
{

constexpr nvrhi::Format kColorFormat = nvrhi::Format::RGBA8_UNORM;
// Frames a replaced texture stays registered with the UI (frames in flight may still sample it).
constexpr u32 kRetireFrames = 4;
constexpr u32 kOutlineRadius = 2;
constexpr u32 kHoverRadius = 1;
// Repeated cycling picks within this many pixels count as the same spot.
constexpr f32 kCycleDistance = 2.0f;
constexpr std::string_view kSceneId = "scene";
constexpr std::string_view kGameId = "game";
// Shadows of the secondary viewports.
constexpr u32 kSecondaryCascades = 2;
constexpr u32 kSecondaryShadowResolution = 1024;
// Marks picks and readbacks of a closed viewport.
constexpr usize kClosed = ~usize{0};
// Readback slots shared by picks, hovers and readbacks.
constexpr u32 kReadbackSlots = 8;
// Lines the visualisers of one viewport can draw.
constexpr usize kVisualiserLines = 65536;
// Width of the picture-in-picture camera view: a quarter of the Scene view, within these bounds.
constexpr u32 kPreviewMinWidth = 128;
constexpr u32 kPreviewMaxWidth = 480;

RenderDebugView debugViewOf(ViewportShading shading) noexcept
{
    switch (shading)
    {
    case ViewportShading::Lit:
        return RenderDebugView::Lit;
    case ViewportShading::Unlit:
        return RenderDebugView::Unlit;
    case ViewportShading::Albedo:
        return RenderDebugView::Albedo;
    case ViewportShading::Normals:
        return RenderDebugView::Normals;
    case ViewportShading::Roughness:
        return RenderDebugView::Roughness;
    case ViewportShading::Metallic:
        return RenderDebugView::Metallic;
    case ViewportShading::AmbientOcclusion:
        return RenderDebugView::AmbientOcclusion;
    case ViewportShading::ShadowCascades:
        return RenderDebugView::ShadowCascades;
    case ViewportShading::MotionVectors:
        return RenderDebugView::MotionVectors;
    }
    return RenderDebugView::Lit;
}

void setBit(std::vector<u64>& bits, u32 index)
{
    const usize word = index / 64;
    if (word >= bits.size())
    {
        bits.resize(word + 1, 0);
    }
    bits[word] |= u64{1} << (index % 64);
}

bool hasBit(std::span<const u64> bits, u32 index) noexcept
{
    const usize word = index / 64;
    return word < bits.size() && (bits[word] >> (index % 64) & 1u) != 0;
}

bool hasVisualisers(const ViewportVisualisers& kinds) noexcept
{
    return kinds.lights || kinds.cameras || kinds.colliders || kinds.characters || kinds.joints ||
           kinds.audio;
}

// The grid plane, cell size and fades for a camera (see Grid.slang).
detail::GridPass gridPass(const ViewportCamera& camera, const Mat4& viewProjection,
                          const ViewportStyle& style)
{
    detail::GridPass pass;
    pass.inverseViewProjection = glm::inverse(viewProjection);
    const Vec3 eye = viewportEye(camera);
    const Vec3 forward = viewportForward(camera);
    const bool orthographic = camera.projection == ViewportProjection::Orthographic;
    // Orthographic side views draw the grid in the plane facing the camera.
    u32 axis = 1;
    if (orthographic && std::abs(forward.x) > 0.99f)
    {
        axis = 0;
    }
    else if (orthographic && std::abs(forward.z) > 0.99f)
    {
        axis = 2;
    }
    const f32 extent = orthographic ? camera.orthographicHeight * 0.1f
                                    : std::max(std::abs(eye[static_cast<int>(axis)]), camera.distance * 0.1f);
    const f32 level = std::log10(std::max(extent, 0.001f));
    const f32 cell = std::pow(10.0f, std::floor(level));
    pass.eye = Vec4(eye, static_cast<f32>(axis));
    pass.scale = Vec4(cell, level - std::floor(level), orthographic ? 0.0f : extent * 60.0f,
                      orthographic ? 0.0f : 1.0f);
    pass.color = style.grid;
    // The line where the second coordinate is 0 runs along the first axis, and the other way round.
    const Vec4 axes[3] = {style.axisX, style.axisY, style.axisZ};
    const u32 first = axis == 0 ? 1u : 0u;
    const u32 second = axis == 2 ? 1u : 2u;
    pass.axisU = axes[first];
    pass.axisV = axes[second];
    return pass;
}

} // namespace

struct ViewportService::Impl
{
    struct Viewport
    {
        std::string id;
        ViewportKind kind = ViewportKind::Scene;
        EditorCamera camera;
        ViewportSettings settings;
        ViewportStyle style;
        ResizePolicy resize;
        f64 time = 0.0;
        std::unique_ptr<SceneRenderer> renderer;
        std::unique_ptr<detail::ViewportOverlays> overlays;
        std::unique_ptr<DebugDrawRenderer> debugRenderer;
        nvrhi::TextureHandle color;
        UVec2 colorSize{0};
        u64 texture = 0;
        bool drawn = false;        // frame() since the last render()
        bool visible = false;      // drawn in the last UI frame
        Mat4 viewProjection{1.0f}; // of the last render
        Vec2 lastPickPixel{-1000.0f};
        Entity lastPick{};
        Entity sceneCamera{}; // the Camera a Game viewport looked through last
        u32 frames = 0;
        RenderStats renderStats;
        // Hover: the entity under the cursor, read from the ids one pixel at a time.
        bool hoverWanted = false;
        Vec2 hoverPixel{0.0f};
        bool hoverSubmitted = false;
        u64 hoverReadback = 0;
        Entity hovered{};
        std::vector<ViewportIcon> icons;
    };

    struct Readback
    {
        u64 ticket = 0;
        usize viewport = 0;
        ViewportChannel channel = ViewportChannel::Color;
        UVec2 origin{0}; // image pixels
        UVec2 extent{0};
        bool submitted = false;
        u64 readback = 0;
        UVec2 readExtent{0}; // texels copied
        UVec2 size{0};       // image size when submitted
        std::optional<Expected<ViewportReadback, CommandError>> result;
    };

    struct Pick
    {
        u64 ticket = 0;
        usize viewport = 0;
        Vec2 pixel{0.0f};
        bool cycle = false;
        bool submitted = false;
        u64 readback = 0;
        UVec2 origin{0};
        UVec2 extent{0};
        UVec2 center{0};
        Mat4 viewProjection{1.0f};
        UVec2 size{0};
        std::optional<Expected<std::optional<ViewportHit>, CommandError>> result;
    };

    struct Retired
    {
        nvrhi::TextureHandle texture;
        u64 uiTexture = 0;
        u32 frames = 0;
    };

    Impl(RenderDevice& renderDevice, EditorContext& editorContext, ui::UiRenderer& uiRenderer)
        : device(renderDevice)
        , editor(editorContext)
        , ui(uiRenderer)
        , streamer(editorContext.files(), jobs, AssetStreamerDesc{.cacheMount = ""})
        , resources(renderDevice)
        , cache(resources, streamer)
        , resolver(cache)
    {
    }

    RenderDevice& device;
    EditorContext& editor;
    ui::UiRenderer& ui;
    JobSystem jobs;
    AssetStreamer streamer;
    RenderResources resources;
    RenderAssetCache cache;
    RenderAssetResolver resolver;
    HierarchyIndex hierarchy;
    std::unique_ptr<ReadbackRing> readbacks;
    nvrhi::CommandListHandle commands;
    std::vector<Viewport> viewports;
    std::vector<Pick> picks;
    std::vector<Readback> reads;
    std::vector<Retired> retired;
    // The picture-in-picture view through the selected Camera, shared by the Scene viewports.
    std::unique_ptr<SceneRenderer> previewRenderer;
    nvrhi::TextureHandle previewColor;
    UVec2 previewSize{0};
    u64 previewTexture = 0;
    Entity previewCamera{};
    u32 previewWidth = 0; // wanted this frame (0: no Scene viewport asked)
    DebugDraw debugDraw{kVisualiserLines};
    u64 nextTicket = 1;
    u64 viewportsVersion = 0;
    ViewportServiceStats stats;
    // Scratch, kept to avoid allocating every frame.
    std::vector<u64> hiddenBits;
    std::vector<u64> selectionBits;
    std::vector<u64> hoverBits;
    std::vector<u64> iconBits;
    std::vector<EntityIdTexel> texels;
    std::vector<Entity> candidates;
    std::vector<std::pair<f32, Entity>> behind;
    std::vector<u8> colorScratch;

    Viewport* find(std::string_view id) noexcept
    {
        const auto found = std::find_if(viewports.begin(), viewports.end(),
                                        [&](const Viewport& viewport) { return viewport.id == id; });
        return found != viewports.end() ? &*found : nullptr;
    }

    const Viewport* find(std::string_view id) const noexcept
    {
        const auto found = std::find_if(viewports.begin(), viewports.end(),
                                        [&](const Viewport& viewport) { return viewport.id == id; });
        return found != viewports.end() ? &*found : nullptr;
    }

    static CommandError notFound(std::string_view id)
    {
        return CommandError::make(ErrorKind::NotFound, "no viewport '" + std::string(id) + "'");
    }

    // Adds a viewport; secondary ones render fewer and smaller shadow cascades.
    bool addViewport(std::string id, ViewportKind kind, bool secondary)
    {
        Viewport viewport;
        viewport.id = std::move(id);
        viewport.kind = kind;
        if (kind == ViewportKind::Game)
        {
            // The game view shows what the game shows.
            viewport.settings.grid = false;
            viewport.settings.selectionOutline = false;
            viewport.settings.hover = false;
            viewport.settings.icons = false;
            viewport.settings.viewCube = false;
            viewport.settings.cameraPreview = false;
            viewport.settings.visualisers = ViewportVisualisers{false, false, false, false, false, false};
        }
        SceneRendererDesc desc;
        desc.colorFormat = kColorFormat;
        desc.entityIds = true;
        if (secondary)
        {
            desc.shadows.cascadeCount = kSecondaryCascades;
            desc.shadows.resolution = kSecondaryShadowResolution;
        }
        viewport.renderer = SceneRenderer::create(device, resources, desc);
        viewport.overlays = detail::ViewportOverlays::create(device, kColorFormat);
        viewport.debugRenderer = DebugDrawRenderer::create(device, kColorFormat);
        if (!viewport.renderer || !viewport.overlays || !viewport.debugRenderer)
        {
            return false;
        }
        viewport.icons.reserve(64);
        viewports.push_back(std::move(viewport));
        return true;
    }

    // Recreates the colour target of @p viewport at its settled size; the old one is retired.
    bool ensureTarget(Viewport& viewport)
    {
        const UVec2 size = viewport.resize.current();
        if (size.x == 0 || size.y == 0 || (viewport.color && viewport.colorSize == size))
        {
            return viewport.color != nullptr;
        }
        nvrhi::TextureDesc desc;
        desc.width = size.x;
        desc.height = size.y;
        desc.format = kColorFormat;
        desc.isRenderTarget = true;
        desc.debugName = "Viewport colour";
        desc.setClearValue(nvrhi::Color(0.0f));
        desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
        nvrhi::TextureHandle color = device.device()->createTexture(desc);
        if (!color)
        {
            ILL_LOG_ERROR("editor", "cannot create a {}x{} viewport target", size.x, size.y);
            return false;
        }
        if (viewport.color)
        {
            retired.push_back(Retired{viewport.color, viewport.texture, 0});
        }
        viewport.color = color;
        viewport.colorSize = size;
        viewport.texture = ui.registerTexture(color);
        return true;
    }

    // Bits of the entities the user hid (by Entity index).
    void buildHidden()
    {
        std::fill(hiddenBits.begin(), hiddenBits.end(), 0);
        for (const EntityGuid guid : editor.view().hiddenEntities())
        {
            if (const Entity entity = editor.entities().lookup(guid); !entity.isNull())
            {
                setBit(hiddenBits, entity.index);
            }
        }
    }

    void buildSelection()
    {
        std::fill(selectionBits.begin(), selectionBits.end(), 0);
        for (const EntityGuid guid : editor.selection().items())
        {
            if (const Entity entity = editor.entities().lookup(guid); !entity.isNull())
            {
                setBit(selectionBits, entity.index);
            }
        }
    }

    // The entity of the selection's primary id when it has a Camera (else null).
    [[nodiscard]] Entity selectedCamera()
    {
        World& world = editor.world();
        const Entity entity = editor.entities().lookup(editor.selection().primary());
        if (entity.isNull() || !world.isAlive(entity) || world.has<Inactive>(entity) ||
            !world.has<Camera>(entity) || !world.has<Transform>(entity))
        {
            return Entity{};
        }
        return entity;
    }

    // Width / height of what the game view shows (16:9 before it has a size).
    [[nodiscard]] f32 gameAspect() const noexcept
    {
        const auto game = std::find_if(viewports.begin(), viewports.end(), [](const Viewport& viewport) {
            return viewport.kind == ViewportKind::Game;
        });
        return game != viewports.end() ? aspectOf(game->resize.current()) : 16.0f / 9.0f;
    }

    // Renders the picture-in-picture view through the selected Camera at previewWidth.
    void renderPreview()
    {
        ILL_PROFILE_SCOPE();
        const u32 width = previewWidth;
        previewWidth = 0;
        previewCamera = width != 0 ? selectedCamera() : Entity{};
        if (previewCamera.isNull())
        {
            return;
        }
        const f32 aspect = gameAspect();
        const UVec2 size(width, std::max(1u, static_cast<u32>(static_cast<f32>(width) / aspect + 0.5f)));
        if (!previewRenderer)
        {
            SceneRendererDesc desc;
            desc.colorFormat = kColorFormat;
            desc.shadows.cascadeCount = kSecondaryCascades;
            desc.shadows.resolution = kSecondaryShadowResolution;
            previewRenderer = SceneRenderer::create(device, resources, desc);
            if (!previewRenderer)
            {
                ILL_LOG_ERROR("editor", "cannot create the camera preview renderer");
                previewCamera = Entity{};
                return;
            }
        }
        if (!previewColor || previewSize != size)
        {
            nvrhi::TextureDesc desc;
            desc.width = size.x;
            desc.height = size.y;
            desc.format = kColorFormat;
            desc.isRenderTarget = true;
            desc.debugName = "Camera preview";
            desc.setClearValue(nvrhi::Color(0.0f));
            desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
            nvrhi::TextureHandle color = device.device()->createTexture(desc);
            if (!color)
            {
                previewCamera = Entity{};
                return;
            }
            if (previewColor)
            {
                retired.push_back(Retired{previewColor, previewTexture, 0});
            }
            previewColor = color;
            previewSize = size;
            previewTexture = ui.registerTexture(color);
        }
        World& world = editor.world();
        const RenderView view = RenderView::fromCamera(world.get<Transform>(previewCamera),
                                                       world.get<Camera>(previewCamera), aspect);
        commands->open();
        previewRenderer->render(commands, previewColor, world, view, &jobs);
        commands->close();
        device.device()->executeCommandList(commands);
        ++stats.previews;
    }

    // Icon billboards of @p viewport from its last render, farthest first.
    void buildIcons(Viewport& viewport)
    {
        viewport.icons.clear();
        if (viewport.kind != ViewportKind::Scene || !viewport.settings.icons || viewport.colorSize.x == 0)
        {
            return;
        }
        std::fill(iconBits.begin(), iconBits.end(), 0);
        World& world = editor.world();
        const auto add = [&](Entity entity, const Transform& transform, ViewportIconKind kind) {
            if (hasBit(iconBits, entity.index) || hasBit(hiddenBits, entity.index))
            {
                return;
            }
            const ViewportPoint point =
                projectToViewport(viewport.viewProjection, viewport.colorSize, transform.position);
            if (!point.inside)
            {
                return;
            }
            setBit(iconBits, entity.index);
            viewport.icons.push_back(
                ViewportIcon{entity, kind, point.pixel, point.depth, hasBit(selectionBits, entity.index)});
        };
        world.query<Transform, Camera>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const Camera&) {
                add(entity, transform, ViewportIconKind::Camera);
            });
        world.query<Transform, DirectionalLight>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const DirectionalLight&) {
                add(entity, transform, ViewportIconKind::DirectionalLight);
            });
        world.query<Transform, PointLight>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const PointLight&) {
                add(entity, transform, ViewportIconKind::PointLight);
            });
        world.query<Transform, SkyLight>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const SkyLight&) {
                add(entity, transform, ViewportIconKind::SkyLight);
            });
        world.query<Transform, ProceduralSky>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const ProceduralSky&) {
                add(entity, transform, ViewportIconKind::SkyLight);
            });
        world.query<Transform, AudioSource>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const AudioSource&) {
                add(entity, transform, ViewportIconKind::AudioSource);
            });
        world.query<Transform>().with<AudioListener>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform) {
                add(entity, transform, ViewportIconKind::AudioListener);
            });
        std::sort(viewport.icons.begin(), viewport.icons.end(),
                  [](const ViewportIcon& a, const ViewportIcon& b) {
                      return a.depth < b.depth || (a.depth == b.depth && a.entity.index < b.entity.index);
                  });
    }

    // Asks for the id under the cursor of @p viewport (one at a time; after its render was submitted).
    void submitHover(Viewport& viewport)
    {
        const RenderViewTargets& targets = viewport.renderer->viewTargets();
        if (!viewport.hoverWanted || viewport.hoverSubmitted || targets.ids == nullptr ||
            targets.outputSize.x == 0 || targets.renderSize.x == 0)
        {
            return;
        }
        const Vec2 scale = Vec2(targets.renderSize) / Vec2(targets.outputSize);
        const UVec2 texel =
            glm::min(UVec2(glm::max(viewport.hoverPixel * scale, Vec2(0.0f))), targets.renderSize - 1u);
        // A full ring leaves the slots to picks and readbacks: try again next frame.
        if (const std::optional<u64> readback = readbacks->request(targets.ids, texel, UVec2(1)))
        {
            viewport.hoverSubmitted = true;
            viewport.hoverReadback = *readback;
        }
    }

    void readHovers(bool block)
    {
        for (Viewport& viewport : viewports)
        {
            if (!viewport.hoverSubmitted)
            {
                continue;
            }
            EntityIdTexel texel{};
            const ReadbackStatus status = readbacks->read(
                viewport.hoverReadback, std::as_writable_bytes(std::span<EntityIdTexel>(&texel, 1)), block);
            if (status == ReadbackStatus::Pending)
            {
                continue;
            }
            viewport.hoverSubmitted = false;
            const Entity entity{texel.index, texel.generation};
            viewport.hovered = status == ReadbackStatus::Ready && viewport.hoverWanted &&
                                       texel.index != ~0u && pickable(entity)
                                   ? entity
                                   : Entity{};
        }
    }

    [[nodiscard]] bool pickable(Entity entity)
    {
        World& world = editor.world();
        if (!world.isAlive(entity) || world.has<Inactive>(entity))
        {
            return false;
        }
        const EntityGuid guid = editor.entities().guidOf(entity);
        return !editor.view().hidden(guid) && !editor.view().locked(guid);
    }

    // The view of @p viewport for an image of @p aspect: its editor camera (with @p animated, where the
    // camera is now rather than where it goes), or the scene's main Camera (std::nullopt without one).
    std::optional<RenderView> viewOf(const Viewport& viewport, f32 aspect, bool animated)
    {
        if (viewport.kind == ViewportKind::Game)
        {
            World& world = editor.world();
            const Entity entity = findMainCamera(world);
            if (entity.isNull())
            {
                return std::nullopt;
            }
            return RenderView::fromCamera(world.get<Transform>(entity), world.get<Camera>(entity), aspect);
        }
        const ViewportCamera& camera = animated ? viewport.camera.camera() : viewport.camera.target();
        RenderView view;
        view.view = viewportViewMatrix(camera);
        view.projection = viewportProjectionMatrix(camera, aspect);
        view.position = viewportEye(camera);
        return view;
    }

    static f32 aspectOf(UVec2 size) noexcept
    {
        return size.x != 0 && size.y != 0 ? static_cast<f32>(size.x) / static_cast<f32>(size.y)
                                          : 16.0f / 9.0f;
    }

    // Renders @p viewport; false when there is nothing to render (no size, or no Camera for a Game view).
    bool renderViewport(Viewport& viewport)
    {
        ILL_PROFILE_SCOPE();
        if (viewport.kind == ViewportKind::Game)
        {
            viewport.sceneCamera = findMainCamera(editor.world());
            if (viewport.sceneCamera.isNull())
            {
                return false;
            }
        }
        if (!ensureTarget(viewport))
        {
            return false;
        }
        const ViewportCamera& camera = viewport.camera.camera();
        std::optional<RenderView> found = viewOf(viewport, aspectOf(viewport.colorSize), true);
        if (!found)
        {
            return false;
        }
        RenderView& view = *found;
        // The user's hidden entities are a Scene view setting; the game view shows what the game shows.
        if (viewport.kind == ViewportKind::Scene)
        {
            view.hiddenEntities = hiddenBits;
        }
        viewport.renderer->setDebugView(debugViewOf(viewport.settings.shading));
        (void)viewport.renderer->setWireframeOverlay(viewport.settings.wireframe);

        commands->open();
        viewport.renderer->render(commands, viewport.color, editor.world(), view, &jobs);
        const RenderViewTargets& targets = viewport.renderer->viewTargets();
        viewport.viewProjection = targets.unjitteredViewProjection;
        if (viewport.settings.grid && viewport.kind == ViewportKind::Scene)
        {
            (void)viewport.overlays->drawGrid(
                commands, viewport.color, targets,
                gridPass(camera, targets.unjitteredViewProjection, viewport.style));
        }
        const ViewportSettings& settings = viewport.settings;
        const bool all = settings.visualiserScope == ViewportVisualiserScope::All;
        if (viewport.kind == ViewportKind::Scene && hasVisualisers(settings.visualisers) &&
            (all || !editor.selection().empty()))
        {
            debugDraw.clear();
            VisualiserOptions options;
            options.kinds = settings.visualisers;
            options.all = all;
            options.selection = selectionBits;
            options.hidden = hiddenBits;
            options.cameraAspect = gameAspect();
            const VisualiserStats recorded = recordVisualisers(editor.world(), debugDraw, options);
            stats.visualisers += recorded.objects;
            if (!debugDraw.lines().empty())
            {
                (void)viewport.debugRenderer->render(commands, viewport.color, targets, debugDraw);
            }
        }
        const Entity hovered = viewport.hovered;
        if (settings.hover && !hovered.isNull() && editor.world().isAlive(hovered) &&
            !(settings.selectionOutline && hasBit(selectionBits, hovered.index)))
        {
            std::fill(hoverBits.begin(), hoverBits.end(), 0);
            setBit(hoverBits, hovered.index);
            (void)viewport.overlays->drawOutline(commands, viewport.color, targets, hoverBits,
                                                 viewport.style.hover, kHoverRadius);
        }
        if (settings.selectionOutline && !editor.selection().empty() && !selectionBits.empty())
        {
            (void)viewport.overlays->drawOutline(commands, viewport.color, targets, selectionBits,
                                                 viewport.style.selection, kOutlineRadius);
        }
        commands->close();
        device.device()->executeCommandList(commands);
        viewport.renderStats = viewport.renderer->stats();
        ++viewport.frames;
        ++stats.rendered;
        return true;
    }

    // Copies the ID texels around each new pick of @p index (after its render was submitted).
    void submitPicks(usize index)
    {
        Viewport& viewport = viewports[index];
        const RenderViewTargets& targets = viewport.renderer->viewTargets();
        for (Pick& pick : picks)
        {
            if (pick.viewport != index || pick.submitted || pick.result)
            {
                continue;
            }
            if (targets.ids == nullptr || targets.outputSize.x == 0 || targets.renderSize.x == 0)
            {
                pick.result.emplace(
                    unexpected(CommandError::make(ErrorKind::Internal, "the viewport has no entity ids")));
                continue;
            }
            const Vec2 scale = Vec2(targets.renderSize) / Vec2(targets.outputSize);
            const UVec2 texel =
                glm::min(UVec2(glm::max(pick.pixel * scale, Vec2(0.0f))), targets.renderSize - 1u);
            const UVec2 low = UVec2(texel.x > kPickRadius ? texel.x - kPickRadius : 0,
                                    texel.y > kPickRadius ? texel.y - kPickRadius : 0);
            const UVec2 high = glm::min(texel + kPickRadius, targets.renderSize - 1u);
            const std::optional<u64> readback = readbacks->request(targets.ids, low, high - low + 1u);
            if (!readback)
            {
                continue; // the ring is full: try again after the next render
            }
            pick.submitted = true;
            pick.readback = *readback;
            pick.origin = low;
            pick.extent = high - low + 1u;
            pick.center = texel - low;
            pick.viewProjection = viewport.viewProjection;
            pick.size = viewport.colorSize;
        }
    }

    // Entities whose bounds the pick ray crosses, nearest first, after those already in candidates.
    void appendBehind(const Pick& pick)
    {
        behind.clear();
        const Ray ray = screenToRay(pick.pixel, Vec2(pick.size), pick.viewProjection);
        editor.world().query<Transform, RenderMesh>().without<Inactive>().each(
            [&](Entity entity, const Transform& transform, const RenderMesh& mesh) {
                if ((mesh.flags & RenderMesh::kVisible) == 0 || mesh.mesh.index == 0)
                {
                    return;
                }
                const Aabb bounds =
                    resources.submeshBounds(mesh.mesh, mesh.submesh).transformed(transform.matrix());
                if (const std::optional<f32> hit = intersect(ray, bounds))
                {
                    behind.emplace_back(*hit, entity);
                }
            });
        std::sort(behind.begin(), behind.end(), [](const auto& a, const auto& b) {
            return a.first < b.first || (a.first == b.first && a.second.index < b.second.index);
        });
        for (const auto& [distance, entity] : behind)
        {
            if (std::find(candidates.begin(), candidates.end(), entity) == candidates.end())
            {
                candidates.push_back(entity);
            }
        }
    }

    void finishPick(Pick& pick)
    {
        Viewport& viewport = viewports[pick.viewport];
        candidates.clear();
        appendVisibleCandidates(texels, pick.extent, pick.center, candidates);
        appendBehind(pick);
        std::erase_if(candidates, [&](Entity entity) { return !pickable(entity); });
        if (candidates.empty())
        {
            viewport.lastPick = Entity{};
            pick.result.emplace(std::optional<ViewportHit>{});
            return;
        }
        const bool samePlace = glm::length(pick.pixel - viewport.lastPickPixel) <= kCycleDistance;
        const u32 index = chooseCandidate(candidates, samePlace ? viewport.lastPick : Entity{}, pick.cycle);
        viewport.lastPick = candidates[index];
        viewport.lastPickPixel = pick.pixel;
        pick.result.emplace(std::optional<ViewportHit>(
            ViewportHit{candidates[index], index, static_cast<u32>(candidates.size())}));
    }

    // Copies the regions of the new readbacks of @p index (after its render was submitted).
    void submitReads(usize index, bool rendered)
    {
        Viewport& viewport = viewports[index];
        const RenderViewTargets& targets = viewport.renderer->viewTargets();
        for (Readback& read : reads)
        {
            if (read.viewport != index || read.submitted || read.result)
            {
                continue;
            }
            if (!rendered)
            {
                read.result.emplace(unexpected(CommandError::make(
                    ErrorKind::PreconditionFailed, viewport.kind == ViewportKind::Game
                                                       ? "the scene has no active Camera for the game view"
                                                       : "the viewport has no image")));
                continue;
            }
            const UVec2 size = viewport.colorSize;
            if (read.origin.x >= size.x || read.origin.y >= size.y)
            {
                read.result.emplace(unexpected(
                    CommandError::make(ErrorKind::PreconditionFailed, "the viewport was resized")));
                continue;
            }
            const UVec2 extent = glm::min(read.extent, size - read.origin);
            std::optional<u64> ticket;
            UVec2 readExtent = extent;
            if (read.channel == ViewportChannel::Color)
            {
                ticket = readbacks->request(viewport.color, read.origin, extent);
            }
            else
            {
                if (targets.ids == nullptr || targets.outputSize.x == 0 || targets.renderSize.x == 0)
                {
                    read.result.emplace(unexpected(
                        CommandError::make(ErrorKind::Internal, "the viewport has no entity ids")));
                    continue;
                }
                const Vec2 scale = Vec2(targets.renderSize) / Vec2(targets.outputSize);
                const UVec2 low = glm::min(UVec2(Vec2(read.origin) * scale), targets.renderSize - 1u);
                const UVec2 high = glm::clamp(UVec2(glm::ceil(Vec2(read.origin + extent) * scale)), low + 1u,
                                              targets.renderSize);
                readExtent = high - low;
                ticket = readbacks->request(targets.ids, low, readExtent);
            }
            if (!ticket)
            {
                continue; // the ring is full: try again after the next render
            }
            read.submitted = true;
            read.readback = *ticket;
            read.extent = extent;
            read.readExtent = readExtent;
            read.size = size;
        }
    }

    void finishRead(Readback& read, std::span<const EntityIdTexel> ids)
    {
        ViewportReadback out;
        out.size = read.size;
        out.origin = read.origin;
        out.extent = read.extent;
        if (read.channel == ViewportChannel::Color)
        {
            out.color = std::move(colorScratch);
            colorScratch = {};
        }
        else
        {
            for (const EntityIdTexel& texel : ids)
            {
                if (texel.index != ~0u)
                {
                    out.entities.push_back(Entity{texel.index, texel.generation});
                }
            }
            std::sort(out.entities.begin(), out.entities.end(), [](Entity a, Entity b) {
                return a.index < b.index || (a.index == b.index && a.generation < b.generation);
            });
            out.entities.erase(std::unique(out.entities.begin(), out.entities.end()), out.entities.end());
            std::erase_if(out.entities, [&](Entity entity) { return !pickable(entity); });
        }
        read.result.emplace(std::move(out));
    }

    void readReads(bool block)
    {
        for (Readback& read : reads)
        {
            if (!read.submitted || read.result)
            {
                continue;
            }
            const usize count = static_cast<usize>(read.readExtent.x) * read.readExtent.y;
            ReadbackStatus status = ReadbackStatus::Invalid;
            if (read.channel == ViewportChannel::Color)
            {
                colorScratch.resize(count * 4);
                status = readbacks->read(read.readback, std::as_writable_bytes(std::span<u8>(colorScratch)),
                                         block);
            }
            else
            {
                texels.resize(count);
                status = readbacks->read(read.readback,
                                         std::as_writable_bytes(std::span<EntityIdTexel>(texels)), block);
            }
            if (status == ReadbackStatus::Ready)
            {
                finishRead(read, texels);
            }
            else if (status == ReadbackStatus::Invalid)
            {
                read.result.emplace(unexpected(
                    CommandError::make(ErrorKind::Internal, "the viewport could not be read back")));
            }
        }
    }

    // True while @p index has picks or readbacks that wait for a render.
    [[nodiscard]] bool waiting(usize index) const noexcept
    {
        return std::any_of(picks.begin(), picks.end(),
                           [&](const Pick& pick) {
                               return pick.viewport == index && !pick.submitted && !pick.result;
                           }) ||
               std::any_of(reads.begin(), reads.end(), [&](const Readback& read) {
                   return read.viewport == index && !read.submitted && !read.result;
               });
    }

    void readPicks(bool block)
    {
        for (Pick& pick : picks)
        {
            if (!pick.submitted || pick.result)
            {
                continue;
            }
            texels.resize(static_cast<usize>(pick.extent.x) * pick.extent.y);
            const ReadbackStatus status = readbacks->read(
                pick.readback, std::as_writable_bytes(std::span<EntityIdTexel>(texels)), block);
            if (status == ReadbackStatus::Ready)
            {
                finishPick(pick);
            }
            else if (status == ReadbackStatus::Invalid)
            {
                pick.result.emplace(
                    unexpected(CommandError::make(ErrorKind::Internal, "the pick could not be read back")));
            }
        }
    }
};

ViewportService::ViewportService(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
    m_impl->editor.setViewports(this);
}

ViewportService::~ViewportService()
{
    Impl& impl = *m_impl;
    if (impl.editor.viewports() == this)
    {
        impl.editor.setViewports(nullptr);
    }
    impl.device.waitIdle();
    impl.cache.waitIdle();
    for (Impl::Viewport& viewport : impl.viewports)
    {
        if (viewport.texture != 0)
        {
            impl.ui.unregisterTexture(viewport.texture);
        }
    }
    for (const Impl::Retired& retired : impl.retired)
    {
        impl.ui.unregisterTexture(retired.uiTexture);
    }
    if (impl.previewTexture != 0)
    {
        impl.ui.unregisterTexture(impl.previewTexture);
    }
}

std::unique_ptr<ViewportService> ViewportService::create(RenderDevice& device, EditorContext& editor,
                                                         ui::UiRenderer& ui)
{
    auto impl = std::make_unique<Impl>(device, editor, ui);
    impl->readbacks = ReadbackRing::create(device, kReadbackSlots);
    impl->commands = device.device()->createCommandList();
    if (!impl->readbacks || !impl->commands ||
        !impl->addViewport(std::string(kSceneId), ViewportKind::Scene, false) ||
        !impl->addViewport(std::string(kGameId), ViewportKind::Game, false))
    {
        ILL_LOG_ERROR("editor", "cannot create the viewport renderer");
        return nullptr;
    }
    return std::unique_ptr<ViewportService>(new ViewportService(std::move(impl)));
}

void ViewportService::render(bool uiShown)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.stats = {};
    World& world = impl.editor.world();
    impl.streamer.update();
    impl.cache.update();
    impl.resolver.update(world, &impl.jobs);
    updateTransformHierarchy(world, impl.hierarchy, &impl.jobs);
    impl.buildHidden();
    impl.buildSelection();
    const bool loading = impl.resolver.statistics().pending != 0 || impl.cache.loading() != 0;
    for (usize i = 0; i < impl.viewports.size(); ++i)
    {
        Impl::Viewport& viewport = impl.viewports[i];
        const bool waiting = impl.waiting(i);
        viewport.visible = viewport.drawn;
        if ((uiShown && viewport.drawn) || waiting)
        {
            const bool rendered = impl.renderViewport(viewport);
            // Picks and readbacks wait until the image shows every asset (they render again next frame).
            if (!loading)
            {
                if (rendered)
                {
                    impl.submitPicks(i);
                }
                impl.submitReads(i, rendered);
            }
            if (rendered)
            {
                impl.submitHover(viewport);
            }
        }
        viewport.drawn = false;
    }
    if (uiShown)
    {
        impl.renderPreview();
    }
    impl.previewWidth = 0;
    impl.readPicks(false);
    impl.readReads(false);
    impl.readHovers(false);
    for (Impl::Retired& retired : impl.retired)
    {
        ++retired.frames;
    }
    std::erase_if(impl.retired, [&](const Impl::Retired& retired) {
        if (retired.frames < kRetireFrames)
        {
            return false;
        }
        impl.ui.unregisterTexture(retired.uiTexture);
        return true;
    });
    impl.stats.picksPending =
        static_cast<u32>(std::count_if(impl.picks.begin(), impl.picks.end(),
                                       [](const Impl::Pick& pick) { return !pick.result.has_value(); }));
    impl.stats.readbacksPending =
        static_cast<u32>(std::count_if(impl.reads.begin(), impl.reads.end(),
                                       [](const Impl::Readback& read) { return !read.result.has_value(); }));
}

void ViewportService::waitIdle()
{
    Impl& impl = *m_impl;
    impl.cache.waitIdle();
    impl.resolver.waitIdle(impl.editor.world(), &impl.jobs);
    impl.readPicks(true);
    impl.readReads(true);
    impl.readHovers(true);
}

const ViewportServiceStats& ViewportService::stats() const noexcept
{
    return m_impl->stats;
}

nvrhi::ITexture* ViewportService::target(std::string_view id) const noexcept
{
    for (const Impl::Viewport& viewport : m_impl->viewports)
    {
        if (viewport.id == id)
        {
            return viewport.color;
        }
    }
    return nullptr;
}

std::vector<ViewportInfo> ViewportService::viewports() const
{
    Impl& impl = *m_impl;
    std::vector<ViewportInfo> out;
    for (const Impl::Viewport& viewport : impl.viewports)
    {
        ViewportInfo info;
        info.id = viewport.id;
        info.kind = viewport.kind;
        info.size = viewport.resize.current();
        info.visible = viewport.visible;
        info.camera = viewport.camera.target();
        info.settings = viewport.settings;
        if (const std::optional<RenderView> view = impl.viewOf(viewport, Impl::aspectOf(info.size), false))
        {
            info.eye = view->position;
            info.view = view->view;
            info.projection = view->projection;
        }
        if (viewport.kind == ViewportKind::Game)
        {
            info.sceneCamera = findMainCamera(impl.editor.world());
        }
        out.push_back(std::move(info));
    }
    return out;
}

Expected<void, CommandError> ViewportService::setCamera(std::string_view id, const ViewportCamera& camera,
                                                        bool animate)
{
    Impl::Viewport* viewport = m_impl->find(id);
    if (viewport == nullptr)
    {
        return unexpected(Impl::notFound(id));
    }
    viewport->camera.set(camera, animate && !m_impl->editor.preferences().reduceMotion);
    return {};
}

Expected<void, CommandError> ViewportService::setSettings(std::string_view id,
                                                          const ViewportSettings& settings)
{
    Impl::Viewport* viewport = m_impl->find(id);
    if (viewport == nullptr)
    {
        return unexpected(Impl::notFound(id));
    }
    const bool wasFixed = viewport->settings.aspect == ViewportAspect::Fixed;
    viewport->settings = settings;
    if (settings.aspect == ViewportAspect::Fixed)
    {
        viewport->resize.reset(UVec2(settings.width, settings.height));
    }
    else if (wasFixed)
    {
        viewport->resize.reset(UVec2(0)); // the panel gives the size again
    }
    return {};
}

Expected<ViewportCamera, CommandError> ViewportService::framing(std::string_view id,
                                                                std::span<const Entity> entities)
{
    Impl& impl = *m_impl;
    Impl::Viewport* viewport = impl.find(id);
    if (viewport == nullptr)
    {
        return unexpected(Impl::notFound(id));
    }
    World& world = impl.editor.world();
    // Children placed this frame need their Transforms before their bounds count.
    updateTransformHierarchy(world, impl.hierarchy, &impl.jobs);
    Aabb bounds;
    const auto include = [&](Entity entity) {
        if (!world.isAlive(entity) || world.has<Inactive>(entity) ||
            impl.editor.view().hidden(impl.editor.entities().guidOf(entity)))
        {
            return;
        }
        const Transform* transform = world.tryGet<Transform>(entity);
        if (transform == nullptr)
        {
            return;
        }
        const RenderMesh* mesh = world.tryGet<RenderMesh>(entity);
        if (mesh != nullptr && mesh->mesh.index != 0)
        {
            bounds.expand(
                impl.resources.submeshBounds(mesh->mesh, mesh->submesh).transformed(transform->matrix()));
        }
        else
        {
            bounds.expand(transform->position);
        }
    };
    if (entities.empty())
    {
        world.query<Transform>().each([&](Entity entity, const Transform&) { include(entity); });
    }
    for (const Entity entity : entities)
    {
        if (impl.hierarchy.contains(entity))
        {
            for (const Entity member : impl.hierarchy.subtree(entity))
            {
                include(member);
            }
        }
        else
        {
            include(entity);
        }
    }
    const UVec2 size = viewport->resize.current();
    const f32 aspect =
        size.x != 0 && size.y != 0 ? static_cast<f32>(size.x) / static_cast<f32>(size.y) : 16.0f / 9.0f;
    return frameBounds(viewport->camera.target(), bounds, aspect);
}

Expected<u64, CommandError> ViewportService::requestPick(std::string_view id, Vec2 pixel, bool cycle)
{
    Impl& impl = *m_impl;
    Impl::Viewport* viewport = impl.find(id);
    if (viewport == nullptr)
    {
        return unexpected(Impl::notFound(id));
    }
    Impl::Pick pick;
    pick.ticket = impl.nextTicket++;
    pick.viewport = static_cast<usize>(viewport - impl.viewports.data());
    pick.pixel = pixel;
    pick.cycle = cycle;
    impl.picks.push_back(pick);
    return pick.ticket;
}

std::optional<Expected<std::optional<ViewportHit>, CommandError>> ViewportService::pick(u64 ticket)
{
    Impl& impl = *m_impl;
    const auto found = std::find_if(impl.picks.begin(), impl.picks.end(),
                                    [&](const Impl::Pick& pick) { return pick.ticket == ticket; });
    if (found == impl.picks.end())
    {
        return Expected<std::optional<ViewportHit>, CommandError>(
            unexpected(CommandError::make(ErrorKind::NotFound, "unknown pick")));
    }
    if (!found->result)
    {
        return std::nullopt;
    }
    std::optional<Expected<std::optional<ViewportHit>, CommandError>> result = std::move(found->result);
    impl.picks.erase(found);
    return result;
}

ViewportFrame ViewportService::frame(std::string_view id, UVec2 size, const ViewportInput& input,
                                     const ViewportStyle& style)
{
    Impl& impl = *m_impl;
    Impl::Viewport* viewport = impl.find(id);
    if (viewport == nullptr)
    {
        return {};
    }
    const f32 delta = std::isfinite(input.deltaSeconds) ? std::max(input.deltaSeconds, 0.0f) : 0.0f;
    viewport->time += static_cast<f64>(delta);
    viewport->style = style;
    const bool dragging = input.buttons[0] || input.buttons[1] || input.buttons[2];
    if (viewport->settings.aspect != ViewportAspect::Fixed)
    {
        (void)viewport->resize.update(viewportImageSize(size, viewport->settings), viewport->time, dragging);
    }
    const bool scene = viewport->kind == ViewportKind::Scene;
    viewport->camera.setPreset(navigationPresetFromName(viewportNavigationOf(impl.editor.preferences())));
    const bool navigating = scene && viewport->camera.update(input, viewport->resize.current());
    viewport->drawn = true;
    // The entity under a resting cursor is read after the next render.
    viewport->hoverWanted = viewport->settings.hover && input.hovered && !dragging && !navigating;
    viewport->hoverPixel = input.cursor;
    if (!viewport->hoverWanted || (!viewport->hovered.isNull() && !impl.pickable(viewport->hovered)))
    {
        viewport->hovered = Entity{};
    }
    ViewportFrame out;
    out.navigating = navigating;
    out.settings = viewport->settings;
    out.projection = viewport->camera.target().projection;
    out.kind = viewport->kind;
    out.hasCamera = scene || !findMainCamera(impl.editor.world()).isNull();
    out.camera = viewport->camera.camera();
    out.hovered = viewport->hovered;
    if (out.hasCamera && impl.ensureTarget(*viewport))
    {
        out.texture = viewport->texture;
        out.size = viewport->colorSize;
    }
    impl.buildIcons(*viewport);
    out.icons = viewport->icons;
    if (scene && viewport->settings.cameraPreview && out.size.x != 0)
    {
        const u32 width = std::clamp(out.size.x / 4, kPreviewMinWidth, kPreviewMaxWidth);
        impl.previewWidth = std::max(impl.previewWidth, width);
        const Entity camera = impl.selectedCamera();
        if (!camera.isNull() && camera == impl.previewCamera && impl.previewTexture != 0)
        {
            out.preview = impl.previewTexture;
            out.previewSize = impl.previewSize;
            out.previewCamera = camera;
        }
    }
    return out;
}

Expected<std::string, CommandError> ViewportService::createViewport()
{
    Impl& impl = *m_impl;
    for (u32 number = 2; number < 2 + kMaxSecondaryViewports; ++number)
    {
        std::string id = std::string(kSceneId) + "-" + std::to_string(number);
        if (impl.find(id) != nullptr)
        {
            continue;
        }
        const Impl::Viewport& scene = *impl.find(kSceneId);
        const ViewportCamera camera = scene.camera.target();
        const ViewportSettings settings = scene.settings;
        if (!impl.addViewport(id, ViewportKind::Scene, true))
        {
            return unexpected(CommandError::make(ErrorKind::Internal, "cannot create the viewport renderer"));
        }
        Impl::Viewport& created = impl.viewports.back();
        created.camera.set(camera, false);
        created.settings = settings;
        if (settings.aspect == ViewportAspect::Fixed)
        {
            created.resize.reset(UVec2(settings.width, settings.height));
        }
        ++impl.viewportsVersion;
        return id;
    }
    return unexpected(CommandError::make(ErrorKind::LimitExceeded, "at most 3 extra viewports can be open"));
}

Expected<void, CommandError> ViewportService::closeViewport(std::string_view id)
{
    Impl& impl = *m_impl;
    Impl::Viewport* viewport = impl.find(id);
    if (viewport == nullptr)
    {
        return unexpected(Impl::notFound(id));
    }
    if (id == kSceneId || id == kGameId)
    {
        return unexpected(
            CommandError::make(ErrorKind::PreconditionFailed, "this viewport cannot be closed"));
    }
    const usize index = static_cast<usize>(viewport - impl.viewports.data());
    // The renderer goes once the GPU is done with it; the UI may still draw the texture this frame.
    impl.device.waitIdle();
    // Copies already in the ring finish now: a ticket holds its slot until it is read.
    impl.readPicks(true);
    impl.readReads(true);
    impl.readHovers(true);
    if (viewport->color)
    {
        impl.retired.push_back(Impl::Retired{viewport->color, viewport->texture, 0});
    }
    const auto closed = [](auto& work) {
        work.result.emplace(unexpected(CommandError::make(ErrorKind::NotFound, "the viewport was closed")));
        work.viewport = kClosed;
    };
    for (Impl::Pick& pick : impl.picks)
    {
        if (pick.viewport == index && !pick.result)
        {
            closed(pick);
        }
        else if (pick.viewport != kClosed && pick.viewport > index)
        {
            --pick.viewport;
        }
    }
    for (Impl::Readback& read : impl.reads)
    {
        if (read.viewport == index && !read.result)
        {
            closed(read);
        }
        else if (read.viewport != kClosed && read.viewport > index)
        {
            --read.viewport;
        }
    }
    impl.viewports.erase(impl.viewports.begin() + static_cast<std::ptrdiff_t>(index));
    ++impl.viewportsVersion;
    return {};
}

u64 ViewportService::viewportsVersion() const noexcept
{
    return m_impl->viewportsVersion;
}

Expected<u64, CommandError> ViewportService::requestReadback(std::string_view id, ViewportChannel channel,
                                                             UVec2 origin, UVec2 extent)
{
    Impl& impl = *m_impl;
    Impl::Viewport* viewport = impl.find(id);
    if (viewport == nullptr)
    {
        return unexpected(Impl::notFound(id));
    }
    if (extent.x == 0 || extent.y == 0)
    {
        return unexpected(CommandError::invalid("", "the region is empty"));
    }
    Impl::Readback read;
    read.ticket = impl.nextTicket++;
    read.viewport = static_cast<usize>(viewport - impl.viewports.data());
    read.channel = channel;
    read.origin = origin;
    read.extent = extent;
    impl.reads.push_back(std::move(read));
    return impl.reads.back().ticket;
}

std::optional<Expected<ViewportReadback, CommandError>> ViewportService::readback(u64 ticket)
{
    Impl& impl = *m_impl;
    const auto found = std::find_if(impl.reads.begin(), impl.reads.end(),
                                    [&](const Impl::Readback& read) { return read.ticket == ticket; });
    if (found == impl.reads.end())
    {
        return Expected<ViewportReadback, CommandError>(
            unexpected(CommandError::make(ErrorKind::NotFound, "unknown readback")));
    }
    if (!found->result)
    {
        return std::nullopt;
    }
    std::optional<Expected<ViewportReadback, CommandError>> result = std::move(found->result);
    impl.reads.erase(found);
    return result;
}

Expected<ViewportRenderStats, CommandError> ViewportService::renderStats(std::string_view id) const
{
    const Impl::Viewport* viewport = m_impl->find(id);
    if (viewport == nullptr)
    {
        return unexpected(Impl::notFound(id));
    }
    const RenderStats& stats = viewport->renderStats;
    return ViewportRenderStats{viewport->frames,
                               stats.candidates,
                               stats.visible,
                               stats.drawCalls,
                               static_cast<u32>(std::min<u64>(stats.triangles, ~u32{0})),
                               stats.shadowCasters,
                               stats.shadowDrawCalls,
                               stats.lightsDropped};
}

ViewportGpuInfo ViewportService::gpuInfo() const
{
    const RenderDevice& device = m_impl->device;
    const GpuInfo& gpu = device.gpu();
    ViewportGpuInfo out;
    out.name = gpu.name;
    out.driver = gpu.driverName;
    out.vendorId = gpu.vendorId;
    out.deviceId = gpu.deviceId;
    // VK_MAKE_API_VERSION: variant 3 bits, major 7, minor 10, patch 12.
    out.apiVersion = std::to_string((gpu.apiVersion >> 22u) & 0x7Fu) + "." +
                     std::to_string((gpu.apiVersion >> 12u) & 0x3FFu) + "." +
                     std::to_string(gpu.apiVersion & 0xFFFu);
    out.discrete = gpu.discrete;
    out.software = gpu.software;
    out.bindless = gpu.bindless;
    out.validation = device.isValidationEnabled();
    out.validationErrors = device.validationErrorCount();
    return out;
}

} // namespace illusion::editor::viewport
