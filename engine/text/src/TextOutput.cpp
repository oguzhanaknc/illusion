#include <illusion/core/Memory.hpp>
#include <illusion/text/TextOutput.hpp>

#include <algorithm>
#include <cstring>

namespace illusion::text
{

TextOutput::TextOutput(std::string& out) noexcept
    : m_string(&out)
    , m_start(out.size())
{
}

TextOutput::TextOutput(LinearAllocator& arena) noexcept
    : m_arena(&arena)
{
}

void TextOutput::append(std::string_view text)
{
    if (text.empty() || m_failed)
    {
        return;
    }
    if (m_string != nullptr)
    {
        m_string->append(text);
        return;
    }
    if (m_size + text.size() > m_capacity)
    {
        constexpr usize kMinCapacity = 64;
        const usize capacity = std::max({kMinCapacity, m_size + text.size(), m_capacity * 2});
        void* grown = m_arena->reallocate(m_data, m_capacity, capacity, 1);
        if (grown == nullptr)
        {
            m_failed = true;
            return;
        }
        m_data = static_cast<char*>(grown);
        m_capacity = capacity;
    }
    std::memcpy(m_data + m_size, text.data(), text.size());
    m_size += text.size();
}

std::string_view TextOutput::view() const noexcept
{
    if (m_string != nullptr)
    {
        return std::string_view(*m_string).substr(m_start);
    }
    return {m_data, m_size};
}

usize TextOutput::size() const noexcept
{
    return m_string != nullptr ? m_string->size() - m_start : m_size;
}

} // namespace illusion::text
