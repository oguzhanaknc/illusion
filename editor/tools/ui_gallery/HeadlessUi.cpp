#include "HeadlessUi.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/editor/ui/UiContext.hpp>

#include <imgui.h>

namespace illusion::editor::gallery
{

HeadlessUi::HeadlessUi(ui::UiContext& context)
    : m_context(context)
{
    context.makeCurrent();
    ImGuiIO& io = ImGui::GetIO();
    ILL_ASSERT(io.BackendRendererUserData == nullptr, "the context already has a renderer backend");
    io.BackendRendererUserData = this;
    io.BackendRendererName = "illusion_headless";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
}

HeadlessUi::~HeadlessUi()
{
    m_context.makeCurrent();
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
    {
        if (texture->RefCount == 1 && texture->Status != ImTextureStatus_Destroyed)
        {
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererUserData = nullptr;
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
}

void HeadlessUi::beginFrame(Vec2 displaySize, f32 deltaSeconds)
{
    m_context.makeCurrent();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(displaySize.x, displaySize.y);
    io.DeltaTime = deltaSeconds;
    ImGui::NewFrame();
}

void HeadlessUi::endFrame()
{
    ImGui::Render();
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
    {
        switch (texture->Status)
        {
        case ImTextureStatus_WantCreate:
            texture->SetTexID(static_cast<ImTextureID>(m_nextId++));
            texture->SetStatus(ImTextureStatus_OK);
            ++m_textures;
            break;
        case ImTextureStatus_WantUpdates:
            texture->SetStatus(ImTextureStatus_OK);
            break;
        case ImTextureStatus_WantDestroy:
            if (texture->UnusedFrames > 0)
            {
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
                --m_textures;
            }
            break;
        default:
            break;
        }
    }
}

} // namespace illusion::editor::gallery
