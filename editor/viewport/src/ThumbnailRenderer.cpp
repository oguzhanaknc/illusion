#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/AssetId.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Time.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/viewport/ThumbnailRenderer.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/ReadbackRing.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

namespace illusion::editor::viewport
{
namespace
{

constexpr nvrhi::Format kColorFormat = nvrhi::Format::RGBA8_UNORM;
// Frames a request may wait for its assets before it fails (assets load in well under a second; a stuck
// one must not block the queue).
constexpr u32 kMaxRequestFrames = 3600;
constexpr u32 kCheckerCell = 16;
constexpr u8 kCheckerLight = 0x99;
constexpr u8 kCheckerDark = 0x66;
// Background radiance behind models and materials (before tonemapping).
constexpr Vec4 kBackground{0.045f, 0.047f, 0.055f, 1.0f};
constexpr std::string_view kSphere = "builtin:/mesh/sphere";
constexpr std::string_view kSky = "builtin:/sky/procedural";

f32 srgbEncode(f32 linear) noexcept
{
    linear = std::clamp(linear, 0.0f, 1.0f);
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

u8 toByte(f32 value) noexcept
{
    return static_cast<u8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// The display RGBA (0-1, sRGB-encoded except for linear 8-bit images) of texel @p index of @p pixels.
Vec4 displayTexel(const std::byte* pixels, usize index, ImageFormat format) noexcept
{
    if (format == ImageFormat::RGBA32F)
    {
        Vec4 value;
        std::memcpy(&value, pixels + index * 16, sizeof(value));
        const auto tonemap = [](f32 c) {
            return srgbEncode(std::max(c, 0.0f) / (1.0f + std::max(c, 0.0f)));
        };
        return Vec4(tonemap(value.x), tonemap(value.y), tonemap(value.z), std::clamp(value.w, 0.0f, 1.0f));
    }
    const std::byte* texel = pixels + index * 4;
    constexpr f32 kScale = 1.0f / 255.0f;
    return Vec4(static_cast<f32>(texel[0]) * kScale, static_cast<f32>(texel[1]) * kScale,
                static_cast<f32>(texel[2]) * kScale, static_cast<f32>(texel[3]) * kScale);
}

} // namespace

ThumbnailImage textureThumbnail(const Image& image, u32 size)
{
    ILL_PROFILE_SCOPE();
    ThumbnailImage out;
    out.size = UVec2(size);
    out.rgba.resize(static_cast<usize>(size) * size * 4);
    for (u32 y = 0; y < size; ++y)
    {
        for (u32 x = 0; x < size; ++x)
        {
            const u8 checker =
                ((x / kCheckerCell) + (y / kCheckerCell)) % 2 == 0 ? kCheckerLight : kCheckerDark;
            u8* pixel = out.rgba.data() + (static_cast<usize>(y) * size + x) * 4;
            pixel[0] = checker;
            pixel[1] = checker;
            pixel[2] = checker;
            pixel[3] = 255;
        }
    }
    if (!image.isValid() || size == 0)
    {
        return out;
    }
    // The smallest mip that still covers the thumbnail.
    usize level = 0;
    while (level + 1 < image.mips.size() &&
           std::max(image.mips[level + 1].width, image.mips[level + 1].height) >= size)
    {
        ++level;
    }
    const ImageMip& mip = image.mips[level];
    const usize texelBytes = bytesPerPixel(image.format);
    if (mip.size < static_cast<usize>(mip.width) * mip.height * texelBytes ||
        mip.offset + mip.size > image.data.size())
    {
        return out;
    }
    const std::byte* pixels = image.data.data() + mip.offset;
    // The image fitted into the square with its aspect kept, centred.
    const f32 scale = static_cast<f32>(size) / static_cast<f32>(std::max(mip.width, mip.height));
    const u32 width =
        std::clamp(static_cast<u32>(std::lround(static_cast<f32>(mip.width) * scale)), 1u, size);
    const u32 height =
        std::clamp(static_cast<u32>(std::lround(static_cast<f32>(mip.height) * scale)), 1u, size);
    const u32 left = (size - width) / 2;
    const u32 top = (size - height) / 2;
    for (u32 y = 0; y < height; ++y)
    {
        const u32 y0 = static_cast<u32>(static_cast<u64>(y) * mip.height / height);
        const u32 y1 = std::max(y0 + 1, static_cast<u32>(static_cast<u64>(y + 1) * mip.height / height));
        for (u32 x = 0; x < width; ++x)
        {
            const u32 x0 = static_cast<u32>(static_cast<u64>(x) * mip.width / width);
            const u32 x1 = std::max(x0 + 1, static_cast<u32>(static_cast<u64>(x + 1) * mip.width / width));
            Vec4 sum(0.0f);
            for (u32 sy = y0; sy < y1; ++sy)
            {
                for (u32 sx = x0; sx < x1; ++sx)
                {
                    sum += displayTexel(pixels, static_cast<usize>(sy) * mip.width + sx, image.format);
                }
            }
            const Vec4 color = sum / static_cast<f32>((y1 - y0) * (x1 - x0));
            u8* pixel = out.rgba.data() + (static_cast<usize>(top + y) * size + left + x) * 4;
            for (usize c = 0; c < 3; ++c)
            {
                const f32 background = static_cast<f32>(pixel[c]) / 255.0f;
                pixel[c] =
                    toByte(color[static_cast<glm::length_t>(c)] * color.w + background * (1.0f - color.w));
            }
        }
    }
    return out;
}

ViewportCamera thumbnailCamera(const Aabb& bounds) noexcept
{
    Sphere sphere = Sphere::fromAabb(bounds);
    if (!(sphere.radius > 0.0f) || !std::isfinite(sphere.radius))
    {
        sphere = Sphere{Vec3(0.0f), 0.8660254f};
    }
    ViewportCamera camera;
    camera.pivot = sphere.center;
    camera.yaw = 35.0f;
    camera.pitch = -20.0f;
    camera.fieldOfView = 30.0f;
    const f32 halfAngle = glm::radians(camera.fieldOfView) * 0.5f;
    camera.distance =
        std::clamp(sphere.radius / std::sin(halfAngle) * 1.02f, kViewportMinExtent, kViewportMaxExtent);
    return camera;
}

struct ThumbnailRenderer::Impl
{
    enum class Stage : u8
    {
        Start,     // nothing done yet
        Loading,   // waiting for the model or image file
        Resolving, // waiting for the preview World's assets
        Reading,   // waiting for the GPU readback
    };

    struct Request
    {
        u64 ticket = 0;
        ThumbnailRequest request;
        std::optional<Expected<ThumbnailImage, CommandError>> result;
    };

    Impl(RenderDevice& renderDevice, RenderResources& renderResources, RenderAssetCache& assetCache,
         AssetStreamer& assetStreamer, JobSystem& jobSystem)
        : device(renderDevice)
        , resources(renderResources)
        , cache(assetCache)
        , streamer(assetStreamer)
        , jobs(jobSystem)
        , resolver(assetCache)
    {
    }

    RenderDevice& device;
    RenderResources& resources;
    RenderAssetCache& cache;
    AssetStreamer& streamer;
    JobSystem& jobs;
    World world;
    RenderAssetResolver resolver;
    std::unique_ptr<SceneRenderer> renderer;
    std::unique_ptr<ReadbackRing> readbacks;
    nvrhi::TextureHandle color;
    nvrhi::CommandListHandle commands;
    std::deque<Request> requests; // in arrival order; answered ones wait to be collected
    u64 nextTicket = 1;
    // The request in progress (the first one without a result).
    Stage stage = Stage::Start;
    u64 active = 0;
    u32 frames = 0;
    u32 failuresBefore = 0;
    AssetHandle<ModelData> model;
    AssetHandle<Image> image;
    Aabb bounds;
    bool level = false; // look at the horizon (environments) instead of down at the object
    u64 readback = 0;
    ThumbnailRendererStats stats;

    [[nodiscard]] Request* current() noexcept
    {
        const auto found = std::find_if(requests.begin(), requests.end(),
                                        [](const Request& request) { return !request.result.has_value(); });
        return found != requests.end() ? &*found : nullptr;
    }

    // Answers the current request and empties the preview World.
    void finish(Request& request, Expected<ThumbnailImage, CommandError> result)
    {
        request.result = std::move(result);
        ++stats.completed;
        reset();
    }

    void reset()
    {
        if (stage == Stage::Reading && readback != 0)
        {
            // Read only to free the slot; the image is no longer wanted.
            (void)readbacks->read(readback, readbackPixels(), true);
        }
        stage = Stage::Start;
        active = 0;
        frames = 0;
        readback = 0;
        model = {};
        image = {};
        bounds = Aabb{};
        level = false;
        if (world.aliveCount() != 0)
        {
            world.clear();
            resolver.update(world, &jobs);
            (void)resolver.releaseUnused(world);
            (void)cache.evictUnused();
        }
    }

    static CommandError loadError(const ThumbnailRequest& request, std::string_view why)
    {
        std::string message = "'" + request.path + "' could not be loaded";
        if (!why.empty())
        {
            message += " (" + std::string(why) + ")";
        }
        return CommandError::make(ErrorKind::IoError, std::move(message));
    }

    // Studio lighting for models and materials: a procedural sky for ambient light and reflections (not
    // drawn) and a key light from the upper left.
    void addStudioLights()
    {
        world.create(Transform{},
                     SkyLight{.environment = AssetRef(kSky), .intensity = 1.0f, .background = false},
                     ProceduralSky{});
        Transform key;
        key.rotation = glm::quatLookAt(glm::normalize(Vec3(0.5f, -0.7f, -0.5f)), Vec3(0.0f, 1.0f, 0.0f));
        world.create(key, DirectionalLight{.color = Vec3(1.0f), .intensity = 2.5f, .castShadows = false});
    }

    void start(Request& request)
    {
        active = request.ticket;
        frames = 0;
        failuresBefore = resolver.statistics().failures;
        switch (request.request.kind)
        {
        case ThumbnailKind::Model:
            model = streamer.loadModel(request.request.path);
            stage = Stage::Loading;
            break;
        case ThumbnailKind::Texture:
            image = streamer.loadImage(request.request.path);
            stage = Stage::Loading;
            break;
        case ThumbnailKind::Material:
            addStudioLights();
            world.create(Transform{}, MeshRenderer{.mesh = AssetRef(kSphere),
                                                   .material = AssetRef(request.request.path),
                                                   .castShadows = false});
            bounds = Aabb{Vec3(-0.5f), Vec3(0.5f)};
            stage = Stage::Resolving;
            break;
        case ThumbnailKind::Environment:
            world.create(Transform{}, SkyLight{.environment = AssetRef(request.request.path),
                                               .intensity = 1.0f,
                                               .background = true});
            world.create(Transform{}, MeshRenderer{.mesh = AssetRef(kSphere), .castShadows = false});
            // Framed loosely, so that the environment shows around the sphere.
            bounds = Aabb{Vec3(-1.0f), Vec3(1.0f)};
            level = true;
            stage = Stage::Resolving;
            break;
        }
    }

    // Returns true if a thumbnail was rendered or scaled.
    bool step(Request& request)
    {
        if (stage == Stage::Start)
        {
            start(request);
        }
        if (++frames > kMaxRequestFrames)
        {
            finish(request, unexpected(CommandError::make(ErrorKind::Timeout, "'" + request.request.path +
                                                                                  "' did not load in time")));
            return false;
        }
        if (stage == Stage::Loading)
        {
            const AssetState state =
                request.request.kind == ThumbnailKind::Model ? model.state() : image.state();
            if (state == AssetState::Loading)
            {
                return false;
            }
            if (state == AssetState::Failed)
            {
                const std::string why =
                    request.request.kind == ThumbnailKind::Model ? model.error() : image.error();
                finish(request, unexpected(loadError(request.request, why)));
                return false;
            }
            if (request.request.kind == ThumbnailKind::Texture)
            {
                finish(request, textureThumbnail(image.value()));
                return true;
            }
            addStudioLights();
            (void)spawnModel(world, model.value(), request.request.path);
            bounds = model.value().bounds();
            stage = Stage::Resolving;
        }
        if (stage == Stage::Resolving)
        {
            resolver.update(world, &jobs);
            if (resolver.statistics().pending != 0)
            {
                return false;
            }
            if (resolver.statistics().failures != failuresBefore)
            {
                finish(request, unexpected(loadError(request.request,
                                                     cache.error(AssetId::fromPath(request.request.path)))));
                return false;
            }
            render();
            return true;
        }
        std::vector<std::byte>& pixels = readbackPixels();
        const ReadbackStatus status = readbacks->read(readback, pixels);
        if (status == ReadbackStatus::Pending)
        {
            return false;
        }
        readback = 0;
        if (status != ReadbackStatus::Ready)
        {
            finish(request,
                   unexpected(CommandError::make(ErrorKind::Internal, "the thumbnail readback failed")));
            return false;
        }
        ThumbnailImage out;
        out.size = UVec2(kThumbnailSize);
        out.rgba.resize(pixels.size());
        std::memcpy(out.rgba.data(), pixels.data(), pixels.size());
        for (usize i = 3; i < out.rgba.size(); i += 4)
        {
            out.rgba[i] = 255;
        }
        finish(request, std::move(out));
        return false;
    }

    // The readback destination, made when the first image is read.
    std::vector<std::byte>& readbackPixels()
    {
        if (readPixels.empty())
        {
            readPixels.resize(static_cast<usize>(kThumbnailSize) * kThumbnailSize * 4);
        }
        return readPixels;
    }

    void render()
    {
        ILL_PROFILE_SCOPE();
        ViewportCamera camera = thumbnailCamera(bounds);
        if (level)
        {
            camera.pitch = 0.0f;
        }
        RenderView view;
        view.view = viewportViewMatrix(camera);
        view.projection = viewportProjectionMatrix(camera, 1.0f);
        view.position = viewportEye(camera);
        commands->open();
        renderer->render(commands, color, world, view, &jobs);
        commands->close();
        device.device()->executeCommandList(commands);
        const std::optional<u64> ticket = readbacks->request(color);
        ILL_CHECK(ticket.has_value(), "the thumbnail readback slot is taken");
        readback = *ticket;
        stage = Stage::Reading;
        ++stats.rendered;
    }

    std::vector<std::byte> readPixels; // readback destination
};

std::unique_ptr<ThumbnailRenderer> ThumbnailRenderer::create(RenderDevice& device, RenderResources& resources,
                                                             RenderAssetCache& cache, AssetStreamer& streamer,
                                                             JobSystem& jobs)
{
    auto impl = std::make_unique<Impl>(device, resources, cache, streamer, jobs);
    SceneRendererDesc desc;
    desc.colorFormat = kColorFormat;
    desc.clearColor = kBackground;
    desc.shadows.enabled = false;
    desc.post.bloom.enabled = false;
    desc.post.temporalAntiAliasing.enabled = false;
    desc.occlusionCulling = false;
    impl->renderer = SceneRenderer::create(device, resources, desc);
    impl->readbacks = ReadbackRing::create(device, 1);
    nvrhi::TextureDesc target;
    target.width = kThumbnailSize;
    target.height = kThumbnailSize;
    target.format = kColorFormat;
    target.isRenderTarget = true;
    target.debugName = "Thumbnail";
    target.setClearValue(nvrhi::Color(0.0f));
    target.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    impl->color = device.device()->createTexture(target);
    impl->commands = device.device()->createCommandList();
    if (!impl->renderer || !impl->readbacks || !impl->color || !impl->commands)
    {
        ILL_LOG_ERROR("editor", "cannot create the thumbnail renderer");
        return nullptr;
    }
    return std::unique_ptr<ThumbnailRenderer>(new ThumbnailRenderer(std::move(impl)));
}

ThumbnailRenderer::ThumbnailRenderer(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

ThumbnailRenderer::~ThumbnailRenderer()
{
    m_impl->reset();
}

void ThumbnailRenderer::update()
{
    Impl& impl = *m_impl;
    impl.stats.steps = 0;
    Impl::Request* request = impl.current();
    if (request == nullptr)
    {
        impl.stats.updateMilliseconds = 0.0;
        return;
    }
    ILL_PROFILE_SCOPE();
    const Stopwatch watch;
    impl.stats.steps = impl.step(*request) ? 1 : 0;
    impl.stats.updateMilliseconds = watch.elapsedSeconds() * 1000.0;
}

void ThumbnailRenderer::waitIdle()
{
    Impl& impl = *m_impl;
    while (Impl::Request* request = impl.current())
    {
        const u64 ticket = request->ticket;
        for (Impl::Request* active = request; active != nullptr && active->ticket == ticket;
             active = impl.current())
        {
            if (impl.stage == Impl::Stage::Reading)
            {
                impl.device.waitIdle();
            }
            else if (impl.stage != Impl::Stage::Start)
            {
                impl.cache.waitIdle();
            }
            update();
        }
    }
}

const ThumbnailRendererStats& ThumbnailRenderer::stats() const noexcept
{
    ThumbnailRendererStats& stats = m_impl->stats;
    stats.pending =
        static_cast<u32>(std::count_if(m_impl->requests.begin(), m_impl->requests.end(),
                                       [](const Impl::Request& request) { return !request.result; }));
    return stats;
}

Expected<u64, CommandError> ThumbnailRenderer::requestThumbnail(const ThumbnailRequest& request)
{
    Impl& impl = *m_impl;
    const u64 ticket = impl.nextTicket++;
    impl.requests.push_back(Impl::Request{ticket, request, std::nullopt});
    return ticket;
}

std::optional<Expected<ThumbnailImage, CommandError>> ThumbnailRenderer::thumbnail(u64 ticket)
{
    Impl& impl = *m_impl;
    const auto found = std::find_if(impl.requests.begin(), impl.requests.end(),
                                    [&](const Impl::Request& request) { return request.ticket == ticket; });
    if (found == impl.requests.end())
    {
        return Expected<ThumbnailImage, CommandError>(unexpected(
            CommandError::make(ErrorKind::NotFound, "no thumbnail request " + std::to_string(ticket))));
    }
    if (!found->result)
    {
        return std::nullopt;
    }
    std::optional<Expected<ThumbnailImage, CommandError>> result = std::move(found->result);
    impl.requests.erase(found);
    return result;
}

void ThumbnailRenderer::cancelThumbnail(u64 ticket)
{
    Impl& impl = *m_impl;
    const auto found = std::find_if(impl.requests.begin(), impl.requests.end(),
                                    [&](const Impl::Request& request) { return request.ticket == ticket; });
    if (found == impl.requests.end())
    {
        return;
    }
    if (impl.active == ticket)
    {
        impl.reset();
    }
    impl.requests.erase(found);
}

} // namespace illusion::editor::viewport
