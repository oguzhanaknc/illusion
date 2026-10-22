#include <illusion/core/VirtualPath.hpp>

#include <algorithm>
#include <vector>

namespace illusion
{
namespace
{

char toLowerAscii(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return toLowerAscii(x) == toLowerAscii(y);
           });
}

// Names Windows maps to devices regardless of extension ("nul.txt" is the NUL device). Rejected on every
// platform so a virtual path behaves the same everywhere.
bool isReservedDeviceName(std::string_view component) noexcept
{
    const std::string_view stem = component.substr(0, component.find('.'));
    for (const std::string_view name : {"con", "prn", "aux", "nul", "conin$", "conout$"})
    {
        if (equalsIgnoreCase(stem, name))
        {
            return true;
        }
    }
    if (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9' &&
        (equalsIgnoreCase(stem.substr(0, 3), "com") || equalsIgnoreCase(stem.substr(0, 3), "lpt")))
    {
        return true;
    }
    return false;
}

} // namespace

bool isValidMountName(std::string_view name) noexcept
{
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

bool isValidRelativePath(std::string_view relative) noexcept
{
    if (relative.empty())
    {
        return true;
    }
    usize start = 0;
    while (start <= relative.size())
    {
        const usize end = std::min(relative.find('/', start), relative.size());
        const std::string_view component = relative.substr(start, end - start);
        // Windows strips trailing dots and spaces, which would make "save." and "save" the same file.
        if (component.empty() || component == "." || component == ".." || component.back() == '.' ||
            component.back() == ' ' || isReservedDeviceName(component))
        {
            return false;
        }
        for (const char c : component)
        {
            if (c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20)
            {
                return false;
            }
        }
        if (end == relative.size())
        {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool isNormalizedVirtualPath(std::string_view virtualPath) noexcept
{
    const usize separator = virtualPath.find(":/");
    return separator != std::string_view::npos && isValidMountName(virtualPath.substr(0, separator)) &&
           isValidRelativePath(virtualPath.substr(separator + 2));
}

std::optional<std::string> normalizeVirtualPath(std::string_view virtualPath)
{
    const usize separator = virtualPath.find(":/");
    if (separator == std::string_view::npos || !isValidMountName(virtualPath.substr(0, separator)))
    {
        return std::nullopt;
    }
    std::vector<std::string_view> components;
    usize start = separator + 2;
    while (start <= virtualPath.size())
    {
        const usize end = std::min(virtualPath.find('/', start), virtualPath.size());
        const std::string_view component = virtualPath.substr(start, end - start);
        if (component == "..")
        {
            if (components.empty())
            {
                return std::nullopt; // would leave the mount
            }
            components.pop_back();
        }
        else if (!component.empty() && component != ".")
        {
            components.push_back(component);
        }
        start = end + 1;
    }
    std::string result(virtualPath.substr(0, separator + 2));
    for (usize i = 0; i < components.size(); ++i)
    {
        if (i > 0)
        {
            result.push_back('/');
        }
        result.append(components[i]);
    }
    if (!isValidRelativePath(std::string_view(result).substr(separator + 2)))
    {
        return std::nullopt;
    }
    return result;
}

} // namespace illusion
