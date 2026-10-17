#pragma once

/// @file
/// Asynchronous asset loading: file reads on a dedicated I/O thread, cache verification, decoding and
/// importing on the Job System, results published on the calling thread by AssetStreamer::update().

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/AssetFormat.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace illusion
{

class JobSystem;
class VirtualFileSystem;

/// Loading state of an asset handle.
enum class AssetState : u8
{
    Loading, ///< Requested; not finished (or finished but not yet published by update()).
    Ready,   ///< Loaded; AssetHandle::get() returns the data.
    Failed,  ///< Loading failed; AssetHandle::error() says why.
};

namespace detail
{

struct AssetSlotBase
{
    virtual ~AssetSlotBase() = default;
    AssetId id;
    AssetState state = AssetState::Loading; // written only on the thread that calls update()
    std::string error;                      // written by the loader before the slot is published
};

template <class T>
struct AssetSlot : AssetSlotBase
{
    std::optional<T> value; // written by the loader before the slot is published
};

} // namespace detail

/// Shared reference to an asynchronously loaded asset. Copies refer to the same asset; the data is freed
/// when the last handle is gone. Read it only on the thread that calls AssetStreamer::update().
template <class T>
class AssetHandle
{
public:
    /// An empty handle (state Failed, no data).
    AssetHandle() = default;

    /// Wraps a loader slot (used by AssetStreamer).
    explicit AssetHandle(std::shared_ptr<detail::AssetSlot<T>> slot) noexcept
        : m_slot(std::move(slot))
    {
    }

    /// Current state, as of the last AssetStreamer::update().
    [[nodiscard]] AssetState state() const noexcept { return m_slot ? m_slot->state : AssetState::Failed; }

    /// True once the asset is loaded.
    [[nodiscard]] bool ready() const noexcept { return state() == AssetState::Ready; }

    /// The loaded asset, or nullptr unless ready().
    [[nodiscard]] const T* get() const noexcept { return ready() ? &*m_slot->value : nullptr; }

    /// The loaded asset. The handle must be ready() (asserted); use get() when that is not known.
    [[nodiscard]] const T& value() const
    {
        ILL_ASSERT(ready(), "AssetHandle::value() on an asset that is not loaded");
        return *m_slot->value;
    }

    /// Why loading failed (empty otherwise).
    [[nodiscard]] const std::string& error() const noexcept
    {
        static const std::string kNone;
        return m_slot && m_slot->state == AssetState::Failed ? m_slot->error : kNone;
    }

    /// Id of the asset (empty for an empty handle).
    [[nodiscard]] AssetId id() const noexcept { return m_slot ? m_slot->id : AssetId{}; }

    /// True if the handle refers to an asset.
    [[nodiscard]] explicit operator bool() const noexcept { return m_slot != nullptr; }

    /// Handles are equal when they refer to the same request.
    friend bool operator==(const AssetHandle& a, const AssetHandle& b) noexcept
    {
        return a.m_slot == b.m_slot;
    }

private:
    std::shared_ptr<detail::AssetSlot<T>> m_slot;
};

/// Creation parameters of an AssetStreamer.
struct AssetStreamerDesc
{
    std::string cacheMount = "cache"; ///< Mount for the import cache; empty disables caching.
    /// Check cached files against their sources (development). False trusts the cache, for shipped games
    /// that mount a cooked archive without sources.
    bool verifySources = true;
};

/// Loads images and models in the background through the import cache (see AssetDatabase).
///
/// File reads and cache writes run on one dedicated I/O thread, so Jobs never block on files. Cache
/// verification, deserialization and image imports run on the Job System. glTF imports (only when a
/// model is not cached yet) run on the I/O thread because they read their buffers and images while
/// parsing. Request and update from one thread (typically the main thread).
class AssetStreamer : NonCopyable
{
public:
    /// Starts the I/O thread. @p files and @p jobs must outlive the streamer.
    AssetStreamer(VirtualFileSystem& files, JobSystem& jobs, AssetStreamerDesc desc = {});

    /// Stops the I/O thread and waits for running jobs; unfinished loads stay in the Loading state.
    ~AssetStreamer();

    /// Requests the image at @p virtualPath. Requests for the same path and settings share a handle.
    [[nodiscard]] AssetHandle<Image> loadImage(std::string_view virtualPath,
                                               const ImageImportSettings& settings = {});

    /// Requests the glTF model at @p virtualPath. Requests for the same path share a handle.
    [[nodiscard]] AssetHandle<ModelData> loadModel(std::string_view virtualPath);

    /// Publishes finished loads (their handles become Ready or Failed). Call once per frame. Returns the
    /// number of loads published.
    u32 update();

    /// Requests not yet published by update().
    [[nodiscard]] u32 pending() const noexcept;

    /// Calls update() until every request is published (for loading screens, tools and tests).
    void waitIdle();

    /// Cache hits, imports and cache writes so far.
    [[nodiscard]] AssetDatabaseStatistics statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
