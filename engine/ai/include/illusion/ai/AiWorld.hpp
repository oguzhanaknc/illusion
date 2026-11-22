#pragma once

/// @file
/// The AI of a World (docs/adr/0062-navigation-and-ai.md): perception of the AiPerception Entities and the
/// behaviour trees of the BehaviorAgent Entities, ticked on the Job System, moving their NavAgents through a
/// NavWorld.
///
/// @code
/// illusion::AiWorld ai(world, navigation, &streamer);
/// while (running)
/// {
///     streamer.update();
///     ai.update(step, &jobs, &physics, true); // after the modules, before navigation
///     navigation.update(step, &jobs, true);
/// }
/// @endcode

#include <illusion/ai/AiService.hpp>
#include <illusion/ai/BehaviorTree.hpp>
#include <illusion/ai/BehaviorTreeState.hpp>
#include <illusion/ai/Perception.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/ecs/Entity.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace illusion
{

class AssetStreamer;
class JobSystem;
class NavDebugSink;
class NavWorld;
class PhysicsWorld;
class World;

/// Creation parameters of an AiWorld.
struct AiWorldDesc
{
    u32 maxAgents = 1024;            ///< Slots, reserved at construction.
    u32 maxSightRaysPerUpdate = 256; ///< Line-of-sight raycasts per update, served round-robin.
    u32 maxSightCandidates = 8;      ///< Stimuli in a perceiver's cone tested per update, nearest first.
    u32 maxNoisesPerUpdate = 256;    ///< Noises kept per update; AiService::reportNoise() fails beyond.
    u32 maxActions = 1024; ///< Action starts and aborts kept per update; more are counted and dropped.
    u64 seed = 0;          ///< Mixed with each Entity's guid to seed its tree's Random.
};

/// Counters of an AiWorld.
struct AiStatistics
{
    u32 agents = 0;         ///< Entities with a slot.
    u32 ticked = 0;         ///< Trees ticked by the last update.
    u32 pending = 0;        ///< Agents waiting for their tree after the last update.
    u32 failures = 0;       ///< Trees that failed to load, since construction.
    u32 rays = 0;           ///< Sight raycasts of the last update.
    u32 droppedActions = 0; ///< Actions dropped by the last update.
};

/// What AiWorld::drawDebug() draws.
struct AiDebugOptions
{
    bool sight = true;    ///< Sight cones and lines to what is seen.
    bool hearing = false; ///< Hearing radii of the last update's noises.
    bool memory = true;   ///< Last known positions of what is remembered.
};

/// Keeps one slot per Entity with a BehaviorAgent or an AiPerception. update() loads each `.behavior` file
/// once through the AssetStreamer (decoder `illusion.behavior`), adds AiAgentState, and with @p advance:
/// perceives (cone candidates as Jobs; line-of-sight raycasts through PhysicsWorld on the World's thread,
/// at most AiWorldDesc::maxSightRaysPerUpdate, round-robin in slot order; the noises reported since the last
/// update), writes the perception's blackboard keys, ticks the enabled trees as Jobs (each with a context
/// that buffers its moves and actions in its slot; trees with a tickInterval staggered by slot), then applies
/// the buffered requests in slot order (destinations through the NavWorld). Deterministic for any number of
/// workers with fixed steps. Entities with the Inactive tag keep their state and do not tick. A changed tree
/// file (AssetReloadedEvent) restarts its agents. Allocates only when slots are added or trees load. Use on
/// the World's thread.
class AiWorld : NonCopyable
{
public:
    /// @p world, @p navigation and @p streamer (null: trees never load) must outlive this object.
    AiWorld(World& world, NavWorld& navigation, AssetStreamer* streamer, const AiWorldDesc& desc = {});
    ~AiWorld();

    /// Syncs slots and trees, then, with @p advance, perceives and ticks for @p seconds (Jobs on @p jobs
    /// when given). Null @p physics skips the line-of-sight test (everything in the cone is seen). Adds
    /// AiAgentState, so call it outside queries.
    void update(f32 seconds, JobSystem* jobs, const PhysicsWorld* physics, bool advance);

    /// Calls update(0, nullptr, nullptr, false) and waits for the streamer until no agent waits for its tree.
    void waitIdle();

    /// Reports a noise heard in the next update; fails when the update's noises are full or a value is out of
    /// range.
    Expected<void, AiError> reportNoise(const AiNoise& noise);

    /// Writes @p value to @p key of @p entity's blackboard; @p value's type must be the key's.
    Expected<void, AiError> setValue(Entity entity, std::string_view key, const BlackboardValue& value);

    /// The value of @p key of @p entity's blackboard.
    [[nodiscard]] Expected<BlackboardValue, AiError> value(Entity entity, std::string_view key) const;

    /// The actions the last update started or aborted, in slot order; names stay valid until the next one.
    [[nodiscard]] std::span<const AiActionRequest> actions() const noexcept;

    /// Finishes the running action @p action of @p entity for the next update.
    Expected<void, AiError> finishAction(Entity entity, u32 action, bool success);

    /// Writes what @p entity perceives or remembers into @p out, nearest first; returns the count.
    [[nodiscard]] Expected<u32, AiError> perceived(Entity entity, std::span<AiPerceived> out) const;

    /// Starts @p entity's tree again from its root with the starting blackboard.
    Expected<void, AiError> restart(Entity entity);

    /// The slot of @p entity, or std::nullopt before its first update().
    [[nodiscard]] std::optional<u32> findSlot(Entity entity) const noexcept;

    /// The tree of @p slot, or null without one or while it loads.
    [[nodiscard]] const BehaviorTree* tree(u32 slot) const noexcept;

    /// The tree state of @p slot after the last update, or null when tree() is.
    [[nodiscard]] const BehaviorTreeState* treeState(u32 slot) const noexcept;

    /// Draws what @p options select into @p sink.
    void drawDebug(const AiDebugOptions& options, NavDebugSink& sink) const;

    /// Counters.
    [[nodiscard]] const AiStatistics& statistics() const noexcept;

    /// The World it runs.
    [[nodiscard]] World& world() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
