#include <illusion/camera/CameraComponents.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/StructRegistry.hpp>

#include <string_view>

namespace illusion
{
namespace
{

constexpr std::string_view kRequiresVirtualCamera[] = {"illusion.Transform", "illusion.VirtualCamera"};
constexpr std::string_view kRequiresTransform[] = {"illusion.Transform"};
constexpr std::string_view kRequiresCamera[] = {"illusion.Transform", "illusion.Camera"};
constexpr f64 kQuarterTurn = 1.5707963267948966;
constexpr f64 kHalfTurn = 3.141592653589793;

constexpr FieldMeta angle(std::string_view description, std::string_view category = {}) noexcept
{
    return {.description = description,
            .category = category,
            .unit = Unit::Radians,
            .displayUnit = Unit::Degrees,
            .hint = FieldHint::Angle};
}

constexpr FieldMeta pitch(std::string_view description) noexcept
{
    return {.description = description,
            .category = "Angles",
            .unit = Unit::Radians,
            .displayUnit = Unit::Degrees,
            .min = -kQuarterTurn,
            .max = kQuarterTurn,
            .hint = FieldHint::Angle};
}

constexpr FieldMeta seconds(std::string_view description, std::string_view category = {}) noexcept
{
    return {.description = description, .category = category, .unit = Unit::Seconds, .min = 0.0};
}

void registerStructs(ComponentRegistry& registry)
{
    reflectStruct<CameraBlend>("illusion.CameraBlend", 1, registry.structs())
        .meta({.displayName = "Camera blend", .docsAnchor = "manual/cameras.md#the-camera-brain"})
        .field("style", &CameraBlend::style, {.description = "How the weight changes over the blend."})
        .field("duration", &CameraBlend::duration,
               {.description = "Length of the blend; 0 cuts.", .unit = Unit::Seconds, .min = 0.0})
        .field("control1", &CameraBlend::control1,
               {.description = "First control point of a Curve, as CSS cubic-bezier()."})
        .field("control2", &CameraBlend::control2,
               {.description = "Second control point of a Curve, as CSS cubic-bezier()."});

    reflectStruct<CameraPathPoint>("illusion.CameraPathPoint", 1, registry.structs())
        .meta({.displayName = "Path point", .docsAnchor = "manual/cameras.md#dolly"})
        .field("position", &CameraPathPoint::position,
               {.description = "Point in the space of the camera's Entity.", .unit = Unit::Meters})
        .field("tangent", &CameraPathPoint::tangent,
               {.description = "Curve derivative at the point, per unit of path position."});

    reflectStruct<CameraBlendOverride>("illusion.CameraBlendOverride", 1, registry.structs())
        .meta({.displayName = "Blend override", .docsAnchor = "manual/cameras.md#the-camera-brain"})
        .field("from", &CameraBlendOverride::from, {.description = "The camera blended from; empty: any."})
        .field("to", &CameraBlendOverride::to, {.description = "The camera blended to; empty: any."})
        .field("blend", &CameraBlendOverride::blend, {.description = "The blend of this pair."});
}

void registerVirtualCamera(ComponentRegistry& registry)
{
    reflect<VirtualCamera>("illusion.VirtualCamera", 1, registry)
        .meta({.displayName = "Virtual camera",
               .category = "Camera",
               .icon = "videocam",
               .description = "A camera shot that a CameraBrain shows when it has the highest priority.",
               .docsAnchor = "manual/cameras.md#virtual-cameras",
               .requiredComponents = kRequiresTransform})
        .field("priority", &VirtualCamera::priority,
               {.description = "The brain shows the enabled camera with the highest priority."})
        .field("enabled", &VirtualCamera::enabled, {.description = "Can be shown."})
        .field("follow", &VirtualCamera::follow,
               {.description = "Target of the body stage; empty: the camera's own Entity.",
                .category = "Targets"})
        .field("lookAt", &VirtualCamera::lookAt,
               {.description = "Target of the aim stage; empty: the follow target.", .category = "Targets"})
        .field("fovY", &VirtualCamera::fovY,
               {.description = "Vertical field of view.",
                .category = "Lens",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0017453292519943296,
                .max = 3.12413936106985,
                .hint = FieldHint::Angle})
        .field(
            "nearPlane", &VirtualCamera::nearPlane,
            {.description = "Near clip distance.", .category = "Lens", .unit = Unit::Meters, .min = 0.0001})
        .field("dutch", &VirtualCamera::dutch, angle("Roll about the view axis.", "Lens"))
        .field("customBlend", &VirtualCamera::customBlend,
               {.description = "Blend to this camera with blendIn unless the brain has a pair override.",
                .category = "Blend"})
        .field("blendIn", &VirtualCamera::blendIn,
               {.description = "The blend to this camera.", .category = "Blend"});
}

void registerBodies(ComponentRegistry& registry)
{
    reflect<CameraFollow>("illusion.CameraFollow", 1, registry)
        .meta({.displayName = "Camera follow",
               .category = "Camera/Body",
               .icon = "directions_run",
               .description = "Keeps the virtual camera at an offset from its follow target.",
               .docsAnchor = "manual/cameras.md#follow",
               .requiredComponents = kRequiresVirtualCamera})
        .field("offset", &CameraFollow::offset,
               {.description = "From the target to the camera.", .unit = Unit::Meters})
        .field("space", &CameraFollow::space,
               {.description = "Target: the offset turns with the target; World: world axes."})
        .field("damping", &CameraFollow::damping,
               {.description = "Smooth time per axis.", .unit = Unit::Seconds, .min = 0.0})
        .field("rotationDamping", &CameraFollow::rotationDamping, seconds("Smooth time of the rotation."));

    reflect<CameraOrbit>("illusion.CameraOrbit", 1, registry)
        .meta({.displayName = "Camera orbit",
               .category = "Camera/Body",
               .icon = "360",
               .description = "Turns the virtual camera around its follow target with input actions.",
               .docsAnchor = "manual/cameras.md#orbit",
               .requiredComponents = kRequiresVirtualCamera})
        .field("yaw", &CameraOrbit::yaw, angle("Starting yaw about world +y.", "Angles"))
        .field("pitch", &CameraOrbit::pitch, pitch("Starting pitch; positive looks up."))
        .field("minPitch", &CameraOrbit::minPitch, pitch("Lowest pitch."))
        .field("maxPitch", &CameraOrbit::maxPitch, pitch("Highest pitch."))
        .field("limitYaw", &CameraOrbit::limitYaw,
               {.description = "Clamp yaw to [minYaw, maxYaw] instead of wrapping it.", .category = "Angles"})
        .field("minYaw", &CameraOrbit::minYaw,
               {.description = "Lowest yaw when limitYaw.",
                .category = "Angles",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = -kHalfTurn,
                .max = kHalfTurn,
                .hint = FieldHint::Angle})
        .field("maxYaw", &CameraOrbit::maxYaw,
               {.description = "Highest yaw when limitYaw.",
                .category = "Angles",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = -kHalfTurn,
                .max = kHalfTurn,
                .hint = FieldHint::Angle})
        .field("distance", &CameraOrbit::distance,
               {.description = "Starting distance from the pivot.",
                .category = "Distance",
                .unit = Unit::Meters,
                .min = 0.0})
        .field("minDistance", &CameraOrbit::minDistance,
               {.description = "Closest zoom.", .category = "Distance", .unit = Unit::Meters, .min = 0.0})
        .field("maxDistance", &CameraOrbit::maxDistance,
               {.description = "Farthest zoom.", .category = "Distance", .unit = Unit::Meters, .min = 0.0})
        .field("pivotOffset", &CameraOrbit::pivotOffset,
               {.description = "From the target to the pivot, in world axes.", .unit = Unit::Meters})
        .field("damping", &CameraOrbit::damping, seconds("Smooth time of the angles."))
        .field("lookAction", &CameraOrbit::lookAction,
               {.description = "Vector action that turns it: x yaws, y pitches; empty for none.",
                .category = "Input"})
        .field(
            "lookSpeed", &CameraOrbit::lookSpeed,
            {.description = "Radians per second at full deflection; negative inverts.", .category = "Input"})
        .field("zoomAction", &CameraOrbit::zoomAction,
               {.description = "Axis action that changes the distance; empty for none.", .category = "Input"})
        .field("zoomSpeed", &CameraOrbit::zoomSpeed,
               {.description = "Distance change at full deflection.",
                .category = "Input",
                .unit = Unit::MetersPerSecond});

    reflect<CameraSpringArm>("illusion.CameraSpringArm", 1, registry)
        .meta({.displayName = "Camera spring arm",
               .category = "Camera/Body",
               .icon = "switch_camera",
               .description = "Holds the virtual camera over its follow target's shoulder, pulled in to stay "
                              "out of walls.",
               .docsAnchor = "manual/cameras.md#spring-arm",
               .requiredComponents = kRequiresVirtualCamera})
        .field("shoulderOffset", &CameraSpringArm::shoulderOffset,
               {.description = "Side distance, height and back distance of the arm's start.",
                .unit = Unit::Meters})
        .field("side", &CameraSpringArm::side,
               {.description = "-1 for the left shoulder, 1 for the right.", .min = -1.0, .max = 1.0})
        .field("sideDamping", &CameraSpringArm::sideDamping, seconds("Smooth time of a shoulder swap."))
        .field("armLength", &CameraSpringArm::armLength,
               {.description = "Distance behind the shoulder.", .unit = Unit::Meters, .min = 0.0})
        .field("collisionRadius", &CameraSpringArm::collisionRadius,
               {.description = "Radius of the cast sphere.",
                .category = "Collision",
                .unit = Unit::Meters,
                .min = 0.0})
        .field("collisionLayers", &CameraSpringArm::collisionLayers,
               {.description = "Physics layers the arm collides with.",
                .category = "Collision",
                .hint = FieldHint::LayerMask})
        .field("pullInSpeed", &CameraSpringArm::pullInSpeed,
               {.description = "Speed the arm shortens at; 0 is at once.",
                .category = "Collision",
                .unit = Unit::MetersPerSecond,
                .min = 0.0})
        .field("recoverySpeed", &CameraSpringArm::recoverySpeed,
               {.description = "Speed the arm grows back at; 0 is at once.",
                .category = "Collision",
                .unit = Unit::MetersPerSecond,
                .min = 0.0})
        .field("alignToTarget", &CameraSpringArm::alignToTarget,
               {.description = "Yaw is relative to the target's facing (its -z axis).", .category = "Angles"})
        .field("yaw", &CameraSpringArm::yaw, angle("Starting yaw.", "Angles"))
        .field("pitch", &CameraSpringArm::pitch, pitch("Starting pitch; positive looks up."))
        .field("minPitch", &CameraSpringArm::minPitch, pitch("Lowest pitch."))
        .field("maxPitch", &CameraSpringArm::maxPitch, pitch("Highest pitch."))
        .field("lookAction", &CameraSpringArm::lookAction,
               {.description = "Vector action that turns it: x yaws, y pitches; empty for none.",
                .category = "Input"})
        .field(
            "lookSpeed", &CameraSpringArm::lookSpeed,
            {.description = "Radians per second at full deflection; negative inverts.", .category = "Input"});

    reflect<CameraDolly>("illusion.CameraDolly", 1, registry)
        .meta({.displayName = "Camera dolly",
               .category = "Camera/Body",
               .icon = "timeline",
               .description = "Moves the virtual camera along a track.",
               .docsAnchor = "manual/cameras.md#dolly",
               .requiredComponents = kRequiresVirtualCamera})
        .field("path", &CameraDolly::path,
               {.description = "The track, in the space of the camera's Entity.", .category = "Track"})
        .field("closed", &CameraDolly::closed,
               {.description = "The last point joins the first.", .category = "Track"})
        .field("mode", &CameraDolly::mode,
               {.description = "Parameter: go to position; NearestToTarget: follow the target."})
        .field(
            "position", &CameraDolly::position,
            {.description = "Goal path position in Parameter mode (point index plus fraction).", .min = 0.0})
        .field("damping", &CameraDolly::damping, seconds("Smooth time of the path position."));
}

void registerAims(ComponentRegistry& registry)
{
    reflect<CameraLookAt>("illusion.CameraLookAt", 1, registry)
        .meta({.displayName = "Camera look-at",
               .category = "Camera/Aim",
               .icon = "center_focus_strong",
               .description = "Turns the virtual camera to face its look-at target.",
               .docsAnchor = "manual/cameras.md#look-at",
               .requiredComponents = kRequiresVirtualCamera})
        .field("targetOffset", &CameraLookAt::targetOffset,
               {.description = "Added to the target's position, in world axes.", .unit = Unit::Meters})
        .field("damping", &CameraLookAt::damping, seconds("Smooth time of the rotation."));

    reflect<CameraComposer>("illusion.CameraComposer", 1, registry)
        .meta({.displayName = "Camera composer",
               .category = "Camera/Aim",
               .icon = "grid_on",
               .description =
                   "Keeps the look-at target at a place on screen, turning only when it leaves the "
                   "dead zone.",
               .docsAnchor = "manual/cameras.md#composer",
               .requiredComponents = kRequiresVirtualCamera})
        .field("targetOffset", &CameraComposer::targetOffset,
               {.description = "Added to the target's position, in world axes.", .unit = Unit::Meters})
        .field("screenPosition", &CameraComposer::screenPosition,
               {.description = "Where the target should be, from -1 (left, bottom) to 1 (right, top).",
                .min = -1.0,
                .max = 1.0})
        .field(
            "deadZone", &CameraComposer::deadZone,
            {.description = "Half size of the zone where the camera does not turn.", .min = 0.0, .max = 2.0})
        .field("softZone", &CameraComposer::softZone,
               {.description = "Half size of the zone the target never leaves.", .min = 0.0, .max = 2.0})
        .field("damping", &CameraComposer::damping,
               {.description = "Smooth time of the horizontal and vertical correction.",
                .unit = Unit::Seconds,
                .min = 0.0});
}

void registerEffects(ComponentRegistry& registry)
{
    reflect<CameraNoise>("illusion.CameraNoise", 1, registry)
        .meta({.displayName = "Camera noise",
               .category = "Camera",
               .icon = "vibration",
               .description = "Handheld shake of the virtual camera.",
               .docsAnchor = "manual/cameras.md#noise-and-shakes",
               .requiredComponents = kRequiresVirtualCamera})
        .field("positionAmplitude", &CameraNoise::positionAmplitude,
               {.description = "Largest offset along the camera's x, y and z.",
                .category = "Position",
                .unit = Unit::Meters,
                .min = 0.0})
        .field("positionFrequency", &CameraNoise::positionFrequency,
               {.description = "Base frequency per position axis.",
                .category = "Position",
                .unit = Unit::Hertz,
                .min = 0.0})
        .field("rotationAmplitude", &CameraNoise::rotationAmplitude,
               {.description = "Largest pitch, yaw and roll.",
                .category = "Rotation",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .hint = FieldHint::Angle})
        .field("rotationFrequency", &CameraNoise::rotationFrequency,
               {.description = "Base frequency per rotation axis.",
                .category = "Rotation",
                .unit = Unit::Hertz,
                .min = 0.0})
        .field("amplitudeGain", &CameraNoise::amplitudeGain,
               {.description = "Scales every amplitude.", .min = 0.0})
        .field("frequencyGain", &CameraNoise::frequencyGain,
               {.description = "Scales every frequency.", .min = 0.0})
        .field(
            "seed", &CameraNoise::seed,
            {.description = "Chooses the phases, so that cameras with the same settings shake differently."});

    reflect<CameraImpulse>("illusion.CameraImpulse", 1, registry)
        .meta({.displayName = "Camera impulse",
               .category = "Camera",
               .icon = "flash_on",
               .description = "Shakes every live camera within its radius.",
               .docsAnchor = "manual/cameras.md#noise-and-shakes",
               .requiredComponents = kRequiresTransform})
        .field("strength", &CameraImpulse::strength,
               {.description = "Shake at the source; 1 is a strong hit.", .min = 0.0})
        .field(
            "radius", &CameraImpulse::radius,
            {.description = "Distance where the shake fades to nothing.", .unit = Unit::Meters, .min = 0.0})
        .field("duration", &CameraImpulse::duration,
               {.description = "Time until it fades out.", .unit = Unit::Seconds, .min = 0.0})
        .field("playOnStart", &CameraImpulse::playOnStart,
               {.description = "Fire in the first update that sees it."})
        .field("interval", &CameraImpulse::interval, seconds("Time between repeats; 0 fires once."));
}

} // namespace

void registerCameraComponents(ComponentRegistry& registry)
{
    registerStructs(registry);
    registerVirtualCamera(registry);
    registerBodies(registry);
    registerAims(registry);
    registerEffects(registry);

    reflect<CameraBrain>("illusion.CameraBrain", 1, registry)
        .meta({.displayName = "Camera brain",
               .category = "Camera",
               .icon = "linked_camera",
               .description =
                   "Moves the Camera on its Entity to the live virtual camera, blending between them.",
               .docsAnchor = "manual/cameras.md#the-camera-brain",
               .requiredComponents = kRequiresCamera})
        .field("defaultBlend", &CameraBrain::defaultBlend, {.description = "The blend without an override."})
        .field("blends", &CameraBrain::blends, {.description = "Per-pair blends; the first match wins."});

    reflectRuntime<VirtualCameraState>("illusion.VirtualCameraState", {.displayName = "Virtual camera state"},
                                       registry);
    reflectRuntime<CameraBrainState>("illusion.CameraBrainState", {.displayName = "Camera brain state"},
                                     registry);
    reflectRuntime<CameraImpulseState>("illusion.CameraImpulseState", {.displayName = "Camera impulse state"},
                                       registry);
}

} // namespace illusion
