#include <illusion/core/Assert.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/editor/ui/FrameText.hpp>
#include <illusion/editor/ui/Icon.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/TextOutput.hpp>

namespace illusion::editor::ui
{

FrameText::FrameText(const text::Localization& localization, usize capacity)
    : m_localization(localization)
    , m_arena(std::make_unique<LinearAllocator>(capacity))
{
    ILL_ASSERT(capacity > 0, "FrameText needs a capacity");
}

FrameText::~FrameText() = default;

void FrameText::reset() noexcept
{
    if (!m_retired.empty())
    {
        // The last frame needed more than the arena: keep one arena as large as all of them together.
        usize capacity = m_arena->capacity();
        for (const std::unique_ptr<LinearAllocator>& arena : m_retired)
        {
            capacity += arena->capacity();
        }
        m_retired.clear();
        m_arena = std::make_unique<LinearAllocator>(capacity);
    }
    m_arena->reset();
}

const char* FrameText::format(text::TextKey key, Arguments arguments)
{
    Parts parts;
    parts.key = &key;
    parts.arguments = arguments;
    return build(parts);
}

const char* FrameText::label(text::TextKey key, std::string_view id, Arguments arguments)
{
    Parts parts;
    parts.key = &key;
    parts.arguments = arguments;
    parts.id = id;
    parts.hasId = true;
    return build(parts);
}

const char* FrameText::iconLabel(const Icon& icon, text::TextKey key, std::string_view id,
                                 Arguments arguments)
{
    Parts parts;
    parts.icon = &icon;
    parts.key = &key;
    parts.arguments = arguments;
    parts.id = id;
    parts.hasId = true;
    return build(parts);
}

const char* FrameText::iconOnly(const Icon& icon, std::string_view id)
{
    Parts parts;
    parts.icon = &icon;
    parts.id = id;
    parts.hasId = true;
    return build(parts);
}

const char* FrameText::verbatim(std::string_view text, std::string_view id)
{
    Parts parts;
    parts.text = text;
    parts.id = id;
    parts.hasId = true;
    return build(parts);
}

const char* FrameText::hidden(std::string_view id)
{
    Parts parts;
    parts.id = id;
    parts.hasId = true;
    return build(parts);
}

const char* FrameText::copy(std::string_view text)
{
    Parts parts;
    parts.text = text;
    return build(parts);
}

const char* FrameText::concat(std::string_view a, std::string_view b)
{
    Parts parts;
    parts.text = a;
    parts.suffix = b;
    return build(parts);
}

usize FrameText::used() const noexcept
{
    usize used = m_arena->used();
    for (const std::unique_ptr<LinearAllocator>& arena : m_retired)
    {
        used += arena->used();
    }
    return used;
}

const char* FrameText::build(const Parts& parts)
{
    for (;;)
    {
        const LinearAllocator::Marker marker = m_arena->mark();
        text::TextOutput out(*m_arena);
        if (parts.icon != nullptr)
        {
            out.append(parts.icon->view());
            if (parts.key != nullptr)
            {
                out.push(' ');
            }
        }
        if (parts.key != nullptr)
        {
            const auto formatted = m_localization.format(*parts.key, parts.arguments, out);
            ILL_ASSERT(formatted.hasValue(), "formatting '{}' failed: {}", parts.key->name(),
                       formatted.hasValue() ? std::string_view()
                                            : std::string_view(formatted.error().message));
        }
        out.append(parts.text);
        out.append(parts.suffix);
        if (parts.hasId)
        {
            out.append("###");
            out.append(parts.id);
        }
        out.push('\0');
        if (!out.failed())
        {
            return out.view().data();
        }
        // Full: continue in a new arena twice as large; the old one keeps this frame's earlier text.
        m_arena->rewind(marker);
        const usize capacity = m_arena->capacity() * 2;
        m_retired.push_back(std::move(m_arena));
        m_arena = std::make_unique<LinearAllocator>(capacity);
    }
}

} // namespace illusion::editor::ui
