#include "CommandSupport.hpp"

#include <illusion/core/Log.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Prefab.hpp>

#include <algorithm>

namespace illusion::editor::detail
{
namespace
{

constexpr u32 kDefaultFrames = 120;

Expected<ProfilerGetResult, CommandError> profilerGet(CommandContext& context,
                                                      const ProfilerGetParams& params)
{
    const FrameProfiler& profiler = context.editor().profiler();
    const u32 kept = profiler.desc().frames;
    const u32 frames = params.frames.value_or(std::min(kDefaultFrames, kept));
    if (frames == 0 || frames > kept)
    {
        return unexpected(CommandError::invalid("/frames", "must be between 1 and " + std::to_string(kept)));
    }
    ProfilerGetResult result;
    result.paused = profiler.paused();
    result.frameIndex = static_cast<u32>(std::min<u64>(profiler.frameIndex(), ~u32{0}));
    const usize reported = std::min<usize>(frames, profiler.frameCount());
    result.frames.reserve(reported);
    f32 total = 0.0f;
    for (usize age = reported; age-- > 0;)
    {
        const ProfileFrame frame = profiler.frame(age);
        result.frames.push_back(frame.durationMs);
        total += frame.durationMs;
        result.maxMs = std::max(result.maxMs, frame.durationMs);
        result.truncated = result.truncated || frame.truncated;
    }
    if (reported != 0)
    {
        result.averageMs = total / static_cast<f32>(reported);
        result.fps = result.averageMs > 0.0f ? 1000.0f / result.averageMs : 0.0f;
    }
    std::vector<ProfileScopeStats> stats;
    profiler.summarize(reported, stats);
    result.scopes.reserve(stats.size());
    for (const ProfileScopeStats& place : stats)
    {
        result.scopes.push_back(ProfilerScopeEntry{std::string(place.name), place.depth, place.calls,
                                                   place.lastMs, place.averageMs, place.maxMs});
    }
    return result;
}

Expected<ProfilerSetResult, CommandError> profilerSet(CommandContext& context,
                                                      const ProfilerSetParams& params)
{
    FrameProfiler& profiler = context.editor().profiler();
    const bool paused = params.paused.value_or(profiler.paused());
    if (context.dryRun())
    {
        return ProfilerSetResult{paused,
                                 params.clear.value_or(false) ? 0u : static_cast<u32>(profiler.frameCount())};
    }
    if (paused)
    {
        profiler.pause();
    }
    else
    {
        profiler.resume();
    }
    if (params.clear.value_or(false))
    {
        profiler.clear();
    }
    return ProfilerSetResult{profiler.paused(), static_cast<u32>(profiler.frameCount())};
}

DiagnosticEntry issue(std::string_view severity, std::string_view code, std::string subject, u32 count,
                      std::string message)
{
    return DiagnosticEntry{std::string(severity), std::string(code), std::move(subject), count,
                           std::move(message)};
}

void renderIssues(CommandContext& context, std::vector<DiagnosticEntry>& errors,
                  std::vector<DiagnosticEntry>& warnings)
{
    ViewportHost* host = context.editor().viewports();
    if (host == nullptr)
    {
        return;
    }
    const ViewportGpuInfo gpu = host->gpuInfo();
    if (gpu.validationErrors != 0)
    {
        errors.push_back(issue("error", "render.validation", "", gpu.validationErrors,
                               std::to_string(gpu.validationErrors) +
                                   " Vulkan validation error(s) since the editor started; see the log"));
    }
    for (const ViewportInfo& info : host->viewports())
    {
        const Expected<ViewportRenderStats, CommandError> stats = host->renderStats(info.id);
        if (stats && stats->lightsDropped != 0)
        {
            warnings.push_back(issue("warning", "render.lights_dropped", info.id, stats->lightsDropped,
                                     std::to_string(stats->lightsDropped) + " active light(s) beyond the " +
                                         "renderer's limits are not drawn in viewport " + info.id));
        }
    }
}

void logIssues(CommandContext& context, std::vector<DiagnosticEntry>& errors,
               std::vector<DiagnosticEntry>& warnings)
{
    const LogRingSink* log = context.editor().log();
    if (log == nullptr)
    {
        return;
    }
    u32 errorCount = 0;
    u32 warningCount = 0;
    (void)log->readSince(context.editor().logCleared(), [&](const LogRecord& record) {
        if (record.level >= LogLevel::Error)
        {
            ++errorCount;
        }
        else if (record.level == LogLevel::Warn)
        {
            ++warningCount;
        }
    });
    if (errorCount != 0)
    {
        errors.push_back(issue("error", "log.errors", "", errorCount,
                               std::to_string(errorCount) + " error message(s) in the log (log.read)"));
    }
    if (warningCount != 0)
    {
        warnings.push_back(issue("warning", "log.warnings", "", warningCount,
                                 std::to_string(warningCount) + " warning message(s) in the log (log.read)"));
    }
}

void assetIssues(CommandContext& context, std::vector<DiagnosticEntry>& errors)
{
    if (context.editor().project() == nullptr)
    {
        return;
    }
    Expected<AssetFindMissingResult, CommandError> missing = findMissingAssets(context);
    if (!missing)
    {
        return;
    }
    for (const std::string& asset : missing->assets)
    {
        const u32 references = static_cast<u32>(std::count_if(
            missing->references.begin(), missing->references.end(), [&](const AssetReference& r) {
                return std::string_view(r.asset).substr(0, r.asset.find('#')) == asset;
            }));
        errors.push_back(issue("error", "asset.missing", asset, references,
                               asset + " does not exist; " + std::to_string(references) +
                                   " reference(s) point to it (asset.find_missing)"));
    }
}

void prefabIssues(CommandContext& context, std::vector<DiagnosticEntry>& warnings)
{
    EditorContext& editor = context.editor();
    const usize first = warnings.size();
    editor.world().query<const UnresolvedPrefab>().each(
        [&](Entity entity, const UnresolvedPrefab& unresolved) {
            warnings.push_back(issue("warning", "prefab.unresolved",
                                     toString(editor.entities().guidOf(entity)), 1,
                                     "an instance of " + unresolved.prefab +
                                         " could not be loaded; it keeps its record and overrides"));
        });
    std::sort(warnings.begin() + static_cast<std::ptrdiff_t>(first), warnings.end(),
              [](const DiagnosticEntry& a, const DiagnosticEntry& b) { return a.subject < b.subject; });
}

Expected<DiagnosticsResult, CommandError> diagnosticsGet(CommandContext& context, const NoParams&)
{
    std::vector<DiagnosticEntry> errors;
    std::vector<DiagnosticEntry> warnings;
    renderIssues(context, errors, warnings);
    logIssues(context, errors, warnings);
    assetIssues(context, errors);
    prefabIssues(context, warnings);
    DiagnosticsResult result;
    result.errors = static_cast<u32>(errors.size());
    result.warnings = static_cast<u32>(warnings.size());
    result.issues = std::move(errors);
    std::move(warnings.begin(), warnings.end(), std::back_inserter(result.issues));
    return result;
}

} // namespace

void registerProfilerCommands(CommandRegistry& registry)
{
    registry.add<&profilerGet>({
        .name = "profiler.get",
        .title = strings::kCommandProfilerGetTitle,
        .description = strings::kCommandProfilerGetDescription,
        .agentSummary =
            "Returns the editor's CPU frame times (milliseconds per frame, oldest first, with average, "
            "max and fps) and the time per place of the frame (phases such as ui, render, play and editor, "
            "panels nested under ui) over the newest frames (default 120). Example: {\"frames\": 60}.",
        .category = "Profiler",
        .icon = "speed",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&profilerSet>({
        .name = "profiler.set",
        .title = strings::kCommandProfilerSetTitle,
        .description = strings::kCommandProfilerSetDescription,
        .agentSummary = "Pauses or resumes recording of the editor's frames (paused keeps the recorded "
                        "ones) and with clear forgets them. Example: {\"paused\": true}.",
        .category = "Profiler",
        .icon = "pause_circle",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .palette = true,
    });
    registry.add<&diagnosticsGet>({
        .name = "diagnostics.get",
        .title = strings::kCommandDiagnosticsGetTitle,
        .description = strings::kCommandDiagnosticsGetDescription,
        .agentSummary =
            "Lists problems in the editor, errors first: Vulkan validation errors, errors and warnings "
            "in the log, missing assets with how many references point to each, prefab instances that "
            "could not be loaded and lights a viewport cannot draw. Each issue has a code, a subject "
            "(viewport, asset path or entity id) and a count. Example: {}.",
        .category = "Profiler",
        .icon = "health_and_safety",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .palette = true,
    });
}

} // namespace illusion::editor::detail
