#pragma once

/// @file
/// World: owns entities and their components, organised in archetypes.
///
/// ```cpp
/// World world;
/// Entity e = world.create(Position{0, 0, 0}, Velocity{1, 0, 0});
/// world.query<Position, const Velocity>().each([](Position& p, const Velocity& v) { p.x += v.x; });
/// ```
///
/// Structural changes (create, destroy, adding or removing components) are not allowed while a query
/// is iterating; record them in a CommandBuffer and apply it afterwards. Component constructors and
/// assignments invoked by the World must not throw: an exception terminates the process.

#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/ecs/Archetype.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Entity.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace illusion
{

class CommandBuffer;
template <class... Ts>
class Query;

/// One chunk of entities passed to World::forEachMatching(): the type-erased counterpart of ChunkView. Valid
/// only during the callback.
class RawChunk
{
public:
    /// Creates a view of chunk @p chunk of @p archetype; used by World.
    RawChunk(Archetype& archetype, u32 chunk) noexcept
        : m_archetype(&archetype)
        , m_chunk(chunk)
        , m_size(archetype.chunkSize(chunk))
    {
    }

    /// Number of entities in the chunk.
    [[nodiscard]] u32 size() const noexcept { return m_size; }

    /// Entity handles of the chunk.
    [[nodiscard]] std::span<const Entity> entities() const noexcept
    {
        return {m_archetype->entities(m_chunk), m_size};
    }

    /// Component types of the chunk's entities (sorted ids, tags included).
    [[nodiscard]] std::span<const ComponentId> signature() const noexcept { return m_archetype->signature(); }

    /// Returns true if the chunk's entities have component @p id (tags included).
    [[nodiscard]] bool has(ComponentId id) const noexcept { return m_archetype->has(id); }

    /// Start of the array of component @p id (size() elements of ComponentInfo::size bytes), or nullptr if
    /// the entities do not have it or it is a tag.
    [[nodiscard]] void* column(ComponentId id) noexcept
    {
        const u16 column = m_archetype->columnIndex(id);
        return column == Archetype::kNoColumn ? nullptr : m_archetype->columnData(m_chunk, column);
    }

    /// @copydoc column
    [[nodiscard]] const void* column(ComponentId id) const noexcept
    {
        return const_cast<RawChunk*>(this)->column(id);
    }

private:
    Archetype* m_archetype;
    u32 m_chunk;
    u32 m_size;
};

/// Container of entities and components. Not thread-safe for structural changes; queries may run
/// jobs in parallel (see Query::parallelEach()).
class World : NonCopyable
{
public:
    /// Creates an empty world that uses ComponentRegistry::instance().
    World();

    /// Destroys every entity and component.
    ~World();

    /// Creates an entity without components.
    Entity create();

    /// Creates an entity with the given components (each type at most once).
    template <class... Ts>
        requires(sizeof...(Ts) > 0 && (ComponentType<std::decay_t<Ts>> && ...))
    Entity create(Ts&&... components) noexcept
    {
        assertNotIterating();
        // Look the archetype up by its sorted signature directly; walking add-edges would create an
        // intermediate archetype per component.
        std::array<ComponentId, sizeof...(Ts)> signature{componentId<std::decay_t<Ts>>()...};
        std::sort(signature.begin(), signature.end());
        ILL_ASSERT(std::adjacent_find(signature.begin(), signature.end()) == signature.end(),
                   "duplicate component type in create()");
        Archetype* archetype = findOrCreateArchetype(signature);
        const Entity entity = allocateEntity();
        const u32 row = archetype->allocateRow(entity);
        EntitySlot& slot = m_slots[entity.index];
        slot.archetype = archetype;
        slot.row = row;
        (constructAt<std::decay_t<Ts>>(*archetype, row, std::forward<Ts>(components)), ...);
        for (const ComponentId id : signature)
        {
            ++m_componentVersions[id];
        }
        ++m_structuralVersion;
        return entity;
    }

    /// Destroys @p entity and its components. The handle (and copies of it) become stale.
    void destroy(Entity entity);

    /// Returns true if @p entity refers to a live entity of this world.
    [[nodiscard]] bool isAlive(Entity entity) const noexcept
    {
        return entity.index < m_slots.size() && m_slots[entity.index].generation == entity.generation &&
               m_slots[entity.index].archetype != nullptr;
    }

    /// Adds component @p T constructed from @p args. The entity must not already have @p T.
    /// Returns a reference to the new component (nothing for tag types). The reference is invalidated
    /// by the next structural change.
    template <ComponentType T, class... Args>
    decltype(auto) emplace(Entity entity, Args&&... args) noexcept
    {
        // Construct first: args may reference components of this entity, which move when it changes
        // archetype.
        T value(std::forward<Args>(args)...);
        void* storage = addComponentRaw(entity, componentId<T>());
        if constexpr (std::is_empty_v<T>)
        {
            (void)storage;
            (void)value;
        }
        else
        {
            return *::new (storage) T(std::move(value));
        }
    }

    /// Adds @p value, or assigns it if the entity already has a component of that type.
    template <class T>
        requires ComponentType<std::decay_t<T>>
    void set(Entity entity, T&& value) noexcept
    {
        using U = std::decay_t<T>;
        if constexpr (std::is_empty_v<U>)
        {
            if (!has<U>(entity))
            {
                emplace<U>(entity);
            }
        }
        else if (U* existing = tryGet<U>(entity))
        {
            *existing = std::forward<T>(value);
        }
        else
        {
            emplace<U>(entity, std::forward<T>(value));
        }
    }

    /// Removes component @p T. Returns false if the entity did not have it.
    template <ComponentType T>
    bool remove(Entity entity)
    {
        return removeComponentRaw(entity, componentId<T>());
    }

    /// Returns true if @p entity has component @p T. Asserts (and returns false) for dead entities.
    template <ComponentType T>
    [[nodiscard]] bool has(Entity entity) const
    {
        ILL_ASSERT(isAlive(entity), "has() on a dead entity");
        return isAlive(entity) && m_slots[entity.index].archetype->has(componentId<T>());
    }

    /// Returns component @p T of @p entity, or nullptr if it has none. Invalidated by structural changes.
    template <ComponentType T>
    [[nodiscard]] T* tryGet(Entity entity) noexcept
    {
        static_assert(!std::is_empty_v<T>, "tag components have no data; use has<T>()");
        return static_cast<T*>(componentRaw(entity, componentId<T>()));
    }

    /// @copydoc tryGet
    template <ComponentType T>
    [[nodiscard]] const T* tryGet(Entity entity) const noexcept
    {
        return const_cast<World*>(this)->tryGet<T>(entity);
    }

    /// Returns component @p T of @p entity, which must have it. Invalidated by structural changes.
    template <ComponentType T>
    [[nodiscard]] T& get(Entity entity) noexcept
    {
        T* component = tryGet<T>(entity);
        ILL_ASSERT(component != nullptr, "entity does not have component {}", typeName<T>());
        return *component;
    }

    /// @copydoc get
    template <ComponentType T>
    [[nodiscard]] const T& get(Entity entity) const noexcept
    {
        return const_cast<World*>(this)->get<T>(entity);
    }

    /// Creates a query over entities that have all of @p Ts. Use `const T` for read-only access.
    template <class... Ts>
    [[nodiscard]] Query<Ts...> query() noexcept
    {
        return Query<Ts...>(*this);
    }

    /// Destroys every entity. Outstanding handles become stale.
    void clear();

    /// @name Type-erased access
    /// For serialization, the editor and tools, which work with ComponentIds instead of C++ types.
    /// @{

    /// Replaces @p out with every live entity, ordered by index.
    void entities(std::vector<Entity>& out) const;

    /// Component types of @p entity (sorted ids, tags included). Invalidated by structural changes.
    [[nodiscard]] std::span<const ComponentId> componentsOf(Entity entity) const;

    /// Returns the component @p id of @p entity, or nullptr if it has none (or @p id is a tag).
    /// Invalidated by structural changes.
    [[nodiscard]] void* componentData(Entity entity, ComponentId id) noexcept;

    /// @copydoc componentData
    [[nodiscard]] const void* componentData(Entity entity, ComponentId id) const noexcept;

    /// Adds a default-constructed component @p id (the type must be default constructible) and
    /// returns it (nullptr for tags). The entity must not already have it.
    void* emplaceDefault(Entity entity, ComponentId id);

    /// Removes component @p id. Returns false if the entity did not have it.
    bool remove(Entity entity, ComponentId id);

    /// The registry whose ids this world uses.
    [[nodiscard]] const ComponentRegistry& registry() const noexcept { return m_registry; }

    /// Calls `fn(RawChunk&)` once per non-empty chunk of the entities that have every component in @p all
    /// and none in @p none: a query by ComponentId. An empty @p all matches every entity. Structural changes
    /// are not allowed during the iteration, as with Query.
    template <class F>
    void forEachMatching(const ComponentMask& all, const ComponentMask& none, F&& fn)
    {
        static_assert(std::is_invocable_v<F&, RawChunk&>,
                      "forEachMatching() callable must accept (RawChunk&)");
        IterationScope scope(*this);
        for (Archetype* archetype : matchArchetypes(all, none))
        {
            for (u32 chunk = 0; chunk < archetype->chunkCount(); ++chunk)
            {
                RawChunk view(*archetype, chunk);
                fn(view);
            }
        }
    }

    /// Calls `fn(const RawChunk&)`; see forEachMatching().
    template <class F>
    void forEachMatching(const ComponentMask& all, const ComponentMask& none, F&& fn) const
    {
        static_assert(std::is_invocable_v<F&, const RawChunk&>,
                      "forEachMatching() callable must accept (const RawChunk&)");
        const_cast<World*>(this)->forEachMatching(all, none, [&fn](const RawChunk& view) { fn(view); });
    }

    /// @}

    /// Number of live entities.
    [[nodiscard]] u32 aliveCount() const noexcept { return m_aliveCount; }

    /// Counter of structural changes, for caches built from the World's layout (editor outliners, query
    /// results kept across frames). It starts at 0 and grows by one whenever the set of entities or the
    /// set of components of an entity changes: create(), destroy(), adding a missing component (emplace(),
    /// set(), emplaceDefault(), CommandBuffer::apply()), removing one (remove() when it was present) and
    /// clear() of a non-empty world. Writing component values, assigning an existing component with
    /// set(), and calls that change nothing leave it unchanged.
    [[nodiscard]] u64 structuralVersion() const noexcept { return m_structuralVersion; }

    /// Counter of changes to component @p id, for caches built from the values of one component type (such
    /// as HierarchyIndex). It starts at 0 and grows whenever an entity gains or loses component @p id
    /// (create(), destroy(), adding or removing it, clear(), CommandBuffer::apply()) and whenever
    /// markChanged() announces that values of it were written. Writing values without markChanged() leaves
    /// it unchanged.
    [[nodiscard]] u64 componentVersion(ComponentId id) const noexcept { return m_componentVersions[id]; }

    /// componentVersion() of component @p T.
    template <ComponentType T>
    [[nodiscard]] u64 componentVersion() const noexcept
    {
        return componentVersion(componentId<T>());
    }

    /// Announces that values of component @p id were written (grows componentVersion()). Like structural
    /// changes, call it only from the thread that owns the World, not from parallel jobs.
    void markChanged(ComponentId id) noexcept { ++m_componentVersions[id]; }

    /// markChanged() for component @p T.
    template <ComponentType T>
    void markChanged() noexcept
    {
        markChanged(componentId<T>());
    }

    /// Number of archetypes created so far (archetypes are never destroyed before clear()).
    [[nodiscard]] u32 archetypeCount() const noexcept { return static_cast<u32>(m_archetypes.size()); }

    /// Returns true while a query is iterating this world.
    [[nodiscard]] bool isIterating() const noexcept
    {
        return m_iterationDepth.load(std::memory_order_relaxed) != 0;
    }

private:
    template <class... Ts>
    friend class Query;
    friend class CommandBuffer;

    struct EntitySlot
    {
        Archetype* archetype = nullptr;
        u32 row = 0;
        u32 generation = 0;
    };

    struct QueryKey
    {
        ComponentMask include;
        ComponentMask exclude;
        bool operator==(const QueryKey&) const = default;
    };

    struct QueryKeyHash
    {
        usize operator()(const QueryKey& key) const noexcept;
    };

    struct QueryCache
    {
        std::vector<Archetype*> archetypes;
        u32 scanned = 0; // number of world archetypes already tested
    };

    /// RAII marker for an iteration in progress.
    class IterationScope : NonCopyable
    {
    public:
        explicit IterationScope(World& world) noexcept
            : m_world(world)
        {
            m_world.m_iterationDepth.fetch_add(1, std::memory_order_relaxed);
        }
        ~IterationScope() { m_world.m_iterationDepth.fetch_sub(1, std::memory_order_relaxed); }

    private:
        World& m_world;
    };

    template <class T, class Arg>
    static void constructAt(Archetype& archetype, u32 row, Arg&& value) noexcept
    {
        if constexpr (!std::is_empty_v<T>)
        {
            void* storage = archetype.componentAt(row, archetype.columnIndex(componentId<T>()));
            ::new (storage) T(std::forward<Arg>(value));
        }
        else
        {
            (void)archetype;
            (void)row;
            (void)value;
        }
    }

    // Not noexcept so that test assert handlers can throw through it.
    void assertNotIterating() const
    {
        ILL_ASSERT(!isIterating(), "structural change while iterating; record it in a CommandBuffer instead");
    }

    Entity allocateEntity();
    Archetype* archetypeWith(Archetype* from, ComponentId id);
    Archetype* archetypeWithout(Archetype* from, ComponentId id);
    Archetype* findOrCreateArchetype(std::span<const ComponentId> signature);
    void moveEntity(Entity entity, Archetype* to);

    // Adds component @p id and returns its uninitialized storage (nullptr for tags).
    void* addComponentRaw(Entity entity, ComponentId id);
    bool removeComponentRaw(Entity entity, ComponentId id);
    void* componentRaw(Entity entity, ComponentId id) noexcept;

    const std::vector<Archetype*>& matchArchetypes(const ComponentMask& include,
                                                   const ComponentMask& exclude);

    const ComponentRegistry& m_registry;
    PoolAllocator m_chunkPool; // must outlive m_archetypes
    std::vector<std::unique_ptr<Archetype>> m_archetypes;
    std::unordered_multimap<u64, Archetype*> m_archetypeLookup;
    Archetype* m_root = nullptr;
    std::vector<EntitySlot> m_slots;
    std::vector<u32> m_freeSlots;
    u32 m_aliveCount = 0;
    u64 m_structuralVersion = 0;
    std::array<u64, kMaxComponentTypes> m_componentVersions{};
    std::atomic<u32> m_iterationDepth{0};
    std::mutex m_queryMutex;
    std::unordered_map<QueryKey, QueryCache, QueryKeyHash> m_queryCache;
};

/// Typed view of one chunk passed to Query::eachChunk(). Valid only during the callback.
template <class... Ts>
class ChunkView
{
public:
    /// Creates a view; used by Query.
    ChunkView(u32 size, const Entity* entities, std::tuple<Ts*...> columns) noexcept
        : m_size(size)
        , m_entities(entities)
        , m_columns(columns)
    {
    }

    /// Number of entities in the chunk.
    [[nodiscard]] u32 size() const noexcept { return m_size; }

    /// Entity handles of the chunk.
    [[nodiscard]] std::span<const Entity> entities() const noexcept { return {m_entities, m_size}; }

    /// Contiguous array of component @p T (spelled exactly as in the query, including const).
    template <class T>
    [[nodiscard]] std::span<T> get() const noexcept
    {
        return {std::get<T*>(m_columns), m_size};
    }

private:
    u32 m_size;
    const Entity* m_entities;
    std::tuple<Ts*...> m_columns;
};

/// Iterates entities that have every component in @p Ts (and every type added with with<>()) and none
/// of the types added with without<>(). Declare read-only access with `const T`.
template <class... Ts>
class Query
{
    static_assert(sizeof...(Ts) > 0, "a query needs at least one component type");
    static_assert((ComponentType<std::remove_const_t<Ts>> && ...), "query types must be component types");
    static_assert((!std::is_empty_v<Ts> && ...), "tag components have no data; filter them with with<Tag>()");

public:
    /// Creates a query on @p world. Prefer World::query().
    explicit Query(World& world) noexcept
        : m_world(&world)
    {
        (m_include.set(componentId<std::remove_const_t<Ts>>()), ...);
    }

    /// Additionally requires the given components (typically tags) without accessing them.
    template <ComponentType... Us>
    Query& with() noexcept
    {
        (m_include.set(componentId<Us>()), ...);
        return *this;
    }

    /// Excludes entities that have any of the given components.
    template <ComponentType... Us>
    Query& without() noexcept
    {
        (m_exclude.set(componentId<Us>()), ...);
        return *this;
    }

    /// Calls `fn(Ts&...)` or `fn(Entity, Ts&...)` for every matching entity.
    template <class F>
    void each(F&& fn)
    {
        static_assert(std::is_invocable_v<F&, Entity, Ts&...> || std::is_invocable_v<F&, Ts&...>,
                      "each() callable must accept (Ts&...) or (Entity, Ts&...)");
        World::IterationScope scope(*m_world);
        for (Archetype* archetype : m_world->matchArchetypes(m_include, m_exclude))
        {
            for (u32 chunk = 0; chunk < archetype->chunkCount(); ++chunk)
            {
                eachInChunk(fn, *archetype, chunk, std::index_sequence_for<Ts...>{});
            }
        }
    }

    /// Calls `fn(ChunkView<Ts...>&)` once per non-empty chunk. Use for vectorizable loops.
    template <class F>
    void eachChunk(F&& fn)
    {
        World::IterationScope scope(*m_world);
        for (Archetype* archetype : m_world->matchArchetypes(m_include, m_exclude))
        {
            for (u32 chunk = 0; chunk < archetype->chunkCount(); ++chunk)
            {
                ChunkView<Ts...> view = makeView(*archetype, chunk, std::index_sequence_for<Ts...>{});
                fn(view);
            }
        }
    }

    /// Like each(), but processes chunks in parallel on @p jobs and returns when all are done.
    /// @p fn is called concurrently and must only touch the components it is given (plus data that is
    /// safe to share).
    template <class F>
    void parallelEach(JobSystem& jobs, F&& fn)
    {
        static_assert(std::is_invocable_v<F&, Entity, Ts&...> || std::is_invocable_v<F&, Ts&...>,
                      "parallelEach() callable must accept (Ts&...) or (Entity, Ts&...)");
        World::IterationScope scope(*m_world);
        auto* function = std::addressof(fn);
        JobCounter counter;
        for (Archetype* archetype : m_world->matchArchetypes(m_include, m_exclude))
        {
            for (u32 chunk = 0; chunk < archetype->chunkCount(); ++chunk)
            {
                jobs.submit(
                    [function, archetype, chunk] {
                        eachInChunk(*function, *archetype, chunk, std::index_sequence_for<Ts...>{});
                    },
                    &counter);
            }
        }
        jobs.wait(counter);
    }

    /// Number of matching entities.
    [[nodiscard]] u32 count()
    {
        u32 total = 0;
        for (const Archetype* archetype : m_world->matchArchetypes(m_include, m_exclude))
        {
            total += archetype->size();
        }
        return total;
    }

private:
    template <usize... I>
    static ChunkView<Ts...> makeView(Archetype& archetype, u32 chunk, std::index_sequence<I...>) noexcept
    {
        const std::array<u16, sizeof...(Ts)> columns{
            archetype.columnIndex(componentId<std::remove_const_t<Ts>>())...};
        return ChunkView<Ts...>(
            archetype.chunkSize(chunk), archetype.entities(chunk),
            std::tuple<Ts*...>{static_cast<Ts*>(archetype.columnData(chunk, columns[I]))...});
    }

    template <class F, usize... I>
    static void eachInChunk(F& fn, Archetype& archetype, u32 chunk, std::index_sequence<I...> sequence)
    {
        const ChunkView<Ts...> view = makeView(archetype, chunk, sequence);
        const u32 size = view.size();
        const Entity* entities = view.entities().data();
        const std::tuple<Ts*...> columns{view.template get<Ts>().data()...};
        for (u32 i = 0; i < size; ++i)
        {
            if constexpr (std::is_invocable_v<F&, Entity, Ts&...>)
            {
                fn(entities[i], std::get<I>(columns)[i]...);
            }
            else
            {
                fn(std::get<I>(columns)[i]...);
            }
        }
    }

    World* m_world;
    ComponentMask m_include;
    ComponentMask m_exclude;
};

} // namespace illusion
