#include <illusion/ecs/Name.hpp>

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

} // namespace illusion
