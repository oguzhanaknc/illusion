#pragma once

/// @file
/// Allocation-free recording of world-space debug lines and depth-sampled GPU overlays.

#include <illusion/core/Math.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <memory>
#include <span>
#include <vector>

namespace illusion
{

/// Occlusion of a debug line by the scene's reversed-Z depth texture.
enum class DebugDepth : u8
{
    Tested, ///< Hidden behind opaque scene surfaces.
    OnTop,  ///< Visible through scene surfaces.
};

/// One packed debug line (also the Shader's structured-buffer layout).
struct DebugLine
{
    Vec4 start; ///< World-space start, w = width in output pixels.
    Vec4 end;   ///< World-space end, w = 1 for OnTop, 0 for Tested.
    Vec4 color; ///< Linear RGBA colour.
};

/// Single-threaded pre-reserved line list. Recording never grows its capacity. A full list or invalid
/// geometry returns false without appending anything. Clear explicitly at the start of each frame.
class DebugDraw
{
public:
    /// Reserves space for @p maxLines at construction.
    explicit DebugDraw(usize maxLines = 16384);
    /// Empties the list without releasing its storage.
    void clear() noexcept;
    /// Recorded lines; valid until the next recording or clear().
    [[nodiscard]] std::span<const DebugLine> lines() const noexcept;
    /// Appends a finite, nonzero-length line of positive width. Colour is linear RGBA.
    bool line(Vec3 start, Vec3 end, Vec4 color, f32 width = 1.0f,
              DebugDepth depth = DebugDepth::Tested) noexcept;
    /// Appends the 12 edges of an axis-aligned box (min must be strictly less than max on every axis).
    bool box(Vec3 min, Vec3 max, Vec4 color, f32 width = 1.0f,
             DebugDepth depth = DebugDepth::Tested) noexcept;
    /// Appends three great circles (8..256 segments each) of a wire sphere.
    bool sphere(Vec3 center, f32 radius, Vec4 color, u32 segments = 32, f32 width = 1.0f,
                DebugDepth depth = DebugDepth::Tested) noexcept;

private:
    std::vector<DebugLine> m_lines;
    usize m_maxLines;
};

/// Records debug overlays into a caller's colour target after SceneRenderer::render(). Lines are
/// expanded into quads, so widths work on GPUs without wide-line support. The device outlives this.
class DebugDrawRenderer : NonCopyable
{
public:
    /// Creates pipelines for the output target format; null on GPU failure.
    [[nodiscard]] static std::unique_ptr<DebugDrawRenderer>
    create(RenderDevice& device, nvrhi::Format outputFormat = nvrhi::Format::SBGRA8_UNORM);
    ~DebugDrawRenderer();
    /// Draws without clearing the target. Depth comes from @p view, sampled using renderSize/outputSize;
    /// it is never attached. Returns false for missing/incompatible targets or GPU allocation failure.
    bool render(nvrhi::ICommandList* commands, nvrhi::ITexture* target, const RenderViewTargets& view,
                const DebugDraw& draw);

private:
    DebugDrawRenderer();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
