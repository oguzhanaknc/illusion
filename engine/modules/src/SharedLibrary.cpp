#include "SharedLibrary.hpp"

#if defined(ILL_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace illusion::detail
{

#if defined(ILL_PLATFORM_WINDOWS)

namespace
{

std::string lastErrorText()
{
    const DWORD code = GetLastError();
    char* text = nullptr;
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<char*>(&text), 0, nullptr);
    std::string result = length > 0 ? std::string(text, length) : "error " + std::to_string(code);
    LocalFree(text);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
    {
        result.pop_back();
    }
    return result;
}

} // namespace

std::unique_ptr<SharedLibrary> SharedLibrary::open(const std::filesystem::path& path, std::string& error)
{
    HMODULE handle = LoadLibraryW(path.c_str());
    if (handle == nullptr)
    {
        error = lastErrorText();
        return nullptr;
    }
    return std::unique_ptr<SharedLibrary>(new SharedLibrary(handle));
}

SharedLibrary::~SharedLibrary()
{
    FreeLibrary(static_cast<HMODULE>(m_handle));
}

void* SharedLibrary::symbol(const char* name) const noexcept
{
    const FARPROC procedure = GetProcAddress(static_cast<HMODULE>(m_handle), name);
    void* address = nullptr;
    static_assert(sizeof(procedure) == sizeof(address), "function pointers must fit in a data pointer");
    std::memcpy(&address, &procedure, sizeof(address));
    return address;
}

#else

std::unique_ptr<SharedLibrary> SharedLibrary::open(const std::filesystem::path& path, std::string& error)
{
    // Local: the module's symbols (its copy of the engine) never resolve another binary's references.
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        const char* message = dlerror();
        error = message != nullptr ? message : "dlopen failed";
        return nullptr;
    }
    return std::unique_ptr<SharedLibrary>(new SharedLibrary(handle));
}

SharedLibrary::~SharedLibrary()
{
    dlclose(m_handle);
}

void* SharedLibrary::symbol(const char* name) const noexcept
{
    return dlsym(m_handle, name);
}

#endif

} // namespace illusion::detail
