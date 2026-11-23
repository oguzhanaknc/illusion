#pragma once

/// @file
/// Animation graphs (docs/adr/0060-animation-graphs.md): the content of a `.animgraph` file, layered state
/// machines whose states play clips or blend spaces, driven by parameters. Plain data and a parser; the live
/// state and evaluation are in AnimationGraphState.hpp.
///
/// @code
/// {
///   "format": "illusion.animgraph", "version": 1,
///   "parameters": [{"name": "speed", "type": "Float"}, {"name": "jump", "type": "Trigger"}],
///   "rootMotion": "hips",
///   "layers": [{
///     "name": "base", "entry": "locomotion",
///     "states": [
///       {"name": "locomotion", "blend1d": {"parameter": "speed",
///         "points": [{"value": 0, "clip": "game:/hero.glb#animation/idle"},
///                    {"value": 2, "clip": "game:/hero.glb#animation/walk"},
///                    {"value": 6, "clip": "game:/hero.glb#animation/run"}]},
///        "events": [{"name": "footstep", "time": 0.1}, {"name": "footstep", "time": 0.6}]},
///       {"name": "jump", "clip": "game:/hero.glb#animation/jump", "loop": false}],
///     "transitions": [
///       {"from": "locomotion", "to": "jump", "duration": 0.1, "conditions": [{"parameter": "jump"}]},
///       {"from": "jump", "to": "locomotion", "duration": 0.2, "exitTime": 0.9}]
///   }]
/// }
/// @endcode

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// File extension of animation graphs.
inline constexpr std::string_view kAnimationGraphExtension = ".animgraph";

/// Version of the `.animgraph` format that parseAnimationGraph() writes and reads.
inline constexpr u32 kAnimationGraphVersion = 1;

/// Most clips one Animator samples in an update; the lightest are dropped beyond it.
inline constexpr u32 kMaxActiveClips = 16;

/// The type of an AnimationParameter.
enum class AnimationParameterType : u8
{
    Float,   ///< A number (speed, direction).
    Int,     ///< A whole number (weapon kind).
    Bool,    ///< On or off (crouching).
    Trigger, ///< Set once; consumed by the transition it fires, or cleared at the end of the update.
};

/// A named input of a graph.
struct AnimationParameter
{
    std::string name;                                            ///< Unique in the graph.
    AnimationParameterType type = AnimationParameterType::Float; ///< Its type.
    f32 defaultValue = 0.0f; ///< Initial value (Int and Bool as 0/1; triggers start unset).
};

/// What a state plays.
enum class AnimationMotionType : u8
{
    Clip,    ///< One clip.
    Blend1D, ///< Clips at values of one Float parameter.
    Blend2D, ///< Clips at points of two Float parameters.
};

/// A clip of a blend space.
struct AnimationBlendPoint
{
    AssetRef clip; ///< `model.glb#animation/<name>`.
    f32 x = 0.0f;  ///< Value (Blend1D) or first coordinate (Blend2D).
    f32 y = 0.0f;  ///< Second coordinate (Blend2D).
};

/// What a state plays: a clip or a blend space whose clips play in sync on normalized time.
struct AnimationMotion
{
    AnimationMotionType type = AnimationMotionType::Clip; ///< Its type.
    AssetRef clip;                                        ///< Clip: the clip.
    u32 parameterX = 0; ///< Blend1D, Blend2D: index of the Float parameter of x.
    u32 parameterY = 0; ///< Blend2D: index of the Float parameter of y.
    /// Blend1D: sorted by x, distinct. Blend2D: at least three points, not all on one line.
    std::vector<AnimationBlendPoint> points;
    /// Blend2D: the Delaunay triangles of points, three point indices each, computed by
    /// parseAnimationGraph().
    std::vector<u32> triangles;
};

/// A named moment of a state, reported when its time crosses it.
struct AnimationStateEvent
{
    std::string name; ///< Reported name.
    f32 time = 0.0f;  ///< Normalized time in [0, 1].
};

/// A state of a layer's state machine.
struct AnimationState
{
    std::string name;                        ///< Unique in its layer.
    AnimationMotion motion;                  ///< What it plays.
    f32 speed = 1.0f;                        ///< Playback rate.
    i32 speedParameter = -1;                 ///< Float parameter the speed is multiplied by, or -1.
    bool loop = true;                        ///< Wrap at the end; otherwise hold the last pose.
    std::vector<AnimationStateEvent> events; ///< Sorted by time.
};

/// How a condition compares a parameter.
enum class AnimationCompare : u8
{
    Greater,   ///< Float, Int: parameter > value.
    Less,      ///< Float, Int: parameter < value.
    Equal,     ///< Int, Bool: parameter == value.
    NotEqual,  ///< Int, Bool: parameter != value.
    TriggerSet ///< Trigger: it is set.
};

/// One condition of a transition.
struct AnimationCondition
{
    u32 parameter = 0;                                       ///< Parameter index.
    AnimationCompare compare = AnimationCompare::TriggerSet; ///< Comparison.
    f32 value = 0.0f;                                        ///< Compared value (Int and Bool as numbers).
};

/// A change of state; fires when all its conditions hold (and its exit time passed, if it has one).
struct AnimationTransition
{
    i32 from = -1;        ///< Source state index, or -1 for any state (not the target itself).
    u32 to = 0;           ///< Target state index.
    f32 duration = 0.0f;  ///< Cross-fade in seconds.
    f32 exitTime = -1.0f; ///< Normalized time of the source after which it may fire, or -1 for any time.
    std::vector<AnimationCondition> conditions; ///< All must hold.
};

/// How a layer combines with the layers below it.
enum class AnimationLayerBlend : u8
{
    Override, ///< Replaces them by the layer's weight and mask.
    Additive, ///< Adds its difference from the first frame of each clip.
};

/// Where an Additive layer adds its difference (ADR 0065).
enum class AnimationAdditiveSpace : u8
{
    Local, ///< To each joint's local rotation (addPose()).
    Model, ///< To each joint's model-space rotation (addPoseInModelSpace()); for aim offsets.
};

/// One joint of a layer mask; it covers the joint and its descendants (a later entry wins for its subtree).
struct AnimationMaskJoint
{
    std::string joint; ///< Joint name.
    f32 weight = 1.0f; ///< In [0, 1].
};

/// A layer: a state machine applied with a weight and an optional mask.
struct AnimationLayer
{
    std::string name;                                          ///< Unique in the graph.
    f32 weight = 1.0f;                                         ///< In [0, 1].
    AnimationLayerBlend blend = AnimationLayerBlend::Override; ///< How it combines.
    std::vector<AnimationMaskJoint> mask;                      ///< Empty: every joint at weight 1.
    u32 entry = 0;                                             ///< Index of the entry state.
    std::vector<AnimationState> states;                        ///< At least one.
    std::vector<AnimationTransition> transitions;              ///< In priority order.
};

/// The content of a `.animgraph` file.
struct AnimationGraph
{
    std::vector<AnimationParameter> parameters; ///< Inputs.
    std::vector<AnimationLayer> layers;         ///< At least one; evaluated in order.
    std::string rootMotion; ///< Joint whose horizontal motion and yaw drive the Entity, or empty.

    /// Index of the parameter @p name, or -1.
    [[nodiscard]] i32 findParameter(std::string_view name) const noexcept;
    /// Index of the layer @p name, or -1.
    [[nodiscard]] i32 findLayer(std::string_view name) const noexcept;
};

/// Parses and validates a `.animgraph` file (names unique, references resolved, values finite, blend spaces
/// well formed). An error message names the JSON path of the problem, e.g. "layers[0].states[2].motion".
[[nodiscard]] Expected<AnimationGraph, std::string> parseAnimationGraph(std::string_view json);

/// Writes @p graph as `.animgraph` JSON (format tag and kAnimationGraphVersion); parseAnimationGraph() reads
/// it back unchanged.
[[nodiscard]] std::string writeAnimationGraph(const AnimationGraph& graph);

} // namespace illusion
