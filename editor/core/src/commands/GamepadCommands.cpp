// The gamepad commands: the state of the connected gamepads and their outputs (rumble, light, DualSense
// adaptive triggers, motion sensors) through the editor's GamepadHost.

#include "CommandSupport.hpp"

#include <illusion/input/Gamepad.hpp>

#include <algorithm>
#include <cmath>

namespace illusion::editor::detail
{
namespace
{

static_assert(kGamepadButtons == static_cast<usize>(GamepadButton::Count));
static_assert(kGamepadAxes == static_cast<usize>(GamepadAxis::Count));
static_assert(kGamepadFingers == GamepadState::kMaxFingers);

constexpr u32 kDefaultRumbleMs = 500;
constexpr u32 kMaxRumbleMs = 10000;

Expected<GamepadHost*, CommandError> requireGamepads(CommandContext& context)
{
    GamepadHost* gamepads = context.editor().gamepads();
    if (gamepads == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "this editor has no gamepad support"));
    }
    return gamepads;
}

GamepadEntry entryOf(const GamepadSnapshot& pad)
{
    GamepadEntry entry;
    entry.id = pad.id;
    entry.name = pad.name;
    entry.type = pad.type;
    entry.player = pad.player;
    const GamepadFeatures& features = pad.features;
    for (const auto& [has, name] :
         {std::pair{features.rumble, "rumble"}, std::pair{features.triggerRumble, "trigger_rumble"},
          std::pair{features.led, "led"}, std::pair{features.motionSensors, "motion"},
          std::pair{features.touchpad, "touchpad"},
          std::pair{features.adaptiveTriggers, "adaptive_triggers"}})
    {
        if (has)
        {
            entry.features.emplace_back(name);
        }
    }
    for (usize b = 0; b < kGamepadButtons; ++b)
    {
        if (pad.buttons[b])
        {
            entry.buttons.emplace_back(gamepadButtonName(static_cast<GamepadButton>(b)));
        }
    }
    for (usize a = 0; a < kGamepadAxes; ++a)
    {
        entry.axes.push_back({std::string(gamepadAxisName(static_cast<GamepadAxis>(a))), pad.axes[a]});
    }
    if (features.touchpad)
    {
        for (const GamepadFingerState& finger : pad.fingers)
        {
            entry.fingers.push_back({finger.down, finger.position, finger.pressure});
        }
    }
    entry.motion = pad.motionEnabled;
    entry.gyroscope = pad.gyroscope;
    entry.accelerometer = pad.accelerometer;
    return entry;
}

// The gamepad a command acts on: @p id, or the first connected one.
Expected<GamepadSnapshot, CommandError> findGamepad(const GamepadHost& host, const std::optional<u32>& id)
{
    std::vector<GamepadSnapshot> pads;
    host.gamepads(pads);
    if (pads.empty())
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "no gamepad is connected"));
    }
    if (!id)
    {
        return std::move(pads.front());
    }
    for (GamepadSnapshot& pad : pads)
    {
        if (pad.id == *id)
        {
            return std::move(pad);
        }
    }
    return unexpected(
        CommandError::make(ErrorKind::NotFound, "no connected gamepad has id " + std::to_string(*id)));
}

Expected<void, CommandError> checkUnit(const std::optional<f32>& value, const char* pointer)
{
    if (value && !(std::isfinite(*value) && *value >= 0.0f && *value <= 1.0f))
    {
        return unexpected(CommandError::invalid(pointer, "must be between 0 and 1"));
    }
    return {};
}

CommandError unsupported(const GamepadSnapshot& pad, std::string_view feature)
{
    return CommandError::make(ErrorKind::Unsupported, "gamepad " + std::to_string(pad.id) + " (" + pad.type +
                                                          ") has no " + std::string(feature));
}

// The state after an output command changed it.
Expected<GamepadEntry, CommandError> stateOf(const GamepadHost& host, u32 id)
{
    Expected<GamepadSnapshot, CommandError> pad = findGamepad(host, id);
    if (!pad)
    {
        return unexpected(std::move(pad).error());
    }
    return entryOf(*pad);
}

Expected<GamepadListResult, CommandError> gamepadList(CommandContext& context, const NoParams&)
{
    Expected<GamepadHost*, CommandError> host = requireGamepads(context);
    if (!host)
    {
        return unexpected(std::move(host).error());
    }
    std::vector<GamepadSnapshot> pads;
    (*host)->gamepads(pads);
    GamepadListResult result;
    for (const GamepadSnapshot& pad : pads)
    {
        result.gamepads.push_back(entryOf(pad));
    }
    return result;
}

Expected<GamepadEntry, CommandError> gamepadRumble(CommandContext& context, const GamepadRumbleParams& params)
{
    Expected<GamepadHost*, CommandError> host = requireGamepads(context);
    if (!host)
    {
        return unexpected(std::move(host).error());
    }
    for (const auto& [value, pointer] : {std::pair{params.low, "/low"}, std::pair{params.high, "/high"},
                                         std::pair{params.left, "/left"}, std::pair{params.right, "/right"}})
    {
        if (Expected<void, CommandError> valid = checkUnit(value, pointer); !valid)
        {
            return unexpected(std::move(valid).error());
        }
    }
    if (params.durationMs && *params.durationMs > kMaxRumbleMs)
    {
        return unexpected(CommandError::invalid("/durationMs", "must be at most 10000"));
    }
    const bool motors = params.low || params.high;
    const bool triggers = params.left || params.right;
    if (!motors && !triggers)
    {
        return unexpected(CommandError::invalid("", "give low and/or high, or left and/or right"));
    }
    Expected<GamepadSnapshot, CommandError> pad = findGamepad(**host, params.gamepad);
    if (!pad)
    {
        return unexpected(std::move(pad).error());
    }
    if (motors && !pad->features.rumble)
    {
        return unexpected(unsupported(*pad, "rumble motors"));
    }
    if (triggers && !pad->features.triggerRumble)
    {
        return unexpected(unsupported(*pad, "trigger motors"));
    }
    const u32 duration = params.durationMs.value_or(kDefaultRumbleMs);
    if (motors)
    {
        (void)(*host)->rumble(pad->id, params.low.value_or(0.0f), params.high.value_or(0.0f), duration);
    }
    if (triggers)
    {
        (void)(*host)->rumbleTriggers(pad->id, params.left.value_or(0.0f), params.right.value_or(0.0f),
                                      duration);
    }
    return stateOf(**host, pad->id);
}

Expected<GamepadEntry, CommandError> gamepadSetLed(CommandContext& context, const GamepadLedParams& params)
{
    Expected<GamepadHost*, CommandError> host = requireGamepads(context);
    if (!host)
    {
        return unexpected(std::move(host).error());
    }
    for (u32 c = 0; c < 3; ++c)
    {
        const f32 channel = params.color[static_cast<int>(c)];
        if (!(std::isfinite(channel) && channel >= 0.0f && channel <= 1.0f))
        {
            return unexpected(
                CommandError::invalid("/color/" + std::to_string(c), "must be between 0 and 1"));
        }
    }
    Expected<GamepadSnapshot, CommandError> pad = findGamepad(**host, params.gamepad);
    if (!pad)
    {
        return unexpected(std::move(pad).error());
    }
    if (!pad->features.led)
    {
        return unexpected(unsupported(*pad, "light"));
    }
    const auto byte = [](f32 channel) {
        return static_cast<u8>(std::lround(channel * 255.0f));
    };
    (void)(*host)->setLed(pad->id, byte(params.color.x), byte(params.color.y), byte(params.color.z));
    return stateOf(**host, pad->id);
}

Expected<GamepadEntry, CommandError> gamepadSetTriggerEffect(CommandContext& context,
                                                             const GamepadTriggerEffectParams& params)
{
    Expected<GamepadHost*, CommandError> host = requireGamepads(context);
    if (!host)
    {
        return unexpected(std::move(host).error());
    }
    if (static_cast<usize>(params.trigger) > static_cast<usize>(GamepadTriggerSide::Right))
    {
        return unexpected(CommandError::invalid("/trigger", "is not one of the allowed values"));
    }
    if (static_cast<usize>(params.mode) > static_cast<usize>(GamepadTriggerMode::Vibration))
    {
        return unexpected(CommandError::invalid("/mode", "is not one of the allowed values"));
    }
    for (const auto& [value, pointer] :
         {std::pair{params.start, "/start"}, std::pair{params.end, "/end"},
          std::pair{params.strength, "/strength"}, std::pair{params.amplitude, "/amplitude"}})
    {
        if (Expected<void, CommandError> valid = checkUnit(value, pointer); !valid)
        {
            return unexpected(std::move(valid).error());
        }
    }
    if (params.frequency && (*params.frequency < 1 || *params.frequency > 255))
    {
        return unexpected(CommandError::invalid("/frequency", "must be between 1 and 255"));
    }
    GamepadTriggerEffect effect;
    effect.mode = params.mode;
    effect.start = params.start.value_or(0.0f);
    effect.end = params.end.value_or(1.0f);
    effect.strength = params.strength.value_or(1.0f);
    effect.amplitude = params.amplitude.value_or(1.0f);
    effect.frequency = static_cast<u8>(params.frequency.value_or(30));
    if (effect.mode == GamepadTriggerMode::Weapon && effect.end <= effect.start)
    {
        return unexpected(CommandError::invalid("/end", "must be greater than start"));
    }
    Expected<GamepadSnapshot, CommandError> pad = findGamepad(**host, params.gamepad);
    if (!pad)
    {
        return unexpected(std::move(pad).error());
    }
    if (!pad->features.adaptiveTriggers)
    {
        return unexpected(unsupported(*pad, "adaptive triggers"));
    }
    (void)(*host)->setTriggerEffect(pad->id, params.trigger, effect);
    return stateOf(**host, pad->id);
}

Expected<GamepadEntry, CommandError> gamepadSetMotion(CommandContext& context,
                                                      const GamepadMotionParams& params)
{
    Expected<GamepadHost*, CommandError> host = requireGamepads(context);
    if (!host)
    {
        return unexpected(std::move(host).error());
    }
    Expected<GamepadSnapshot, CommandError> pad = findGamepad(**host, params.gamepad);
    if (!pad)
    {
        return unexpected(std::move(pad).error());
    }
    if (!pad->features.motionSensors)
    {
        return unexpected(unsupported(*pad, "motion sensors"));
    }
    (void)(*host)->setMotionSensors(pad->id, params.enabled);
    return stateOf(**host, pad->id);
}

} // namespace

void registerGamepadCommands(CommandRegistry& registry)
{
    registry.add<&gamepadList>({
        .name = "gamepad.list",
        .title = strings::kCommandGamepadListTitle,
        .description = strings::kCommandGamepadListDescription,
        .agentSummary =
            "Lists the connected gamepads with their model, features, held buttons, axes, touchpad "
            "fingers and motion sensors (the devices Play mode reads). Example: {}.",
        .category = "Input",
        .icon = "sports_esports",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&gamepadRumble>({
        .name = "gamepad.rumble",
        .title = strings::kCommandGamepadRumbleTitle,
        .description = strings::kCommandGamepadRumbleDescription,
        .agentSummary =
            "Rumbles a gamepad (default: the first connected): the low- and high-frequency motors "
            "(low, high) and/or the Xbox trigger motors (left, right), each 0 to 1, for durationMs "
            "(default 500); 0 stops them. Example: {\"low\": 0.5, \"high\": 0.2, \"durationMs\": 300}.",
        .category = "Input",
        .icon = "vibration",
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&gamepadSetLed>({
        .name = "gamepad.set_led",
        .title = strings::kCommandGamepadSetLedTitle,
        .description = strings::kCommandGamepadSetLedDescription,
        .agentSummary =
            "Sets the light colour of a gamepad with one (DualShock 4, DualSense); default: the first "
            "connected. Each channel is 0 to 1. Example: {\"color\": [1, 0.2, 0]}.",
        .category = "Input",
        .icon = "lightbulb",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&gamepadSetTriggerEffect>({
        .name = "gamepad.set_trigger_effect",
        .title = strings::kCommandGamepadSetTriggerEffectTitle,
        .description = strings::kCommandGamepadSetTriggerEffectDescription,
        .agentSummary =
            "Applies a DualSense adaptive trigger effect to the left or right trigger: off, resistance "
            "(from start, with strength), weapon (between start and end) or vibration (from start, "
            "with amplitude at frequency Hz); positions and strengths are 0 to 1. Example: "
            "{\"trigger\": \"right\", \"mode\": \"weapon\", \"start\": 0.3, \"end\": 0.6}.",
        .category = "Input",
        .icon = "tune",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&gamepadSetMotion>({
        .name = "gamepad.set_motion",
        .title = strings::kCommandGamepadSetMotionTitle,
        .description = strings::kCommandGamepadSetMotionDescription,
        .agentSummary = "Turns a gamepad's gyroscope and accelerometer on or off (off by default to save "
                        "battery); gamepad.list then reports their readings. Example: {\"enabled\": true}.",
        .category = "Input",
        .icon = "screen_rotation",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
}

} // namespace illusion::editor::detail

namespace illusion::editor
{

std::string_view gamepadButtonId(usize index) noexcept
{
    return index < kGamepadButtons ? gamepadButtonName(static_cast<GamepadButton>(index))
                                   : std::string_view{};
}

} // namespace illusion::editor
