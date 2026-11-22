#include <illusion/ai/AiComponents.hpp>
#include <illusion/ai/AiWorld.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/AssetId.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavComponents.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

// Decoder name of `.behavior` files in the AssetStreamer.
constexpr std::string_view kTreeDecoder = "illusion.behavior";
constexpr u32 kNoSlot = ~0u;
// Slots perceived or ticked by one Job.
constexpr u32 kSlotsPerJob = 16;
// Side of a cell of the stimulus grid, in m.
constexpr f32 kCellSize = 8.0f;
// Cells beyond this from the origin are clamped (positions are finite, but may be huge).
constexpr f32 kMaxCell = 1.0e6f;
// Spreads the first ticks of trees with a tickInterval over the interval (the golden ratio's fraction).
constexpr f32 kStagger = 0.618034f;
// A hit this close before the looked-at point does not block sight, in m.
constexpr f32 kSightTolerance = 0.05f;
// A noise radius drawn by drawDebug() is a polygon of this many segments.
constexpr u32 kCircleSegments = 24;

constexpr Vec4 kSightColor{1.0f, 0.85f, 0.2f, 1.0f};
constexpr Vec4 kSeenColor{1.0f, 0.3f, 0.2f, 1.0f};
constexpr Vec4 kHearingColor{0.3f, 0.6f, 1.0f, 1.0f};
constexpr Vec4 kMemoryColor{0.7f, 0.5f, 1.0f, 1.0f};

[[nodiscard]] bool finite(const Vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] f32 flatDistanceSquared(const Vec3& a, const Vec3& b) noexcept
{
    const f32 x = a.x - b.x;
    const f32 z = a.z - b.z;
    return x * x + z * z;
}

[[nodiscard]] i32 cellOf(f32 coordinate) noexcept
{
    return static_cast<i32>(std::floor(std::clamp(coordinate / kCellSize, -kMaxCell, kMaxCell)));
}

[[nodiscard]] u32 cellHash(i32 x, i32 z) noexcept
{
    return (static_cast<u32>(x) * 73856093u) ^ (static_cast<u32>(z) * 19349663u);
}

std::optional<BehaviorTree> decodeTree(std::vector<std::byte>&& bytes, std::string& error)
{
    Expected<BehaviorTree, std::string> tree =
        parseBehaviorTree(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!tree)
    {
        error = std::move(tree.error());
        return std::nullopt;
    }
    return std::move(*tree);
}

enum class TreeStatus : u8
{
    Loading,
    Ready,
    Failed,
};

struct TreeEntry
{
    TreeStatus status = TreeStatus::Loading;
    std::string path;
    AssetHandle<BehaviorTree> load; // the running load
    bool loading = false;
    bool again = false; // the file changed again during the load
    u64 generation = 0; // advanced by every load that replaced tree
    BehaviorTree tree;
    // The tree the last reload replaced: the names of the actions its agents abort stay valid one update.
    BehaviorTree previous;
    std::vector<u32> actionIndex; // per node of tree: its index among the Action nodes, or kNoSlot
    u32 actionCount = 0;
};

struct ActionRecord
{
    u32 id = 0;
    BehaviorStatus status = BehaviorStatus::Idle;
    std::string_view name{};
};

enum class MoveRequest : u8
{
    None,
    Move,
    Stop,
};

enum class LineOfSight : u8
{
    Unknown, // not tested this update: the last result holds
    Visible,
    Blocked,
};

struct Stimulus
{
    Entity entity;
    Vec3 position{0.0f}; // of the Entity
    Vec3 point{0.0f};    // looked at
    u32 team = 0;
    i32 cellX = 0;
    i32 cellZ = 0;
};

struct Candidate
{
    u32 stimulus = 0;
    f32 distanceSquared = 0.0f;
    LineOfSight sight = LineOfSight::Unknown;
};

struct Slot
{
    Entity entity;
    bool used = false;
    u32 index = 0;
    u32 stamp = 0;
    u64 seed = 0;

    // Read from the World at the start of each update.
    bool active = false; // no Inactive tag
    Vec3 position{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool hasNav = false;
    u32 agentType = 0;
    u32 areaMask = ~0u;
    std::optional<NavAgentStatus> navStatus;

    // The tree.
    bool hasAgent = false;
    bool enabled = false;
    f32 interval = 0.0f;
    u64 treeId = 0;
    TreeEntry* tree = nullptr;
    u64 generation = 0; // of tree when it was bound; 0 while unbound
    BehaviorTreeState state;
    f32 timer = 0.0f;   // seconds to the next tick with an interval
    f32 elapsed = 0.0f; // seconds since the last tick with an interval
    bool ticked = false;
    BehaviorStatus status = BehaviorStatus::Idle;
    i32 running = -1;
    std::vector<ActionRecord> actions; // one per Action node
    u32 actionCounter = 0;
    std::vector<AiActionRequest> requests; // since the last update's apply, within its capacity
    u32 droppedRequests = 0;

    // Moves: the request of the running tick, applied after every tree ticked.
    MoveRequest move = MoveRequest::None;
    Vec3 moveTarget{0.0f};
    f32 moveAcceptance = 0.0f;
    bool moveWander = false;
    Vec3 wanderCenter{0.0f};
    f32 wanderRadius = 0.0f;
    u64 wanderSeed = 0;
    bool drawn = false; // randomPoint() drew the wander of the next requestMove()
    u32 moveHandle = 0;
    BehaviorStatus moveStatus = BehaviorStatus::Idle;
    Vec3 activeTarget{0.0f};
    f32 activeAcceptance = 0.0f;

    // Perception.
    bool hasPerception = false;
    AiPerception perception; // copied each update
    i32 targetKey = -1;
    i32 positionKey = -1;
    std::string targetKeyName; // the names targetKey and positionKey were resolved from
    std::string positionKeyName;
    bool hadTarget = false;
    Vec3 eye{0.0f};
    Vec3 forward{0.0f, 0.0f, 1.0f};
    std::array<AiPerceived, kMaxPerceived> memory{};
    u32 remembered = 0;
    u32 candidates = 0;
    i32 target = -1; // index into memory
};

} // namespace

struct AiWorld::Impl
{
    World* world = nullptr;
    NavWorld* navigation = nullptr;
    AssetStreamer* streamer = nullptr;
    const PhysicsWorld* physics = nullptr; // during update()
    AiWorldDesc desc;
    AiStatistics statistics;
    u32 stamp = 0;
    bool capacityLogged = false;

    std::unordered_map<u64, std::unique_ptr<TreeEntry>> trees;
    std::vector<u64> treeLoads; // trees with a running load
    std::vector<AssetId> reloads;
    Subscription reloaded;

    std::vector<Slot> slots;
    std::vector<u32> freeSlots;
    std::vector<u32> slotOf; // by Entity::index
    std::vector<Entity> added;

    std::vector<Stimulus> stimuli;
    std::vector<u32> bucketStart; // per grid bucket, into bucketItems; one more for the end
    std::vector<u32> bucketFill;
    std::vector<u32> bucketItems;
    u32 bucketMask = 0;
    std::vector<Candidate> candidates; // desc.maxSightCandidates per slot
    u32 rayCursor = 0;
    u32 rayOffset = 0;

    std::vector<AiNoise> noises; // of the last update
    std::vector<AiNoise> pendingNoises;
    std::vector<AiActionRequest> actions;
    std::array<bool, kMaxNavAgentTypes> navReady{};

    // --- The context of a tick ------------------------------------------------------------------------

    class Context final : public BehaviorContext
    {
    public:
        Context(const Impl& impl, Slot& slot) noexcept
            : m_impl(impl)
            , m_slot(slot)
        {
        }

        [[nodiscard]] Vec3 selfPosition() const override { return m_slot.position; }

        [[nodiscard]] std::optional<Vec3> position(Entity entity) const override
        {
            const World& scene = *m_impl.world;
            if (!scene.isAlive(entity))
            {
                return std::nullopt;
            }
            const Transform* transform = scene.tryGet<Transform>(entity);
            return transform != nullptr ? std::optional<Vec3>(transform->position) : std::nullopt;
        }

        // NavWorld queries belong to the World's thread, so the point is drawn when the move is applied: this
        // keeps the seed and returns the centre, which the next requestMove() replaces.
        [[nodiscard]] std::optional<Vec3> randomPoint(const Vec3& center, f32 radius, Random& random) override
        {
            if (!m_slot.hasNav || m_slot.agentType >= kMaxNavAgentTypes ||
                !m_impl.navReady[m_slot.agentType] || !finite(center) || !std::isfinite(radius))
            {
                return std::nullopt;
            }
            m_slot.drawn = true;
            m_slot.wanderCenter = center;
            m_slot.wanderRadius = std::max(radius, 0.0f);
            m_slot.wanderSeed = random.next();
            return center;
        }

        u32 requestMove(const Vec3& target, f32 acceptance) override
        {
            m_slot.move = MoveRequest::Move;
            m_slot.moveTarget = target;
            m_slot.moveAcceptance = std::max(acceptance, 0.0f);
            m_slot.moveWander = std::exchange(m_slot.drawn, false);
            m_slot.moveStatus = BehaviorStatus::Running;
            return ++m_slot.moveHandle;
        }

        [[nodiscard]] BehaviorStatus moveStatus(u32 handle) const override
        {
            return handle == m_slot.moveHandle ? m_slot.moveStatus : BehaviorStatus::Failure;
        }

        void stopMoving() override
        {
            m_slot.move = MoveRequest::Stop;
            m_slot.drawn = false;
            m_slot.moveStatus = BehaviorStatus::Failure;
            ++m_slot.moveHandle;
        }

        u32 startAction(u32 node, std::string_view name) override
        {
            const u32 id = ++m_slot.actionCounter;
            const std::vector<u32>& index = m_slot.tree->actionIndex;
            if (node < index.size() && index[node] < m_slot.actions.size())
            {
                m_slot.actions[index[node]] =
                    ActionRecord{.id = id, .status = BehaviorStatus::Running, .name = name};
                push(m_slot, id, name, AiActionPhase::Started);
            }
            return id;
        }

        [[nodiscard]] BehaviorStatus actionStatus(u32 handle) const override
        {
            for (const ActionRecord& record : m_slot.actions)
            {
                if (record.id == handle)
                {
                    return record.status == BehaviorStatus::Idle ? BehaviorStatus::Failure : record.status;
                }
            }
            return BehaviorStatus::Failure;
        }

        void abortAction(u32 handle) override
        {
            for (ActionRecord& record : m_slot.actions)
            {
                if (record.id == handle && record.status == BehaviorStatus::Running)
                {
                    record.status = BehaviorStatus::Idle;
                    push(m_slot, record.id, record.name, AiActionPhase::Aborted);
                }
            }
        }

    private:
        const Impl& m_impl;
        Slot& m_slot;
    };

    static void push(Slot& slot, u32 action, std::string_view name, AiActionPhase phase) noexcept
    {
        if (slot.requests.size() < slot.requests.capacity())
        {
            slot.requests.push_back(
                AiActionRequest{.entity = slot.entity, .action = action, .name = name, .phase = phase});
        }
        else
        {
            ++slot.droppedRequests;
        }
    }

    // --- Trees ----------------------------------------------------------------------------------------

    void fail(TreeEntry& entry, AssetId id, std::string_view message)
    {
        entry.status = TreeStatus::Failed;
        ++statistics.failures;
        ILL_LOG_WARN("AI", "cannot use '{}': {}", AssetPathTable::instance().path(id), message);
    }

    static void install(TreeEntry& entry, const BehaviorTree& tree)
    {
        entry.previous = std::move(entry.tree);
        entry.tree = tree;
        entry.actionIndex.assign(entry.tree.nodes.size(), kNoSlot);
        entry.actionCount = 0;
        for (usize i = 0; i < entry.tree.nodes.size(); ++i)
        {
            if (entry.tree.nodes[i].type == BehaviorNodeType::Action)
            {
                entry.actionIndex[i] = entry.actionCount++;
            }
        }
        entry.status = TreeStatus::Ready;
        ++entry.generation;
    }

    void startLoad(u64 key, TreeEntry& entry)
    {
        entry.load = streamer->loadFile<BehaviorTree>(entry.path, kTreeDecoder, decodeTree);
        if (!entry.loading)
        {
            entry.loading = true;
            treeLoads.push_back(key);
        }
    }

    TreeEntry* requestTree(AssetId id)
    {
        if (!id)
        {
            return nullptr;
        }
        const auto [found, inserted] = trees.try_emplace(id.value);
        if (inserted)
        {
            found->second = std::make_unique<TreeEntry>();
            TreeEntry& entry = *found->second;
            const std::string_view path = AssetPathTable::instance().path(id);
            const std::optional<AssetPathParts> parts = splitAssetPath(path);
            if (!parts || parts->fragment != AssetFragment::None || !path.ends_with(kBehaviorTreeExtension) ||
                isOnBuiltinMount(path))
            {
                fail(entry, id, "not a behaviour tree (.behavior)");
            }
            else if (streamer == nullptr)
            {
                fail(entry, id, "the AiWorld has no AssetStreamer");
            }
            else
            {
                entry.path = path;
                startLoad(id.value, entry);
            }
        }
        return found->second.get();
    }

    void startReloads()
    {
        for (const AssetId file : reloads)
        {
            const auto found = trees.find(file.value);
            if (found == trees.end() || found->second->path.empty())
            {
                continue;
            }
            TreeEntry& entry = *found->second;
            if (entry.loading)
            {
                entry.again = true;
            }
            else
            {
                startLoad(file.value, entry);
            }
        }
        reloads.clear();
    }

    void advanceLoads()
    {
        for (usize i = 0; i < treeLoads.size();)
        {
            const u64 key = treeLoads[i];
            TreeEntry& entry = *trees.at(key);
            if (entry.load.state() == AssetState::Loading)
            {
                ++i;
                continue;
            }
            if (const BehaviorTree* loaded = entry.load.get())
            {
                install(entry, *loaded);
            }
            else if (entry.status == TreeStatus::Ready)
            {
                ILL_LOG_WARN("AI", "cannot reload '{}': {}", entry.path, entry.load.error());
            }
            else
            {
                fail(entry, AssetId{key}, entry.load.error());
            }
            entry.load = {};
            if (std::exchange(entry.again, false))
            {
                entry.load = streamer->loadFile<BehaviorTree>(entry.path, kTreeDecoder, decodeTree);
                ++i;
                continue;
            }
            entry.loading = false;
            treeLoads[i] = treeLoads.back();
            treeLoads.pop_back();
        }
    }

    // --- Slots ----------------------------------------------------------------------------------------

    [[nodiscard]] u32 slotIndex(Entity entity) const noexcept
    {
        if (entity.index >= slotOf.size())
        {
            return kNoSlot;
        }
        const u32 index = slotOf[entity.index];
        return index != kNoSlot && slots[index].used && slots[index].entity == entity ? index : kNoSlot;
    }

    [[nodiscard]] Slot* slotFor(Entity entity) noexcept
    {
        const u32 index = slotIndex(entity);
        return index != kNoSlot ? &slots[index] : nullptr;
    }

    [[nodiscard]] const Slot* slotFor(Entity entity) const noexcept
    {
        const u32 index = slotIndex(entity);
        return index != kNoSlot ? &slots[index] : nullptr;
    }

    u32 acquire(Entity entity)
    {
        u32 index = 0;
        if (!freeSlots.empty())
        {
            index = freeSlots.back();
            freeSlots.pop_back();
        }
        else
        {
            index = static_cast<u32>(slots.size());
            slots.emplace_back();
        }
        Slot& slot = slots[index];
        slot.used = true;
        slot.index = index;
        slot.entity = entity;
        slot.stamp = stamp;
        const EntityGuid* guid = world->tryGet<EntityGuid>(entity);
        slot.seed = hashCombine(desc.seed, guid != nullptr ? guid->value : u64{entity.index});
        if (entity.index >= slotOf.size())
        {
            slotOf.resize(static_cast<usize>(entity.index) + 1, kNoSlot);
        }
        slotOf[entity.index] = index;
        return index;
    }

    void release(u32 index)
    {
        const Entity entity = slots[index].entity;
        if (entity.index < slotOf.size() && slotOf[entity.index] == index)
        {
            slotOf[entity.index] = kNoSlot;
        }
        Slot fresh;
        slots[index] = std::move(fresh);
        freeSlots.push_back(index);
    }

    void syncSlots()
    {
        ++stamp;
        added.clear();
        const auto mark = [this](Entity entity) {
            const u32 index = slotIndex(entity);
            if (index == kNoSlot)
            {
                added.push_back(entity);
            }
            else
            {
                slots[index].stamp = stamp;
            }
        };
        world->query<const BehaviorAgent>().each([&](Entity entity, const BehaviorAgent&) { mark(entity); });
        world->query<const AiPerception>().without<BehaviorAgent>().each(
            [&](Entity entity, const AiPerception&) { mark(entity); });
        for (u32 i = 0; i < slots.size(); ++i)
        {
            Slot& slot = slots[i];
            if (slot.used && slot.stamp != stamp)
            {
                if (world->isAlive(slot.entity) && world->has<AiAgentState>(slot.entity))
                {
                    world->remove<AiAgentState>(slot.entity);
                }
                release(i);
            }
        }
        std::ranges::sort(added, {}, &Entity::index);
        for (const Entity entity : added)
        {
            if (slots.size() - freeSlots.size() >= desc.maxAgents)
            {
                if (!std::exchange(capacityLogged, true))
                {
                    ILL_LOG_WARN("AI", "more than {} AI agents: the rest are ignored", desc.maxAgents);
                }
                break;
            }
            AiAgentState state;
            state.slot = acquire(entity);
            world->set(entity, state);
        }
    }

    // Aborts the running actions (reported as Aborted) and the move of @p slot.
    void abortRunning(Slot& slot)
    {
        for (ActionRecord& record : slot.actions)
        {
            if (record.status == BehaviorStatus::Running)
            {
                push(slot, record.id, record.name, AiActionPhase::Aborted);
            }
            record.status = BehaviorStatus::Idle;
        }
        if (slot.moveStatus == BehaviorStatus::Running && slot.hasNav)
        {
            static_cast<void>(navigation->stop(slot.entity));
        }
        slot.move = MoveRequest::None;
        slot.drawn = false;
        slot.moveStatus = BehaviorStatus::Idle;
        ++slot.moveHandle;
    }

    void resolveKeys(Slot& slot)
    {
        const BehaviorTree& tree = slot.tree->tree;
        const auto resolve = [&tree](const std::string& name, BlackboardType type) {
            const i32 key = name.empty() ? -1 : tree.findKey(name);
            return key >= 0 && tree.blackboard[static_cast<usize>(key)].type == type ? key : -1;
        };
        slot.targetKeyName = slot.perception.targetKey;
        slot.positionKeyName = slot.perception.positionKey;
        slot.targetKey = resolve(slot.targetKeyName, BlackboardType::Entity);
        slot.positionKey = resolve(slot.positionKeyName, BlackboardType::Vector3);
        if (slot.hasPerception && ((!slot.targetKeyName.empty() && slot.targetKey < 0) ||
                                   (!slot.positionKeyName.empty() && slot.positionKey < 0)))
        {
            ILL_LOG_WARN("AI", "'{}': the AiPerception keys need an Entity key '{}' and a Vector3 key '{}'",
                         slot.tree->path, slot.targetKeyName, slot.positionKeyName);
        }
    }

    // Starts @p slot's tree from its root with its defaults and @p agent's overrides.
    void restartTree(Slot& slot, const BehaviorAgent& agent)
    {
        abortRunning(slot);
        const BehaviorTree& tree = slot.tree->tree;
        if (slot.state.nodes.size() != tree.nodes.size() ||
            slot.state.blackboard.size() != tree.blackboard.size())
        {
            slot.state = makeBehaviorTreeState(tree, slot.seed);
        }
        else
        {
            resetBehaviorTree(tree, slot.state);
            slot.state.random.seed(slot.seed);
        }
        for (const BlackboardOverride& value : agent.blackboard)
        {
            const i32 key = tree.findKey(value.key);
            if (key < 0)
            {
                ILL_LOG_WARN("AI", "'{}' has no blackboard key '{}' to start with a value", slot.tree->path,
                             value.key);
                continue;
            }
            BlackboardValue& entry = slot.state.blackboard[static_cast<usize>(key)];
            const f32 number = std::isfinite(value.number) ? value.number : 0.0f;
            switch (entry.type)
            {
            case BlackboardType::Bool:
                entry.flag = number != 0.0f;
                break;
            case BlackboardType::Int:
                entry.integer = static_cast<i32>(std::lround(std::clamp(number, -2.0e9f, 2.0e9f)));
                break;
            case BlackboardType::Float:
                entry.number = number;
                break;
            case BlackboardType::Vector3:
                entry.vector = finite(value.vector) ? value.vector : Vec3(0.0f);
                entry.set = true;
                break;
            case BlackboardType::Entity:
                entry.entity = value.entity;
                entry.set = value.entity != kNullEntity;
                break;
            }
        }
        if (slot.actions.size() != slot.tree->actionCount)
        {
            slot.actions.assign(slot.tree->actionCount, ActionRecord{});
        }
        const usize capacity = 2 * static_cast<usize>(slot.tree->actionCount) + 2;
        if (slot.requests.capacity() < capacity + slot.requests.size())
        {
            slot.requests.reserve(capacity + slot.requests.size());
        }
        resolveKeys(slot);
        slot.status = BehaviorStatus::Idle;
        slot.running = -1;
        slot.hadTarget = false;
        slot.elapsed = 0.0f;
        const f32 phase = static_cast<f32>(slot.index) * kStagger;
        slot.timer = slot.interval * (phase - std::floor(phase));
        slot.generation = slot.tree->generation;
    }

    void readSlot(Slot& slot)
    {
        World& w = *world;
        const Entity entity = slot.entity;
        if (const Transform* transform = w.tryGet<Transform>(entity);
            transform != nullptr && finite(transform->position))
        {
            slot.position = transform->position;
            slot.rotation = transform->rotation;
        }
        slot.active = !w.has<Inactive>(entity);

        const NavAgent* nav = w.tryGet<NavAgent>(entity);
        slot.hasNav = nav != nullptr;
        slot.agentType = nav != nullptr ? nav->agentType : 0u;
        slot.areaMask = nav != nullptr ? nav->areaMask : ~0u;
        const NavAgentState* navState = w.tryGet<NavAgentState>(entity);
        slot.navStatus = navState != nullptr ? std::optional<NavAgentStatus>(navState->status) : std::nullopt;
        if (slot.moveStatus == BehaviorStatus::Running && slot.move == MoveRequest::None)
        {
            if (!slot.hasNav)
            {
                slot.moveStatus = BehaviorStatus::Failure;
            }
            else if (slot.navStatus == NavAgentStatus::Arrived)
            {
                slot.moveStatus = BehaviorStatus::Success;
            }
            else if (slot.navStatus == NavAgentStatus::Failed || slot.navStatus == NavAgentStatus::Idle)
            {
                slot.moveStatus = BehaviorStatus::Failure;
            }
            else if (flatDistanceSquared(slot.position, slot.activeTarget) <=
                     slot.activeAcceptance * slot.activeAcceptance)
            {
                slot.moveStatus = BehaviorStatus::Success;
            }
        }

        const AiPerception* perception = w.tryGet<AiPerception>(entity);
        slot.hasPerception = perception != nullptr;
        if (perception != nullptr)
        {
            slot.perception.sightRadius = perception->sightRadius;
            slot.perception.sightAngle = perception->sightAngle;
            slot.perception.eyeHeight = std::isfinite(perception->eyeHeight) ? perception->eyeHeight : 0.0f;
            slot.perception.occluderMask = perception->occluderMask;
            slot.perception.hearing = perception->hearing;
            slot.perception.memorySeconds = perception->memorySeconds;
            slot.perception.detectTeams = perception->detectTeams;
            if (perception->targetKey != slot.perception.targetKey)
            {
                slot.perception.targetKey = perception->targetKey;
            }
            if (perception->positionKey != slot.perception.positionKey)
            {
                slot.perception.positionKey = perception->positionKey;
            }
            slot.eye = slot.position + Vec3(0.0f, slot.perception.eyeHeight, 0.0f);
            const Vec3 forward = slot.rotation * Vec3(0.0f, 0.0f, 1.0f);
            const f32 length = glm::length(forward);
            slot.forward =
                length > 1e-6f && std::isfinite(length) ? forward / length : Vec3(0.0f, 0.0f, 1.0f);
        }

        const BehaviorAgent* agent = w.tryGet<BehaviorAgent>(entity);
        slot.hasAgent = agent != nullptr;
        if (agent == nullptr)
        {
            if (slot.tree != nullptr)
            {
                abortRunning(slot);
                slot.tree = nullptr;
                slot.treeId = 0;
                slot.generation = 0;
            }
            return;
        }
        slot.enabled = agent->enabled;
        slot.interval = std::isfinite(agent->tickInterval) ? std::max(agent->tickInterval, 0.0f) : 0.0f;
        if (agent->tree.id.value != slot.treeId)
        {
            abortRunning(slot);
            slot.treeId = agent->tree.id.value;
            slot.tree = requestTree(agent->tree.id);
            slot.generation = 0;
        }
        if (slot.tree == nullptr)
        {
            return;
        }
        if (slot.tree->status == TreeStatus::Ready && slot.generation != slot.tree->generation)
        {
            restartTree(slot, *agent);
        }
        else if (slot.tree->status == TreeStatus::Loading)
        {
            ++statistics.pending;
        }
        if (slot.generation != 0 && (slot.perception.targetKey != slot.targetKeyName ||
                                     slot.perception.positionKey != slot.positionKeyName))
        {
            resolveKeys(slot);
        }
    }

    // --- Perception -----------------------------------------------------------------------------------

    void collectStimuli()
    {
        stimuli.clear();
        world->query<const AiStimulus, const Transform>().without<Inactive>().each(
            [this](Entity entity, const AiStimulus& stimulus, const Transform& transform) {
                const Vec3 point = transform.position + stimulus.offset;
                if (!stimulus.visible || stimulus.team >= kMaxAiTeams || !finite(point) ||
                    !finite(transform.position))
                {
                    return;
                }
                stimuli.push_back(Stimulus{.entity = entity,
                                           .position = transform.position,
                                           .point = point,
                                           .team = stimulus.team,
                                           .cellX = cellOf(point.x),
                                           .cellZ = cellOf(point.z)});
            });
        const auto count = static_cast<u32>(stimuli.size());
        u32 buckets = 16;
        while (buckets < count * 2)
        {
            buckets <<= 1;
        }
        bucketMask = buckets - 1;
        bucketStart.assign(static_cast<usize>(buckets) + 1, 0u);
        for (const Stimulus& stimulus : stimuli)
        {
            ++bucketStart[(cellHash(stimulus.cellX, stimulus.cellZ) & bucketMask) + 1];
        }
        for (u32 b = 0; b < buckets; ++b)
        {
            bucketStart[b + 1] += bucketStart[b];
        }
        bucketFill.assign(bucketStart.begin(), bucketStart.end() - 1);
        bucketItems.resize(count);
        for (u32 i = 0; i < count; ++i)
        {
            const u32 bucket = cellHash(stimuli[i].cellX, stimuli[i].cellZ) & bucketMask;
            bucketItems[bucketFill[bucket]++] = i;
        }
    }

    // Nearest first; equal distances by stimulus, so the order does not depend on how they were visited.
    static void insertCandidate(Candidate* out, u32& count, u32 capacity, const Candidate& candidate) noexcept
    {
        const auto before = [](const Candidate& a, const Candidate& b) {
            return a.distanceSquared < b.distanceSquared ||
                   (a.distanceSquared == b.distanceSquared && a.stimulus < b.stimulus);
        };
        if (capacity == 0 || (count == capacity && !before(candidate, out[count - 1])))
        {
            return;
        }
        u32 i = count < capacity ? count++ : capacity - 1;
        while (i > 0 && before(candidate, out[i - 1]))
        {
            out[i] = out[i - 1];
            --i;
        }
        out[i] = candidate;
    }

    void gather(Slot& slot) noexcept
    {
        slot.candidates = 0;
        const AiPerception& p = slot.perception;
        if (!slot.hasPerception || !slot.active || !(p.sightRadius > 0.0f) || !std::isfinite(p.sightRadius) ||
            !finite(slot.eye))
        {
            return;
        }
        Candidate* out = candidates.data() + static_cast<usize>(slot.index) * desc.maxSightCandidates;
        const auto visit = [&](u32 index) {
            const Stimulus& stimulus = stimuli[index];
            if (stimulus.entity == slot.entity || ((p.detectTeams >> stimulus.team) & 1u) == 0 ||
                !inSightCone(slot.eye, slot.forward, p.sightRadius, p.sightAngle, stimulus.point))
            {
                return;
            }
            const Vec3 offset = stimulus.point - slot.eye;
            insertCandidate(out, slot.candidates, desc.maxSightCandidates,
                            Candidate{.stimulus = index,
                                      .distanceSquared = glm::dot(offset, offset),
                                      .sight = LineOfSight::Unknown});
        };
        const auto count = static_cast<u32>(stimuli.size());
        const i32 x0 = cellOf(slot.eye.x - p.sightRadius);
        const i32 x1 = cellOf(slot.eye.x + p.sightRadius);
        const i32 z0 = cellOf(slot.eye.z - p.sightRadius);
        const i32 z1 = cellOf(slot.eye.z + p.sightRadius);
        const i64 cells = (static_cast<i64>(x1) - x0 + 1) * (static_cast<i64>(z1) - z0 + 1);
        if (cells > static_cast<i64>(count))
        {
            for (u32 i = 0; i < count; ++i)
            {
                visit(i);
            }
            return;
        }
        for (i32 z = z0; z <= z1; ++z)
        {
            for (i32 x = x0; x <= x1; ++x)
            {
                const u32 bucket = cellHash(x, z) & bucketMask;
                for (u32 item = bucketStart[bucket]; item < bucketStart[bucket + 1]; ++item)
                {
                    const u32 index = bucketItems[item];
                    if (stimuli[index].cellX == x && stimuli[index].cellZ == z)
                    {
                        visit(index);
                    }
                }
            }
        }
    }

    void castRays()
    {
        ILL_PROFILE_SCOPE_N("AiWorld::castRays");
        const auto count = static_cast<u32>(slots.size());
        if (count == 0)
        {
            return;
        }
        u32 budget = desc.maxSightRaysPerUpdate;
        u32 index = rayCursor % count;
        for (u32 visited = 0; visited < count; ++visited)
        {
            Slot& slot = slots[index];
            Candidate* list = candidates.data() + static_cast<usize>(index) * desc.maxSightCandidates;
            for (u32 c = visited == 0 ? rayOffset : 0u; slot.used && c < slot.candidates; ++c)
            {
                if (budget == 0)
                {
                    rayCursor = index;
                    rayOffset = c;
                    return;
                }
                --budget;
                ++statistics.rays;
                const Stimulus& stimulus = stimuli[list[c].stimulus];
                const Vec3 direction = stimulus.point - slot.eye;
                const f32 distance = glm::length(direction);
                bool visible = true;
                if (distance > 1e-4f)
                {
                    const std::optional<RaycastHit> hit = physics->raycast(
                        slot.eye, direction, distance, slot.perception.occluderMask, slot.entity);
                    visible =
                        !hit || hit->entity == stimulus.entity || hit->distance >= distance - kSightTolerance;
                }
                list[c].sight = visible ? LineOfSight::Visible : LineOfSight::Blocked;
            }
            index = index + 1 < count ? index + 1 : 0;
        }
        rayCursor = index;
        rayOffset = 0;
    }

    [[nodiscard]] static bool seenBefore(const Slot& slot, Entity entity) noexcept
    {
        for (u32 i = 0; i < slot.remembered; ++i)
        {
            const AiPerceived& entry = slot.memory[i];
            if (entry.entity == entity)
            {
                return entry.current && entry.sense == AiSense::Sight;
            }
        }
        return false;
    }

    static void remember(Slot& slot, Entity entity, AiSense sense, const Vec3& position) noexcept
    {
        u32 index = kNoSlot;
        for (u32 i = 0; i < slot.remembered; ++i)
        {
            if (slot.memory[i].entity == entity)
            {
                index = i;
                break;
            }
        }
        if (index == kNoSlot && slot.remembered < kMaxPerceived)
        {
            index = slot.remembered++;
        }
        else if (index == kNoSlot)
        {
            // The oldest memory gives way, the farthest of equally old ones; one sensed now only to a nearer
            // one.
            const f32 distance = flatDistanceSquared(position, slot.position);
            f32 bestAge = -1.0f;
            f32 bestDistance = -1.0f;
            for (u32 i = 0; i < kMaxPerceived; ++i)
            {
                const AiPerceived& entry = slot.memory[i];
                const f32 entryDistance = flatDistanceSquared(entry.position, slot.position);
                if (entry.age > bestAge || (entry.age == bestAge && entryDistance > bestDistance))
                {
                    bestAge = entry.age;
                    bestDistance = entryDistance;
                    index = i;
                }
            }
            if (slot.memory[index].current && bestDistance <= distance)
            {
                return;
            }
        }
        slot.memory[index] =
            AiPerceived{.entity = entity, .sense = sense, .position = position, .age = 0.0f, .current = true};
    }

    void perceive(Slot& slot, f32 seconds) noexcept
    {
        const AiPerception& p = slot.perception;
        Candidate* list = candidates.data() + static_cast<usize>(slot.index) * desc.maxSightCandidates;
        for (u32 c = 0; c < slot.candidates; ++c)
        {
            if (list[c].sight == LineOfSight::Unknown)
            {
                list[c].sight = seenBefore(slot, stimuli[list[c].stimulus].entity) ? LineOfSight::Visible
                                                                                   : LineOfSight::Blocked;
            }
        }
        for (u32 i = 0; i < slot.remembered; ++i)
        {
            slot.memory[i].age += seconds;
            slot.memory[i].current = false;
        }
        if (p.hearing > 0.0f && std::isfinite(p.hearing) && finite(slot.eye))
        {
            for (const AiNoise& noise : noises)
            {
                if (noise.instigator != slot.entity && ((p.detectTeams >> noise.team) & 1u) != 0 &&
                    hearsNoise(slot.eye, p.hearing, noise))
                {
                    remember(slot, noise.instigator, AiSense::Hearing, noise.position);
                }
            }
        }
        for (u32 c = 0; c < slot.candidates; ++c)
        {
            if (list[c].sight == LineOfSight::Visible)
            {
                const Stimulus& stimulus = stimuli[list[c].stimulus];
                remember(slot, stimulus.entity, AiSense::Sight, stimulus.position);
            }
        }
        const f32 memory = std::isfinite(p.memorySeconds) ? std::max(p.memorySeconds, 0.0f) : 0.0f;
        u32 kept = 0;
        for (u32 i = 0; i < slot.remembered; ++i)
        {
            const AiPerceived& entry = slot.memory[i];
            const bool alive = entry.entity == kNullEntity || world->isAlive(entry.entity);
            if (alive && (entry.current || entry.age <= memory))
            {
                slot.memory[kept++] = entry;
            }
        }
        slot.remembered = kept;

        // The nearest one sensed now, else the most recent memory (the nearest of equally recent ones).
        slot.target = -1;
        bool targetCurrent = false;
        f32 targetAge = 0.0f;
        f32 targetDistance = 0.0f;
        for (u32 i = 0; i < slot.remembered; ++i)
        {
            const AiPerceived& entry = slot.memory[i];
            const f32 distance = flatDistanceSquared(entry.position, slot.position);
            const bool better =
                slot.target < 0 || (entry.current && !targetCurrent) ||
                (entry.current == targetCurrent &&
                 (entry.age < targetAge || (entry.age == targetAge && distance < targetDistance)));
            if (better)
            {
                slot.target = static_cast<i32>(i);
                targetCurrent = entry.current;
                targetAge = entry.age;
                targetDistance = distance;
            }
        }

        if (slot.generation == 0)
        {
            return;
        }
        const AiPerceived* target =
            slot.target >= 0 ? &slot.memory[static_cast<usize>(slot.target)] : nullptr;
        if (slot.targetKey >= 0)
        {
            BlackboardValue& value = slot.state.blackboard[static_cast<usize>(slot.targetKey)];
            if (target != nullptr && target->entity != kNullEntity)
            {
                value.entity = target->entity;
                value.set = true;
            }
            else if (slot.hadTarget)
            {
                value.entity = kNullEntity;
                value.set = false;
            }
        }
        if (slot.positionKey >= 0 && target != nullptr)
        {
            BlackboardValue& value = slot.state.blackboard[static_cast<usize>(slot.positionKey)];
            value.vector = target->position;
            value.set = true;
        }
        slot.hadTarget = target != nullptr && target->entity != kNullEntity;
    }

    // --- The update -----------------------------------------------------------------------------------

    void think(Slot& slot, f32 seconds) noexcept
    {
        if (!slot.used || !slot.active)
        {
            return;
        }
        if (slot.hasPerception)
        {
            perceive(slot, seconds);
        }
        if (!slot.hasAgent || !slot.enabled || slot.generation == 0)
        {
            return;
        }
        f32 step = seconds;
        if (slot.interval > 0.0f)
        {
            slot.elapsed += seconds;
            slot.timer -= seconds;
            if (slot.timer > 0.0f)
            {
                return;
            }
            step = slot.elapsed;
            slot.elapsed = 0.0f;
            slot.timer += slot.interval;
            if (slot.timer <= 0.0f)
            {
                slot.timer = slot.interval;
            }
        }
        Context context(*this, slot);
        slot.status = tickBehaviorTree(slot.tree->tree, slot.state, step, context);
        slot.running = runningNode(slot.tree->tree, slot.state);
        slot.ticked = true;
    }

    void applyMove(Slot& slot)
    {
        const MoveRequest move = std::exchange(slot.move, MoveRequest::None);
        if (move == MoveRequest::Stop)
        {
            if (slot.hasNav)
            {
                static_cast<void>(navigation->stop(slot.entity));
            }
            return;
        }
        if (move != MoveRequest::Move)
        {
            return;
        }
        if (!slot.hasNav)
        {
            slot.moveStatus = BehaviorStatus::Failure;
            return;
        }
        Vec3 target = slot.moveTarget;
        if (slot.moveWander)
        {
            NavQueryFilter filter;
            filter.agentType = slot.agentType;
            filter.areaMask = slot.areaMask;
            const Expected<Vec3, NavError> point =
                navigation->randomPoint(slot.wanderCenter, slot.wanderRadius, slot.wanderSeed, filter);
            if (!point)
            {
                slot.moveStatus = BehaviorStatus::Failure;
                return;
            }
            target = *point;
        }
        if (!navigation->setDestination(slot.entity, target))
        {
            slot.moveStatus = BehaviorStatus::Failure;
            return;
        }
        slot.activeTarget = target;
        slot.activeAcceptance = slot.moveAcceptance;
    }

    void apply()
    {
        ILL_PROFILE_SCOPE_N("AiWorld::apply");
        actions.clear();
        for (Slot& slot : slots)
        {
            if (!slot.used)
            {
                continue;
            }
            statistics.ticked += std::exchange(slot.ticked, false) ? 1u : 0u;
            applyMove(slot);
            for (const AiActionRequest& request : slot.requests)
            {
                if (actions.size() < desc.maxActions)
                {
                    actions.push_back(request);
                }
                else
                {
                    ++statistics.droppedActions;
                }
            }
            statistics.droppedActions += std::exchange(slot.droppedRequests, 0u);
            slot.requests.clear();
            if (AiAgentState* state = world->tryGet<AiAgentState>(slot.entity))
            {
                state->status = slot.status;
                state->runningNode = slot.running;
                state->target =
                    slot.target >= 0 ? slot.memory[static_cast<usize>(slot.target)].entity : kNullEntity;
            }
        }
    }

    template <class F>
    void forSlots(JobSystem* jobs, F&& work)
    {
        const auto count = static_cast<u32>(slots.size());
        const auto range = [this, &work](u32 begin, u32 end) {
            for (u32 i = begin; i < end; ++i)
            {
                work(slots[i]);
            }
        };
        if (jobs != nullptr && count > kSlotsPerJob)
        {
            jobs->parallelFor(count, kSlotsPerJob, range);
        }
        else
        {
            range(0, count);
        }
    }

    void update(f32 deltaSeconds, JobSystem* jobs, const PhysicsWorld* physicsWorld, bool advance)
    {
        statistics.ticked = 0;
        statistics.pending = 0;
        statistics.rays = 0;
        if (!reloads.empty())
        {
            startReloads();
        }
        if (!treeLoads.empty())
        {
            advanceLoads();
        }
        {
            ILL_PROFILE_SCOPE_N("AiWorld::sync");
            syncSlots();
            for (u32 type = 0; type < kMaxNavAgentTypes; ++type)
            {
                navReady[type] = navigation->isReady(type);
            }
            for (Slot& slot : slots)
            {
                if (slot.used)
                {
                    readSlot(slot);
                }
            }
        }
        statistics.agents = static_cast<u32>(slots.size() - freeSlots.size());
        if (!advance)
        {
            return;
        }
        statistics.droppedActions = 0;
        const f32 seconds = std::isfinite(deltaSeconds) ? std::max(deltaSeconds, 0.0f) : 0.0f;
        noises.swap(pendingNoises);
        pendingNoises.clear();
        {
            ILL_PROFILE_SCOPE_N("AiWorld::perceive");
            collectStimuli();
            forSlots(jobs, [this](Slot& slot) { gather(slot); });
            physics = physicsWorld;
            if (physics != nullptr)
            {
                castRays();
            }
            else
            {
                for (Candidate& candidate : candidates)
                {
                    candidate.sight = LineOfSight::Visible;
                }
            }
            physics = nullptr;
        }
        {
            ILL_PROFILE_SCOPE_N("AiWorld::think");
            forSlots(jobs, [this, seconds](Slot& slot) { think(slot, seconds); });
        }
        apply();
    }

    // --- Debug drawing --------------------------------------------------------------------------------

    static void circle(NavDebugSink& sink, const Vec3& center, f32 radius, const Vec4& color)
    {
        Vec3 last = center + Vec3(radius, 0.0f, 0.0f);
        for (u32 i = 1; i <= kCircleSegments; ++i)
        {
            const f32 angle =
                2.0f * std::numbers::pi_v<f32> * static_cast<f32>(i) / static_cast<f32>(kCircleSegments);
            const Vec3 next = center + Vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
            sink.line(last, next, color);
            last = next;
        }
    }

    static void cone(NavDebugSink& sink, const Slot& slot)
    {
        const f32 radius = slot.perception.sightRadius;
        const f32 angle = slot.perception.sightAngle;
        if (!(radius > 0.0f) || !std::isfinite(radius) || !(angle > 0.0f))
        {
            return;
        }
        if (angle >= 2.0f * std::numbers::pi_v<f32>)
        {
            circle(sink, slot.eye, radius, kSightColor);
            return;
        }
        const f32 yaw = std::atan2(slot.forward.x, slot.forward.z);
        const auto at = [&](f32 a) {
            return slot.eye + Vec3(std::sin(yaw + a) * radius, 0.0f, std::cos(yaw + a) * radius);
        };
        const f32 half = angle * 0.5f;
        Vec3 last = at(-half);
        sink.line(slot.eye, last, kSightColor);
        for (u32 i = 1; i <= kCircleSegments; ++i)
        {
            const Vec3 next = at(-half + angle * static_cast<f32>(i) / static_cast<f32>(kCircleSegments));
            sink.line(last, next, kSightColor);
            last = next;
        }
        sink.line(last, slot.eye, kSightColor);
    }
};

AiWorld::AiWorld(World& world, NavWorld& navigation, AssetStreamer* streamer, const AiWorldDesc& desc)
    : m_impl(std::make_unique<Impl>())
{
    Impl& impl = *m_impl;
    impl.world = &world;
    impl.navigation = &navigation;
    impl.streamer = streamer;
    impl.desc = desc;
    impl.slots.reserve(desc.maxAgents);
    impl.freeSlots.reserve(desc.maxAgents);
    impl.added.reserve(desc.maxAgents);
    impl.stimuli.reserve(desc.maxAgents);
    impl.bucketStart.reserve(static_cast<usize>(desc.maxAgents) * 4 + 17);
    impl.bucketFill.reserve(static_cast<usize>(desc.maxAgents) * 4 + 16);
    impl.bucketItems.reserve(desc.maxAgents);
    impl.candidates.resize(static_cast<usize>(desc.maxAgents) * desc.maxSightCandidates);
    impl.noises.reserve(desc.maxNoisesPerUpdate);
    impl.pendingNoises.reserve(desc.maxNoisesPerUpdate);
    impl.actions.reserve(desc.maxActions);
    if (streamer != nullptr)
    {
        impl.reloaded = streamer->events().subscribe<AssetReloadedEvent>(
            [&impl](const AssetReloadedEvent& event) { impl.reloads.push_back(event.asset); });
    }
}

AiWorld::~AiWorld() = default;

void AiWorld::update(f32 seconds, JobSystem* jobs, const PhysicsWorld* physics, bool advance)
{
    ILL_PROFILE_SCOPE();
    m_impl->update(seconds, jobs, physics, advance);
}

void AiWorld::waitIdle()
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.update(0.0f, nullptr, nullptr, false);
    while (impl.streamer != nullptr && !impl.treeLoads.empty())
    {
        impl.streamer->waitIdle();
        impl.update(0.0f, nullptr, nullptr, false);
    }
}

Expected<void, AiError> AiWorld::reportNoise(const AiNoise& noise)
{
    Impl& impl = *m_impl;
    if (!finite(noise.position) || !std::isfinite(noise.radius) || noise.radius < 0.0f ||
        noise.team >= kMaxAiTeams)
    {
        return unexpected(AiError::InvalidValue);
    }
    if (impl.pendingNoises.size() >= impl.desc.maxNoisesPerUpdate)
    {
        return unexpected(AiError::Full);
    }
    impl.pendingNoises.push_back(noise);
    return {};
}

namespace
{

// The slot of @p entity whose tree is bound, or why there is none.
template <class ImplT>
auto boundSlot(ImplT& impl, Entity entity) -> Expected<decltype(impl.slotFor(entity)), AiError>
{
    const World& world = *impl.world;
    if (!world.isAlive(entity) || (!world.has<BehaviorAgent>(entity) && !world.has<AiPerception>(entity)))
    {
        return unexpected(AiError::NoAgent);
    }
    auto* slot = impl.slotFor(entity);
    if (slot == nullptr || !slot->hasAgent || slot->tree == nullptr || slot->generation == 0)
    {
        return unexpected(AiError::NoTree);
    }
    return slot;
}

} // namespace

Expected<void, AiError> AiWorld::setValue(Entity entity, std::string_view key, const BlackboardValue& value)
{
    const Expected<Slot*, AiError> found = boundSlot(*m_impl, entity);
    if (!found)
    {
        return unexpected(found.error());
    }
    Slot& slot = **found;
    const i32 index = slot.tree->tree.findKey(key);
    if (index < 0)
    {
        return unexpected(AiError::UnknownKey);
    }
    BlackboardValue& entry = slot.state.blackboard[static_cast<usize>(index)];
    if (entry.type != value.type)
    {
        return unexpected(AiError::TypeMismatch);
    }
    if ((value.type == BlackboardType::Float && !std::isfinite(value.number)) ||
        (value.type == BlackboardType::Vector3 && value.set && !finite(value.vector)))
    {
        return unexpected(AiError::InvalidValue);
    }
    entry = value;
    switch (value.type)
    {
    case BlackboardType::Bool:
    case BlackboardType::Int:
    case BlackboardType::Float:
        entry.set = true;
        break;
    case BlackboardType::Vector3:
        break;
    case BlackboardType::Entity:
        entry.set = value.set && value.entity != kNullEntity;
        break;
    }
    return {};
}

Expected<BlackboardValue, AiError> AiWorld::value(Entity entity, std::string_view key) const
{
    const Impl& impl = *m_impl;
    const Expected<const Slot*, AiError> found = boundSlot(impl, entity);
    if (!found)
    {
        return unexpected(found.error());
    }
    const Slot& slot = **found;
    const i32 index = slot.tree->tree.findKey(key);
    if (index < 0)
    {
        return unexpected(AiError::UnknownKey);
    }
    return slot.state.blackboard[static_cast<usize>(index)];
}

std::span<const AiActionRequest> AiWorld::actions() const noexcept
{
    return m_impl->actions;
}

Expected<void, AiError> AiWorld::finishAction(Entity entity, u32 action, bool success)
{
    const Expected<Slot*, AiError> found = boundSlot(*m_impl, entity);
    if (!found)
    {
        return unexpected(found.error());
    }
    for (ActionRecord& record : (*found)->actions)
    {
        if (record.id == action && record.status == BehaviorStatus::Running)
        {
            record.status = success ? BehaviorStatus::Success : BehaviorStatus::Failure;
            return {};
        }
    }
    return unexpected(AiError::UnknownAction);
}

Expected<u32, AiError> AiWorld::perceived(Entity entity, std::span<AiPerceived> out) const
{
    const Impl& impl = *m_impl;
    const World& world = *impl.world;
    if (!world.isAlive(entity) || (!world.has<BehaviorAgent>(entity) && !world.has<AiPerception>(entity)))
    {
        return unexpected(AiError::NoAgent);
    }
    const Slot* slot = impl.slotFor(entity);
    if (slot == nullptr)
    {
        return 0u;
    }
    std::array<u32, kMaxPerceived> order{};
    std::array<f32, kMaxPerceived> distances{};
    for (u32 i = 0; i < slot->remembered; ++i)
    {
        distances[i] = flatDistanceSquared(slot->memory[i].position, slot->position);
        u32 at = i;
        while (at > 0 && distances[i] < distances[order[at - 1]])
        {
            order[at] = order[at - 1];
            --at;
        }
        order[at] = i;
    }
    const u32 written = std::min(slot->remembered, static_cast<u32>(out.size()));
    for (u32 i = 0; i < written; ++i)
    {
        out[i] = slot->memory[order[i]];
    }
    return slot->remembered;
}

Expected<void, AiError> AiWorld::restart(Entity entity)
{
    Impl& impl = *m_impl;
    const Expected<Slot*, AiError> found = boundSlot(impl, entity);
    if (!found)
    {
        return unexpected(found.error());
    }
    const BehaviorAgent* agent = impl.world->tryGet<BehaviorAgent>(entity);
    if (agent == nullptr)
    {
        return unexpected(AiError::NoAgent);
    }
    impl.restartTree(**found, *agent);
    return {};
}

std::optional<u32> AiWorld::findSlot(Entity entity) const noexcept
{
    const u32 index = m_impl->slotIndex(entity);
    return index != kNoSlot ? std::optional<u32>(index) : std::nullopt;
}

const BehaviorTree* AiWorld::tree(u32 slot) const noexcept
{
    const Impl& impl = *m_impl;
    if (slot >= impl.slots.size())
    {
        return nullptr;
    }
    const Slot& entry = impl.slots[slot];
    return entry.used && entry.tree != nullptr && entry.generation != 0 ? &entry.tree->tree : nullptr;
}

const BehaviorTreeState* AiWorld::treeState(u32 slot) const noexcept
{
    return tree(slot) != nullptr ? &m_impl->slots[slot].state : nullptr;
}

void AiWorld::drawDebug(const AiDebugOptions& options, NavDebugSink& sink) const
{
    ILL_PROFILE_SCOPE();
    const Impl& impl = *m_impl;
    for (const Slot& slot : impl.slots)
    {
        if (!slot.used || !slot.hasPerception || !slot.active)
        {
            continue;
        }
        if (options.sight)
        {
            Impl::cone(sink, slot);
        }
        for (u32 i = 0; i < slot.remembered; ++i)
        {
            const AiPerceived& entry = slot.memory[i];
            if (options.sight && entry.current && entry.sense == AiSense::Sight)
            {
                sink.line(slot.eye, entry.position, kSeenColor);
            }
            if (options.memory && !entry.current)
            {
                const f32 size = 0.25f;
                sink.line(entry.position - Vec3(size, 0.0f, 0.0f), entry.position + Vec3(size, 0.0f, 0.0f),
                          kMemoryColor);
                sink.line(entry.position - Vec3(0.0f, 0.0f, size), entry.position + Vec3(0.0f, 0.0f, size),
                          kMemoryColor);
                sink.line(entry.position, entry.position + Vec3(0.0f, 2.0f * size, 0.0f), kMemoryColor);
            }
        }
    }
    if (options.hearing)
    {
        for (const AiNoise& noise : impl.noises)
        {
            Impl::circle(sink, noise.position, noise.radius, kHearingColor);
        }
    }
}

const AiStatistics& AiWorld::statistics() const noexcept
{
    return m_impl->statistics;
}

World& AiWorld::world() const noexcept
{
    return *m_impl->world;
}

} // namespace illusion
