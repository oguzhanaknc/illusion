#pragma once

// The overlays a viewport draws over the tonemapped scene: the ground grid (depth tested against the scene)
// and the selection outline (from the Entity ID target and a selection bitset).

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/render/SceneRenderer.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>
#include <span>

namespace illusion
{
class RenderDevice;
} // namespace illusion

namespace illusion::editor::viewport::detail
{

/// What the grid pass draws (see Grid.slang).
struct GridPass
{
    Mat4 inverseViewProjection{1.0f};   ///< Unjittered clip to world.
    Vec4 eye{0.0f};                     ///< xyz eye, w plane normal axis.
    Vec4 scale{1.0f, 0.0f, 0.0f, 0.0f}; ///< Minor cell, minor fade, fade distance, grazing fade.
    Vec4 color{0.5f};                   ///< Lines.
    Vec4 axisU{1.0f};                   ///< The axis along the first plane coordinate.
    Vec4 axisV{1.0f};                   ///< The axis along the second plane coordinate.
};

/// Grid and outline pipelines and their bindings for one viewport's targets.
class ViewportOverlays : NonCopyable
{
public:
    /// Creates the pipelines for colour targets of @p format; nullptr on GPU failure.
    [[nodiscard]] static std::unique_ptr<ViewportOverlays> create(RenderDevice& device, nvrhi::Format format);
    ~ViewportOverlays();

    /// Draws the grid into @p target (the output of the render that produced @p view).
    bool drawGrid(nvrhi::ICommandList* commands, nvrhi::ITexture* target, const RenderViewTargets& view,
                  const GridPass& pass);

    /// Outlines the entities whose bit is set in @p selection (64 Entity indices per word) in @p color,
    /// @p radius output pixels wide.
    bool drawOutline(nvrhi::ICommandList* commands, nvrhi::ITexture* target, const RenderViewTargets& view,
                     std::span<const u64> selection, Vec4 color, u32 radius);

private:
    ViewportOverlays();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion::editor::viewport::detail
