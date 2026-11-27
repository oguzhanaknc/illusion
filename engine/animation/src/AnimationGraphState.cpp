#include "PoseDetail.hpp"

#include <illusion/animation/Aim.hpp>
#include <illusion/animation/AnimationGraphState.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace illusion
{
namespace
{

constexpr u8 kTriggerUnset = 0;
constexpr u8 kTriggerSet = 1;

// A state plays at most three clips at once: a 2D blend space blends the corners of one triangle.
constexpr u32 kMaxStateClips = 3;

struct MotionWeights
{
    std::array<u32, kMaxStateClips> points{};
    std::array<f32, kMaxStateClips> weights{};
    u32 count = 0;

    void add(u32 point, f32 weight) noexcept
    {
        if (weight > 0.0f && count < kMaxStateClips)
        {
            points.at(count) = point;
            weights.at(count) = weight;
            ++count;
        }
    }
};

MotionWeights blend1d(const AnimationMotion& motion, f32 value) noexcept
{
    MotionWeights result;
    const std::vector<AnimationBlendPoint>& points = motion.points;
    const auto last = static_cast<u32>(points.size() - 1);
    if (!(value > points.front().x))
    {
        result.add(0, 1.0f);
        return result;
    }
    if (!(value < points.back().x))
    {
        result.add(last, 1.0f);
        return result;
    }
    u32 i = 0;
    while (i + 1 < last && !(value < points[i + 1].x))
    {
        ++i;
    }
    const f32 t = (value - points[i].x) / (points[i + 1].x - points[i].x);
    result.add(i, 1.0f - t);
    result.add(i + 1, t);
    return result;
}

MotionWeights blend2d(const AnimationMotion& motion, f32 x, f32 y) noexcept
{
    MotionWeights result;
    const std::vector<AnimationBlendPoint>& points = motion.points;
    const std::vector<u32>& triangles = motion.triangles;
    const Vec2 p(x, y);
    const auto at = [&](u32 index) {
        return Vec2(points[index].x, points[index].y);
    };
    constexpr f32 kInside = -1.0e-5f;
    for (usize t = 0; t + 2 < triangles.size(); t += 3)
    {
        const Vec2 a = at(triangles[t]);
        const Vec2 ab = at(triangles[t + 1]) - a;
        const Vec2 ac = at(triangles[t + 2]) - a;
        const Vec2 ap = p - a;
        const f32 det = ab.x * ac.y - ac.x * ab.y;
        if (det == 0.0f)
        {
            continue;
        }
        const f32 wb = (ap.x * ac.y - ac.x * ap.y) / det;
        const f32 wc = (ab.x * ap.y - ap.x * ab.y) / det;
        const f32 wa = 1.0f - wb - wc;
        if (wa >= kInside && wb >= kInside && wc >= kInside)
        {
            const f32 ca = std::max(wa, 0.0f);
            const f32 cb = std::max(wb, 0.0f);
            const f32 cc = std::max(wc, 0.0f);
            const f32 sum = ca + cb + cc;
            result.add(triangles[t], ca / sum);
            result.add(triangles[t + 1], cb / sum);
            result.add(triangles[t + 2], cc / sum);
            return result;
        }
    }
    // Outside the hull the nearest point of the triangulation lies on a hull edge.
    f32 best = HUGE_VALF;
    u32 from = 0;
    u32 to = 0;
    f32 along = 0.0f;
    for (usize t = 0; t + 2 < triangles.size(); t += 3)
    {
        for (usize e = 0; e < 3; ++e)
        {
            const u32 a = triangles[t + e];
            const u32 b = triangles[t + (e + 1) % 3];
            const Vec2 ab = at(b) - at(a);
            const f32 s = std::clamp(glm::dot(p - at(a), ab) / glm::dot(ab, ab), 0.0f, 1.0f);
            const Vec2 q = at(a) + ab * s;
            const f32 distance = glm::dot(p - q, p - q);
            if (distance < best)
            {
                best = distance;
                from = a;
                to = b;
                along = s;
            }
        }
    }
    result.add(from, 1.0f - along);
    result.add(to, along);
    return result;
}

MotionWeights motionWeights(const AnimationMotion& motion, std::span<const f32> parameters) noexcept
{
    switch (motion.type)
    {
    case AnimationMotionType::Clip:
        break;
    case AnimationMotionType::Blend1D:
        return blend1d(motion, parameters[motion.parameterX]);
    case AnimationMotionType::Blend2D:
        return blend2d(motion, parameters[motion.parameterX], parameters[motion.parameterY]);
    }
    MotionWeights single;
    single.add(0, 1.0f);
    return single;
}

// Seconds of one loop of @p state: its clip's, or the weighted duration of its blend space.
f32 stateDuration(const GraphBinding& binding, u32 flatState, const AnimationState& state,
                  std::span<const f32> parameters) noexcept
{
    const MotionWeights weights = motionWeights(state.motion, parameters);
    const u32 first = binding.firstClip[flatState];
    f32 duration = 0.0f;
    for (u32 i = 0; i < weights.count; ++i)
    {
        duration += weights.weights.at(i) * binding.clips[first + weights.points.at(i)]->duration;
    }
    return duration;
}

f32 playbackRate(const AnimationState& state, std::span<const f32> parameters) noexcept
{
    const f32 factor =
        state.speedParameter >= 0 ? parameters[static_cast<usize>(state.speedParameter)] : 1.0f;
    // Playing backwards is not supported; a negative factor holds the state.
    return std::max(state.speed * factor, 0.0f);
}

// Normalized time a state advances by in @p seconds.
f32 normalizedStep(const GraphBinding& binding, u32 flatState, const AnimationState& state,
                   std::span<const f32> parameters, f32 seconds) noexcept
{
    const f32 advance = seconds * playbackRate(state, parameters);
    if (!(advance > 0.0f))
    {
        return 0.0f;
    }
    const f32 duration = stateDuration(binding, flatState, state, parameters);
    return duration > 0.0f ? advance / duration : 1.0f;
}

class EventWriter
{
public:
    explicit EventWriter(std::span<GraphEvent> events) noexcept
        : m_events(events)
    {
    }

    // Events of @p state whose time lies in [from, from + step), one loop wrap included.
    void crossed(u16 layer, u16 stateIndex, const AnimationState& state, f32 from, f32 step) noexcept
    {
        if (!(step > 0.0f) || state.events.empty())
        {
            return;
        }
        const f32 to = from + (state.loop ? std::min(step, 1.0f) : step);
        const u32 loops = state.loop ? 2 : 1;
        for (u32 loop = 0; loop < loops; ++loop)
        {
            for (usize e = 0; e < state.events.size(); ++e)
            {
                const f32 time = state.events[e].time + static_cast<f32>(loop);
                if (time >= from && time < to)
                {
                    write(GraphEvent{layer, stateIndex, static_cast<u32>(e)});
                }
            }
        }
    }

    [[nodiscard]] u32 count() const noexcept { return m_count; }

private:
    void write(GraphEvent event) noexcept
    {
        if (m_count < m_events.size())
        {
            m_events[m_count] = event;
        }
        ++m_count;
    }

    std::span<GraphEvent> m_events;
    u32 m_count = 0;
};

// Advances a normalized time; a looping state wraps into [0, 1). Returns true when it wrapped.
bool advanceTime(f32& time, f32 step, bool loop) noexcept
{
    const f32 next = time + step;
    if (!loop || next < 1.0f)
    {
        time = next;
        return false;
    }
    const f32 wrapped = next - std::floor(next);
    time = wrapped < 1.0f ? wrapped : 0.0f;
    return true;
}

bool conditionHolds(const AnimationCondition& condition, const AnimationGraphState& state) noexcept
{
    const f32 value = state.parameters[condition.parameter];
    switch (condition.compare)
    {
    case AnimationCompare::Greater:
        return value > condition.value;
    case AnimationCompare::Less:
        return value < condition.value;
    case AnimationCompare::Equal:
        return value == condition.value;
    case AnimationCompare::NotEqual:
        return value != condition.value;
    case AnimationCompare::TriggerSet:
        return state.triggers[condition.parameter] == kTriggerSet;
    }
    return false;
}

// Weight of the current state of @p layer; the previous state has the rest.
f32 currentWeight(const GraphLayerState& layer) noexcept
{
    if (layer.previous < 0 || !(layer.fadeDuration > 0.0f))
    {
        return 1.0f;
    }
    return std::clamp(layer.fade / layer.fadeDuration, 0.0f, 1.0f);
}

Mat4 poseMatrix(const JointPose& pose) noexcept
{
    Mat4 matrix = glm::mat4_cast(pose.rotation);
    matrix[0] *= pose.scale.x;
    matrix[1] *= pose.scale.y;
    matrix[2] *= pose.scale.z;
    matrix[3] = Vec4(pose.translation, 1.0f);
    return matrix;
}

// Model-space matrix of the parent of @p joint (the skeleton's root transform for a root), from the local
// poses that @p local returns.
template <class Local>
Mat4 parentModel(const Skeleton& skeleton, u32 joint, Local&& local) noexcept
{
    Mat4 matrix(1.0f);
    for (i32 parent = skeleton.joints[joint].parent; parent >= 0;
         parent = skeleton.joints[static_cast<usize>(parent)].parent)
    {
        matrix = poseMatrix(local(static_cast<u32>(parent))) * matrix;
    }
    return skeleton.rootTransform * matrix;
}

f32 yawOf(const Mat4& model) noexcept
{
    const Vec3 forward(model[2]);
    if (forward.x * forward.x + forward.z * forward.z < 1.0e-12f)
    {
        return 0.0f;
    }
    return std::atan2(forward.x, forward.z);
}

f32 wrapAngle(f32 angle) noexcept
{
    constexpr f32 kPi = std::numbers::pi_v<f32>;
    return angle - 2.0f * kPi * std::floor((angle + kPi) / (2.0f * kPi));
}

struct RootSample
{
    Vec3 position{0.0f};
    f32 yaw = 0.0f;
};

RootSample sampleRoot(const AnimationClip& clip, const ClipBinding& binding, const ClipRetarget& retarget,
                      const Skeleton& skeleton, u32 root, f32 seconds) noexcept
{
    const auto local = [&](u32 joint) {
        return detail::sampleJoint(clip, binding, retarget, skeleton, joint, seconds);
    };
    const Mat4 model = parentModel(skeleton, root, local) * poseMatrix(local(root));
    return RootSample{Vec3(model[3]), yawOf(model)};
}

RootMotionDelta rootDelta(RootSample from, RootSample to) noexcept
{
    return RootMotionDelta{Vec3(to.position.x - from.position.x, 0.0f, to.position.z - from.position.z),
                           wrapAngle(to.yaw - from.yaw)};
}

// One clip sampled by evaluateGraph().
struct ActiveClip
{
    u32 clip = 0; // Index into GraphBinding::clips.
    u32 layer = 0;
    f32 weight = 0.0f;     // In its layer; a layer's weights sum to 1.
    f32 importance = 0.0f; // weight times the layer's weight, for the cap.
    f32 time = 0.0f;       // Normalized time of its state.
    f32 lastTime = 0.0f;   // ... before the last stepGraph().
    bool loop = true;
};

void addStateClips(const GraphBinding& binding, const AnimationGraphState& state, u32 layerIndex,
                   u32 stateIndex, f32 weight, f32 time, f32 lastTime, std::span<ActiveClip> clips,
                   u32& count) noexcept
{
    if (!(weight > 0.0f))
    {
        return;
    }
    const AnimationLayer& layer = binding.graph->layers[layerIndex];
    const AnimationState& animationState = layer.states[stateIndex];
    const u32 flat = binding.firstState[layerIndex] + stateIndex;
    const MotionWeights weights = motionWeights(animationState.motion, state.parameters);
    for (u32 i = 0; i < weights.count && count < clips.size(); ++i)
    {
        const f32 clipWeight = weight * weights.weights.at(i);
        clips[count++] = ActiveClip{binding.firstClip[flat] + weights.points.at(i),
                                    layerIndex,
                                    clipWeight,
                                    clipWeight * layer.weight,
                                    time,
                                    lastTime,
                                    animationState.loop};
    }
}

// Drops the lightest clips beyond kMaxActiveClips and renormalizes the layers that lost one.
u32 capClips(std::span<ActiveClip> clips, u32 count, std::span<u8> trimmedLayers) noexcept
{
    while (count > kMaxActiveClips)
    {
        u32 lightest = 0;
        for (u32 i = 1; i < count; ++i)
        {
            if (clips[i].importance <= clips[lightest].importance)
            {
                lightest = i;
            }
        }
        trimmedLayers[clips[lightest].layer] = 1;
        std::copy(clips.begin() + lightest + 1, clips.begin() + count, clips.begin() + lightest);
        --count;
    }
    for (usize layer = 0; layer < trimmedLayers.size(); ++layer)
    {
        if (trimmedLayers[layer] == 0)
        {
            continue;
        }
        f32 sum = 0.0f;
        for (u32 i = 0; i < count; ++i)
        {
            sum += clips[i].layer == layer ? clips[i].weight : 0.0f;
        }
        for (u32 i = 0; i < count && sum > 0.0f; ++i)
        {
            clips[i].weight = clips[i].layer == layer ? clips[i].weight / sum : clips[i].weight;
        }
    }
    return count;
}

// Clip seconds of a normalized time.
f32 clipSeconds(const AnimationClip& clip, f32 normalized, bool loop) noexcept
{
    return clipTime(clip, normalized * clip.duration, loop);
}

RootMotionDelta clipRootMotion(const GraphBinding& binding, const ActiveClip& active, u32 root) noexcept
{
    const AnimationClip& clip = *binding.clips[active.clip];
    const ClipBinding& clipBinding = binding.clipBindings[active.clip];
    const ClipRetarget& retarget = binding.clipRetargets[active.clip];
    const Skeleton& skeleton = *binding.skeleton;
    const auto sample = [&](f32 seconds) {
        return sampleRoot(clip, clipBinding, retarget, skeleton, root, seconds);
    };
    if (active.time == active.lastTime)
    {
        return {};
    }
    const f32 duration = clip.duration;
    if (!active.loop || active.time > active.lastTime)
    {
        const f32 before = std::clamp(active.lastTime, 0.0f, 1.0f) * duration;
        const f32 after = std::clamp(active.time, 0.0f, 1.0f) * duration;
        return rootDelta(sample(before), sample(after));
    }
    // Wrapped: from before to the end, then from the start to after.
    const RootMotionDelta tail = rootDelta(sample(active.lastTime * duration), sample(duration));
    const RootMotionDelta head = rootDelta(sample(0.0f), sample(active.time * duration));
    return RootMotionDelta{tail.translation + head.translation, wrapAngle(tail.yaw + head.yaw)};
}

// Moves the root motion joint of @p pose back to the horizontal position and yaw in model space of @p base,
// or of the rest pose when it is empty.
void removeRootMotion(const Skeleton& skeleton, std::span<const JointPose> base, u32 root,
                      std::span<JointPose> pose) noexcept
{
    const auto current = [&](u32 joint) {
        return pose[joint];
    };
    const auto rest = [&](u32 joint) {
        if (joint < base.size())
        {
            return base[joint];
        }
        const SkeletonJoint& j = skeleton.joints[joint];
        return JointPose{j.translation, j.rotation, j.scale};
    };
    const Mat4 parent = parentModel(skeleton, root, current);
    const Mat4 model = parent * poseMatrix(pose[root]);
    const Mat4 restModel = parentModel(skeleton, root, rest) * poseMatrix(rest(root));
    const Vec3 target(restModel[3].x, model[3].y, restModel[3].z);
    pose[root].translation = Vec3(glm::inverse(parent) * Vec4(target, 1.0f));
    const f32 yaw = wrapAngle(yawOf(restModel) - yawOf(model));
    const Quat parentRotation = glm::normalize(glm::quat_cast(Mat3(
        glm::normalize(Vec3(parent[0])), glm::normalize(Vec3(parent[1])), glm::normalize(Vec3(parent[2])))));
    const Quat turn = glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
    pose[root].rotation =
        glm::normalize(glm::inverse(parentRotation) * turn * parentRotation * pose[root].rotation);
}

// Appends @p clip to @p binding: retargeted from @p source when given and possible, else by name.
void bindGraphClip(GraphBinding& binding, const AnimationClip& clip, const Skeleton* source,
                   const AssetRef& ref)
{
    const Skeleton& skeleton = *binding.skeleton;
    binding.clips.push_back(&clip);
    if (source != nullptr)
    {
        for (usize i = 0; i + 1 < binding.clips.size(); ++i)
        {
            if (binding.clips[i] == &clip && binding.clipRetargets[i].active())
            {
                binding.clipBindings.push_back(binding.clipBindings[i]);
                binding.clipRetargets.push_back(binding.clipRetargets[i]);
                return;
            }
        }
        Expected<RetargetedClip, RetargetError> retargeted = bindRetargetedClip(clip, *source, skeleton);
        if (retargeted)
        {
            if (binding.basePose.empty())
            {
                binding.basePose = retargeted->retarget.reference;
            }
            binding.clipBindings.push_back(std::move(retargeted->binding));
            binding.clipRetargets.push_back(std::move(retargeted->retarget));
            return;
        }
        ILL_LOG_WARN("Animation", "clip '{}' is bound by name: it cannot be retargeted ({})", ref.path(),
                     retargetErrorName(retargeted.error()));
    }
    binding.clipBindings.push_back(bindClip(clip, skeleton));
    binding.clipRetargets.emplace_back();
}

} // namespace

Expected<GraphBinding, std::string>
bindGraph(const AnimationGraph& graph, const Skeleton& skeleton,
          const std::function<const AnimationClip*(const AssetRef&)>& findClip)
{
    return bindGraph(graph, skeleton,
                     std::function<ClipSource(const AssetRef&)>([&findClip](const AssetRef& ref) {
                         return ClipSource{findClip ? findClip(ref) : nullptr, nullptr};
                     }));
}

Expected<GraphBinding, std::string> bindGraph(const AnimationGraph& graph, const Skeleton& skeleton,
                                              const std::function<ClipSource(const AssetRef&)>& findClip)
{
    GraphBinding binding;
    binding.graph = &graph;
    binding.skeleton = &skeleton;
    const usize joints = skeleton.joints.size();
    for (usize l = 0; l < graph.layers.size(); ++l)
    {
        const AnimationLayer& layer = graph.layers[l];
        binding.firstState.push_back(static_cast<u32>(binding.firstClip.size()));
        for (usize s = 0; s < layer.states.size(); ++s)
        {
            const AnimationMotion& motion = layer.states[s].motion;
            binding.firstClip.push_back(static_cast<u32>(binding.clips.size()));
            const auto add = [&](const AssetRef& ref, const std::string& path) -> std::optional<std::string> {
                const ClipSource source = findClip ? findClip(ref) : ClipSource{};
                if (source.clip == nullptr)
                {
                    return std::format("{}: clip '{}' not found", path, ref.path());
                }
                bindGraphClip(binding, *source.clip, source.skeleton, ref);
                return std::nullopt;
            };
            const std::string statePath = std::format("layers[{}].states[{}]", l, s);
            if (motion.type == AnimationMotionType::Clip)
            {
                if (std::optional<std::string> error = add(motion.clip, statePath + ".clip"))
                {
                    return unexpected(std::move(*error));
                }
                continue;
            }
            const char* kind = motion.type == AnimationMotionType::Blend1D ? "blend1d" : "blend2d";
            for (usize p = 0; p < motion.points.size(); ++p)
            {
                if (std::optional<std::string> error =
                        add(motion.points[p].clip, std::format("{}.{}.points[{}].clip", statePath, kind, p)))
                {
                    return unexpected(std::move(*error));
                }
            }
        }
        std::vector<f32> mask;
        if (!layer.mask.empty())
        {
            mask.assign(joints, 0.0f);
            std::vector<u8> subtree(joints, 0);
            for (usize m = 0; m < layer.mask.size(); ++m)
            {
                const i32 joint = findRigJoint(skeleton, layer.mask[m].joint);
                if (joint < 0)
                {
                    return unexpected(std::format("layers[{}].mask[{}].joint: the skeleton has no joint '{}'",
                                                  l, m, layer.mask[m].joint));
                }
                // Parents precede children, so one pass finds the subtree.
                for (usize j = 0; j < joints; ++j)
                {
                    const i32 parent = skeleton.joints[j].parent;
                    const bool inside = static_cast<i32>(j) == joint ||
                                        (parent >= 0 && subtree.at(static_cast<usize>(parent)) != 0);
                    subtree.at(j) = inside ? 1 : 0;
                    if (inside)
                    {
                        mask.at(j) = layer.mask[m].weight;
                    }
                }
            }
        }
        binding.masks.push_back(std::move(mask));
    }
    binding.referenceClips.assign(graph.layers.size(), -1);
    for (usize l = 0; l < graph.layers.size(); ++l)
    {
        const AssetRef& reference = graph.layers[l].reference;
        if (reference.empty())
        {
            continue;
        }
        const ClipSource source = findClip ? findClip(reference) : ClipSource{};
        if (source.clip == nullptr)
        {
            return unexpected(std::format("layers[{}].reference: clip '{}' not found", l, reference.path()));
        }
        binding.referenceClips[l] = static_cast<i32>(binding.clips.size());
        bindGraphClip(binding, *source.clip, source.skeleton, reference);
    }
    if (!graph.rootMotion.empty())
    {
        binding.rootJoint = findRigJoint(skeleton, graph.rootMotion);
        if (binding.rootJoint < 0)
        {
            return unexpected(std::format("rootMotion: the skeleton has no joint '{}'", graph.rootMotion));
        }
    }
    return binding;
}

void AnimationGraphState::setValue(i32 index, f32 value) noexcept
{
    if (index >= 0 && static_cast<usize>(index) < parameters.size() &&
        triggers[static_cast<usize>(index)] == kNotTrigger && std::isfinite(value))
    {
        parameters[static_cast<usize>(index)] = value;
    }
}

void AnimationGraphState::setTrigger(i32 index) noexcept
{
    if (index >= 0 && static_cast<usize>(index) < triggers.size() &&
        triggers[static_cast<usize>(index)] != kNotTrigger)
    {
        triggers[static_cast<usize>(index)] = kTriggerSet;
    }
}

AnimationGraphState makeGraphState(const GraphBinding& binding)
{
    ILL_ASSERT(binding.graph != nullptr, "makeGraphState needs a bound graph");
    const AnimationGraph& graph = *binding.graph;
    AnimationGraphState state;
    state.parameters.reserve(graph.parameters.size());
    state.triggers.reserve(graph.parameters.size());
    for (const AnimationParameter& parameter : graph.parameters)
    {
        const bool trigger = parameter.type == AnimationParameterType::Trigger;
        state.parameters.push_back(trigger ? 0.0f : parameter.defaultValue);
        state.triggers.push_back(trigger ? kTriggerUnset : AnimationGraphState::kNotTrigger);
    }
    state.layers.reserve(graph.layers.size());
    for (const AnimationLayer& layer : graph.layers)
    {
        GraphLayerState layerState;
        layerState.state = layer.entry;
        state.layers.push_back(layerState);
    }
    return state;
}

f32 graphStateDuration(const GraphBinding& binding, const AnimationGraphState& state, u32 layer,
                       u32 stateIndex) noexcept
{
    if (binding.graph == nullptr || layer >= binding.graph->layers.size() ||
        stateIndex >= binding.graph->layers[layer].states.size() ||
        state.parameters.size() != binding.graph->parameters.size())
    {
        return 0.0f;
    }
    return stateDuration(binding, binding.firstState[layer] + stateIndex,
                         binding.graph->layers[layer].states[stateIndex], state.parameters);
}

usize graphScratchBytes(const GraphBinding& binding) noexcept
{
    if (binding.graph == nullptr || binding.skeleton == nullptr)
    {
        return 0;
    }
    const usize layers = binding.graph->layers.size();
    // Each array may need its alignment in padding.
    const bool model = std::ranges::any_of(binding.graph->layers, [](const AnimationLayer& layer) {
        return layer.blend == AnimationLayerBlend::Additive && layer.space == AnimationAdditiveSpace::Model;
    });
    const usize modelBytes = model ? binding.skeleton->joints.size() * 3 * sizeof(Quat) + alignof(Quat) : 0;
    return binding.skeleton->joints.size() * 4 * sizeof(JointPose) + alignof(JointPose) +
           layers * 2 * kMaxStateClips * sizeof(ActiveClip) + alignof(ActiveClip) + layers + modelBytes;
}

u32 stepGraph(const GraphBinding& binding, AnimationGraphState& state, f32 seconds,
              std::span<GraphEvent> events) noexcept
{
    ILL_PROFILE_SCOPE();
    ILL_ASSERT(binding.graph != nullptr && state.layers.size() == binding.graph->layers.size(),
               "stepGraph needs a state made for this binding");
    const AnimationGraph& graph = *binding.graph;
    const f32 dt = std::isfinite(seconds) ? std::max(seconds, 0.0f) : 0.0f;
    EventWriter writer(events);
    const usize layerCount = std::min(graph.layers.size(), state.layers.size());
    for (usize l = 0; l < layerCount; ++l)
    {
        const AnimationLayer& layer = graph.layers[l];
        GraphLayerState& machine = state.layers[l];
        const u32 base = binding.firstState[l];
        const auto layerIndex = static_cast<u16>(l);

        machine.lastTime = machine.time;
        machine.lastPreviousTime = machine.previousTime;
        if (machine.previous >= 0)
        {
            machine.fade = std::min(machine.fade + dt, machine.fadeDuration);
        }
        const f32 weight = currentWeight(machine);
        if (machine.previous >= 0)
        {
            const auto previous = static_cast<u32>(machine.previous);
            const AnimationState& faded = layer.states[previous];
            const f32 step = normalizedStep(binding, base + previous, faded, state.parameters, dt);
            if (weight < 1.0f)
            {
                writer.crossed(layerIndex, static_cast<u16>(previous), faded, machine.previousTime, step);
            }
            advanceTime(machine.previousTime, step, faded.loop);
        }
        const AnimationState& current = layer.states[machine.state];
        const f32 step = normalizedStep(binding, base + machine.state, current, state.parameters, dt);
        if (weight > 0.0f)
        {
            writer.crossed(layerIndex, static_cast<u16>(machine.state), current, machine.time, step);
        }
        const bool wrapped = advanceTime(machine.time, step, current.loop);
        if (machine.previous >= 0 && !(machine.fade < machine.fadeDuration))
        {
            machine.previous = -1;
            machine.fade = 0.0f;
            machine.fadeDuration = 0.0f;
        }
        if (machine.previous >= 0)
        {
            continue;
        }
        for (const AnimationTransition& transition : layer.transitions)
        {
            const bool fromHere = transition.from < 0 ? transition.to != machine.state
                                                      : static_cast<u32>(transition.from) == machine.state;
            if (!fromHere || (transition.exitTime >= 0.0f && machine.time < transition.exitTime && !wrapped))
            {
                continue;
            }
            const bool holds =
                std::all_of(transition.conditions.begin(), transition.conditions.end(),
                            [&](const AnimationCondition& c) { return conditionHolds(c, state); });
            if (!holds)
            {
                continue;
            }
            for (const AnimationCondition& condition : transition.conditions)
            {
                if (condition.compare == AnimationCompare::TriggerSet)
                {
                    state.triggers[condition.parameter] = kTriggerUnset;
                }
            }
            if (transition.duration > 0.0f)
            {
                machine.previous = static_cast<i32>(machine.state);
                machine.previousTime = machine.time;
                machine.lastPreviousTime = machine.lastTime;
                machine.fade = 0.0f;
                machine.fadeDuration = transition.duration;
            }
            machine.state = transition.to;
            machine.time = 0.0f;
            machine.lastTime = 0.0f;
            break;
        }
    }
    for (u8& trigger : state.triggers)
    {
        trigger = trigger == kTriggerSet ? kTriggerUnset : trigger;
    }
    return writer.count();
}

void evaluateGraph(const GraphBinding& binding, const AnimationGraphState& state, LinearAllocator& scratch,
                   std::span<JointPose> out, RootMotionDelta* root) noexcept
{
    ILL_PROFILE_SCOPE();
    ILL_ASSERT(binding.graph != nullptr && binding.skeleton != nullptr &&
                   state.layers.size() == binding.graph->layers.size(),
               "evaluateGraph needs a state made for this binding");
    const AnimationGraph& graph = *binding.graph;
    const Skeleton& skeleton = *binding.skeleton;
    const usize joints = skeleton.joints.size();
    ILL_ASSERT(out.size() >= joints, "evaluateGraph needs one JointPose per joint");
    if (root != nullptr)
    {
        *root = {};
    }
    if (binding.basePose.size() == joints)
    {
        std::copy(binding.basePose.begin(), binding.basePose.end(), out.begin());
    }
    else
    {
        restPose(skeleton, out);
    }
    const usize layerCount = std::min(graph.layers.size(), state.layers.size());
    const usize maxClips = layerCount * 2 * kMaxStateClips;
    const LinearAllocator::Marker marker = scratch.mark();
    JointPose* poses = scratch.allocateArray<JointPose>(joints * 4);
    ActiveClip* clipMemory = scratch.allocateArray<ActiveClip>(maxClips);
    u8* trimmedMemory = scratch.allocateArray<u8>(layerCount);
    const bool modelSpace = std::ranges::any_of(graph.layers, [](const AnimationLayer& layer) {
        return layer.blend == AnimationLayerBlend::Additive && layer.space == AnimationAdditiveSpace::Model;
    });
    Quat* rotations = modelSpace ? scratch.allocateArray<Quat>(joints * 3) : nullptr;
    if (poses == nullptr || clipMemory == nullptr || trimmedMemory == nullptr ||
        (modelSpace && rotations == nullptr))
    {
        ILL_ASSERT(false, "evaluateGraph: the scratch allocator is too small");
        scratch.rewind(marker);
        return;
    }
    const std::span<JointPose> layerPose(poses, joints);
    const std::span<JointPose> samplePose(poses + joints, joints);
    const std::span<JointPose> referencePose(poses + joints * 2, joints);
    const std::span<JointPose> referenceSample(poses + joints * 3, joints);
    const std::span<ActiveClip> clips(clipMemory, maxClips);
    const std::span<u8> trimmed(trimmedMemory, layerCount);
    std::fill(trimmed.begin(), trimmed.end(), u8{0});

    u32 count = 0;
    for (usize l = 0; l < layerCount; ++l)
    {
        const GraphLayerState& machine = state.layers[l];
        const f32 weight = currentWeight(machine);
        if (machine.previous >= 0)
        {
            addStateClips(binding, state, static_cast<u32>(l), static_cast<u32>(machine.previous),
                          1.0f - weight, machine.previousTime, machine.lastPreviousTime, clips, count);
        }
        addStateClips(binding, state, static_cast<u32>(l), machine.state, weight, machine.time,
                      machine.lastTime, clips, count);
    }
    count = capClips(clips, count, trimmed);

    const bool extract = root != nullptr && binding.rootJoint >= 0;
    const auto rootJoint = static_cast<u32>(std::max(binding.rootJoint, 0));
    u32 next = 0;
    for (usize l = 0; l < layerCount; ++l)
    {
        const AnimationLayer& layer = graph.layers[l];
        const bool additive = layer.blend == AnimationLayerBlend::Additive;
        const i32 referenceClip = l < binding.referenceClips.size() ? binding.referenceClips[l] : -1;
        f32 accumulated = 0.0f;
        RootMotionDelta layerRoot;
        for (; next < count && clips[next].layer == l; ++next)
        {
            const ActiveClip& active = clips[next];
            const AnimationClip& clip = *binding.clips[active.clip];
            const ClipBinding& clipBinding = binding.clipBindings[active.clip];
            const ClipRetarget& retarget = binding.clipRetargets[active.clip];
            const f32 share = active.weight / (accumulated + active.weight);
            accumulated += active.weight;
            sampleClip(clip, clipBinding, retarget, skeleton, clipSeconds(clip, active.time, active.loop),
                       share < 1.0f ? samplePose : layerPose);
            if (share < 1.0f)
            {
                blendPoses(layerPose, samplePose, share, {}, layerPose);
            }
            if (additive && referenceClip < 0)
            {
                sampleClip(clip, clipBinding, retarget, skeleton, 0.0f,
                           share < 1.0f ? referenceSample : referencePose);
                if (share < 1.0f)
                {
                    blendPoses(referencePose, referenceSample, share, {}, referencePose);
                }
            }
            if (extract)
            {
                const RootMotionDelta delta = clipRootMotion(binding, active, rootJoint);
                layerRoot.translation += delta.translation * active.weight;
                layerRoot.yaw += delta.yaw * active.weight;
            }
        }
        if (!(accumulated > 0.0f) || !(layer.weight > 0.0f))
        {
            continue;
        }
        const std::span<const f32> mask = binding.masks[l];
        if (additive && referenceClip >= 0)
        {
            const auto index = static_cast<usize>(referenceClip);
            const AnimationClip& clip = *binding.clips[index];
            sampleClip(clip, binding.clipBindings[index], binding.clipRetargets[index], skeleton,
                       clipSeconds(clip, layer.referenceTime, false), referencePose);
        }
        if (additive && layer.space == AnimationAdditiveSpace::Model)
        {
            addPoseInModelSpace(skeleton, out.first(joints), layerPose, referencePose, layer.weight, mask,
                                std::span<Quat>(rotations, joints * 3));
        }
        else if (additive)
        {
            addPose(out.first(joints), layerPose, referencePose, layer.weight, mask);
        }
        else
        {
            blendPoses(out.first(joints), layerPose, layer.weight, mask, out.first(joints));
        }
        if (extract)
        {
            const f32 influence = layer.weight * (mask.empty() ? 1.0f : mask[rootJoint]);
            if (additive)
            {
                root->translation += layerRoot.translation * influence;
                root->yaw += layerRoot.yaw * influence;
            }
            else
            {
                root->translation = glm::mix(root->translation, layerRoot.translation, influence);
                root->yaw += (layerRoot.yaw - root->yaw) * influence;
            }
        }
    }
    if (extract)
    {
        removeRootMotion(skeleton, binding.basePose, rootJoint, out);
    }
    scratch.rewind(marker);
}

} // namespace illusion
