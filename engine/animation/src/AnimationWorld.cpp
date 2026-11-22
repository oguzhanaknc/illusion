#include "PoseDetail.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/InverseKinematics.hpp>
#include <illusion/animation/SpringBones.hpp>
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

// A name resolved to a joint of one skeleton, for a LookAt, a TwoBoneIk or a pelvis.
struct JointName
{
    std::string name;
    const Skeleton* skeleton = nullptr;
    u64 revision = 0;
    i32 index = -1;
    bool reported = false; // a missing joint was logged
    u64 mark = 0;          // the update that last used it
};

// Most TwoBoneIk limbs per Animator, and joints of a LookAt chain.
constexpr u32 kMaxLimbs = 8;
constexpr u32 kMaxLookChain = 4;

// A TwoBoneIk of an Animator, prepared for this update with its targets in model space.
struct Limb
{
    Entity entity;
    u32 joint = 0;
    TwoBoneSolve solve;
    bool align = false; // TwoBoneIk::alignToTarget
    i32 foot = -1;      // its entry in FootPlacementPose::feet, or -1
};

// Most joints of one SpringBone chain, simulation steps of one update and the step length in seconds.
constexpr u32 kMaxSpringJoints = 32;
constexpr u32 kMaxSpringSteps = 4;
constexpr f32 kSpringStep = 1.0f / 60.0f;

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
    // The per-pose stages of ADR 0061, prepared on the World's thread for this update.
    u64 prepared = 0; // the update that queued the pose
    Mat4 inverseWorld{1.0f};
    Quat worldRotation{1.0f, 0.0f, 0.0f, 0.0f};
    f32 stageSeconds = 0.0f; // negative when not advancing: the stages reach their targets at once
    bool look = false;
    LookAtSolve lookSolve;
    f32 lookSpeed = 0.0f;
    std::array<u32, kMaxLookChain> lookChain{};
    u32 lookChainLength = 0;
    JointName lookJoint;
    Vec3 lookDirection{0.0f, 0.0f, 1.0f}; // the smoothed model-space direction from the head
    bool lookValid = false;
    std::array<Limb, kMaxLimbs> limbs{};
    u32 limbCount = 0;
    bool limbsReported = false;
    FootPlacementPose* feet = nullptr;
    JointName pelvisJoint;
    // SpringBone: the root and its descendants, parents first, and their simulation.
    bool spring = false;
    JointName springRoot;
    const Skeleton* springSkeleton = nullptr; // the skeleton springChain was built for
    u64 springRevision = 0;
    i32 springChainRoot = -1;
    std::vector<u32> springChain;
    std::vector<SpringParticle> springParticles;
    bool springValid = false; // the particles hold a state
    bool springReported = false;
    f32 springTime = 0.0f; // seconds not stepped yet
    u32 springSteps = 0;   // steps of this update
    SpringSolve springSolve;
    std::array<JointName, kMaxSpringColliders> colliderJoints{};
    std::array<u32, kMaxSpringColliders> colliderIndex{};
    std::array<Vec3, kMaxSpringColliders> colliderOffset{};
    std::array<SpringSphere, kMaxSpringColliders> springSpheres{};
    u32 springSphereCount = 0;
    // The ragdoll pose handed over by setRagdollPose(), in world space, and its blend.
    std::vector<Mat4> ragdollJoints;
    bool ragdollPose = false;   // ragdollJoints hold a pose to blend with
    bool ragdollActive = false; // blending towards it (else back to the animation)
    f32 ragdollBlend = 0.0f;    // seconds of a whole blend
    f32 ragdollWeight = 0.0f;   // of this update
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
    std::unordered_map<u64, Socket> sockets;       // by the attached Entity's Entity::toBits()
    std::unordered_map<u64, JointName> limbJoints; // by the TwoBoneIk Entity's Entity::toBits()
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
        slot.lookValid = false;
        slot.limbsReported = false;
        slot.lookJoint = JointName{};
        slot.pelvisJoint = JointName{};
        slot.springRoot = JointName{};
        slot.springSkeleton = nullptr;
        slot.springValid = false;
        slot.springReported = false;
        slot.springTime = 0.0f;
        slot.colliderJoints = {};
        slot.ragdollPose = false;
        slot.ragdollActive = false;
        slot.ragdollWeight = 0.0f;
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
            slot.lookValid = false;
            slot.springValid = false;
            slot.ragdollPose = false;
            slot.ragdollWeight = 0.0f;
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
        // A cross-fade needs one local pose; the ragdoll blend a local pose and model-space matrices.
        arenaBytes = std::max(arenaBytes, usize{joints} * (sizeof(JointPose) + sizeof(Mat4)) +
                                              alignof(JointPose) + alignof(Mat4));
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
        slot.worldRotation =
            transform != nullptr ? glm::normalize(transform->rotation) : Quat{1.0f, 0.0f, 0.0f, 0.0f};
        prepareStages(entity, slot, seconds, advance);
        slot.prepared = frame;
        posed.push_back(pose.slot);
    }

    // Resolves @p name against @p slot's skeleton, again after a change of name, skeleton or revision.
    i32 resolveJoint(JointName& joint, std::string_view name, const Slot& slot)
    {
        joint.mark = frame;
        if (joint.name != name)
        {
            joint.name = name;
            joint.skeleton = nullptr;
            joint.reported = false;
        }
        if (joint.skeleton != slot.skeleton || joint.revision != revision)
        {
            joint.skeleton = slot.skeleton;
            joint.revision = revision;
            joint.index = detail::findJoint(*slot.skeleton, joint.name);
        }
        if (joint.index < 0 && !joint.reported)
        {
            joint.reported = true;
            ILL_LOG_WARN("Animation", "entity {}: the skeleton has no joint '{}'; it is not posed by it",
                         slot.entity.index, joint.name);
        }
        return joint.index;
    }

    // The world-space position of @p entity, or @p point when it is null or has no Transform.
    [[nodiscard]] Vec3 positionOf(Entity entity, Vec3 point) const noexcept
    {
        const Transform* transform =
            !entity.isNull() && world->isAlive(entity) ? world->tryGet<Transform>(entity) : nullptr;
        return transform != nullptr ? transform->position : point;
    }

    // The look-at and foot placement of @p slot's Animator for this update (limbs come in gatherLimbs()).
    void prepareStages(Entity entity, Slot& slot, f32 seconds, bool advance)
    {
        const f32 determinant = glm::determinant(slot.world);
        slot.inverseWorld =
            determinant != 0.0f && std::isfinite(determinant) ? glm::inverse(slot.world) : Mat4(1.0f);
        slot.stageSeconds = advance ? seconds : -1.0f;
        slot.limbCount = 0;
        slot.look = false;
        slot.feet = nullptr;
        const LookAt* lookAt = world->tryGet<LookAt>(entity);
        if (lookAt == nullptr || !(lookAt->weight > 0.0f))
        {
            slot.lookValid = false;
        }
        else if (const i32 head = resolveJoint(slot.lookJoint, lookAt->joint, slot); head >= 0)
        {
            const u32 length = std::clamp(lookAt->chainLength, 1u, kMaxLookChain);
            u32 count = 0;
            std::array<u32, kMaxLookChain> chain{};
            for (i32 joint = head; joint >= 0 && count < length;
                 joint = slot.skeleton->joints[static_cast<usize>(joint)].parent)
            {
                chain[count++] = static_cast<u32>(joint);
            }
            for (u32 i = 0; i < count; ++i)
            {
                slot.lookChain[i] = chain[count - 1 - i]; // parents first
            }
            slot.lookChainLength = count;
            slot.lookSolve.target =
                Vec3(slot.inverseWorld * Vec4(positionOf(lookAt->target, lookAt->point), 1.0f));
            slot.lookSolve.forward = lookAt->forward;
            slot.lookSolve.maxAngle = lookAt->maxAngle;
            slot.lookSolve.weight = lookAt->weight;
            slot.lookSpeed = lookAt->speed;
            slot.look = true;
        }
        prepareSpring(entity, slot, seconds, advance);
        prepareRagdoll(slot, seconds, advance);
        const FootPlacement* placement = world->tryGet<FootPlacement>(entity);
        FootPlacementPose* feet = placement != nullptr ? world->tryGet<FootPlacementPose>(entity) : nullptr;
        if (feet != nullptr && resolveJoint(slot.pelvisJoint, placement->pelvis, slot) >= 0)
        {
            slot.feet = feet;
        }
        else if (feet != nullptr)
        {
            feet->footCount = 0;
        }
    }

    // The SpringBone of @p slot's Animator for this update: its chain (built again for another root, skeleton
    // or revision), colliders and the fixed steps the update advances by.
    void prepareSpring(Entity entity, Slot& slot, f32 seconds, bool advance)
    {
        slot.spring = false;
        const SpringBone* bone = world->tryGet<SpringBone>(entity);
        const i32 root = bone != nullptr ? resolveJoint(slot.springRoot, bone->root, slot) : -1;
        if (root < 0)
        {
            slot.springValid = false;
            return;
        }
        if (slot.springSkeleton != slot.skeleton || slot.springRevision != revision ||
            slot.springChainRoot != root)
        {
            slot.springSkeleton = slot.skeleton;
            slot.springRevision = revision;
            slot.springChainRoot = root;
            slot.springValid = false;
            slot.springReported = false;
            slot.springChain.clear();
            slot.springChain.push_back(static_cast<u32>(root));
            const auto& joints = slot.skeleton->joints;
            for (usize j = static_cast<usize>(root) + 1; j < joints.size(); ++j)
            {
                if (std::ranges::find(slot.springChain, static_cast<u32>(joints[j].parent)) ==
                    slot.springChain.end())
                {
                    continue;
                }
                if (slot.springChain.size() == kMaxSpringJoints)
                {
                    if (!slot.springReported)
                    {
                        slot.springReported = true;
                        ILL_LOG_WARN(
                            "Animation",
                            "entity {}: the SpringBone chain below '{}' has more than {} joints; the "
                            "others follow their parents",
                            entity.index, bone->root, kMaxSpringJoints);
                    }
                    break;
                }
                slot.springChain.push_back(static_cast<u32>(j));
            }
            slot.springParticles.resize(slot.springChain.size());
        }
        if (slot.springChain.size() < 2)
        {
            return;
        }
        slot.springSolve.world = slot.world;
        slot.springSolve.stiffness = bone->stiffness;
        slot.springSolve.damping = bone->damping;
        slot.springSolve.gravity = bone->gravity;
        slot.springSolve.radius = bone->radius;
        slot.springSolve.stepSeconds = kSpringStep;
        slot.springSphereCount = 0;
        for (u32 c = 0; c < kMaxSpringColliders; ++c)
        {
            const SpringCollider& collider = bone->colliders[c];
            if (collider.joint.empty() || !(collider.radius > 0.0f))
            {
                continue;
            }
            const i32 joint = resolveJoint(slot.colliderJoints[c], collider.joint, slot);
            if (joint < 0)
            {
                continue;
            }
            const u32 sphere = slot.springSphereCount++;
            slot.colliderIndex[sphere] = static_cast<u32>(joint);
            slot.colliderOffset[sphere] = collider.offset;
            slot.springSpheres[sphere].radius = collider.radius;
        }
        if (!advance)
        {
            // The editor shows the animated pose: the chain starts at rest when the game runs.
            slot.springValid = false;
            slot.springTime = 0.0f;
            slot.springSteps = 0;
        }
        else
        {
            slot.springTime += seconds;
            const f32 due = std::floor(slot.springTime / kSpringStep);
            slot.springSteps =
                std::isfinite(due) ? static_cast<u32>(std::clamp(due, 0.0f, f32{kMaxSpringSteps})) : 0u;
            slot.springTime = std::clamp(slot.springTime - static_cast<f32>(slot.springSteps) * kSpringStep,
                                         0.0f, kSpringStep);
        }
        slot.spring = true;
    }

    // Advances the ragdoll blend of @p slot: towards the ragdoll pose while it is active, back otherwise.
    static void prepareRagdoll(Slot& slot, f32 seconds, bool advance) noexcept
    {
        if (!slot.ragdollPose)
        {
            slot.ragdollWeight = 0.0f;
            return;
        }
        const f32 rate = advance && slot.ragdollBlend > 0.0f ? seconds / slot.ragdollBlend : 1.0f;
        slot.ragdollWeight = std::clamp(slot.ragdollWeight + (slot.ragdollActive ? rate : -rate), 0.0f, 1.0f);
        if (!slot.ragdollActive && slot.ragdollWeight == 0.0f)
        {
            slot.ragdollPose = false;
        }
    }

    // Spring bones, the first stage: the colliders follow their joints, then the chain swings.
    static void swingSprings(Slot& slot, std::span<JointPose> local) noexcept
    {
        const Skeleton& skeleton = *slot.skeleton;
        for (u32 s = 0; s < slot.springSphereCount; ++s)
        {
            const Mat4 joint = slot.world * detail::jointModelMatrix(skeleton, local, slot.colliderIndex[s]);
            slot.springSpheres[s].center = Vec3(joint * Vec4(slot.colliderOffset[s], 1.0f));
        }
        if (!slot.springValid)
        {
            resetSpringChain(skeleton, slot.springChain, slot.world, local, slot.springParticles);
            slot.springValid = true;
        }
        stepSpringChain(skeleton, slot.springChain, slot.springSolve,
                        std::span<const SpringSphere>(slot.springSpheres.data(), slot.springSphereCount),
                        slot.springSteps, slot.springParticles, local);
    }

    // The ragdoll blend, the last stage: turns the world-space ragdoll pose into local transforms under the
    // Animator's current Transform and blends towards them by the slot's weight.
    void blendRagdoll(Slot& slot, std::span<JointPose> local) noexcept
    {
        const Skeleton& skeleton = *slot.skeleton;
        const u32 joints = slot.jointCount;
        LinearAllocator& memory = arena();
        const LinearAllocator::Marker marker = memory.mark();
        Mat4* model = memory.allocateArray<Mat4>(joints);
        JointPose* target = memory.allocateArray<JointPose>(joints);
        ILL_CHECK(model != nullptr && target != nullptr, "the animation scratch arena is too small");
        for (u32 j = 0; j < joints; ++j)
        {
            model[j] = slot.inverseWorld * slot.ragdollJoints[j];
            const i32 parent = skeleton.joints[j].parent;
            const Mat4& above = parent >= 0 ? model[static_cast<usize>(parent)] : skeleton.rootTransform;
            const f32 determinant = glm::determinant(above);
            if (!(determinant != 0.0f && std::isfinite(determinant)))
            {
                target[j] = local[j];
                continue;
            }
            const Transform relative = Transform::fromMatrix(glm::inverse(above) * model[j]);
            target[j] = JointPose{relative.position, relative.rotation, relative.scale};
        }
        blendPoses(local, std::span<const JointPose>(target, joints), slot.ragdollWeight, {}, local);
        memory.rewind(marker);
    }

    // The slot whose pose this update queued for @p entity's Animator, or null.
    [[nodiscard]] Slot* preparedSlot(Entity entity) noexcept
    {
        const AnimationPose* pose =
            !entity.isNull() && world->isAlive(entity) ? world->tryGet<AnimationPose>(entity) : nullptr;
        if (pose == nullptr || pose->slot >= slots.size())
        {
            return nullptr;
        }
        Slot& slot = slots[pose->slot];
        return slot.used && slot.entity == entity && slot.prepared == frame ? &slot : nullptr;
    }

    // Adds the TwoBoneIk of @p entity to its Animator's slot (on the Entity itself, else on its parent).
    void addLimb(Entity entity, const TwoBoneIk& ik)
    {
        Entity owner = entity;
        if (!world->has<Animator>(entity))
        {
            const Hierarchy* hierarchy = world->tryGet<Hierarchy>(entity);
            owner = hierarchy != nullptr ? hierarchy->parent : Entity{};
        }
        Slot* slot = preparedSlot(owner);
        if (slot == nullptr)
        {
            return;
        }
        const i32 joint = resolveJoint(limbJoints[entity.toBits()], ik.joint, *slot);
        if (joint < 0)
        {
            return;
        }
        if (slot->limbCount == kMaxLimbs)
        {
            if (!slot->limbsReported)
            {
                slot->limbsReported = true;
                ILL_LOG_WARN("Animation", "entity {}: more than {} TwoBoneIk limbs; the others are not posed",
                             owner.index, kMaxLimbs);
            }
            return;
        }
        const Transform* target =
            !ik.target.isNull() && world->isAlive(ik.target) ? world->tryGet<Transform>(ik.target) : nullptr;
        Limb& limb = slot->limbs[slot->limbCount++];
        limb.entity = entity;
        limb.joint = static_cast<u32>(joint);
        limb.foot = -1;
        limb.align = ik.alignToTarget;
        limb.solve.target = Vec3(slot->inverseWorld * Vec4(positionOf(ik.target, ik.point), 1.0f));
        limb.solve.pole = ik.pole.isNull()
                              ? ik.polePoint
                              : Vec3(slot->inverseWorld * Vec4(positionOf(ik.pole, Vec3(0.0f)), 1.0f));
        limb.solve.weight = ik.weight;
        limb.solve.alignEnd = ik.alignToTarget && target != nullptr;
        limb.solve.endRotation = target != nullptr
                                     ? glm::normalize(glm::inverse(slot->worldRotation) * target->rotation)
                                     : Quat{1.0f, 0.0f, 0.0f, 0.0f};
    }

    // Collects this update's TwoBoneIk limbs into their Animators' slots and picks the feet of each
    // FootPlacement: the limbs whose chain hangs from the pelvis joint.
    void gatherLimbs()
    {
        ILL_PROFILE_SCOPE();
        usize seen = 0;
        world->query<const TwoBoneIk>().without<Inactive>().each(
            [this, &seen](Entity entity, const TwoBoneIk& ik) {
                ++seen;
                addLimb(entity, ik);
            });
        if (limbJoints.size() > seen)
        {
            std::erase_if(limbJoints, [this](const auto& entry) { return entry.second.mark != frame; });
        }
        for (const u32 index : posed)
        {
            Slot& slot = slots[index];
            if (slot.feet == nullptr)
            {
                continue;
            }
            FootPlacementPose& feet = *slot.feet;
            const i32 pelvis = slot.pelvisJoint.index;
            u32 count = 0;
            for (u32 l = 0; l < slot.limbCount && count < FootPlacementPose::kMaxFeet; ++l)
            {
                Limb& limb = slot.limbs[l];
                const i32 middle = slot.skeleton->joints[limb.joint].parent;
                const i32 root = middle >= 0 ? slot.skeleton->joints[static_cast<usize>(middle)].parent : -1;
                if (root < 0 || slot.skeleton->joints[static_cast<usize>(root)].parent != pelvis)
                {
                    continue;
                }
                FootContact& contact = feet.feet[count];
                if (contact.limb != limb.entity)
                {
                    contact = FootContact{};
                    contact.limb = limb.entity;
                }
                limb.foot = static_cast<i32>(count++);
            }
            for (u32 f = count; f < FootPlacementPose::kMaxFeet; ++f)
            {
                feet.feet[f] = FootContact{};
            }
            feet.footCount = count;
        }
    }

    // Foot placement before the look-at and IK: writes the animated placements of the feet and the pelvis
    // for the session, moves the pelvis by its offset and aims the feet at their ground contacts.
    void placeFeet(Slot& slot, std::span<JointPose> local) const noexcept
    {
        FootPlacementPose& feet = *slot.feet;
        const Skeleton& skeleton = *slot.skeleton;
        const auto pelvis = static_cast<u32>(slot.pelvisJoint.index);
        feet.frame = frame;
        feet.pelvis = Vec3(slot.world * detail::jointModelMatrix(skeleton, local, pelvis)[3]);
        const Quat toModel = glm::inverse(slot.worldRotation);
        for (u32 l = 0; l < slot.limbCount; ++l)
        {
            Limb& limb = slot.limbs[l];
            if (limb.foot < 0)
            {
                continue;
            }
            FootContact& contact = feet.feet[static_cast<usize>(limb.foot)];
            contact.animated = Vec3(slot.world * detail::jointModelMatrix(skeleton, local, limb.joint)[3]);
            const f32 lift = std::isfinite(contact.lift) ? contact.lift : 0.0f;
            const f32 share = std::isfinite(contact.weight) ? std::clamp(contact.weight, 0.0f, 1.0f) : 0.0f;
            limb.solve.weight *= share;
            limb.solve.target =
                Vec3(slot.inverseWorld * Vec4(contact.animated + Vec3(0.0f, lift, 0.0f), 1.0f));
            const f32 tiltLength = glm::length(contact.tilt);
            limb.solve.alignEnd = limb.align && tiltLength > 0.0f && std::isfinite(tiltLength);
            if (limb.solve.alignEnd)
            {
                const Quat tilt = toModel * (contact.tilt / tiltLength) * slot.worldRotation;
                limb.solve.endRotation =
                    tilt * detail::jointModelRotation(skeleton, local, static_cast<i32>(limb.joint));
            }
        }
        const f32 offset = feet.pelvisOffset;
        if (offset == 0.0f || !std::isfinite(offset))
        {
            return;
        }
        const i32 parent = skeleton.joints[pelvis].parent;
        const Mat3 parentBasis(parent >= 0
                                   ? detail::jointModelMatrix(skeleton, local, static_cast<u32>(parent))
                                   : skeleton.rootTransform);
        const f32 determinant = glm::determinant(parentBasis);
        if (determinant != 0.0f && std::isfinite(determinant))
        {
            local[pelvis].translation +=
                glm::inverse(parentBasis) * (Mat3(slot.inverseWorld) * Vec3(0.0f, offset, 0.0f));
        }
    }

    // Turns the LookAt chain towards its target, following it at LookAt::speed while advancing.
    static void turnHead(Slot& slot, std::span<JointPose> local) noexcept
    {
        const Skeleton& skeleton = *slot.skeleton;
        const std::span<const u32> chain(slot.lookChain.data(), slot.lookChainLength);
        const Mat4 head = detail::jointModelMatrix(skeleton, local, chain.back());
        const Vec3 position(head[3]);
        const Vec3 forward = Mat3(head) * slot.lookSolve.forward;
        const Vec3 toTarget = slot.lookSolve.target - position;
        const f32 forwardLength = glm::length(forward);
        const f32 distance = glm::length(toTarget);
        if (!(forwardLength > 0.0f) || !(distance > 0.0f) || !std::isfinite(forwardLength + distance))
        {
            return;
        }
        const Vec3 wanted = toTarget / distance;
        if (!slot.lookValid)
        {
            slot.lookDirection = forward / forwardLength; // eases in from the animated direction
            slot.lookValid = true;
        }
        const f32 turn = slot.lookSpeed * slot.stageSeconds;
        slot.lookDirection =
            slot.stageSeconds < 0.0f || !(slot.lookSpeed > 0.0f) || !std::isfinite(turn)
                ? wanted
                : detail::turnTowards(slot.lookDirection, wanted, turn, Mat3(head) * Vec3(0.0f, 1.0f, 0.0f));
        LookAtSolve solve = slot.lookSolve;
        solve.target = position + slot.lookDirection * distance;
        static_cast<void>(solveLookAt(skeleton, chain, solve, local));
    }

    // The per-pose stages of ADR 0061 on @p local, before the model-space matrices are composed.
    void applyStages(Slot& slot, std::span<JointPose> local) noexcept
    {
        if (slot.spring)
        {
            swingSprings(slot, local);
        }
        if (slot.feet != nullptr)
        {
            placeFeet(slot, local);
        }
        if (slot.look)
        {
            turnHead(slot, local);
        }
        for (u32 l = 0; l < slot.limbCount; ++l)
        {
            static_cast<void>(
                solveTwoBoneIk(*slot.skeleton, slot.limbs[l].joint, slot.limbs[l].solve, local));
        }
        if (slot.ragdollWeight > 0.0f)
        {
            blendRagdoll(slot, local);
        }
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
        applyStages(slot, localPose);
        localToModel(*slot.skeleton, localPose, modelPose);
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
        scratch.clear();
        world->query<const FootPlacement>().without<FootPlacementPose>().each(
            [this](Entity entity, const FootPlacement&) { scratch.push_back(entity); });
        for (const Entity entity : scratch)
        {
            world->emplace<FootPlacementPose>(entity);
        }
        scratch.clear();
        world->query<const FootPlacementPose>().without<FootPlacement>().each(
            [this](Entity entity, const FootPlacementPose&) { scratch.push_back(entity); });
        for (const Entity entity : scratch)
        {
            world->remove<FootPlacementPose>(entity);
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
        gatherLimbs();

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

    [[nodiscard]] const Slot* slotOf(u32 index) const noexcept
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

const GraphBinding* AnimationWorld::graphBinding(u32 slot) const noexcept
{
    const Slot* entry = m_impl->slotOf(slot);
    return entry != nullptr ? entry->graph : nullptr;
}

const AnimationClip* AnimationWorld::clip(u32 slot) const noexcept
{
    const Slot* entry = m_impl->slotOf(slot);
    return entry != nullptr && entry->graph == nullptr && entry->jointCount > 0 ? entry->clip : nullptr;
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

std::span<const Aabb> AnimationWorld::jointBounds(u32 slot) const noexcept
{
    const Slot* entry = m_impl->slotOf(slot);
    return entry != nullptr && entry->jointCount > 0 && entry->jointBounds != nullptr
               ? std::span<const Aabb>(*entry->jointBounds)
               : std::span<const Aabb>();
}

bool AnimationWorld::setRagdollPose(u32 slot, std::span<const Mat4> joints, bool active, f32 blendSeconds)
{
    Slot* entry = m_impl->slotOf(slot);
    if (entry == nullptr || entry->jointCount == 0 ||
        (!joints.empty() && joints.size() != entry->jointCount) ||
        (active && joints.empty() && !entry->ragdollPose))
    {
        return false;
    }
    if (!joints.empty())
    {
        entry->ragdollJoints.assign(joints.begin(), joints.end());
        entry->ragdollPose = true;
    }
    entry->ragdollActive = active;
    entry->ragdollBlend = std::isfinite(blendSeconds) ? std::max(blendSeconds, 0.0f) : 0.0f;
    return true;
}

f32 AnimationWorld::ragdollWeight(u32 slot) const noexcept
{
    const Slot* entry = m_impl->slotOf(slot);
    return entry != nullptr ? entry->ragdollWeight : 0.0f;
}

} // namespace illusion
