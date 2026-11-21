#include "PoseDetail.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

// What keeps a BoneAttachment from following its joint; each is logged once per Entity.
enum class SocketProblem : u8
{
    None,
    NoAnimator, // no parent, or a parent without an Animator
    NoJoint,
};

// A BoneAttachment's joint, resolved against its parent's skeleton.
struct Socket
{
    std::string joint;                  // the BoneAttachment::joint it was resolved for
    const Skeleton* skeleton = nullptr; // the skeleton it was resolved against
    u64 revision = 0;                   // Impl::revision it was resolved with
    i32 index = -1;
    SocketProblem reported = SocketProblem::None;
    u64 mark = 0; // the update that last saw it
};

// Animators posed by one Job.
constexpr u32 kPosesPerJob = 8;

// Smallest per-worker scratch arena, in bytes.
constexpr usize kMinScratchBytes = usize{16} << 10;

// Decoder name of `.animgraph` files in the AssetStreamer.
constexpr std::string_view kGraphDecoder = "illusion.animgraph";

enum class Status : u8
{
    Loading,
    Ready,
    Failed,
};

// The parts of a model file that animation keeps, copied out so that the model itself can go.
struct SkeletonEntry
{
    Status status = Status::Loading;
    AssetId file;
    u32 index = 0; // ModelData::skins
    Skeleton skeleton;
};

struct ClipEntry
{
    Status status = Status::Loading;
    AssetId file;
    AnimationClip clip;
};

struct MeshEntry
{
    Status status = Status::Loading;
    AssetId file;
    i32 skeleton = -1; // MeshData::skeleton: an index into its model's skins
    std::vector<Aabb> jointBounds;
};

struct GraphEntry
{
    Status status = Status::Loading;
    std::string path;
    AssetHandle<AnimationGraph> load; // the running load
    bool loading = false;
    bool again = false; // the file changed again during the load
    u64 generation = 0; // advanced by every load that replaced graph
    AnimationGraph graph;
};

// A graph bound to one skeleton.
struct GraphBindingEntry
{
    Status status = Status::Loading;
    u64 generation = 0; // GraphEntry::generation it was bound for
    u64 revision = 0;   // Impl::revision it was bound with
    GraphBinding binding;
    u32 maxEvents = 0; // most events one step can cross
};

// What a graph Animator does with its state this update.
enum class GraphStep : u8
{
    Restart, // not advancing: the entry states at Animator::time
    Step,
    Hold,
};

struct BindingEntry
{
    const Skeleton* skeleton = nullptr;
    const AnimationClip* clip = nullptr;
    ClipBinding binding;
};

// A model file being loaded for the entries made from it.
struct FileLoad
{
    AssetId file;
    AssetHandle<ModelData> model;
    bool reload = false; // the file changed: every entry of it is filled again
    bool again = false;  // it changed again during the load
};

struct Slot
{
    Entity entity;
    bool used = false;
    bool resolved = false; // its assets are loaded (or failed)
    bool fresh = true;     // its range holds another pose: the next pose's previous equals current
    u64 mark = 0;          // the update that last saw it
    u64 written = 0;       // the update that last wrote its current skin matrices
    u64 revision = 0;      // Impl::revision it was resolved with
    AssetId skeletonId;
    AssetId clipId;
    AssetId meshId;
    u32 offset = 0; // first joint in the joint buffers
    u32 capacity = 0;
    u32 jointCount = 0;
    const Skeleton* skeleton = nullptr;
    const AnimationClip* clip = nullptr;
    const ClipBinding* binding = nullptr;
    const std::vector<Aabb>* jointBounds = nullptr; // null without a skinned mesh
    // Inputs and output of this update's pose.
    f32 time = 0.0f;
    Mat4 world{1.0f};
    AnimationPose* pose = nullptr;
    // Animator::graph.
    AssetId graphId;
    u64 graphGeneration = 0;             // GraphEntry::generation of graphState
    const GraphBinding* graph = nullptr; // null without a ready graph
    AnimationGraphState graphState;
    std::vector<GraphEvent> graphEvents; // sized to the binding's maxEvents
    u32 eventCount = 0;                  // events of this update
    GraphStep step = GraphStep::Hold;
    f32 graphSeconds = 0.0f;
    // The clip faded out after crossFade() (Animators without a graph).
    const AnimationClip* fadeClip = nullptr;
    const ClipBinding* fadeBinding = nullptr;
    f32 fadeTime = 0.0f; // seconds into fadeClip
    f32 fadeSpeed = 1.0f;
    bool fadeLoop = true;
    f32 fade = 0.0f; // seconds of the fade so far
    f32 fadeDuration = 0.0f;
};

struct JointRange
{
    u32 offset = 0;
    u32 capacity = 0;
};

// Animator::time after @p seconds of play; stops a clip that is not looped at its end.
f32 advanceTime(Animator& animator, const AnimationClip& clip, f32 seconds) noexcept
{
    const f32 time = animator.time + animator.speed * seconds;
    if (animator.loop)
    {
        return clipTime(clip, time, true);
    }
    const f32 clamped = clipTime(clip, time, false);
    if ((animator.speed > 0.0f && time >= clip.duration) || (animator.speed < 0.0f && time <= 0.0f))
    {
        animator.playing = false;
    }
    return clamped;
}

std::optional<AnimationGraph> decodeGraph(std::vector<std::byte>&& bytes, std::string& error)
{
    Expected<AnimationGraph, std::string> graph =
        parseAnimationGraph(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!graph)
    {
        error = std::move(graph.error());
        return std::nullopt;
    }
    return std::move(*graph);
}

// Calls @p visit with every clip reference of @p graph.
template <class F>
void forEachClip(const AnimationGraph& graph, F&& visit)
{
    for (const AnimationLayer& layer : graph.layers)
    {
        for (const AnimationState& state : layer.states)
        {
            if (state.motion.type == AnimationMotionType::Clip)
            {
                visit(state.motion.clip);
            }
            for (const AnimationBlendPoint& point : state.motion.points)
            {
                visit(point.clip);
            }
        }
    }
}

// Most events one stepGraph() of @p graph writes: per layer, the events of a fading and a current state.
u32 eventBound(const AnimationGraph& graph) noexcept
{
    u32 bound = 0;
    for (const AnimationLayer& layer : graph.layers)
    {
        usize most = 0;
        for (const AnimationState& state : layer.states)
        {
            most = std::max(most, state.events.size());
        }
        bound += static_cast<u32>(2 * most);
    }
    return bound;
}

} // namespace

struct AnimationWorld::Impl
{
    World* world = nullptr;
    AssetStreamer* streamer = nullptr;
    AnimationStatistics statistics;
    u64 frame = 0;
    u64 revision = 1; // advanced when a reload changes entries: every slot resolves again
    std::unordered_map<u64, SkeletonEntry> skeletons;
    std::unordered_map<u64, ClipEntry> clips;
    std::unordered_map<u64, MeshEntry> meshes;
    std::unordered_map<u64, BindingEntry> bindings;
    std::unordered_set<u64> mismatches; // skeleton and mesh pairs reported
    std::vector<FileLoad> loading;
    std::unordered_map<u64, std::string> filePaths; // of the files entries were made from
    std::vector<AssetId> reloads;                   // files reloaded since the last update()
    Subscription reloaded;
    std::vector<Slot> slots;
    std::vector<u32> freeSlots;
    std::vector<JointRange> freeRanges;
    std::vector<JointPose> locals;
    std::vector<Mat4> models;
    std::array<std::vector<SkinMatrix>, 2> skins; // [frame & 1] is the current update's
    std::unordered_map<u64, GraphEntry> graphs;
    std::vector<u64> graphLoads;                              // graphs with a running load
    std::unordered_map<u64, GraphBindingEntry> graphBindings; // by graph and skeleton
    std::vector<AnimationEvent> events;                       // of the last update
    usize eventCapacity = 0;                                  // sum of the slots' event bounds
    std::vector<std::unique_ptr<LinearAllocator>> arenas;     // per worker, the last for other threads
    usize arenaBytes = kMinScratchBytes;
    std::vector<Entity> scratch;
    std::vector<u32> posed;
    std::unordered_map<u64, Socket> sockets; // by the attached Entity's Entity::toBits()
    // MeshRenderer::mesh, read through reflection: engine/render links this library.
    ComponentId meshRenderer = kInvalidComponentId;
    u32 meshOffset = 0;
    u32 registryCount = 0; // ComponentRegistry::count() when meshRenderer was last looked up

    [[nodiscard]] u32 current() const noexcept { return static_cast<u32>(frame & 1u); }

    // MeshRenderer::mesh of @p entity, or an empty id.
    AssetId meshOf(Entity entity)
    {
        const ComponentRegistry& registry = world->registry();
        if (meshRenderer == kInvalidComponentId && registry.count() != registryCount)
        {
            registryCount = registry.count();
            const ComponentId id = registry.findByStableName("illusion.MeshRenderer");
            const TypeReflection* type = id != kInvalidComponentId ? registry.reflection(id) : nullptr;
            const FieldInfo* field = type != nullptr ? type->field("mesh") : nullptr;
            if (field != nullptr && field->type == FieldType::Asset)
            {
                meshRenderer = id;
                meshOffset = field->offset;
            }
        }
        if (meshRenderer == kInvalidComponentId)
        {
            return {};
        }
        const void* data = world->componentData(entity, meshRenderer);
        if (data == nullptr)
        {
            return {};
        }
        return reinterpret_cast<const AssetRef*>(static_cast<const std::byte*>(data) + meshOffset)->id;
    }

    void fail(Status& status, AssetId asset, std::string_view message)
    {
        status = Status::Failed;
        ++statistics.failures;
        ILL_LOG_WARN("Animation", "cannot use '{}': {}", AssetPathTable::instance().path(asset), message);
    }

    // Loads @p file unless a load of it runs.
    void requestFile(AssetId file, std::string_view path)
    {
        filePaths.try_emplace(file.value, path);
        if (std::ranges::find(loading, file, &FileLoad::file) == loading.end())
        {
            loading.push_back(FileLoad{file, streamer->loadModel(path), false, false});
        }
    }

    SkeletonEntry& requestSkeleton(AssetId id)
    {
        const auto [found, inserted] = skeletons.try_emplace(id.value);
        SkeletonEntry& entry = found->second;
        if (inserted)
        {
            const std::optional<AssetPathParts> parts = splitAssetPath(AssetPathTable::instance().path(id));
            if (!parts || parts->fragment != AssetFragment::Skin || isOnBuiltinMount(parts->file))
            {
                fail(entry.status, id, "not a skeleton (model.glb#skin/N)");
                return entry;
            }
            entry.file = AssetId::fromPath(parts->file);
            entry.index = parts->index;
            requestFile(entry.file, parts->file);
        }
        return entry;
    }

    ClipEntry& requestClip(AssetId id)
    {
        const auto [found, inserted] = clips.try_emplace(id.value);
        ClipEntry& entry = found->second;
        if (inserted)
        {
            const std::optional<AssetPathParts> parts = splitAssetPath(AssetPathTable::instance().path(id));
            if (!parts || parts->fragment != AssetFragment::Animation || isOnBuiltinMount(parts->file))
            {
                fail(entry.status, id, "not an animation clip (model.glb#animation/<name>)");
                return entry;
            }
            entry.file = AssetId::fromPath(parts->file);
            requestFile(entry.file, parts->file);
        }
        return entry;
    }

    MeshEntry& requestMesh(AssetId id)
    {
        const auto [found, inserted] = meshes.try_emplace(id.value);
        MeshEntry& entry = found->second;
        if (inserted)
        {
            const std::optional<AssetPathParts> parts = splitAssetPath(AssetPathTable::instance().path(id));
            const bool mesh =
                parts && (parts->fragment == AssetFragment::None || parts->fragment == AssetFragment::Mesh ||
                          parts->fragment == AssetFragment::Node);
            if (!mesh)
            {
                entry.status = Status::Failed; // the render resolver reports it
            }
            else if (isOnBuiltinMount(parts->file))
            {
                entry.status = Status::Ready; // built-in meshes are rigid
            }
            else
            {
                entry.file = AssetId::fromPath(parts->file);
                requestFile(entry.file, parts->file);
            }
        }
        return entry;
    }

    const ClipBinding& bindingFor(AssetId skeletonId, const Skeleton& skeleton, AssetId clipId,
                                  const AnimationClip& clip)
    {
        const auto [found, inserted] = bindings.try_emplace(hashCombine(skeletonId.value, clipId.value));
        if (inserted)
        {
            found->second = BindingEntry{&skeleton, &clip, bindClip(clip, skeleton)};
        }
        return found->second.binding;
    }

    // Fills the entries of @p file from @p model (null when it failed to load); a reload keeps what it
    // cannot replace.
    void fill(AssetId file, const ModelData* model, std::string_view error, bool reload)
    {
        const auto filePath = filePaths.find(file.value);
        if (model != nullptr && filePath != filePaths.end())
        {
            // Every clip of the file is kept, so that crossFade() to another one needs no load.
            for (const AnimationClip& clip : model->animations)
            {
                const AssetId id =
                    AssetPathTable::instance().intern(filePath->second + "#animation/" + clip.name);
                if (id)
                {
                    const auto [found, inserted] = clips.try_emplace(id.value);
                    if (inserted)
                    {
                        found->second.file = file;
                    }
                }
            }
        }
        for (auto& [key, entry] : skeletons)
        {
            if (entry.file != file || (entry.status != Status::Loading && !reload))
            {
                continue;
            }
            const bool keep = entry.status == Status::Ready;
            if (model != nullptr && entry.index < model->skins.size())
            {
                entry.skeleton = model->skins[entry.index];
                entry.status = Status::Ready;
            }
            else if (!keep)
            {
                fail(entry.status, AssetId{key}, model == nullptr ? error : "the model has no such skin");
            }
        }
        for (auto& [key, entry] : clips)
        {
            if (entry.file != file || (entry.status != Status::Loading && !reload))
            {
                continue;
            }
            const bool keep = entry.status == Status::Ready;
            const std::optional<AssetPathParts> parts =
                splitAssetPath(AssetPathTable::instance().path(AssetId{key}));
            const auto found = model != nullptr
                                   ? std::ranges::find(model->animations, parts->node, &AnimationClip::name)
                                   : std::vector<AnimationClip>::const_iterator{};
            if (model != nullptr && found != model->animations.end())
            {
                entry.clip = *found;
                entry.status = Status::Ready;
            }
            else if (!keep)
            {
                fail(entry.status, AssetId{key},
                     model == nullptr ? error : "the model has no such animation");
            }
        }
        for (auto& [key, entry] : meshes)
        {
            if (entry.file != file || (entry.status != Status::Loading && !reload))
            {
                continue;
            }
            const bool keep = entry.status == Status::Ready;
            const std::optional<AssetPathParts> parts =
                splitAssetPath(AssetPathTable::instance().path(AssetId{key}));
            i32 index = parts->fragment == AssetFragment::Mesh ? static_cast<i32>(parts->index) : 0;
            if (model != nullptr && parts->fragment == AssetFragment::Node)
            {
                const auto node = std::ranges::find(model->nodes, parts->node, &ModelNode::name);
                index = node != model->nodes.end() ? node->mesh : -1;
            }
            if (model != nullptr && index >= 0 && static_cast<usize>(index) < model->meshes.size())
            {
                const MeshData& mesh = model->meshes[static_cast<usize>(index)];
                entry.skeleton = mesh.skeleton;
                entry.jointBounds = mesh.jointBounds;
                entry.status = Status::Ready;
            }
            else if (!keep)
            {
                entry.status = Status::Failed;
            }
        }
        if (reload)
        {
            for (auto& [key, entry] : bindings)
            {
                entry.binding = bindClip(*entry.clip, *entry.skeleton);
            }
            ++revision;
        }
    }

    void startGraphLoad(u64 key, GraphEntry& entry)
    {
        entry.load = streamer->loadFile<AnimationGraph>(entry.path, kGraphDecoder, decodeGraph);
        if (!entry.loading)
        {
            entry.loading = true;
            graphLoads.push_back(key);
        }
    }

    GraphEntry& requestGraph(AssetId id)
    {
        const auto [found, inserted] = graphs.try_emplace(id.value);
        GraphEntry& entry = found->second;
        if (inserted)
        {
            const std::string_view path = AssetPathTable::instance().path(id);
            const std::optional<AssetPathParts> parts = splitAssetPath(path);
            if (!parts || parts->fragment != AssetFragment::None ||
                !path.ends_with(kAnimationGraphExtension) || isOnBuiltinMount(path))
            {
                fail(entry.status, id, "not an animation graph (.animgraph)");
                return entry;
            }
            entry.path = path;
            startGraphLoad(id.value, entry);
        }
        return entry;
    }

    // The Animators of the graph @p key are bound and started again by their next update.
    void restartGraphSlots(u64 key) noexcept
    {
        for (Slot& slot : slots)
        {
            if (slot.used && slot.graphId.value == key)
            {
                slot.resolved = false;
                slot.graph = nullptr;
            }
        }
    }

    void advanceGraphLoads()
    {
        for (usize i = 0; i < graphLoads.size();)
        {
            const u64 key = graphLoads[i];
            GraphEntry& entry = graphs.at(key);
            if (entry.load.state() == AssetState::Loading)
            {
                ++i;
                continue;
            }
            if (const AnimationGraph* loaded = entry.load.get())
            {
                entry.graph = *loaded;
                entry.status = Status::Ready;
                ++entry.generation;
                restartGraphSlots(key);
            }
            else if (entry.status == Status::Ready)
            {
                ILL_LOG_WARN("Animation", "cannot reload '{}': {}", entry.path, entry.load.error());
            }
            else
            {
                fail(entry.status, AssetId{key}, entry.load.error());
            }
            entry.load = {};
            if (entry.again)
            {
                entry.again = false;
                entry.load = streamer->loadFile<AnimationGraph>(entry.path, kGraphDecoder, decodeGraph);
                ++i;
                continue;
            }
            entry.loading = false;
            graphLoads[i] = graphLoads.back();
            graphLoads.pop_back();
        }
    }

    // @p graph bound to @p skeleton; bound again after the graph or a model reloaded.
    GraphBindingEntry& graphBindingFor(AssetId graphId, const GraphEntry& graph, AssetId skeletonId,
                                       const Skeleton& skeleton)
    {
        GraphBindingEntry& entry = graphBindings[hashCombine(graphId.value, skeletonId.value)];
        if (entry.status != Status::Loading && entry.generation == graph.generation &&
            entry.revision == revision)
        {
            return entry;
        }
        entry.generation = graph.generation;
        entry.revision = revision;
        Expected<GraphBinding, std::string> bound =
            bindGraph(graph.graph, skeleton, [this](const AssetRef& ref) -> const AnimationClip* {
                const auto found = clips.find(ref.id.value);
                return found != clips.end() && found->second.status == Status::Ready ? &found->second.clip
                                                                                     : nullptr;
            });
        if (!bound)
        {
            entry.binding = {};
            entry.maxEvents = 0;
            fail(entry.status, graphId, bound.error());
            return entry;
        }
        entry.binding = std::move(*bound);
        entry.status = Status::Ready;
        entry.maxEvents = eventBound(graph.graph);
        arenaBytes = std::max(arenaBytes, graphScratchBytes(entry.binding));
        return entry;
    }

    // Puts @p slot's graph back in its entry states at @p seconds, with the default parameters. Does not
    // allocate: the state has the binding's shape.
    static void restartGraph(Slot& slot, f32 seconds) noexcept
    {
        const AnimationGraph& graph = *slot.graph->graph;
        AnimationGraphState& state = slot.graphState;
        for (usize p = 0; p < graph.parameters.size(); ++p)
        {
            const bool trigger = graph.parameters[p].type == AnimationParameterType::Trigger;
            state.parameters[p] = trigger ? 0.0f : graph.parameters[p].defaultValue;
            state.triggers[p] = trigger ? u8{0} : AnimationGraphState::kNotTrigger;
        }
        for (usize l = 0; l < graph.layers.size(); ++l)
        {
            GraphLayerState& layer = state.layers[l];
            layer = GraphLayerState{};
            layer.state = graph.layers[l].entry;
            const f32 duration = graphStateDuration(*slot.graph, state, static_cast<u32>(l), layer.state);
            layer.time =
                duration > 0.0f && std::isfinite(seconds) ? std::max(seconds, 0.0f) / duration : 0.0f;
            layer.lastTime = layer.time;
        }
    }

    // No motion since the last step: root motion and events see none.
    static void holdGraph(AnimationGraphState& state) noexcept
    {
        for (GraphLayerState& layer : state.layers)
        {
            layer.lastTime = layer.time;
            layer.lastPreviousTime = layer.previousTime;
        }
    }

    // The scratch arena of the calling thread.
    LinearAllocator& arena() noexcept
    {
        const usize last = arenas.size() - 1;
        const i32 worker = JobSystem::currentWorkerIndex();
        return *arenas[worker >= 0 && static_cast<usize>(worker) < last ? static_cast<usize>(worker) : last];
    }

    // One arena per worker of @p jobs and one for other threads, each of arenaBytes.
    void prepareArenas(JobSystem* jobs)
    {
        const usize count = (jobs != nullptr ? jobs->workerCount() : 0u) + 1u;
        if (arenas.size() >= count && arenas.front()->capacity() >= arenaBytes)
        {
            return;
        }
        const usize size = std::max(count, arenas.size());
        arenas.clear();
        for (usize i = 0; i < size; ++i)
        {
            arenas.push_back(std::make_unique<LinearAllocator>(arenaBytes));
        }
    }

    void advanceLoads()
    {
        for (usize i = 0; i < loading.size();)
        {
            FileLoad& load = loading[i];
            if (load.model.state() == AssetState::Loading)
            {
                ++i;
                continue;
            }
            fill(load.file, load.model.get(), load.model.error(), load.reload);
            if (load.again)
            {
                load.model = streamer->loadModel(filePaths.at(load.file.value));
                load.reload = true;
                load.again = false;
                ++i;
                continue;
            }
            loading[i] = std::move(loading.back());
            loading.pop_back();
        }
    }

    void startReloads()
    {
        std::vector<AssetId> files;
        files.swap(reloads);
        for (const AssetId file : files)
        {
            if (const auto graph = graphs.find(file.value); graph != graphs.end())
            {
                if (graph->second.loading)
                {
                    graph->second.again = true;
                }
                else if (!graph->second.path.empty())
                {
                    startGraphLoad(file.value, graph->second);
                }
                continue;
            }
            const auto running = std::ranges::find(loading, file, &FileLoad::file);
            if (running != loading.end())
            {
                running->again = true;
            }
            else if (const auto path = filePaths.find(file.value); path != filePaths.end())
            {
                loading.push_back(FileLoad{file, streamer->loadModel(path->second), true, false});
            }
        }
    }

    u32 acquireSlot(Entity entity)
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
            posed.reserve(slots.size());
        }
        Slot& slot = slots[index];
        slot.entity = entity;
        slot.used = true;
        slot.resolved = false;
        slot.fresh = true;
        slot.jointCount = 0;
        slot.mark = frame;
        slot.graphId = {};
        slot.graph = nullptr;
        slot.eventCount = 0;
        slot.fadeClip = nullptr;
        return index;
    }

    void releaseSlot(u32 index)
    {
        Slot& slot = slots[index];
        slot.used = false;
        slot.entity = Entity{};
        slot.pose = nullptr;
        freeSlots.push_back(index);
    }

    // The slot of @p entity's pose; a pose copied from another Entity gets a slot of its own.
    Slot& claim(Entity entity, AnimationPose& pose)
    {
        if (pose.slot >= slots.size() || !slots[pose.slot].used || slots[pose.slot].entity != entity)
        {
            pose.slot = acquireSlot(entity);
            pose.jointCount = 0;
        }
        Slot& slot = slots[pose.slot];
        slot.mark = frame;
        slot.pose = &pose;
        return slot;
    }

    // Makes room for @p joints joints in @p slot's range of the joint buffers.
    void reserveJoints(Slot& slot, u32 joints)
    {
        if (slot.capacity >= joints)
        {
            return;
        }
        if (slot.capacity != 0)
        {
            freeRanges.push_back(JointRange{slot.offset, slot.capacity});
        }
        const auto fits = std::ranges::find_if(
            freeRanges, [joints](const JointRange& range) { return range.capacity >= joints; });
        if (fits != freeRanges.end())
        {
            slot.offset = fits->offset;
            slot.capacity = fits->capacity;
            *fits = freeRanges.back();
            freeRanges.pop_back();
            return;
        }
        slot.offset = static_cast<u32>(locals.size());
        slot.capacity = joints;
        const usize size = locals.size() + joints;
        locals.resize(size);
        models.resize(size);
        skins[0].resize(size);
        skins[1].resize(size);
    }

    // Finds the assets of @p animator and @p mesh for @p slot; leaves it unresolved while they load.
    // Requests the clips of @p graph; true while one of them loads.
    bool graphClipsLoading(const AnimationGraph& graph)
    {
        bool waiting = false;
        forEachClip(graph, [&](const AssetRef& ref) {
            if (ref && requestClip(ref.id).status == Status::Loading)
            {
                waiting = true;
            }
        });
        return waiting;
    }

    void resolve(Slot& slot, const Animator& animator, AssetId mesh)
    {
        const bool sameSkeleton = slot.skeletonId == animator.skeleton.id && slot.revision == revision;
        const bool sameGraph = slot.skeletonId == animator.skeleton.id && slot.graphId == animator.graph.id;
        slot.skeletonId = animator.skeleton.id;
        slot.clipId = animator.clip.id;
        slot.graphId = animator.graph.id;
        slot.meshId = mesh;
        slot.revision = revision;
        slot.resolved = false;
        slot.jointCount = 0;
        slot.graph = nullptr;
        if (!animator.skeleton)
        {
            slot.resolved = true;
            return;
        }
        SkeletonEntry& skeleton = requestSkeleton(animator.skeleton.id);
        GraphEntry* graph = animator.graph ? &requestGraph(animator.graph.id) : nullptr;
        ClipEntry* clip = graph == nullptr && animator.clip ? &requestClip(animator.clip.id) : nullptr;
        MeshEntry* meshEntry = mesh ? &requestMesh(mesh) : nullptr;
        if (skeleton.status == Status::Loading || (clip != nullptr && clip->status == Status::Loading) ||
            (meshEntry != nullptr && meshEntry->status == Status::Loading) ||
            (graph != nullptr && graph->status == Status::Loading) ||
            (graph != nullptr && graph->status == Status::Ready && graphClipsLoading(graph->graph)))
        {
            return;
        }
        slot.resolved = true;
        if (!sameSkeleton)
        {
            slot.fadeClip = nullptr;
        }
        if (skeleton.status == Status::Failed)
        {
            return;
        }
        const auto joints = static_cast<u32>(skeleton.skeleton.joints.size());
        slot.jointBounds = nullptr;
        if (meshEntry != nullptr && meshEntry->status == Status::Ready)
        {
            const bool otherSkin = meshEntry->file == skeleton.file && meshEntry->skeleton >= 0 &&
                                   static_cast<u32>(meshEntry->skeleton) != skeleton.index;
            if (meshEntry->skeleton < 0 || otherSkin || meshEntry->jointBounds.size() != joints)
            {
                if (mismatches.insert(hashCombine(animator.skeleton.id.value, mesh.value)).second)
                {
                    ILL_LOG_WARN("Animation",
                                 "the skeleton '{}' ({} joints) does not fit the mesh '{}'; it is "
                                 "drawn in its bind pose",
                                 animator.skeleton.path(), joints, AssetPathTable::instance().path(mesh));
                }
                return;
            }
            slot.jointBounds = &meshEntry->jointBounds;
        }
        const bool clipReady = clip != nullptr && clip->status == Status::Ready;
        slot.skeleton = &skeleton.skeleton;
        slot.clip = clipReady ? &clip->clip : nullptr;
        slot.binding =
            clipReady ? &bindingFor(animator.skeleton.id, skeleton.skeleton, animator.clip.id, clip->clip)
                      : nullptr;
        const u32 offset = slot.offset;
        reserveJoints(slot, joints);
        slot.fresh = slot.fresh || !sameSkeleton || slot.offset != offset;
        slot.jointCount = joints;
        arenaBytes = std::max(arenaBytes, usize{joints} * sizeof(JointPose) + alignof(JointPose));
        if (graph != nullptr && graph->status == Status::Ready)
        {
            const GraphBindingEntry& binding =
                graphBindingFor(animator.graph.id, *graph, animator.skeleton.id, skeleton.skeleton);
            if (binding.status == Status::Ready)
            {
                attachGraph(slot, binding, *graph, sameGraph, animator.time);
            }
        }
    }

    void attachGraph(Slot& slot, const GraphBindingEntry& binding, const GraphEntry& graph, bool sameGraph,
                     f32 seconds)
    {
        slot.graph = &binding.binding;
        AnimationGraphState& state = slot.graphState;
        const bool fits = state.parameters.size() == graph.graph.parameters.size() &&
                          state.layers.size() == graph.graph.layers.size();
        if (!sameGraph || !fits || slot.graphGeneration != graph.generation)
        {
            state = makeGraphState(binding.binding);
            restartGraph(slot, seconds);
            slot.graphGeneration = graph.generation;
        }
        if (slot.graphEvents.size() < binding.maxEvents)
        {
            eventCapacity += binding.maxEvents - slot.graphEvents.size();
            slot.graphEvents.resize(binding.maxEvents);
            events.reserve(eventCapacity);
        }
        slot.eventCount = 0;
    }

    // Advances @p animator and queues its pose for evaluate(), unless it gets none.
    void prepare(Entity entity, Animator& animator, AnimationPose& pose, f32 seconds, bool advance)
    {
        Slot& slot = claim(entity, pose);
        const AssetId mesh = meshOf(entity);
        if (!slot.resolved || slot.revision != revision || slot.skeletonId != animator.skeleton.id ||
            slot.clipId != animator.clip.id || slot.meshId != mesh)
        {
            resolve(slot, animator, mesh);
        }
        if (!slot.resolved)
        {
            ++statistics.pending;
        }
        slot.eventCount = 0;
        if (slot.jointCount == 0)
        {
            pose.jointCount = 0;
            pose.rootTranslation = Vec3(0.0f);
            pose.rootYaw = 0.0f;
            return;
        }
        f32 time = 0.0f;
        if (slot.graph != nullptr)
        {
            time = animator.time;
            slot.step =
                !advance ? GraphStep::Restart : (animator.playing ? GraphStep::Step : GraphStep::Hold);
            slot.graphSeconds = seconds * animator.speed;
        }
        else if (slot.clip != nullptr)
        {
            const bool moving = advance && animator.playing;
            if (moving)
            {
                animator.time = advanceTime(animator, *slot.clip, seconds);
            }
            time = clipTime(*slot.clip, animator.time, animator.loop);
            if (slot.fadeClip != nullptr && moving)
            {
                slot.fade += seconds;
                slot.fadeTime =
                    clipTime(*slot.fadeClip, slot.fadeTime + slot.fadeSpeed * seconds, slot.fadeLoop);
            }
            if (slot.fadeClip != nullptr && !(slot.fade < slot.fadeDuration))
            {
                slot.fadeClip = nullptr;
            }
        }
        slot.time = time;
        const Transform* transform = world->tryGet<Transform>(entity);
        slot.world = transform != nullptr ? transform->matrix() : Mat4(1.0f);
        posed.push_back(pose.slot);
    }

    // An Inactive Entity keeps its pose: the current matrices repeat the previous ones.
    void keep(Entity entity, AnimationPose& pose)
    {
        Slot& slot = claim(entity, pose);
        slot.eventCount = 0;
        pose.rootTranslation = Vec3(0.0f);
        pose.rootYaw = 0.0f;
        if (slot.jointCount == 0 || pose.jointCount == 0 || slot.written + 1 != frame)
        {
            return;
        }
        const std::vector<SkinMatrix>& previous = skins[current() ^ 1u];
        std::copy_n(previous.begin() + slot.offset, slot.jointCount, skins[current()].begin() + slot.offset);
        slot.written = frame;
    }

    void evaluate(Slot& slot)
    {
        const u32 joints = slot.jointCount;
        const std::span<JointPose> localPose(locals.data() + slot.offset, joints);
        const std::span<Mat4> modelPose(models.data() + slot.offset, joints);
        const std::span<SkinMatrix> skinPose(skins[current()].data() + slot.offset, joints);
        Vec3 rootTranslation(0.0f);
        f32 rootYaw = 0.0f;
        if (slot.graph != nullptr)
        {
            switch (slot.step)
            {
            case GraphStep::Restart:
                restartGraph(slot, slot.time);
                break;
            case GraphStep::Step:
                slot.eventCount =
                    std::min(stepGraph(*slot.graph, slot.graphState, slot.graphSeconds, slot.graphEvents),
                             static_cast<u32>(slot.graphEvents.size()));
                break;
            case GraphStep::Hold:
                holdGraph(slot.graphState);
                break;
            }
            RootMotionDelta root;
            evaluateGraph(*slot.graph, slot.graphState, arena(), localPose, &root);
            const Mat3 basis(slot.world);
            rootTranslation = basis * root.translation;
            rootYaw = glm::determinant(basis) < 0.0f ? -root.yaw : root.yaw;
        }
        else if (slot.clip != nullptr)
        {
            sampleClip(*slot.clip, *slot.binding, *slot.skeleton, slot.time, localPose);
            if (slot.fadeClip != nullptr)
            {
                LinearAllocator& memory = arena();
                const LinearAllocator::Marker marker = memory.mark();
                JointPose* faded = memory.allocateArray<JointPose>(joints);
                ILL_CHECK(faded != nullptr, "the animation scratch arena is too small");
                const std::span<JointPose> fadedPose(faded, joints);
                sampleClip(*slot.fadeClip, *slot.fadeBinding, *slot.skeleton, slot.fadeTime, fadedPose);
                blendPoses(fadedPose, localPose, slot.fade / slot.fadeDuration, {}, localPose);
                memory.rewind(marker);
            }
        }
        else
        {
            restPose(*slot.skeleton, localPose);
        }
        slot.pose->rootTranslation = rootTranslation;
        slot.pose->rootYaw = rootYaw;
        localToModel(*slot.skeleton, localPose, modelPose);
        // The per-pose stages of ADR 0061 (spring bones, look-at, two-bone IK, ragdoll blend) change
        // modelPose here, in that order, before the skin matrices and bounds are taken from it.
        illusion::skinMatrices(*slot.skeleton, modelPose, skinPose);
        if (slot.fresh || slot.written + 1 != frame)
        {
            std::ranges::copy(skinPose, skins[current() ^ 1u].begin() + slot.offset);
            slot.fresh = false;
        }
        slot.written = frame;
        Aabb bounds;
        if (slot.jointBounds != nullptr)
        {
            bounds = skinnedBounds(*slot.jointBounds, skinPose);
        }
        else
        {
            for (const Mat4& joint : modelPose)
            {
                bounds.expand(Vec3(joint[3]));
            }
        }
        slot.pose->jointCount = joints;
        slot.pose->frame = frame;
        slot.pose->bounds = bounds.isValid() ? bounds.transformed(slot.world) : Aabb{};
    }

    void report(Socket& socket, SocketProblem problem, Entity entity, std::string_view joint)
    {
        if (socket.reported == problem)
        {
            return;
        }
        socket.reported = problem;
        if (problem == SocketProblem::NoAnimator)
        {
            ILL_LOG_WARN("Animation",
                         "entity {}: the parent of its BoneAttachment has no Animator; it stays where it is",
                         entity.index);
        }
        else
        {
            ILL_LOG_WARN("Animation",
                         "entity {}: the parent's skeleton has no joint '{}'; it stays where it is",
                         entity.index, joint);
        }
    }

    // The slot of @p parent's Animator if this update posed it, or null.
    [[nodiscard]] const Slot* posedSlot(Entity parent) const noexcept
    {
        const AnimationPose* pose = world->tryGet<AnimationPose>(parent);
        if (pose == nullptr || pose->slot >= slots.size())
        {
            return nullptr;
        }
        const Slot& slot = slots[pose->slot];
        return slot.used && slot.entity == parent && slot.jointCount > 0 && slot.written == frame ? &slot
                                                                                                  : nullptr;
    }

    // Places @p entity at its joint: the local transform is the joint's model-space transform times the
    // offset, and the world Transform follows from the parent's current one.
    void follow(Entity entity, const BoneAttachment& attachment, Entity parent, LocalTransform& local,
                Transform& transform)
    {
        Socket& socket = sockets[entity.toBits()];
        socket.mark = frame;
        if (!world->isAlive(parent) || !world->has<Animator>(parent))
        {
            report(socket, SocketProblem::NoAnimator, entity, attachment.joint);
            return;
        }
        const Slot* slot = posedSlot(parent);
        if (slot == nullptr)
        {
            return; // its model loads
        }
        if (socket.joint != attachment.joint)
        {
            socket.joint = attachment.joint;
            socket.skeleton = nullptr;
            socket.reported = SocketProblem::None;
        }
        if (socket.skeleton != slot->skeleton || socket.revision != revision)
        {
            socket.skeleton = slot->skeleton;
            socket.revision = revision;
            socket.index = detail::findJoint(*slot->skeleton, socket.joint);
        }
        if (socket.index < 0 || static_cast<u32>(socket.index) >= slot->jointCount)
        {
            report(socket, SocketProblem::NoJoint, entity, attachment.joint);
            return;
        }
        socket.reported = SocketProblem::None;
        const f32 length = glm::length(attachment.rotation);
        const Transform offset{attachment.translation,
                               length > 0.0f && std::isfinite(length) ? attachment.rotation / length
                                                                      : Quat{1.0f, 0.0f, 0.0f, 0.0f},
                               attachment.scale};
        const Mat4& joint = models[slot->offset + static_cast<u32>(socket.index)];
        const Transform placed = Transform::fromMatrix(joint * offset.matrix());
        local = LocalTransform::from(placed);
        const Transform* parentTransform = world->tryGet<Transform>(parent);
        transform = parentTransform != nullptr ? combine(*parentTransform, placed) : placed;
    }

    // Runs after every pose of the update is evaluated: moves the Entities attached to joints.
    void updateSockets()
    {
        ILL_PROFILE_SCOPE();
        usize seen = 0;
        world->query<const BoneAttachment, const Hierarchy, LocalTransform, Transform>()
            .without<Inactive>()
            .each([&](Entity entity, const BoneAttachment& attachment, const Hierarchy& hierarchy,
                      LocalTransform& local, Transform& transform) {
                ++seen;
                follow(entity, attachment, hierarchy.parent, local, transform);
            });
        world->query<const BoneAttachment>().without<Hierarchy, Inactive>().each(
            [&](Entity entity, const BoneAttachment& attachment) {
                ++seen;
                Socket& socket = sockets[entity.toBits()];
                socket.mark = frame;
                report(socket, SocketProblem::NoAnimator, entity, attachment.joint);
            });
        if (sockets.size() > seen)
        {
            std::erase_if(sockets, [this](const auto& entry) { return entry.second.mark != frame; });
        }
    }

    void update(f32 deltaSeconds, JobSystem* jobs, bool advance)
    {
        ++frame;
        statistics.posed = 0;
        statistics.pending = 0;
        statistics.joints = 0;
        if (!reloads.empty())
        {
            startReloads();
        }
        if (!loading.empty())
        {
            advanceLoads();
        }
        if (!graphLoads.empty())
        {
            advanceGraphLoads();
        }

        // Poses come and go outside the queries below; the scratch list keeps its capacity.
        scratch.clear();
        world->query<const Animator>().without<AnimationPose>().each(
            [this](Entity entity, const Animator&) { scratch.push_back(entity); });
        for (const Entity entity : scratch)
        {
            AnimationPose pose;
            pose.slot = acquireSlot(entity);
            world->emplace<AnimationPose>(entity, pose);
        }
        scratch.clear();
        world->query<const AnimationPose>().without<Animator>().each(
            [this](Entity entity, const AnimationPose&) { scratch.push_back(entity); });
        for (const Entity entity : scratch)
        {
            world->remove<AnimationPose>(entity);
        }

        const f32 seconds = std::max(deltaSeconds, 0.0f);
        posed.clear();
        world->query<AnimationPose>().with<Animator, Inactive>().each(
            [this](Entity entity, AnimationPose& pose) { keep(entity, pose); });
        world->query<Animator, AnimationPose>().without<Inactive>().each(
            [&](Entity entity, Animator& animator, AnimationPose& pose) {
                prepare(entity, animator, pose, seconds, advance);
            });
        for (u32 index = 0; index < slots.size(); ++index)
        {
            if (slots[index].used && slots[index].mark != frame)
            {
                releaseSlot(index);
            }
        }

        prepareArenas(jobs);
        const auto work = [this](u32 begin, u32 end) {
            for (u32 i = begin; i < end; ++i)
            {
                evaluate(slots[posed[i]]);
            }
        };
        const auto count = static_cast<u32>(posed.size());
        if (jobs != nullptr && count > kPosesPerJob)
        {
            jobs->parallelFor(count, kPosesPerJob, work);
        }
        else
        {
            work(0, count);
        }
        statistics.poses = static_cast<u32>(slots.size() - freeSlots.size());
        statistics.posed = count;
        events.clear();
        for (const u32 index : posed)
        {
            const Slot& slot = slots[index];
            statistics.joints += slot.jointCount;
            for (u32 e = 0; e < slot.eventCount; ++e)
            {
                const GraphEvent& event = slot.graphEvents[e];
                const AnimationLayer& layer = slot.graph->graph->layers[event.layer];
                const AnimationState& state = layer.states[event.state];
                events.push_back(
                    AnimationEvent{slot.entity, state.events[event.event].name, layer.name, state.name});
            }
        }
        updateSockets();
    }

    [[nodiscard]] Slot* slotOf(u32 index) noexcept
    {
        return index < slots.size() && slots[index].used ? &slots[index] : nullptr;
    }
};

AnimationWorld::AnimationWorld(World& world, AssetStreamer& streamer)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->world = &world;
    m_impl->streamer = &streamer;
    m_impl->reloaded = streamer.events().subscribe<AssetReloadedEvent>(
        [impl = m_impl.get()](const AssetReloadedEvent& event) { impl->reloads.push_back(event.asset); });
}

AnimationWorld::~AnimationWorld() = default;

void AnimationWorld::update(f32 deltaSeconds, JobSystem* jobs, bool advance)
{
    ILL_PROFILE_SCOPE();
    m_impl->update(deltaSeconds, jobs, advance);
}

void AnimationWorld::waitIdle()
{
    update(0.0f, nullptr, false);
    while (m_impl->statistics.pending > 0 || !m_impl->loading.empty() || !m_impl->graphLoads.empty())
    {
        m_impl->streamer->waitIdle();
        update(0.0f, nullptr, false);
    }
}

u64 AnimationWorld::frame() const noexcept
{
    return m_impl->frame;
}

std::span<const SkinMatrix> AnimationWorld::skinMatrices(u32 slot) const noexcept
{
    if (slot >= m_impl->slots.size())
    {
        return {};
    }
    const Slot& entry = m_impl->slots[slot];
    return {m_impl->skins[m_impl->current()].data() + entry.offset, entry.jointCount};
}

std::span<const SkinMatrix> AnimationWorld::previousSkinMatrices(u32 slot) const noexcept
{
    if (slot >= m_impl->slots.size())
    {
        return {};
    }
    const Slot& entry = m_impl->slots[slot];
    return {m_impl->skins[m_impl->current() ^ 1u].data() + entry.offset, entry.jointCount};
}

std::span<const Mat4> AnimationWorld::jointMatrices(u32 slot) const noexcept
{
    if (slot >= m_impl->slots.size())
    {
        return {};
    }
    const Slot& entry = m_impl->slots[slot];
    return {m_impl->models.data() + entry.offset, entry.jointCount};
}

const Skeleton* AnimationWorld::skeleton(u32 slot) const noexcept
{
    if (slot >= m_impl->slots.size() || m_impl->slots[slot].jointCount == 0)
    {
        return nullptr;
    }
    return m_impl->slots[slot].skeleton;
}

const AnimationStatistics& AnimationWorld::statistics() const noexcept
{
    return m_impl->statistics;
}

World& AnimationWorld::world() const noexcept
{
    return *m_impl->world;
}

std::optional<u32> AnimationWorld::findSlot(Entity entity) const noexcept
{
    const AnimationPose* pose = m_impl->world->tryGet<AnimationPose>(entity);
    if (pose == nullptr || pose->slot >= m_impl->slots.size())
    {
        return std::nullopt;
    }
    const Slot& slot = m_impl->slots[pose->slot];
    if (!slot.used || slot.entity != entity)
    {
        return std::nullopt;
    }
    return pose->slot;
}

const AnimationGraph* AnimationWorld::graph(u32 slot) const noexcept
{
    const Slot* entry = m_impl->slotOf(slot);
    return entry != nullptr && entry->graph != nullptr ? entry->graph->graph : nullptr;
}

const AnimationGraphState* AnimationWorld::graphState(u32 slot) const noexcept
{
    const Slot* entry = m_impl->slotOf(slot);
    return entry != nullptr && entry->graph != nullptr ? &entry->graphState : nullptr;
}

bool AnimationWorld::setParameter(u32 slot, i32 index, f32 value) noexcept
{
    Slot* entry = m_impl->slotOf(slot);
    if (entry == nullptr || entry->graph == nullptr)
    {
        return false;
    }
    entry->graphState.setValue(index, value);
    return true;
}

bool AnimationWorld::setTrigger(u32 slot, i32 index) noexcept
{
    Slot* entry = m_impl->slotOf(slot);
    if (entry == nullptr || entry->graph == nullptr)
    {
        return false;
    }
    entry->graphState.setTrigger(index);
    return true;
}

bool AnimationWorld::crossFade(Entity entity, const AssetRef& clip, f32 seconds)
{
    Animator* animator = m_impl->world->tryGet<Animator>(entity);
    if (animator == nullptr || animator->graph)
    {
        return false;
    }
    if (const std::optional<u32> index = findSlot(entity))
    {
        Slot& slot = m_impl->slots[*index];
        slot.fadeClip = nullptr;
        if (slot.clip != nullptr && slot.jointCount > 0 && seconds > 0.0f && std::isfinite(seconds))
        {
            slot.fadeClip = slot.clip;
            slot.fadeBinding = slot.binding;
            slot.fadeTime = slot.time;
            slot.fadeSpeed = animator->playing ? animator->speed : 0.0f;
            slot.fadeLoop = animator->loop;
            slot.fade = 0.0f;
            slot.fadeDuration = seconds;
        }
    }
    animator->clip = clip;
    animator->time = 0.0f;
    animator->playing = true;
    if (clip)
    {
        // Starts the load now, so that a clip of a model not loaded yet is ready sooner.
        static_cast<void>(m_impl->requestClip(clip.id));
    }
    return true;
}

std::span<const AnimationEvent> AnimationWorld::events() const noexcept
{
    return m_impl->events;
}

} // namespace illusion
