#pragma once

#include <illusion/core/Json.hpp>

#include <charconv>
#include <string_view>

namespace illusion::editor::detail
{

// Command paths use slash-separated JSON fields and indices. Axis names also work on vectors.
inline JsonValue fieldChild(JsonValue parent, std::string_view segment)
{
    if (segment.empty())
    {
        return {};
    }
    if (parent.isObject())
    {
        return parent.find(segment);
    }
    if (!parent.isArray())
    {
        return {};
    }
    usize index = 0;
    if (segment.size() == 1 && std::string_view("xyzw").find(segment) != std::string_view::npos)
    {
        index = std::string_view("xyzw").find(segment);
    }
    else
    {
        const auto [end, error] = std::from_chars(segment.data(), segment.data() + segment.size(), index);
        if (error != std::errc() || end != segment.data() + segment.size())
        {
            return {};
        }
    }
    return parent[index];
}

inline JsonValue fieldAt(JsonValue root, std::string_view path)
{
    while (!path.empty())
    {
        const usize separator = path.find('/');
        root = fieldChild(root, path.substr(0, separator));
        if (separator == std::string_view::npos)
        {
            return root;
        }
        path.remove_prefix(separator + 1);
        if (path.empty())
        {
            return {};
        }
    }
    return {};
}

// Rebuilds only the path's ancestors, keeping siblings intact. The caller validates the whole value.
inline JsonMutValue replaceField(JsonMutDocument& doc, JsonValue root, std::string_view path,
                                 JsonValue replacement)
{
    const usize separator = path.find('/');
    const std::string_view segment = path.substr(0, separator);
    const JsonValue child = fieldChild(root, segment);
    if (child.isAbsent())
    {
        return {};
    }
    const JsonMutValue changed = separator == std::string_view::npos
                                     ? doc.copy(replacement)
                                     : replaceField(doc, child, path.substr(separator + 1), replacement);
    if (changed.isAbsent())
    {
        return {};
    }
    if (root.isObject())
    {
        JsonMutValue result = doc.copy(root);
        result.set(segment, changed);
        return result;
    }
    JsonMutValue result = doc.makeArray();
    for (const JsonValue element : root.elements())
    {
        result.push(element == child ? changed : doc.copy(element));
    }
    return result;
}

} // namespace illusion::editor::detail
