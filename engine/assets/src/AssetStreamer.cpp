#include "AssetImport.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Thread.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

enum class Kind : u8
{
    Image,
    Model,
};

// One load moving through the stages: I/O thread (reads) -> job (verify, decode) -> completion.
struct Request
{
    std::shared_ptr<detail::AssetSlotBase> slot;
    Kind kind = Kind::Image;
    std::string path; // normalized
    std::string cachePath;
    ImageImportSettings settings;
    bool forceImport = false; // the cache entry was stale; import from source

    // Filled on the I/O thread.
    std::optional<AssetFile> cached;
    std::vector<std::vector<std::byte>> dependencies; // current contents of cached->dependencies
    std::vector<std::byte> source;
};

struct CacheWrite
{
    std::string path;
    std::vector<std::byte> bytes;
};

template <class T>
detail::AssetSlot<T>& slotOf(Request& request)
{
    return static_cast<detail::AssetSlot<T>&>(*request.slot);
}

} // namespace

struct AssetStreamer::Impl
{
    VirtualFileSystem* files = nullptr;
    JobSystem* jobs = nullptr;
    AssetStreamerDesc desc;

    // I/O thread work: loads to read and cache files to write.
    std::mutex ioMutex;
    std::condition_variable ioWake;
    std::deque<std::unique_ptr<Request>> loads;
    std::deque<CacheWrite> writes;
    bool cancelLoads = false; // shutting down: skip the remaining loads
    bool exiting = false;     // write what is queued, then stop
    bool busy = false;        // the I/O thread is handling a load (and may still submit a job)
    std::condition_variable ioIdle;

    std::thread ioThread;

    // Finished loads waiting for update().
    std::mutex doneMutex;
    std::condition_variable doneWake;
    std::vector<std::pair<std::shared_ptr<detail::AssetSlotBase>, bool>> done;

    JobCounter inFlight;
    std::unordered_map<std::string, std::weak_ptr<detail::AssetSlotBase>> requests; // dedup, by key
    u32 pendingCount = 0;
    std::atomic<u32> cacheHits{0};
    std::atomic<u32> imports{0};
    std::atomic<u32> cacheWrites{0};

    void ioLoop();
    void read(Request& request);
    void process(Request* request); // runs as a job
    void importModel(std::unique_ptr<Request> request);
    void finish(std::unique_ptr<Request> request, bool ok);
    void queueLoad(std::unique_ptr<Request> request);
    void queueWrite(const std::string& path, const AssetFile& file);
    void submit(std::unique_ptr<Request> request);
};

void AssetStreamer::Impl::queueLoad(std::unique_ptr<Request> request)
{
    {
        std::scoped_lock lock(ioMutex);
        loads.push_back(std::move(request));
    }
    ioWake.notify_one();
}

void AssetStreamer::Impl::queueWrite(const std::string& path, const AssetFile& file)
{
    if (desc.cacheMount.empty() || file.dependencies.empty())
    {
        return;
    }
    {
        std::scoped_lock lock(ioMutex);
        writes.push_back(CacheWrite{path, writeAssetFile(file)});
    }
    ioWake.notify_one();
}

void AssetStreamer::Impl::finish(std::unique_ptr<Request> request, bool ok)
{
    {
        std::scoped_lock lock(doneMutex);
        done.emplace_back(std::move(request->slot), ok);
    }
    doneWake.notify_all();
}

void AssetStreamer::Impl::submit(std::unique_ptr<Request> request)
{
    Request* raw = request.release(); // owned by the job until it finishes
    jobs->submit([this, raw] { process(raw); }, &inFlight);
}

void AssetStreamer::Impl::ioLoop()
{
    setCurrentThreadName("Asset I/O");
    while (true)
    {
        std::unique_ptr<Request> load;
        std::optional<CacheWrite> write;
        {
            std::unique_lock lock(ioMutex);
            ioWake.wait(lock, [&] { return exiting || (!loads.empty() && !cancelLoads) || !writes.empty(); });
            // Writes first: they are short and keep the cache current for the loads behind them.
            if (!writes.empty())
            {
                write = std::move(writes.front());
                writes.pop_front();
            }
            else if (exiting)
            {
                return;
            }
            else
            {
                load = std::move(loads.front());
                loads.pop_front();
                busy = true;
            }
        }
        if (write)
        {
            ILL_PROFILE_SCOPE_N("Asset cache write");
            if (const FileError result = files->writeFile(write->path, write->bytes);
                result != FileError::None)
            {
                ILL_LOG_WARN("Assets", "cannot write import cache {}: {}", write->path,
                             fileErrorName(result));
            }
            else
            {
                cacheWrites.fetch_add(1, std::memory_order_relaxed);
            }
            continue;
        }
        if (load->kind == Kind::Model && load->forceImport)
        {
            importModel(std::move(load));
        }
        else
        {
            read(*load);
            if (load->kind == Kind::Model && !load->cached)
            {
                importModel(std::move(load));
            }
            else if (load->kind == Kind::Image && !load->cached && load->source.empty())
            {
                finish(std::move(load), false);
            }
            else
            {
                submit(std::move(load));
            }
        }
        {
            std::scoped_lock lock(ioMutex);
            busy = false;
        }
        ioIdle.notify_all();
    }
}

void AssetStreamer::Impl::read(Request& request)
{
    ILL_PROFILE_SCOPE_N("Asset read");
    const AssetType type = request.kind == Kind::Image ? AssetType::Image : AssetType::Model;
    const u32 version = request.kind == Kind::Image ? kImageFormatVersion : kModelFormatVersion;
    std::vector<std::byte> bytes;
    if (!desc.cacheMount.empty() && files->readFile(request.cachePath, bytes) == FileError::None)
    {
        request.cached = detail::parseCache(bytes, type, version);
        if (request.cached)
        {
            request.dependencies.resize(request.cached->dependencies.size());
            for (usize i = 0; i < request.dependencies.size(); ++i)
            {
                if (files->readFile(request.cached->dependencies[i].path, request.dependencies[i]) !=
                    FileError::None)
                {
                    request.cached.reset(); // a source is gone: import (and report) from source
                    request.dependencies.clear();
                    break;
                }
            }
        }
    }
    if (!request.cached && request.kind == Kind::Image)
    {
        if (const FileError result = files->readFile(request.path, request.source); result != FileError::None)
        {
            request.slot->error = std::format("{}: {}", request.path, fileErrorName(result));
        }
    }
}

void AssetStreamer::Impl::importModel(std::unique_ptr<Request> request)
{
    imports.fetch_add(1, std::memory_order_relaxed);
    AssetFile cooked;
    auto& slot = slotOf<ModelData>(*request);
    slot.value = detail::importModel(*files, request->path, cooked, &slot.error);
    const bool ok = slot.value.has_value();
    if (ok && !desc.cacheMount.empty() && !cooked.dependencies.empty())
    {
        // Already on the I/O thread: write directly.
        if (files->writeFile(request->cachePath, writeAssetFile(cooked)) == FileError::None)
        {
            cacheWrites.fetch_add(1, std::memory_order_relaxed);
        }
    }
    finish(std::move(request), ok);
}

void AssetStreamer::Impl::process(Request* raw)
{
    ILL_PROFILE_SCOPE_N("Asset process");
    std::unique_ptr<Request> request(raw);
    if (request->cached && detail::dependenciesUnchanged(*request->cached, request->dependencies))
    {
        bool ok = false;
        if (request->kind == Kind::Image)
        {
            auto& slot = slotOf<Image>(*request);
            slot.value = deserializeImage(request->cached->payload);
            ok = slot.value.has_value();
        }
        else
        {
            auto& slot = slotOf<ModelData>(*request);
            slot.value = deserializeModel(request->cached->payload);
            ok = slot.value.has_value();
        }
        if (ok)
        {
            cacheHits.fetch_add(1, std::memory_order_relaxed);
            finish(std::move(request), true);
            return;
        }
    }
    if (request->kind == Kind::Model)
    {
        // Stale or broken entry: the import reads files, so it goes back to the I/O thread.
        request->forceImport = true;
        request->cached.reset();
        queueLoad(std::move(request));
        return;
    }
    // Image: the stale entry's only dependency is the image itself, so its bytes are already here.
    if (request->cached && request->source.empty() && !request->dependencies.empty())
    {
        request->source = std::move(request->dependencies.front());
    }
    if (request->source.empty())
    {
        // The cache entry was valid on disk but its payload was not: read the source on the I/O thread.
        request->cached.reset();
        request->dependencies.clear();
        queueLoad(std::move(request));
        return;
    }
    imports.fetch_add(1, std::memory_order_relaxed);
    auto& slot = slotOf<Image>(*request);
    AssetFile cooked;
    slot.value = detail::importImage(request->source, request->path, request->settings, cooked, &slot.error);
    if (slot.value)
    {
        queueWrite(request->cachePath, cooked);
    }
    const bool ok = slot.value.has_value();
    finish(std::move(request), ok);
}

AssetStreamer::AssetStreamer(VirtualFileSystem& files, JobSystem& jobs, AssetStreamerDesc desc)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->files = &files;
    m_impl->jobs = &jobs;
    m_impl->desc = std::move(desc);
    m_impl->ioThread = std::thread([impl = m_impl.get()] { impl->ioLoop(); });
}

AssetStreamer::~AssetStreamer()
{
    // Skip queued loads, let the load in hand and running jobs finish (they may queue cache writes), then
    // write those and stop.
    {
        std::unique_lock lock(m_impl->ioMutex);
        m_impl->cancelLoads = true;
        m_impl->ioIdle.wait(lock, [&] { return !m_impl->busy; });
    }
    m_impl->jobs->wait(m_impl->inFlight);
    {
        std::scoped_lock lock(m_impl->ioMutex);
        m_impl->exiting = true;
    }
    m_impl->ioWake.notify_all();
    m_impl->ioThread.join();
}

AssetHandle<Image> AssetStreamer::loadImage(std::string_view virtualPath, const ImageImportSettings& settings)
{
    Impl& impl = *m_impl;
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    const std::string importKey = detail::imageImportKey(settings);
    const std::string key = std::format("{}|{}", importKey, path.value_or(std::string(virtualPath)));
    if (auto existing = impl.requests[key].lock())
    {
        return AssetHandle<Image>(std::static_pointer_cast<detail::AssetSlot<Image>>(existing));
    }
    auto slot = std::make_shared<detail::AssetSlot<Image>>();
    slot->id = AssetId::fromPath(virtualPath);
    impl.requests[key] = slot;
    if (!path)
    {
        slot->state = AssetState::Failed;
        slot->error = std::format("{}: invalid path", virtualPath);
        return AssetHandle<Image>(std::move(slot));
    }
    auto request = std::make_unique<Request>();
    request->slot = slot;
    request->kind = Kind::Image;
    request->path = *path;
    request->cachePath = detail::assetCachePath(impl.desc.cacheMount, *path, importKey);
    request->settings = settings;
    ++impl.pendingCount;
    impl.queueLoad(std::move(request));
    return AssetHandle<Image>(std::move(slot));
}

AssetHandle<ModelData> AssetStreamer::loadModel(std::string_view virtualPath)
{
    Impl& impl = *m_impl;
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    const std::string key =
        std::format("{}|{}", detail::kModelImporter, path.value_or(std::string(virtualPath)));
    if (auto existing = impl.requests[key].lock())
    {
        return AssetHandle<ModelData>(std::static_pointer_cast<detail::AssetSlot<ModelData>>(existing));
    }
    auto slot = std::make_shared<detail::AssetSlot<ModelData>>();
    slot->id = AssetId::fromPath(virtualPath);
    impl.requests[key] = slot;
    if (!path)
    {
        slot->state = AssetState::Failed;
        slot->error = std::format("{}: invalid path", virtualPath);
        return AssetHandle<ModelData>(std::move(slot));
    }
    auto request = std::make_unique<Request>();
    request->slot = slot;
    request->kind = Kind::Model;
    request->path = *path;
    request->cachePath = detail::assetCachePath(impl.desc.cacheMount, *path, detail::kModelImporter);
    ++impl.pendingCount;
    impl.queueLoad(std::move(request));
    return AssetHandle<ModelData>(std::move(slot));
}

u32 AssetStreamer::update()
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    std::vector<std::pair<std::shared_ptr<detail::AssetSlotBase>, bool>> finished;
    {
        std::scoped_lock lock(impl.doneMutex);
        finished.swap(impl.done);
    }
    for (auto& [slot, ok] : finished)
    {
        slot->state = ok ? AssetState::Ready : AssetState::Failed;
    }
    impl.pendingCount -= static_cast<u32>(finished.size());
    // Forget requests whose handles are all gone, so their data can be freed and reloaded later.
    if (!finished.empty())
    {
        std::erase_if(impl.requests, [](const auto& entry) { return entry.second.expired(); });
    }
    return static_cast<u32>(finished.size());
}

u32 AssetStreamer::pending() const noexcept
{
    return m_impl->pendingCount;
}

void AssetStreamer::waitIdle()
{
    Impl& impl = *m_impl;
    while (impl.pendingCount > 0)
    {
        if (update() == 0)
        {
            std::unique_lock lock(impl.doneMutex);
            impl.doneWake.wait(lock, [&] { return !impl.done.empty(); });
        }
    }
}

AssetDatabaseStatistics AssetStreamer::statistics() const noexcept
{
    AssetDatabaseStatistics statistics;
    statistics.cacheHits = m_impl->cacheHits.load(std::memory_order_relaxed);
    statistics.imports = m_impl->imports.load(std::memory_order_relaxed);
    statistics.cacheWrites = m_impl->cacheWrites.load(std::memory_order_relaxed);
    return statistics;
}

} // namespace illusion
