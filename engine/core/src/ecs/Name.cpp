#include <illusion/ecs/Name.hpp>

#include <algorithm>

namespace illusion
{

std::string_view entityNameError(std::string_view name) noexcept
{
    if (name.size() > kMaxEntityNameBytes)
    {
        return "must not be longer than 256 bytes";
    }
    for (const char c : name)
    {
        if (c == '/')
        {
            return "must not contain '/'";
        }
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F)
        {
            return "must not contain control characters";
        }
    }
    return {};
}

std::string makeValidEntityName(std::string_view text)
{
    usize size = std::min(text.size(), kMaxEntityNameBytes);
    // Cut before a UTF-8 continuation byte so that no character is split.
    while (size < text.size() && size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80)
    {
        --size;
    }
    std::string name(text.substr(0, size));
    for (char& c : name)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '/' || byte < 0x20 || byte == 0x7F)
        {
            c = '_';
        }
    }
    return name;
}

} // namespace illusion
