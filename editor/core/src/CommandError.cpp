#include <illusion/core/Json.hpp>
#include <illusion/editor/core/CommandError.hpp>
#include <illusion/text/FuzzyMatcher.hpp>

#include <algorithm>
#include <limits>

namespace illusion::editor
{
namespace
{

// Optimal string alignment distance: edits, with adjacent transpositions counting as one.
usize editDistance(std::string_view a, std::string_view b)
{
    std::vector<usize> previous2(b.size() + 1);
    std::vector<usize> previous(b.size() + 1);
    std::vector<usize> current(b.size() + 1);
    for (usize j = 0; j <= b.size(); ++j)
    {
        previous[j] = j;
    }
    for (usize i = 1; i <= a.size(); ++i)
    {
        current[0] = i;
        for (usize j = 1; j <= b.size(); ++j)
        {
            const usize cost = a[i - 1] == b[j - 1] ? 0 : 1;
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
            {
                current[j] = std::min(current[j], previous2[j - 2] + 1);
            }
        }
        std::swap(previous2, previous);
        std::swap(previous, current);
    }
    return previous[b.size()];
}

} // namespace

std::string_view errorKindName(ErrorKind kind) noexcept
{
    switch (kind)
    {
    case ErrorKind::Unauthenticated:
        return "unauthenticated";
    case ErrorKind::PermissionDenied:
        return "permission_denied";
    case ErrorKind::Busy:
        return "busy";
    case ErrorKind::Timeout:
        return "timeout";
    case ErrorKind::Cancelled:
        return "cancelled";
    case ErrorKind::WrongMode:
        return "wrong_mode";
    case ErrorKind::NotFound:
        return "not_found";
    case ErrorKind::AlreadyExists:
        return "already_exists";
    case ErrorKind::ValidationFailed:
        return "validation_failed";
    case ErrorKind::Conflict:
        return "conflict";
    case ErrorKind::Ambiguous:
        return "ambiguous";
    case ErrorKind::PreconditionFailed:
        return "precondition_failed";
    case ErrorKind::IoError:
        return "io_error";
    case ErrorKind::BuildFailed:
        return "build_failed";
    case ErrorKind::ModuleError:
        return "module_error";
    case ErrorKind::Unsupported:
        return "unsupported";
    case ErrorKind::LimitExceeded:
        return "limit_exceeded";
    case ErrorKind::Internal:
        return "internal";
    }
    return "internal";
}

CommandError CommandError::make(ErrorKind kind, std::string message)
{
    CommandError error;
    error.kind = kind;
    error.message = std::move(message);
    error.retryable = kind == ErrorKind::Busy || kind == ErrorKind::Timeout || kind == ErrorKind::Conflict;
    return error;
}

CommandError CommandError::invalid(std::string pointer, std::string message)
{
    CommandError error =
        make(ErrorKind::ValidationFailed, pointer.empty() ? message : "'" + pointer + "' " + message);
    error.errors.push_back(ValueError{std::move(pointer), std::move(message)});
    return error;
}

CommandError withPointerPrefix(CommandError error, std::string_view prefix)
{
    for (ValueError& problem : error.errors)
    {
        problem.pointer = std::string(prefix) + problem.pointer;
    }
    if (error.kind == ErrorKind::ValidationFailed && error.errors.size() == 1)
    {
        error.message = "'" + error.errors.front().pointer + "' " + error.errors.front().message;
    }
    return error;
}

JsonMutValue writeCommandError(JsonMutDocument& doc, const CommandError& error)
{
    JsonMutValue object = doc.makeObject();
    object.add("code", doc.makeI64(static_cast<i64>(error.kind)));
    object.add("message", doc.makeString(error.message));
    JsonMutValue data = object.addObject("data");
    data.add("kind", doc.makeString(errorKindName(error.kind)));
    JsonMutValue errors = data.addArray("errors");
    for (const ValueError& problem : error.errors)
    {
        JsonMutValue entry = errors.pushObject();
        entry.add("path", doc.makeString(problem.pointer));
        entry.add("message", doc.makeString(problem.message));
    }
    JsonMutValue suggestions = data.addArray("suggestions");
    for (const std::string& suggestion : error.suggestions)
    {
        suggestions.push(doc.makeString(suggestion));
    }
    data.add("retryable", doc.makeBool(error.retryable));
    return object;
}

std::vector<std::string> closestMatches(std::string_view name,
                                        const std::vector<std::string_view>& candidates, usize limit)
{
    struct Match
    {
        usize distance;
        i32 score;
        std::string_view candidate;
    };
    std::vector<Match> matches;
    text::FuzzyMatcher matcher(name);
    const usize allowed = std::max<usize>(2, name.size() / 3);
    for (const std::string_view candidate : candidates)
    {
        const usize distance = editDistance(name, candidate);
        const std::optional<i32> score = name.empty() ? std::nullopt : matcher.score(candidate);
        if (distance <= allowed || score)
        {
            // Fuzzy matches of what was typed so far rank after close spellings.
            matches.push_back(
                Match{score && distance > allowed ? std::numeric_limits<usize>::max() : distance,
                      score.value_or(0), candidate});
        }
    }
    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        if (a.distance != b.distance)
        {
            return a.distance < b.distance;
        }
        if (a.score != b.score)
        {
            return a.score > b.score;
        }
        return a.candidate < b.candidate;
    });
    std::vector<std::string> out;
    for (usize i = 0; i < matches.size() && i < limit; ++i)
    {
        out.emplace_back(matches[i].candidate);
    }
    return out;
}

} // namespace illusion::editor
