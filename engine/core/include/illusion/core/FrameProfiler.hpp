#pragma once

/// @file
/// An in-process CPU frame profiler for tools: nested, named scopes of the last frames in a fixed ring.
/// Unlike Tracy (Profiler.hpp), it is always compiled in and the program itself reads the results, for
/// example to draw a frame-time graph in the editor.
///
/// ```cpp
/// FrameProfiler profiler;
/// while (running)
/// {
///     profiler.beginFrame();
///     {
///         FrameProfiler::Scope scope(profiler, "update");
///         update();
///     }
///     profiler.endFrame();
/// }
/// ```

#include <illusion/core/Base.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace illusion
{

/// One measured scope of a frame.
struct ProfileScope
{
    std::string_view name; ///< Name given to FrameProfiler::beginScope().
    u64 path = 0;          ///< Hash of the names from the outermost scope to this one: the same place.
    f32 startMs = 0.0f;    ///< Start, in milliseconds since the frame began.
    f32 durationMs = 0.0f; ///< Duration in milliseconds.
    u16 depth = 0;         ///< 0 for a scope opened directly in the frame, 1 inside one of those, ...
};

/// One recorded frame. The scopes are in the order they began, so each scope's children follow it.
struct ProfileFrame
{
    u64 index = 0;                        ///< Number of the frame, counted from 0 by beginFrame().
    f32 durationMs = 0.0f;                ///< From beginFrame() to endFrame().
    std::span<const ProfileScope> scopes; ///< Scopes of the frame; valid until the next beginFrame().
    bool truncated = false;               ///< Scopes were dropped: more than the profiler keeps per frame.
};

/// Time spent in one place (ProfileScope::path) over the last frames.
struct ProfileScopeStats
{
    std::string_view name; ///< Name of the scope.
    u64 path = 0;          ///< The place, as in ProfileScope::path.
    u16 depth = 0;         ///< Nesting depth.
    u32 calls = 0;         ///< Times the scope ran in the newest frame.
    f32 lastMs = 0.0f;     ///< Total time in the newest frame (0 if it did not run there).
    f32 averageMs = 0.0f;  ///< Total time over the frames, divided by the number of frames.
    f32 maxMs = 0.0f;      ///< Largest total time in one frame.
};

/// Sizes of a FrameProfiler's storage, all reserved when it is created.
struct FrameProfilerDesc
{
    u32 frames = 240;         ///< Frames kept (at least 1).
    u32 scopesPerFrame = 256; ///< Scopes kept per frame (at least 1); later ones are dropped.
    u32 maxDepth = 32;        ///< Scopes open at once (at least 1); deeper ones are not recorded.
};

/// Records named CPU scopes per frame into a fixed ring of the last frames. Recording never allocates.
/// Use from one thread: the one that runs the frame loop (summarize() included).
class FrameProfiler : NonCopyable
{
public:
    /// Source of time in seconds; monotonicSeconds() by default.
    using Clock = f64 (*)() noexcept;

    /// Opens a scope for its lifetime.
    class Scope : NonCopyable
    {
    public:
        /// Begins a scope named @p name (see FrameProfiler::beginScope()) in @p profiler.
        Scope(FrameProfiler& profiler, std::string_view name) noexcept
            : m_profiler(profiler)
        {
            m_profiler.beginScope(name);
        }
        /// Ends the scope.
        ~Scope() { m_profiler.endScope(); }

    private:
        FrameProfiler& m_profiler;
    };

    /// Creates a profiler with storage for @p desc, reading time from @p clock.
    explicit FrameProfiler(const FrameProfilerDesc& desc = {}, Clock clock = nullptr);
    ~FrameProfiler();

    /// Starts a frame. A frame that was not ended is discarded. While paused, nothing is recorded until
    /// the next beginFrame() after resume().
    void beginFrame() noexcept;
    /// Ends the frame, closing scopes left open, and makes it the newest of frames().
    void endFrame() noexcept;
    /// Opens a scope named @p name inside the innermost open one. @p name is not copied: it must outlive the
    /// recorded frames (use string literals or other static text). Outside a frame, does nothing.
    void beginScope(std::string_view name) noexcept;
    /// Closes the innermost open scope. Asserts that one is open (inside a frame).
    void endScope();

    /// Stops recording at the next beginFrame(), keeping the recorded frames.
    void pause() noexcept { m_pauseRequested = true; }
    /// Records again from the next beginFrame().
    void resume() noexcept { m_pauseRequested = false; }
    /// True while paused (or about to pause at the next beginFrame()).
    [[nodiscard]] bool paused() const noexcept { return m_pauseRequested; }
    /// Forgets the recorded frames (the frame in progress, if any, is kept).
    void clear() noexcept;

    /// Number of recorded frames (at most FrameProfilerDesc::frames).
    [[nodiscard]] usize frameCount() const noexcept { return m_count; }
    /// The recorded frame @p age frames before the newest one (0: the newest). @p age < frameCount().
    [[nodiscard]] ProfileFrame frame(usize age) const noexcept;
    /// Number of frames begun since creation (recorded or not).
    [[nodiscard]] u64 frameIndex() const noexcept { return m_nextIndex; }
    /// The storage sizes.
    [[nodiscard]] const FrameProfilerDesc& desc() const noexcept { return m_desc; }

    /// Fills @p out with the time spent per place over the newest @p frames frames (at most frameCount()),
    /// in the order the places first ran in the newest frame, then places that ran only in older frames.
    /// Reuses @p out's storage, so it allocates only when @p out must grow.
    void summarize(usize frames, std::vector<ProfileScopeStats>& out) const;

private:
    struct FrameRecord
    {
        u64 index = 0;
        f32 durationMs = 0.0f;
        u32 scopeCount = 0;
        bool truncated = false;
    };
    struct Open
    {
        u32 scope = 0; // index in the frame's scopes, or kDropped
        f64 start = 0.0;
        u64 path = 0;
    };

    struct CurrentPlace
    {
        usize entry = 0; // index in summarize()'s output
        f32 ms = 0.0f;
    };

    [[nodiscard]] f64 now() const noexcept;
    [[nodiscard]] ProfileScope* slot(usize ring) const noexcept;

    FrameProfilerDesc m_desc;
    Clock m_clock;
    std::unique_ptr<FrameRecord[]> m_frames;
    std::unique_ptr<ProfileScope[]> m_scopes;
    std::unique_ptr<Open[]> m_open;
    usize m_newest = 0; // ring index of the newest recorded frame
    usize m_count = 0;
    u64 m_nextIndex = 0;
    // The frame in progress lives in the ring slot after the newest one.
    f64 m_frameStart = 0.0;
    u32 m_scopeCount = 0;
    u32 m_depth = 0; // open scopes, including those too deep to record
    bool m_inFrame = false;
    bool m_recording = false;
    bool m_truncated = false;
    bool m_pauseRequested = false;
    mutable std::vector<CurrentPlace> m_current; // scratch of summarize()
};

} // namespace illusion
