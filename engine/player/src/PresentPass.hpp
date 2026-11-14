#pragma once

// Draws the player's 3D view onto a back buffer, scaling it to the back buffer's size.

#include <illusion/core/Base.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>

namespace illusion
{
class RenderDevice;
} // namespace illusion

namespace illusion::detail
{

class PresentPass : NonCopyable
{
public:
    // Null (logged) when the shaders or the pipeline cannot be created for @p framebuffer's format.
    [[nodiscard]] static std::unique_ptr<PresentPass> create(RenderDevice& device,
                                                             nvrhi::IFramebuffer* framebuffer);

    // Records the draw of @p source over the whole of @p framebuffer.
    void draw(nvrhi::ICommandList* commands, nvrhi::IFramebuffer* framebuffer, nvrhi::ITexture* source);

private:
    PresentPass() = default;

    nvrhi::IDevice* m_device = nullptr;
    nvrhi::ShaderHandle m_vertex;
    nvrhi::ShaderHandle m_fragment;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::SamplerHandle m_sampler;
    nvrhi::GraphicsPipelineHandle m_pipeline;
    nvrhi::BindingSetHandle m_bindings;
    nvrhi::TextureHandle m_boundSource; // the source m_bindings was made for (kept alive with it)
    bool m_decodeSrgb = false;
};

} // namespace illusion::detail
