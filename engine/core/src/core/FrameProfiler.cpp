#include <illusion/core/Assert.hpp>
#include <illusion/core/FrameProfiler.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Time.hpp>

#include <algorithm>

namespace illusion
{

namespace
{

constexpr u32 kDropped = ~0u;

f64 defaultClock() noexcept
{
    return monotonicSeconds();
}

f32 milliseconds(f64 seconds) noexcept
{
    return static_cast<f32>(seconds * 1000.0);
}

} // namespace

FrameProfiler::FrameProfiler(const FrameProfilerDesc& desc, Clock clock)
    : m_desc{std::max(desc.frames, 1u), std::max(desc.scopesPerFrame, 1u), std::max(desc.maxDepth, 1u)}
    , m_clock(clock != nullptr ? clock : &defaultClock)
    // One slot more than the frames kept: the frame in progress never overwrites a recorded one.
    , m_frames(std::make_unique<FrameRecord[]>(static_cast<usize>(m_desc.frames) + 1))
    , m_scopes(
          std::make_unique<ProfileScope[]>((static_cast<usize>(m_desc.frames) + 1) * m_desc.scopesPerFrame))
    , m_open(std::make_unique<Open[]>(m_desc.maxDepth))
    , m_newest(m_desc.frames)
{
    m_current.reserve(m_desc.scopesPerFrame);
}

FrameProfiler::~FrameProfiler() = default;

f64 FrameProfiler::now() const noexcept
{
    return m_clock();
}

ProfileScope* FrameProfiler::slot(usize ring) const noexcept
{
    return m_scopes.get() + ring * m_desc.scopesPerFrame;
}

void FrameProfiler::beginFrame() noexcept
{
    m_inFrame = true;
    m_recording = !m_pauseRequested;
    m_scopeCount = 0;
    m_depth = 0;
    m_truncated = false;
    ++m_nextIndex;
    m_frameStart = now();
}

void FrameProfiler::endFrame() noexcept
{
    if (!m_inFrame)
    {
        return;
    }
    while (m_depth > 0)
    {
        endScope();
    }
    const f64 end = now();
    m_inFrame = false;
    if (!m_recording)
    {
        return;
    }
    const usize ring = (m_newest + 1) % (static_cast<usize>(m_desc.frames) + 1);
    FrameRecord& record = m_frames[ring];
    record.index = m_nextIndex - 1;
    record.durationMs = milliseconds(end - m_frameStart);
    record.scopeCount = m_scopeCount;
    record.truncated = m_truncated;
    m_newest = ring;
    m_count = std::min<usize>(m_count + 1, m_desc.frames);
}

void FrameProfiler::beginScope(std::string_view name) noexcept
{
    if (!m_inFrame)
    {
        return;
    }
    const u32 depth = m_depth++;
    if (!m_recording || depth >= m_desc.maxDepth)
    {
        m_truncated = m_truncated || m_recording;
        return;
    }
    Open& open = m_open[depth];
    open.path = hashCombine(depth == 0 ? 0 : m_open[depth - 1].path, fnv1a64(name));
    if (m_scopeCount == m_desc.scopesPerFrame)
    {
        open.scope = kDropped;
        m_truncated = true;
        return;
    }
    open.scope = m_scopeCount++;
    open.start = now();
    ProfileScope& scope = slot((m_newest + 1) % (static_cast<usize>(m_desc.frames) + 1))[open.scope];
    scope.name = name;
    scope.path = open.path;
    scope.startMs = milliseconds(open.start - m_frameStart);
    scope.durationMs = 0.0f;
    scope.depth = static_cast<u16>(depth);
}

void FrameProfiler::endScope()
{
    ILL_ASSERT(!m_inFrame || m_depth > 0, "endScope() without an open scope");
    if (!m_inFrame || m_depth == 0)
    {
        return;
    }
    const u32 depth = --m_depth;
    if (!m_recording || depth >= m_desc.maxDepth || m_open[depth].scope == kDropped)
    {
        return;
    }
    const Open& open = m_open[depth];
    ProfileScope& scope = slot((m_newest + 1) % (static_cast<usize>(m_desc.frames) + 1))[open.scope];
    scope.durationMs = milliseconds(now() - open.start);
}

void FrameProfiler::clear() noexcept
{
    m_count = 0;
}

ProfileFrame FrameProfiler::frame(usize age) const noexcept
{
    ILL_ASSERT(age < m_count, "frame age out of range");
    const usize slots = static_cast<usize>(m_desc.frames) + 1;
    const usize ring = (m_newest + slots - age) % slots;
    const FrameRecord& record = m_frames[ring];
    return ProfileFrame{record.index, record.durationMs,
                        std::span<const ProfileScope>(slot(ring), record.scopeCount), record.truncated};
}

void FrameProfiler::summarize(usize frames, std::vector<ProfileScopeStats>& out) const
{
    out.clear();
    frames = std::min(frames, m_count);
    if (frames == 0)
    {
        return;
    }
    // ProfileScopeStats::averageMs holds the running total until the end; each frame's totals per place
    // are summed in m_current and folded into the maximum when the frame is done. A frame has at most
    // scopesPerFrame places, the capacity reserved for m_current, so it never grows.
    std::vector<CurrentPlace>& current = m_current;
    for (usize age = 0; age < frames; ++age)
    {
        const ProfileFrame recorded = frame(age);
        current.clear();
        for (const ProfileScope& scope : recorded.scopes)
        {
            auto entry = std::find_if(out.begin(), out.end(), [&](const ProfileScopeStats& stats) {
                return stats.path == scope.path;
            });
            if (entry == out.end())
            {
                out.push_back(ProfileScopeStats{scope.name, scope.path, scope.depth, 0, 0.0f, 0.0f, 0.0f});
                entry = out.end() - 1;
            }
            const usize index = static_cast<usize>(entry - out.begin());
            auto open = std::find_if(current.begin(), current.end(),
                                     [&](const CurrentPlace& item) { return item.entry == index; });
            if (open == current.end())
            {
                current.push_back(CurrentPlace{index, 0.0f});
                open = current.end() - 1;
            }
            open->ms += scope.durationMs;
            if (age == 0)
            {
                ++entry->calls;
            }
        }
        for (const CurrentPlace& item : current)
        {
            ProfileScopeStats& stats = out[item.entry];
            stats.averageMs += item.ms;
            stats.maxMs = std::max(stats.maxMs, item.ms);
            if (age == 0)
            {
                stats.lastMs = item.ms;
            }
        }
    }
    for (ProfileScopeStats& stats : out)
    {
        // Rounding can put the f32 quotient one step above the largest value it averages.
        stats.averageMs = std::min(stats.averageMs / static_cast<f32>(frames), stats.maxMs);
    }
}

} // namespace illusion
