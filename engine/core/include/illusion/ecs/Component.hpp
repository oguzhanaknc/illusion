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
#include <illusion/core/Module.hpp>
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
#include <vector>

namespace illusion
{

struct TypeReflection;
struct FieldInfo;
struct TypeLayout;
struct TypeMeta;
struct Migration;
class StructRegistry;

namespace detail
{
class ReflectionData;
} // namespace detail

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
    u32 module = 0;        ///< Module whose code the functions below are in (0: the host; see Module.hpp).

    /// Move-constructs the object at @p dst from the one at @p src, then destroys @p src.
    void (*relocate)(void* dst, void* src) noexcept = nullptr;

    /// Destroys the object at @p ptr.
    void (*destroy)(void* ptr) noexcept = nullptr;

    /// Default-constructs an object at @p ptr. Null if T is not default constructible.
    void (*construct)(void* ptr) noexcept = nullptr;

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
    info.module = detail::currentModuleId();
    info.relocate = [](void* dst, void* src) noexcept {
        T* source = std::launder(static_cast<T*>(src));
        ::new (dst) T(std::move(*source));
        std::destroy_at(source);
    };
    info.destroy = [](void* ptr) noexcept {
        std::destroy_at(std::launder(static_cast<T*>(ptr)));
    };
    if constexpr (std::is_default_constructible_v<T>)
    {
        info.construct = [](void* ptr) noexcept {
            ::new (ptr) T();
        };
    }
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
    /// returns the existing id; it is a fatal error if size or alignment differ. @p info takes the type
    /// over (its lifetime functions and module replace the old ones) if it comes from the host (module 0)
    /// and the type was registered by a module, or if the type was registered by a module that is being
    /// reloaded (retireModule()) or was unloaded (orphanModule()).
    ComponentId registerType(const ComponentInfo& info);

    /// Registers @p T. See registerType(const ComponentInfo&).
    template <ComponentType T>
    ComponentId registerType()
    {
        return registerType(makeComponentInfo<T>());
    }

    /// Returns the id registered for @p hash, or kInvalidComponentId.
    [[nodiscard]] ComponentId find(u64 hash) const;

    /// Returns the description of a registered id (asserts that @p id is registered).
    [[nodiscard]] const ComponentInfo& info(ComponentId id) const noexcept;

    /// Returns true if @p id is a registered id. Use it to validate ids from outside the program (tools,
    /// files, remote commands) before passing them to functions that assert.
    [[nodiscard]] bool contains(ComponentId id) const noexcept { return id < count(); }

    /// Returns the description of @p id, or nullptr if @p id is not registered.
    [[nodiscard]] const ComponentInfo* tryInfo(ComponentId id) const noexcept;

    /// Number of registered types.
    [[nodiscard]] u32 count() const noexcept { return m_count.load(std::memory_order_acquire); }

    /// @name Modules
    /// Used by ModuleHost for hot reload (docs/adr/0027-module-system-and-hot-reload.md). Call them only
    /// while no other thread uses component types of @p module.
    /// @{

    /// Lets the next registration of each type that @p module registered take it over. A registration
    /// with a different size or alignment does not take over and is reported by takeLayoutConflicts().
    void retireModule(u32 module);

    /// Ends retireModule() for @p module (its types keep their current owners).
    void restoreModule(u32 module);

    /// Gives @p id back the lifetime functions and module of @p info, a copy of info(id) taken earlier
    /// (rolls back a takeover; the layout must not differ).
    void restoreType(ComponentId id, const ComponentInfo& info);

    /// Moves the names of types whose registration conflicted with the layout of a retired module's type
    /// to @p out, and returns true if there were any.
    bool takeLayoutConflicts(std::vector<std::string>& out);

    /// Appends the ids of the types whose lifetime functions are in @p module to @p out.
    void typesOfModule(u32 module, std::vector<ComponentId>& out) const;

    /// Detaches the types of @p module from its code before the module is unloaded: their lifetime
    /// functions become fatal errors until a module registers them again. Remove their Components from
    /// every World first.
    void orphanModule(u32 module);

    /// @}

    /// @name Reflection
    /// See Reflection.hpp. Reflect types during startup, before other threads read their reflection.
    /// @{

    /// Starts the reflection of @p id, laid out as @p layout, under @p stableName, replacing any earlier
    /// reflection of the same type (which stays valid until the registry is destroyed). Stable names must
    /// be unique; reusing one for another type is a fatal error. A layout that differs from the registered
    /// one (a reloaded module's layout conflict, see registerType()) is reflected but not published.
    void beginReflection(ComponentId id, std::string_view stableName, u32 version, const TypeLayout& layout);

    /// Appends a field to the reflection of @p id. Strings, enumeration values and element types are
    /// copied; a struct field's type must be reflected in structs() first.
    void addReflectedField(ComponentId id, const FieldInfo& field);

    /// Sets the metadata of the reflection of @p id (strings are copied).
    void setReflectionMeta(ComponentId id, const TypeMeta& meta);

    /// Appends a migration step to the reflection of @p id.
    void addReflectionMigration(ComponentId id, const Migration& migration);

    /// Returns the reflection of @p id, or nullptr if the type is not reflected.
    [[nodiscard]] const TypeReflection* reflection(ComponentId id) const noexcept;

    /// Returns the id of the type reflected under @p stableName, or kInvalidComponentId.
    [[nodiscard]] ComponentId findByStableName(std::string_view stableName) const;

    /// The reflected structs that Components of this registry can use as fields.
    [[nodiscard]] StructRegistry& structs() noexcept { return *m_structs; }

    /// The reflected structs that Components of this registry can use as fields.
    [[nodiscard]] const StructRegistry& structs() const noexcept { return *m_structs; }

    /// @}

private:
    struct Entry
    {
        std::string name;
        ComponentInfo info;
        detail::ReflectionData* reflection = nullptr; // published; owned by Lookup::reflections
        detail::ReflectionData* building = nullptr;   // receives fields and metadata; usually the same
    };

    detail::ReflectionData& reflectionData(ComponentId id) const;

    struct Lookup;
    std::unique_ptr<Lookup> m_lookup;
    std::unique_ptr<StructRegistry> m_structs;
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
