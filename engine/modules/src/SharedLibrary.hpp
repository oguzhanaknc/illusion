#pragma once

// Opens shared libraries and finds their symbols (internal to illusion::modules).

#include <illusion/core/Base.hpp>

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace illusion::detail
{

class SharedLibrary : NonCopyable
{
public:
    // Opens @p path, resolving every symbol now. Returns null and sets @p error on failure.
    static std::unique_ptr<SharedLibrary> open(const std::filesystem::path& path, std::string& error);

    ~SharedLibrary();

    // The function exported as @p name, or null.
    template <class Function>
    [[nodiscard]] Function function(const char* name) const noexcept
    {
        void* address = symbol(name);
        Function result = nullptr;
        static_assert(sizeof(result) == sizeof(address), "function pointers must fit in a data pointer");
        std::memcpy(&result, &address, sizeof(result));
        return result;
    }

private:
    explicit SharedLibrary(void* handle) noexcept
        : m_handle(handle)
    {
    }

    [[nodiscard]] void* symbol(const char* name) const noexcept;

    void* m_handle;
};

} // namespace illusion::detail
