#include "ViewportOverlays.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/ui/UiRenderer.hpp>
#include <illusion/editor/viewport/EditorCamera.hpp>
#include <illusion/editor/viewport/PickCandidates.hpp>
#include <illusion/editor/viewport/ResizePolicy.hpp>
#include <illusion/editor/viewport/ViewportService.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/ReadbackRing.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
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
// Repeated cycling picks within this many pixels count as the same spot.
constexpr f32 kCycleDistance = 2.0f;

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
        EditorCamera camera;
        ViewportSettings settings;
        ViewportStyle style;
        ResizePolicy resize;
        f64 time = 0.0;
        std::unique_ptr<SceneRenderer> renderer;
        std::unique_ptr<detail::ViewportOverlays> overlays;
        nvrhi::TextureHandle color;
        UVec2 colorSize{0};
        u64 texture = 0;
        bool drawn = false;        // frame() since the last render()
        bool visible = false;      // drawn in the last UI frame
        Mat4 viewProjection{1.0f}; // of the last render
        Vec2 lastPickPixel{-1000.0f};
        Entity lastPick{};
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
    std::vector<Retired> retired;
    u64 nextTicket = 1;
    ViewportServiceStats stats;
    // Scratch, kept to avoid allocating every frame.
    std::vector<u64> hiddenBits;
    std::vector<u64> selectionBits;
    std::vector<EntityIdTexel> texels;
    std::vector<Entity> candidates;
    std::vector<std::pair<f32, Entity>> behind;

    Viewport* find(std::string_view id) noexcept
    {
        const auto found = std::find_if(viewports.begin(), viewports.end(),
                                        [&](const Viewport& viewport) { return viewport.id == id; });
        return found != viewports.end() ? &*found : nullptr;
    }

    static CommandError notFound(std::string_view id)
    {
        return CommandError::make(ErrorKind::NotFound, "no viewport '" + std::string(id) + "'");
    }

    bool addViewport(std::string id)
    {
        Viewport viewport;
        viewport.id = std::move(id);
        SceneRendererDesc desc;
        desc.colorFormat = kColorFormat;
        desc.entityIds = true;
        viewport.renderer = SceneRenderer::create(device, resources, desc);
        viewport.overlays = detail::ViewportOverlays::create(device, kColorFormat);
        if (!viewport.renderer || !viewport.overlays)
        {
            return false;
        }
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

    void renderViewport(Viewport& viewport)
    {
        ILL_PROFILE_SCOPE();
        if (!ensureTarget(viewport))
        {
            return;
        }
        const ViewportCamera& camera = viewport.camera.camera();
        const f32 aspect = static_cast<f32>(viewport.colorSize.x) / static_cast<f32>(viewport.colorSize.y);
        RenderView view;
        view.view = viewportViewMatrix(camera);
        view.projection = viewportProjectionMatrix(camera, aspect);
        view.position = viewportEye(camera);
        view.hiddenEntities = hiddenBits;
        viewport.renderer->setDebugView(debugViewOf(viewport.settings.shading));
        (void)viewport.renderer->setWireframeOverlay(viewport.settings.wireframe);

        commands->open();
        viewport.renderer->render(commands, viewport.color, editor.world(), view, &jobs);
        const RenderViewTargets& targets = viewport.renderer->viewTargets();
        viewport.viewProjection = targets.unjitteredViewProjection;
        if (viewport.settings.grid)
        {
            (void)viewport.overlays->drawGrid(
                commands, viewport.color, targets,
                gridPass(camera, targets.unjitteredViewProjection, viewport.style));
        }
        if (viewport.settings.selectionOutline && !editor.selection().empty() && !selectionBits.empty())
        {
            (void)viewport.overlays->drawOutline(commands, viewport.color, targets, selectionBits,
                                                 viewport.style.selection, kOutlineRadius);
        }
        commands->close();
        device.device()->executeCommandList(commands);
        ++stats.rendered;
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
}

std::unique_ptr<ViewportService> ViewportService::create(RenderDevice& device, EditorContext& editor,
                                                         ui::UiRenderer& ui)
{
    auto impl = std::make_unique<Impl>(device, editor, ui);
    impl->readbacks = ReadbackRing::create(device, 4);
    impl->commands = device.device()->createCommandList();
    if (!impl->readbacks || !impl->commands || !impl->addViewport("scene"))
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
    impl.cache.update();
    impl.resolver.update(world, &impl.jobs);
    updateTransformHierarchy(world, impl.hierarchy, &impl.jobs);
    impl.buildHidden();
    impl.buildSelection();
    for (usize i = 0; i < impl.viewports.size(); ++i)
    {
        Impl::Viewport& viewport = impl.viewports[i];
        const bool picking = std::any_of(impl.picks.begin(), impl.picks.end(), [&](const Impl::Pick& pick) {
            return pick.viewport == i && !pick.submitted && !pick.result;
        });
        viewport.visible = viewport.drawn;
        if ((uiShown && viewport.drawn) || picking)
        {
            impl.renderViewport(viewport);
            impl.submitPicks(i);
        }
        viewport.drawn = false;
    }
    impl.readPicks(false);
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
}

void ViewportService::waitIdle()
{
    Impl& impl = *m_impl;
    impl.cache.waitIdle();
    impl.resolver.waitIdle(impl.editor.world(), &impl.jobs);
    impl.readPicks(true);
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
    std::vector<ViewportInfo> out;
    for (const Impl::Viewport& viewport : m_impl->viewports)
    {
        const ViewportCamera& camera = viewport.camera.target();
        out.push_back(ViewportInfo{viewport.id, viewport.resize.current(), viewport.visible, camera,
                                   viewportEye(camera), viewport.settings});
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
    viewport->settings = settings;
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
    (void)viewport->resize.update(size, viewport->time, dragging);
    const bool navigating = viewport->camera.update(input, viewport->resize.current());
    viewport->drawn = true;
    if (!impl.ensureTarget(*viewport))
    {
        return ViewportFrame{0, UVec2(0), navigating, viewport->settings,
                             viewport->camera.target().projection};
    }
    return ViewportFrame{viewport->texture, viewport->colorSize, navigating, viewport->settings,
                         viewport->camera.target().projection};
}

} // namespace illusion::editor::viewport
