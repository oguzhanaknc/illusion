#pragma once

/// @file
/// Component type registration.
///
/// Every component type gets a small dense ComponentId from the process-wide ComponentRegistry the
/// first time it is used. Types are identified by the hash of their fully qualified name, so the same
/// type gets the same id in the host executable and in hot-reloaded modules that share the host's
/// registry (see ComponentRegistry::setInstance() and docs/adr/0003-type-identity-across-modules.md).
///
/// Component types must be unique by fully qualified name: do not declare components in anonymous
/// namespaces or locally inside functions.

#include <illusion/core/Base.hpp>
#include <illusion/core/TypeName.hpp>

#include <array>
#include <atomic>
#include <bitset>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace illusion
{

/// Dense identifier of a component type within a ComponentRegistry.
using ComponentId = u16;

/// Maximum number of distinct component types per process.
inline constexpr u32 kMaxComponentTypes = 256;

/// Value returned for unknown component types.
inline constexpr ComponentId kInvalidComponentId = 0xFFFF;

/// Set of component types, indexed by ComponentId.
using ComponentMask = std::bitset<kMaxComponentTypes>;

/// Requirements for a component type: a non-const, non-reference object type that can be moved and
/// destroyed without throwing. Empty types are tags: they are tracked in the archetype signature but
/// have no storage.
template <class T>
concept ComponentType =
    std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> && !std::is_array_v<T> &&
    std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>;

/// Type-erased description of a component type.
struct ComponentInfo
{
    std::string_view name; ///< Fully qualified type name (storage owned by the registry).
    u64 hash = 0;          ///< fnv1a64(name). Identifies the type across modules.
    u32 size = 0;          ///< sizeof(T), or 0 for tags.
    u32 alignment = 1;     ///< alignof(T).
    bool trivial = false;  ///< True if T can be relocated with memcpy and needs no destructor.

    /// Move-constructs the object at @p dst from the one at @p src, then destroys @p src.
    void (*relocate)(void* dst, void* src) noexcept = nullptr;

    /// Destroys the object at @p ptr.
    void (*destroy)(void* ptr) noexcept = nullptr;

    /// Returns true for empty (tag) types without storage.
    [[nodiscard]] bool isTag() const noexcept { return size == 0; }
};

/// Builds the ComponentInfo for @p T. The name view points at static storage of the calling module.
template <ComponentType T>
[[nodiscard]] ComponentInfo makeComponentInfo() noexcept
{
    ComponentInfo info;
    info.name = typeName<T>();
    info.hash = typeHash<T>();
    info.size = std::is_empty_v<T> ? 0u : static_cast<u32>(sizeof(T));
    info.alignment = static_cast<u32>(alignof(T));
    info.trivial = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;
    info.relocate = [](void* dst, void* src) noexcept {
        T* source = std::launder(static_cast<T*>(src));
        ::new (dst) T(std::move(*source));
        std::destroy_at(source);
    };
    info.destroy = [](void* ptr) noexcept {
        std::destroy_at(std::launder(static_cast<T*>(ptr)));
    };
    return info;
}

/// Process-wide table of component types. Registration is thread-safe; lookups of registered ids are
/// lock-free.
class ComponentRegistry : NonCopyable
{
public:
    ComponentRegistry();
    ~ComponentRegistry();

    /// Returns the active registry (a built-in default unless setInstance() was called).
    [[nodiscard]] static ComponentRegistry& instance() noexcept;

    /// Makes @p registry the active instance (nullptr restores the default). Must be called before any
    /// component is used in the calling module, because ids are cached per module.
    static void setInstance(ComponentRegistry* registry) noexcept;

    /// Registers a type described by @p info and returns its id. Registering a type again (same hash)
    /// returns the existing id; it is a fatal error if size or alignment differ.
    ComponentId registerType(const ComponentInfo& info);

    /// Registers @p T. See registerType(const ComponentInfo&).
    template <ComponentType T>
    ComponentId registerType()
    {
        return registerType(makeComponentInfo<T>());
    }

    /// Returns the id registered for @p hash, or kInvalidComponentId.
    [[nodiscard]] ComponentId find(u64 hash) const;

    /// Returns the description of a registered id.
    [[nodiscard]] const ComponentInfo& info(ComponentId id) const noexcept;

    /// Number of registered types.
    [[nodiscard]] u32 count() const noexcept { return m_count.load(std::memory_order_acquire); }

private:
    struct Entry
    {
        std::string name;
        ComponentInfo info;
    };

    struct Lookup;
    std::unique_ptr<Lookup> m_lookup;
    std::unique_ptr<std::array<Entry, kMaxComponentTypes>> m_entries;
    std::atomic<u32> m_count{0};
};

/// Returns the id of @p T in the active registry, registering it on first use. The result is cached
/// per module, so this is a single guarded static load after the first call.
template <ComponentType T>
[[nodiscard]] ComponentId componentId()
{
    static const ComponentId id = ComponentRegistry::instance().registerType<T>();
    return id;
}

} // namespace illusion
