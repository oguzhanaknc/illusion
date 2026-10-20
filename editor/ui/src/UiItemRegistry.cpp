#include <illusion/core/Assert.hpp>
#include <illusion/editor/ui/UiItemRegistry.hpp>

#include <cstdarg>
#include <imgui.h>
#include <imgui_internal.h>
#include <utility>

namespace illusion::editor::ui
{

namespace
{

// Registries by context. Few contexts exist at a time (one per editor window), so a small list suffices.
struct Attachment
{
    const ImGuiContext* context;
    UiItemRegistry* registry;
};
thread_local std::vector<Attachment> t_attachments;

} // namespace

std::string_view UiItem::stableId() const noexcept
{
    const std::string_view view = label;
    const usize separator = view.find("###");
    return separator == std::string_view::npos ? view : view.substr(separator + 3);
}

UiItemRegistry::UiItemRegistry(ImGuiContext* context)
    : m_context(context)
{
    ILL_ASSERT(attachedTo(context) == nullptr, "a UiItemRegistry is already attached to this context");
    t_attachments.push_back(Attachment{context, this});
    context->TestEngineHookItems = true;
}

UiItemRegistry::~UiItemRegistry()
{
    m_context->TestEngineHookItems = false;
    std::erase_if(t_attachments,
                  [this](const Attachment& attachment) { return attachment.registry == this; });
}

void UiItemRegistry::beginFrame()
{
    m_items.clear();
    m_pendingId = 0;
}

std::optional<UiItem> UiItemRegistry::find(std::string_view stableId) const
{
    for (auto it = m_items.rbegin(); it != m_items.rend(); ++it)
    {
        if (it->stableId() == stableId)
        {
            return *it;
        }
    }
    return std::nullopt;
}

void UiItemRegistry::onItemAdd(u32 id, Vec2 min, Vec2 max)
{
    UiItem item;
    item.id = id;
    item.min = min;
    item.max = max;
    if (m_pendingId == id && id != 0)
    {
        item.label = std::move(m_pendingLabel);
        item.statusFlags = m_pendingFlags;
        m_pendingId = 0;
    }
    m_items.push_back(std::move(item));
}

void UiItemRegistry::onItemInfo(u32 id, const char* label, u32 flags)
{
    // The label belongs to the item added last with the same id: ItemInfo usually follows ItemAdd, but some
    // widgets (tabs) report it first, so an unknown id is kept for the next ItemAdd.
    for (auto it = m_items.rbegin(); it != m_items.rend(); ++it)
    {
        if (it->id == id)
        {
            it->label = label != nullptr ? label : "";
            it->statusFlags = flags;
            return;
        }
    }
    m_pendingId = id;
    m_pendingLabel = label != nullptr ? label : "";
    m_pendingFlags = flags;
}

UiItemRegistry* UiItemRegistry::attachedTo(const ImGuiContext* context) noexcept
{
    for (const Attachment& attachment : t_attachments)
    {
        if (attachment.context == context)
        {
            return attachment.registry;
        }
    }
    return nullptr;
}

} // namespace illusion::editor::ui

// Dear ImGui's test engine hooks (IMGUI_ENABLE_TEST_ENGINE). They are only called while a registry has
// enabled ImGuiContext::TestEngineHookItems.

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*)
{
    if (illusion::editor::ui::UiItemRegistry* registry =
            illusion::editor::ui::UiItemRegistry::attachedTo(ctx))
    {
        registry->onItemAdd(id, illusion::Vec2(bb.Min.x, bb.Min.y), illusion::Vec2(bb.Max.x, bb.Max.y));
    }
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label,
                                  ImGuiItemStatusFlags flags)
{
    if (illusion::editor::ui::UiItemRegistry* registry =
            illusion::editor::ui::UiItemRegistry::attachedTo(ctx))
    {
        registry->onItemInfo(id, label, static_cast<illusion::u32>(flags));
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...)
{
}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID)
{
    return nullptr;
}
