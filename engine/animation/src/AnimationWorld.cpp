#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>
#include <array>
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

// Animators posed by one Job.
constexpr u32 kPosesPerJob = 8;

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
    std::vector<Entity> scratch;
    std::vector<u32> posed;
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
    void resolve(Slot& slot, const Animator& animator, AssetId mesh)
    {
        const bool sameSkeleton = slot.skeletonId == animator.skeleton.id && slot.revision == revision;
        slot.skeletonId = animator.skeleton.id;
        slot.clipId = animator.clip.id;
        slot.meshId = mesh;
        slot.revision = revision;
        slot.resolved = false;
        slot.jointCount = 0;
        if (!animator.skeleton)
        {
            slot.resolved = true;
            return;
        }
        SkeletonEntry& skeleton = requestSkeleton(animator.skeleton.id);
        ClipEntry* clip = animator.clip ? &requestClip(animator.clip.id) : nullptr;
        MeshEntry* meshEntry = mesh ? &requestMesh(mesh) : nullptr;
        if (skeleton.status == Status::Loading || (clip != nullptr && clip->status == Status::Loading) ||
            (meshEntry != nullptr && meshEntry->status == Status::Loading))
        {
            return;
        }
        slot.resolved = true;
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
        if (slot.jointCount == 0)
        {
            pose.jointCount = 0;
            return;
        }
        f32 time = 0.0f;
        if (slot.clip != nullptr)
        {
            if (advance && animator.playing)
            {
                animator.time = advanceTime(animator, *slot.clip, seconds);
            }
            time = clipTime(*slot.clip, animator.time, animator.loop);
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
        if (slot.clip != nullptr)
        {
            sampleClip(*slot.clip, *slot.binding, *slot.skeleton, slot.time, localPose);
        }
        else
        {
            restPose(*slot.skeleton, localPose);
        }
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
        for (const u32 index : posed)
        {
            statistics.joints += slots[index].jointCount;
        }
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
    while (m_impl->statistics.pending > 0 || !m_impl->loading.empty())
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

} // namespace illusion
