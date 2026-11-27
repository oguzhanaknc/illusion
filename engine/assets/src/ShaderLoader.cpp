#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/ShaderLoader.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace illusion
{

struct ShaderLoader::Impl
{
    struct Entry
    {
        u32 pending = 0; // queued or running builds
        u64 completed = 0;
        std::shared_ptr<const ShaderBuild> build;
    };

    Impl(VirtualFileSystem& virtualFiles, ShaderLoaderDesc loaderDesc)
        : files(&virtualFiles)
        , desc(std::move(loaderDesc))
        , database(virtualFiles, desc.cacheMount, desc.verifySources)
        , worker([this] { run(); })
    {
    }

    ~Impl()
    {
        {
            const std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        worker.join();
    }

    void run()
    {
        std::unique_lock lock(mutex);
        while (true)
        {
            wake.wait(lock, [this] { return stopping || !queue.empty(); });
            if (stopping)
            {
                return;
            }
            std::string path = std::move(queue.front());
            queue.pop_front();
            queued.erase(path);
            lock.unlock();
            std::shared_ptr<const ShaderBuild> build = buildShader(path);
            lock.lock();
            Entry& entry = entries[path];
            entry.build = std::move(build);
            ++entry.completed;
            --entry.pending;
            idle.notify_all();
        }
    }

    std::shared_ptr<const ShaderBuild> buildShader(const std::string& path)
    {
        ILL_PROFILE_SCOPE();
        auto build = std::make_shared<ShaderBuild>();
        build->path = path;
        std::vector<std::byte> source;
        if (const FileError error = files->readFile(path, source); error != FileError::None)
        {
            build->error = std::format("{}: {}", path, fileErrorName(error));
            ILL_LOG_ERROR("shader", "{}", build->error);
            return build;
        }
        build->sourceHash = xxhash64(source);
        if (desc.compiler && !compiler && compilerError.empty())
        {
            Expected<ShaderCompiler, std::string> created = ShaderCompiler::create(*desc.compiler);
            if (created)
            {
                compiler.emplace(std::move(*created));
            }
            else
            {
                compilerError = std::move(created.error());
            }
        }
        if (!compilerError.empty())
        {
            build->error = std::format("{}: the shader compiler is not available: {}", path, compilerError);
            ILL_LOG_ERROR("shader", "{}", build->error);
            return build;
        }
        build->shader =
            database.loadShader(path, compiler ? &*compiler : nullptr, &build->diagnostics, &build->error);
        for (const ShaderDiagnostic& diagnostic : build->diagnostics)
        {
            if (diagnostic.severity == ShaderDiagnosticSeverity::Error)
            {
                ILL_LOG_ERROR("shader", "{}", formatShaderDiagnostic(diagnostic));
            }
            else if (diagnostic.severity == ShaderDiagnosticSeverity::Warning)
            {
                ILL_LOG_WARN("shader", "{}", formatShaderDiagnostic(diagnostic));
            }
        }
        if (!build->shader && build->diagnostics.empty())
        {
            ILL_LOG_ERROR("shader", "{}", build->error);
        }
        return build;
    }

    VirtualFileSystem* files;
    ShaderLoaderDesc desc;
    AssetDatabase database;                 // used by the worker only
    std::optional<ShaderCompiler> compiler; // made by the worker
    std::string compilerError;              // why it could not be made; worker only
    mutable std::mutex mutex;
    std::condition_variable wake;
    mutable std::condition_variable idle;
    std::deque<std::string> queue;
    std::unordered_set<std::string> queued; // paths in `queue`
    std::unordered_map<std::string, Entry> entries;
    bool stopping = false;
    std::thread worker; // last: starts when the rest exists
};

ShaderLoader::ShaderLoader(VirtualFileSystem& files, ShaderLoaderDesc desc)
    : m_impl(std::make_unique<Impl>(files, std::move(desc)))
{
}

ShaderLoader::~ShaderLoader() = default;

void ShaderLoader::request(std::string_view virtualPath)
{
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    const std::string key = path ? *path : std::string(virtualPath);
    {
        const std::lock_guard lock(m_impl->mutex);
        if (m_impl->queued.contains(key))
        {
            return;
        }
        m_impl->queued.insert(key);
        m_impl->queue.push_back(key);
        ++m_impl->entries[key].pending;
    }
    m_impl->wake.notify_one();
}

ShaderLoader::State ShaderLoader::state(std::string_view virtualPath) const
{
    const std::optional<std::string> path = VirtualFileSystem::normalizePath(virtualPath);
    const std::lock_guard lock(m_impl->mutex);
    const auto found = m_impl->entries.find(path ? *path : std::string(virtualPath));
    if (found == m_impl->entries.end())
    {
        return {};
    }
    return State{found->second.pending > 0, found->second.completed, found->second.build};
}

void ShaderLoader::waitIdle() const
{
    std::unique_lock lock(m_impl->mutex);
    m_impl->idle.wait(lock, [this] {
        return std::none_of(m_impl->entries.begin(), m_impl->entries.end(),
                            [](const auto& entry) { return entry.second.pending > 0; });
    });
}

u32 ShaderLoader::pending() const
{
    const std::lock_guard lock(m_impl->mutex);
    u32 count = 0;
    for (const auto& entry : m_impl->entries)
    {
        count += entry.second.pending;
    }
    return count;
}

bool ShaderLoader::canCompile() const noexcept
{
    return m_impl->desc.compiler.has_value();
}

} // namespace illusion
