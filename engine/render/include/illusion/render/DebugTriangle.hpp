#pragma once

/// @file
/// Draws one coloured triangle without any buffers. A diagnostic that exercises shader compilation,
/// pipeline creation, rendering and presentation; used by GPU smoke tests and the hello_triangle sample.

#include <illusion/core/Base.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>

namespace illusion
{

class RenderDevice;

/// Pipeline for the diagnostic triangle (red top, green bottom right, blue bottom left).
class DebugTriangle : NonCopyable
{
public:
    /// Creates the pipeline for framebuffers compatible with @p framebufferInfo. Returns nullptr (and
    /// logs why) on failure.
    [[nodiscard]] static std::unique_ptr<DebugTriangle> create(RenderDevice& device,
                                                               const nvrhi::FramebufferInfo& framebufferInfo);

    ~DebugTriangle();

    /// Records a draw of the triangle into @p framebuffer, covering the whole framebuffer.
    void draw(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer) const;

private:
    DebugTriangle() = default;
    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_fragmentShader;
    nvrhi::GraphicsPipelineHandle m_pipeline;
};

} // namespace illusion
