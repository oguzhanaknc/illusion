#include "AssetImport.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Thread.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <format>
#include <functional>
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
    File,   // read the file and run a decoder from another module (AssetStreamer::loadFile())
    Cooked, // look up a cache entry the caller makes itself (AssetStreamer::loadCooked())
};

// One load moving through the stages: I/O thread (reads) -> job (verify, decode) -> completion.
struct Request
{
    std::shared_ptr<detail::AssetSlotBase> slot;
    Kind kind = Kind::Image;
    std::string path; // normalized
    std::string cachePath;
    ImageImportSettings settings;
    AssetType cookedType = AssetType::Image;                                      // Kind::Cooked
    u32 cookedVersion = 0;                                                        // Kind::Cooked
    std::function<bool(std::vector<std::byte>&&, detail::AssetSlotBase&)> decode; // Kind::File
    bool forceImport = false; // the cache entry was stale; import from source
    bool skipCache = false;   // the cache entry could not be used; read the source instead

    // Filled on the I/O thread.
    std::optional<AssetFile> cached;
    std::vector<std::vector<std::byte>> dependencies; // current contents of cached->dependencies
    std::vector<std::byte> source;
    std::vector<std::byte> sidecar; // Kind::Image, Kind::Cooked: its import settings sidecar (empty if none)
    bool sourceRead = false;        // Kind::Cooked: `source` and `sidecar` hold the current files
};

struct CacheWrite
{
    std::string path;
    std::vector<std::byte> bytes;
    bool remove = false; // delete the file instead (AssetReloadMode::Reimport)
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
    std::unordered_map<std::string, std::weak_ptr<detail::AssetSlotBase>> requests; // dedup, by key|path
    EventBus events;
    u32 pendingCount = 0;
    std::atomic<u32> cacheHits{0};
    std::atomic<u32> imports{0};
    std::atomic<u32> cacheWrites{0};

    void ioLoop();
    void read(Request& request);
    void process(Request* request); // runs as a job
    void importModel(std::unique_ptr<Request> request);
    void processCooked(std::unique_ptr<Request> request);
    void finish(std::unique_ptr<Request> request, bool ok);
    void queueLoad(std::unique_ptr<Request> request);
    void queueWrite(const std::string& path, const AssetFile& file);
    void queueRemove(std::string path);
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

void AssetStreamer::Impl::queueRemove(std::string path)
{
    {
        std::scoped_lock lock(ioMutex);
        writes.push_back(CacheWrite{std::move(path), {}, true});
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
        if (write && write->remove)
        {
            if (const FileError result = files->remove(write->path);
                result != FileError::None && result != FileError::NotFound)
            {
                ILL_LOG_WARN("Assets", "cannot delete import cache {}: {}", write->path,
                             fileErrorName(result));
            }
            continue;
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
            else if ((load->kind == Kind::Image || load->kind == Kind::File) && !load->cached &&
                     load->source.empty())
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
    if (request.kind == Kind::File)
    {
        if (const FileError result = files->readFile(request.path, request.source); result != FileError::None)
        {
            request.slot->error = std::format("{}: {}", request.path, fileErrorName(result));
        }
        else if (request.source.empty())
        {
            request.slot->error = std::format("{}: the file is empty", request.path);
        }
        return;
    }
    AssetType type = request.kind == Kind::Image ? AssetType::Image : AssetType::Model;
    u32 version = request.kind == Kind::Image ? kImageFormatVersion : kModelFormatVersion;
    if (request.kind == Kind::Cooked)
    {
        type = request.cookedType;
        version = request.cookedVersion;
    }
    std::vector<std::byte> bytes;
    if (!request.skipCache && !desc.cacheMount.empty() &&
        files->readFile(request.cachePath, bytes) == FileError::None)
    {
        request.cached = detail::parseCache(bytes, type, version);
        if (request.cached && desc.verifySources)
        {
            request.dependencies.resize(request.cached->dependencies.size());
            for (usize i = 0; i < request.dependencies.size(); ++i)
            {
                if (!detail::readDependency(*files, request.cached->dependencies[i].path,
                                            request.dependencies[i]))
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
        else if (!detail::readImportSettingsFile(*files, request.path, request.sidecar, &request.slot->error))
        {
            request.source.clear(); // fails the load with the sidecar's error
        }
    }
    if (!request.cached && request.kind == Kind::Cooked)
    {
        // A miss is not an error: without readable sources the caller's own load reports why.
        std::string ignored;
        request.sourceRead = files->readFile(request.path, request.source) == FileError::None &&
                             detail::readImportSettingsFile(*files, request.path, request.sidecar, &ignored);
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
    if (request->kind == Kind::Cooked)
    {
        processCooked(std::move(request));
        return;
    }
    if (request->kind == Kind::File)
    {
        const bool ok = request->decode(std::move(request->source), *request->slot);
        finish(std::move(request), ok);
        return;
    }
    if (request->cached &&
        (!desc.verifySources || detail::dependenciesUnchanged(*request->cached, request->dependencies)))
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
    // Image: the stale entry's dependencies are the image and its sidecar, so their bytes are already here.
    if (request->cached && request->source.empty())
    {
        const std::string sidecarPath = importSettingsPath(request->path);
        bool haveSidecar = false;
        for (usize i = 0; i < request->dependencies.size(); ++i)
        {
            const std::string& path = request->cached->dependencies[i].path;
            if (path == request->path)
            {
                request->source = std::move(request->dependencies[i]);
            }
            else if (path == sidecarPath)
            {
                request->sidecar = std::move(request->dependencies[i]);
                haveSidecar = true;
            }
        }
        if (!haveSidecar)
        {
            request->source.clear();
        }
    }
    if (request->source.empty())
    {
        // The cache entry was valid on disk but its payload was not: read the source on the I/O thread.
        request->cached.reset();
        request->dependencies.clear();
        request->skipCache = true;
        queueLoad(std::move(request));
        return;
    }
    imports.fetch_add(1, std::memory_order_relaxed);
    auto& slot = slotOf<Image>(*request);
    AssetFile cooked;
    slot.value = detail::importImage(request->source, request->sidecar, request->path, request->settings,
                                     cooked, &slot.error);
    if (slot.value)
    {
        queueWrite(request->cachePath, cooked);
    }
    const bool ok = slot.value.has_value();
    finish(std::move(request), ok);
}

void AssetStreamer::Impl::processCooked(std::unique_ptr<Request> request)
{
    auto& slot = slotOf<CookedAsset>(*request);
    if (request->cached &&
        (!desc.verifySources || detail::dependenciesUnchanged(*request->cached, request->dependencies)))
    {
        slot.value.emplace().file = std::move(request->cached);
        cacheHits.fetch_add(1, std::memory_order_relaxed);
        finish(std::move(request), true);
        return;
    }
    if (request->cached)
    {
        // Stale: the entry's dependencies usually are the file and its sidecar, already read for the check.
        const std::string sidecarPath = importSettingsPath(request->path);
        bool haveSource = false;
        bool haveSidecar = false;
        for (usize i = 0; i < request->dependencies.size(); ++i)
        {
            const std::string& path = request->cached->dependencies[i].path;
            if (path == request->path)
            {
                request->source = std::move(request->dependencies[i]);
                haveSource = true;
            }
            else if (path == sidecarPath)
            {
                request->sidecar = std::move(request->dependencies[i]);
                haveSidecar = true;
            }
        }
        if (!haveSource || !haveSidecar)
        {
            // Other dependencies: read the file and its sidecar on the I/O thread.
            request->cached.reset();
            request->dependencies.clear();
            request->source.clear();
            request->sidecar.clear();
            request->skipCache = true;
            queueLoad(std::move(request));
            return;
        }
        request->sourceRead = true;
    }
    CookedAsset& result = slot.value.emplace();
    if (request->sourceRead)
    {
        result.sources = {AssetDependency{request->path, xxhash64(request->source)},
                          AssetDependency{importSettingsPath(request->path), xxhash64(request->sidecar)}};
    }
    finish(std::move(request), true);
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

AssetHandle<CookedAsset> AssetStreamer::loadCooked(std::string_view virtualPath, std::string_view importKey,
                                                   AssetType type, u32 version, AssetReloadMode mode)
{
    ILL_ASSERT(importKey.find('|') == std::string_view::npos,
               "loadCooked: the import key must not contain '|'");
    Impl& impl = *m_impl;
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    const bool reimport = mode == AssetReloadMode::Reimport;
    const std::string key = std::format("{}|{}", importKey, path.value_or(std::string(virtualPath)));
    if (!reimport)
    {
        if (auto existing = impl.requests[key].lock())
        {
            return AssetHandle<CookedAsset>(
                std::static_pointer_cast<detail::AssetSlot<CookedAsset>>(existing));
        }
    }
    auto slot = std::make_shared<detail::AssetSlot<CookedAsset>>();
    slot->id = AssetId::fromPath(virtualPath);
    if (!reimport)
    {
        impl.requests[key] = slot;
    }
    if (!path)
    {
        slot->state = AssetState::Failed;
        slot->error = std::format("{}: invalid path", virtualPath);
        return AssetHandle<CookedAsset>(std::move(slot));
    }
    if (impl.desc.cacheMount.empty())
    {
        slot->value.emplace(); // no cache: always a miss, and nothing to store
        slot->state = AssetState::Ready;
        return AssetHandle<CookedAsset>(std::move(slot));
    }
    auto request = std::make_unique<Request>();
    request->slot = slot;
    request->kind = Kind::Cooked;
    request->path = *path;
    request->cachePath = detail::assetCachePath(impl.desc.cacheMount, *path, importKey);
    request->cookedType = type;
    request->cookedVersion = version;
    request->skipCache = reimport;
    ++impl.pendingCount;
    impl.queueLoad(std::move(request));
    return AssetHandle<CookedAsset>(std::move(slot));
}

void AssetStreamer::storeCooked(std::string_view virtualPath, std::string_view importKey,
                                const AssetFile& file)
{
    ILL_ASSERT(importKey.find('|') == std::string_view::npos,
               "storeCooked: the import key must not contain '|'");
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    if (!path)
    {
        return;
    }
    m_impl->queueWrite(detail::assetCachePath(m_impl->desc.cacheMount, *path, importKey), file);
}

std::shared_ptr<detail::AssetSlotBase> AssetStreamer::requestFile(std::string_view virtualPath,
                                                                  std::string_view decoderName,
                                                                  std::shared_ptr<detail::AssetSlotBase> slot,
                                                                  FileDecoder decode)
{
    Impl& impl = *m_impl;
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    const std::string key = std::format("{}|{}", decoderName, path.value_or(std::string(virtualPath)));
    if (auto existing = impl.requests[key].lock())
    {
        return existing;
    }
    slot->id = AssetId::fromPath(virtualPath);
    impl.requests[key] = slot;
    if (!path)
    {
        slot->state = AssetState::Failed;
        slot->error = std::format("{}: invalid path", virtualPath);
        return slot;
    }
    auto request = std::make_unique<Request>();
    request->slot = slot;
    request->kind = Kind::File;
    request->path = *path;
    request->decode = std::move(decode);
    ++impl.pendingCount;
    impl.queueLoad(std::move(request));
    return slot;
}

u32 AssetStreamer::invalidate(std::string_view virtualPath)
{
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    if (!path)
    {
        return 0;
    }
    // Keys are "<importer or decoder>|<path>"; importer names never contain '|'.
    const auto forgotten = std::erase_if(m_impl->requests, [&](const auto& entry) {
        const std::string& key = entry.first;
        return key.size() > path->size() && key.ends_with(*path) && key[key.size() - path->size() - 1] == '|';
    });
    return static_cast<u32>(forgotten);
}

bool AssetStreamer::reload(std::string_view virtualPath, AssetReloadMode mode)
{
    ILL_PROFILE_SCOPE();
    std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    if (!path)
    {
        return false;
    }
    if (mode == AssetReloadMode::Reimport && !m_impl->desc.cacheMount.empty())
    {
        // Keys are "<importer>|<path>"; the importer part (with its settings) is the cache file's import key.
        for (const auto& [key, slot] : m_impl->requests)
        {
            const bool imported =
                key.starts_with(detail::kImageImporter) || key.starts_with(detail::kModelImporter);
            if (imported && key.size() > path->size() && key.ends_with(*path) &&
                key[key.size() - path->size() - 1] == '|')
            {
                const std::string_view importKey =
                    std::string_view(key).substr(0, key.size() - path->size() - 1);
                m_impl->queueRemove(detail::assetCachePath(m_impl->desc.cacheMount, *path, importKey));
            }
        }
    }
    invalidate(*path);
    const AssetId asset = AssetId::fromPath(*path);
    m_impl->events.publish(AssetReloadedEvent{std::move(*path), asset, mode});
    return true;
}

EventBus& AssetStreamer::events() noexcept
{
    return m_impl->events;
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
