#pragma once

/// @file
/// Deferred structural changes for a World.
///
/// Queries forbid structural changes while iterating. Record them in a CommandBuffer instead and call
/// apply() once iteration is done. Commands run in the order they were recorded. A CommandBuffer reuses
/// its memory after apply()/clear(), so steady-state use does not allocate. Not thread-safe: use one
/// buffer per thread or job.

#include <illusion/core/Base.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Entity.hpp>

#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace illusion
{

class World;

/// Records create/destroy/set/remove commands and applies them to a World later.
class CommandBuffer : NonCopyable
{
public:
    /// Creates an empty buffer. Memory is reserved in blocks of @p blockSize bytes on first use.
    explicit CommandBuffer(usize blockSize = 64 * 1024);

    /// Destroys any component values still recorded.
    ~CommandBuffer();

    /// Records destruction of @p entity. Ignored at apply() time if the entity is already dead.
    void destroy(Entity entity);

    /// Records adding (or assigning, if present) @p value to @p entity. Ignored if the entity is dead
    /// at apply() time.
    template <class T>
        requires ComponentType<std::decay_t<T>>
    void set(Entity entity, T&& value)
    {
        using U = std::decay_t<T>;
        void* payload = record(CommandType::Set, entity, componentId<U>(), payloadSize<U>(), alignof(U));
        if constexpr (!std::is_empty_v<U>)
        {
            ::new (payload) U(std::forward<T>(value));
        }
        else
        {
            (void)payload;
        }
    }

    /// Records removal of component @p T from @p entity. Ignored if the entity is dead or lacks @p T.
    template <ComponentType T>
    void remove(Entity entity)
    {
        record(CommandType::Remove, entity, componentId<T>(), 0, 1);
    }

    /// Records creation of an entity with the given components. The entity handle is not available
    /// until apply(); use World::create() directly when you need it immediately.
    template <class... Ts>
        requires((ComponentType<std::decay_t<Ts>> && ...))
    void create(Ts&&... components)
    {
        record(CommandType::Create, kNullEntity, kInvalidComponentId, 0, 1);
        (setOnCreated(std::forward<Ts>(components)), ...);
    }

    /// Executes every recorded command on @p world, then clears the buffer. The world must not be
    /// iterating.
    void apply(World& world);

    /// Discards every recorded command without executing it.
    void clear() noexcept;

    /// Returns true if no command is recorded.
    [[nodiscard]] bool empty() const noexcept { return m_commandCount == 0; }

    /// Number of recorded commands (each component of a create() counts separately).
    [[nodiscard]] u32 size() const noexcept { return m_commandCount; }

private:
    enum class CommandType : u8
    {
        Destroy,
        Set,
        Remove,
        Create,
        SetOnCreated,
    };

    struct Header
    {
        CommandType type;
        ComponentId component;
        u32 payloadOffset; // from the start of the block, 0 if the command has no payload
        u32 next;          // offset of the next header in the same block (== Block::used for the last)
        Entity entity;
    };

    struct Block
    {
        std::byte* data = nullptr;
        usize capacity = 0;
        usize used = 0;
    };

    template <class T>
    static constexpr u32 payloadSize() noexcept
    {
        return std::is_empty_v<T> ? 0u : static_cast<u32>(sizeof(T));
    }

    template <class T>
    void setOnCreated(T&& value)
    {
        using U = std::decay_t<T>;
        void* payload =
            record(CommandType::SetOnCreated, kNullEntity, componentId<U>(), payloadSize<U>(), alignof(U));
        if constexpr (!std::is_empty_v<U>)
        {
            ::new (payload) U(std::forward<T>(value));
        }
        else
        {
            (void)payload;
        }
    }

    // Appends a command and returns storage for its payload (nullptr if payloadSize is 0).
    void* record(CommandType type, Entity entity, ComponentId component, u32 payloadSize,
                 usize payloadAlignment);

    // Calls visitor(Header&, void* payload) for every recorded command in order.
    template <class Visitor>
    void forEachCommand(Visitor&& visitor);

    // Forgets every command without destroying payloads; memory blocks are kept for reuse.
    void reset() noexcept;

    usize m_blockSize;
    std::vector<Block> m_blocks;
    usize m_currentBlock = 0;
    u32 m_commandCount = 0;
};

} // namespace illusion
