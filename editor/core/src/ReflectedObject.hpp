#pragma once

// A default-constructed object of a reflected type, owned on the heap: command params and results, settings
// values and other structs that the editor reads and writes through reflection.

#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <new>

namespace illusion::editor::detail
{

class ReflectedObject : NonCopyable
{
public:
    // A default value of @p type, which has fields (tags have no value to hold).
    explicit ReflectedObject(const TypeReflection* type)
        : m_type(type)
    {
        ILL_ASSERT(m_type != nullptr && m_type->size > 0, "ReflectedObject of a type without a value");
        m_data = ::operator new(m_type->size, std::align_val_t(m_type->alignment));
        m_type->ops->construct(m_data);
    }

    ~ReflectedObject()
    {
        m_type->ops->destroy(m_data);
        ::operator delete(m_data, std::align_val_t(m_type->alignment));
    }

    [[nodiscard]] void* data() noexcept { return m_data; }
    [[nodiscard]] const void* data() const noexcept { return m_data; }

private:
    const TypeReflection* m_type;
    void* m_data = nullptr;
};

} // namespace illusion::editor::detail
