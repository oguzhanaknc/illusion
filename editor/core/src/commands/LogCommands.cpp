#include "CommandSupport.hpp"

#include <illusion/core/Log.hpp>

#include <array>
#include <deque>

namespace illusion::editor::detail
{
namespace
{

struct LevelName
{
    std::string_view name;
    LogLevel level;
};

constexpr std::array<LevelName, 7> kLevels{{{"trace", LogLevel::Trace},
                                            {"debug", LogLevel::Debug},
                                            {"info", LogLevel::Info},
                                            {"warn", LogLevel::Warn},
                                            {"error", LogLevel::Error},
                                            {"fatal", LogLevel::Fatal},
                                            {"off", LogLevel::Off}}};

constexpr u32 kDefaultLimit = 100;
constexpr u32 kMaxLimit = 1000;

std::string_view levelName(LogLevel level) noexcept
{
    for (const LevelName& entry : kLevels)
    {
        if (entry.level == level)
        {
            return entry.name;
        }
    }
    return "off";
}

Expected<LogLevel, CommandError> parseLevel(std::string_view name, std::string_view pointer, bool allowOff)
{
    std::vector<std::string_view> names;
    for (const LevelName& entry : kLevels)
    {
        if (entry.level == LogLevel::Off && !allowOff)
        {
            continue;
        }
        if (entry.name == name)
        {
            return entry.level;
        }
        names.push_back(entry.name);
    }
    CommandError error = CommandError::invalid(
        std::string(pointer), allowOff ? "must be trace, debug, info, warn, error, fatal or off"
                                       : "must be trace, debug, info, warn, error or fatal");
    error.suggestions = closestMatches(name, names);
    return unexpected(std::move(error));
}

Expected<const LogRingSink*, CommandError> ring(CommandContext& context)
{
    const LogRingSink* log = context.editor().log();
    if (log == nullptr)
    {
        return unexpected(CommandError::make(
            ErrorKind::Unsupported, "this editor keeps no log (EditorContextOptions::logCapacity is 0)"));
    }
    return log;
}

Expected<LogReadResult, CommandError> logRead(CommandContext& context, const LogReadParams& params)
{
    Expected<const LogRingSink*, CommandError> log = ring(context);
    if (!log)
    {
        return unexpected(std::move(log).error());
    }
    LogLevel minLevel = LogLevel::Trace;
    if (params.minLevel)
    {
        Expected<LogLevel, CommandError> parsed = parseLevel(*params.minLevel, "/minLevel", false);
        if (!parsed)
        {
            return unexpected(std::move(parsed).error());
        }
        minLevel = *parsed;
    }
    const u32 limit = params.limit.value_or(kDefaultLimit);
    if (limit == 0 || limit > kMaxLimit)
    {
        return unexpected(CommandError::invalid("/limit", "must be between 1 and 1000"));
    }
    const u64 since = std::max(params.since.value_or(0), context.editor().logCleared());
    // With a cursor the oldest matches come first, so that reading on from `last` loses none; without one, a
    // console wants the newest.
    const bool paging = params.since.has_value();
    std::deque<LogEntry> matches;
    LogReadResult result;
    bool full = false;
    const u64 visited = (*log)->readSince(since, [&](const LogRecord& record) {
        if (full || record.level < minLevel ||
            (params.category && record.categoryView() != *params.category) ||
            (params.contains && record.textView().find(*params.contains) == std::string_view::npos))
        {
            return;
        }
        matches.push_back(LogEntry{record.sequence, record.timeSeconds, std::string(levelName(record.level)),
                                   std::string(record.categoryView()), std::string(record.textView())});
        if (matches.size() > limit)
        {
            matches.pop_front();
        }
        full = paging && matches.size() == limit;
    });
    result.last = full ? matches.back().sequence : visited;
    result.entries.assign(std::make_move_iterator(matches.begin()), std::make_move_iterator(matches.end()));
    result.dropped = (*log)->droppedCount();
    return result;
}

Expected<LogStateResult, CommandError> logClear(CommandContext& context, const NoParams&)
{
    Expected<const LogRingSink*, CommandError> log = ring(context);
    if (!log)
    {
        return unexpected(std::move(log).error());
    }
    if (!context.dryRun())
    {
        context.editor().clearLog();
    }
    return LogStateResult{std::string(levelName(Logger::instance().level())), (*log)->latestSequence()};
}

Expected<LogStateResult, CommandError> logSetLevel(CommandContext& context, const LogSetLevelParams& params)
{
    Expected<LogLevel, CommandError> level = parseLevel(params.level, "/level", true);
    if (!level)
    {
        return unexpected(std::move(level).error());
    }
    if (!context.dryRun())
    {
        Logger::instance().setLevel(*level);
    }
    const LogRingSink* log = context.editor().log();
    return LogStateResult{std::string(levelName(context.dryRun() ? Logger::instance().level() : *level)),
                          log != nullptr ? log->latestSequence() : 0};
}

} // namespace

void registerLogCommands(CommandRegistry& registry)
{
    registry.add<&logRead>({
        .name = "log.read",
        .title = strings::kCommandLogReadTitle,
        .description = strings::kCommandLogReadDescription,
        .agentSummary =
            "Returns recent log messages, oldest first, filtered by level, category or text; pass "
            "the returned last as since to read only newer ones. Example: {\"minLevel\": \"warn\", "
            "\"limit\": 20}.",
        .category = "Log",
        .icon = "receipt_long",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&logClear>({
        .name = "log.clear",
        .title = strings::kCommandLogClearTitle,
        .description = strings::kCommandLogClearDescription,
        .agentSummary =
            "Hides the messages logged so far from log.read (the console starts empty). Example: {}.",
        .category = "Log",
        .icon = "clear_all",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
    registry.add<&logSetLevel>({
        .name = "log.set_level",
        .title = strings::kCommandLogSetLevelTitle,
        .description = strings::kCommandLogSetLevelDescription,
        .agentSummary =
            "Sets the lowest level the process logs (trace and debug exist only in Debug builds). "
            "Example: {\"level\": \"debug\"}.",
        .category = "Log",
        .icon = "tune",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::System,
    });
}

} // namespace illusion::editor::detail
