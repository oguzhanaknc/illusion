#include "SdlGamepad.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/input/GamepadSystem.hpp>

#include <atomic>
#include <cstring>

namespace illusion
{
namespace
{

std::atomic<bool> g_systemAlive{false};

// Buttons and axes come straight from SDL's enums; keep the engine enums in the same order.
static_assert(static_cast<int>(GamepadButton::South) == SDL_GAMEPAD_BUTTON_SOUTH);
static_assert(static_cast<int>(GamepadButton::DPadRight) == SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
static_assert(static_cast<int>(GamepadButton::Misc1) == SDL_GAMEPAD_BUTTON_MISC1);
static_assert(static_cast<int>(GamepadButton::Touchpad) == SDL_GAMEPAD_BUTTON_TOUCHPAD);
static_assert(static_cast<int>(GamepadAxis::LeftX) == SDL_GAMEPAD_AXIS_LEFTX);
static_assert(static_cast<int>(GamepadAxis::RightTrigger) == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

struct VendorProduct
{
    u16 vendor;
    u16 product;
};

// USB ids that make SDL report the requested controller type for a virtual gamepad.
VendorProduct idsFor(GamepadType type) noexcept
{
    switch (type)
    {
    case GamepadType::Xbox360:
        return {0x045E, 0x028E};
    case GamepadType::XboxOne:
        return {0x045E, 0x0B12};
    case GamepadType::PS4:
        return {0x054C, 0x09CC};
    case GamepadType::PS5:
        return {0x054C, 0x0CE6};
    case GamepadType::SwitchPro:
        return {0x057E, 0x2009};
    default:
        return {0, 0};
    }
}

} // namespace

struct GamepadSystem::Impl
{
    EventBus* events = nullptr;
    std::vector<std::unique_ptr<Gamepad>> gamepads;
    std::vector<Gamepad*> view;    // gamepads as raw pointers for gamepads()
    std::vector<bool> playerSlots; // true while a gamepad occupies the slot

    Gamepad* find(SDL_JoystickID id) const noexcept
    {
        for (const auto& gamepad : gamepads)
        {
            if (gamepad->m_id == static_cast<GamepadId>(id))
            {
                return gamepad.get();
            }
        }
        return nullptr;
    }

    void rebuildView()
    {
        view.clear();
        for (const auto& gamepad : gamepads)
        {
            view.push_back(gamepad.get());
        }
    }

    void connect(SDL_JoystickID id);
    void disconnect(SDL_JoystickID id);
};

void GamepadSystem::Impl::connect(SDL_JoystickID id)
{
    if (find(id) != nullptr)
    {
        return;
    }
    SDL_Gamepad* handle = SDL_OpenGamepad(id);
    if (handle == nullptr)
    {
        ILL_LOG_WARN("Input", "cannot open gamepad {}: {}", id, SDL_GetError());
        return;
    }

    std::unique_ptr<Gamepad> gamepad(new Gamepad());
    gamepad->m_handle = handle;
    gamepad->m_id = static_cast<GamepadId>(id);
    const char* name = SDL_GetGamepadName(handle);
    gamepad->m_name = name != nullptr ? name : "Gamepad";
    gamepad->m_state.setType(detail::typeFromSdl(SDL_GetGamepadType(handle)));

    const SDL_PropertiesID properties = SDL_GetGamepadProperties(handle);
    GamepadCapabilities& caps = gamepad->m_capabilities;
    caps.rumble = SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    caps.triggerRumble =
        SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false);
    caps.led = SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false);
    caps.motionSensors =
        SDL_GamepadHasSensor(handle, SDL_SENSOR_GYRO) && SDL_GamepadHasSensor(handle, SDL_SENSOR_ACCEL);
    caps.touchpad = SDL_GetNumGamepadTouchpads(handle) > 0;
    caps.adaptiveTriggers = gamepad->type() == GamepadType::PS5;

    u32 slot = 0;
    while (slot < playerSlots.size() && playerSlots[slot])
    {
        ++slot;
    }
    if (slot == playerSlots.size())
    {
        playerSlots.push_back(false);
    }
    playerSlots[slot] = true;
    gamepad->m_playerIndex = slot;
    SDL_SetGamepadPlayerIndex(handle, static_cast<int>(slot));

    ILL_LOG_INFO("Input", "gamepad connected: {} ({}), player {}", gamepad->m_name,
                 gamepadTypeName(gamepad->type()), slot + 1);
    if (events != nullptr)
    {
        events->enqueue(GamepadConnectedEvent{gamepad->m_id, gamepad->type(), slot});
    }
    gamepads.push_back(std::move(gamepad));
    rebuildView();
}

void GamepadSystem::Impl::disconnect(SDL_JoystickID id)
{
    const auto it = std::find_if(gamepads.begin(), gamepads.end(), [id](const auto& gamepad) {
        return gamepad->m_id == static_cast<GamepadId>(id);
    });
    if (it == gamepads.end())
    {
        return;
    }
    const u32 slot = (*it)->m_playerIndex;
    playerSlots[slot] = false;
    ILL_LOG_INFO("Input", "gamepad disconnected: {}", (*it)->m_name);
    gamepads.erase(it);
    rebuildView();
    if (events != nullptr)
    {
        events->enqueue(GamepadDisconnectedEvent{static_cast<GamepadId>(id), slot});
    }
}

GamepadSystem::GamepadSystem()
    : m_impl(std::make_unique<Impl>())
{
}

std::unique_ptr<GamepadSystem> GamepadSystem::create(EventBus* events)
{
    const bool alreadyAlive = g_systemAlive.exchange(true);
    ILL_ASSERT(!alreadyAlive, "only one GamepadSystem may exist at a time");
    if (alreadyAlive)
    {
        return nullptr;
    }

    // SDL has no window of its own here: accept input while another (GLFW) window has focus, and leave
    // signal handling to the application.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_SENSOR))
    {
        ILL_LOG_ERROR("Input", "gamepad subsystem unavailable: {}", SDL_GetError());
        g_systemAlive.store(false);
        return nullptr;
    }

    std::unique_ptr<GamepadSystem> system(new GamepadSystem());
    system->m_impl->events = events;
    // Gamepads already plugged in are reported as SDL_EVENT_GAMEPAD_ADDED by the first update().
    return system;
}

GamepadSystem::~GamepadSystem()
{
    m_impl->gamepads.clear();
    m_impl->view.clear();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_SENSOR);
    SDL_Quit();
    g_systemAlive.store(false);
}

void GamepadSystem::update(bool acceptInput)
{
    ILL_PROFILE_SCOPE();
    for (const auto& gamepad : m_impl->gamepads)
    {
        gamepad->m_state.beginFrame();
    }

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_EVENT_GAMEPAD_ADDED:
            m_impl->connect(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            m_impl->disconnect(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            if (Gamepad* gamepad = m_impl->find(event.gbutton.which);
                gamepad != nullptr && event.gbutton.button < static_cast<u8>(GamepadButton::Count))
            {
                gamepad->m_state.setButton(static_cast<GamepadButton>(event.gbutton.button),
                                           event.gbutton.down);
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (Gamepad* gamepad = m_impl->find(event.gaxis.which);
                gamepad != nullptr && event.gaxis.axis < static_cast<u8>(GamepadAxis::Count))
            {
                const auto axis = static_cast<GamepadAxis>(event.gaxis.axis);
                gamepad->m_state.setAxis(axis, detail::axisFromSdl(axis, event.gaxis.value));
            }
            break;
        case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
            if (Gamepad* gamepad = m_impl->find(event.gtouchpad.which);
                gamepad != nullptr && event.gtouchpad.touchpad == 0 && event.gtouchpad.finger >= 0)
            {
                TouchpadFinger finger;
                finger.down = event.type != SDL_EVENT_GAMEPAD_TOUCHPAD_UP;
                finger.position = Vec2(event.gtouchpad.x, event.gtouchpad.y);
                finger.pressure = event.gtouchpad.pressure;
                gamepad->m_state.setFinger(static_cast<usize>(event.gtouchpad.finger), finger);
            }
            break;
        case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
            if (Gamepad* gamepad = m_impl->find(event.gsensor.which))
            {
                const Vec3 value(event.gsensor.data[0], event.gsensor.data[1], event.gsensor.data[2]);
                if (event.gsensor.sensor == SDL_SENSOR_GYRO)
                {
                    gamepad->m_state.setGyroscope(value);
                }
                else if (event.gsensor.sensor == SDL_SENSOR_ACCEL)
                {
                    gamepad->m_state.setAccelerometer(value);
                }
            }
            break;
        default:
            break;
        }
    }

    if (!acceptInput)
    {
        for (const auto& gamepad : m_impl->gamepads)
        {
            gamepad->m_state.releaseAll();
        }
    }
}

std::span<Gamepad* const> GamepadSystem::gamepads() const noexcept
{
    return m_impl->view;
}

Gamepad* GamepadSystem::find(GamepadId id) const noexcept
{
    return m_impl->find(static_cast<SDL_JoystickID>(id));
}

Gamepad* GamepadSystem::findByPlayer(u32 playerIndex) const noexcept
{
    for (const auto& gamepad : m_impl->gamepads)
    {
        if (gamepad->m_playerIndex == playerIndex)
        {
            return gamepad.get();
        }
    }
    return nullptr;
}

// --- VirtualGamepad -------------------------------------------------------------------------------------

struct VirtualGamepad::Shared
{
    Outputs outputs;
    std::array<SDL_VirtualJoystickTouchpadDesc, 1> touchpads{};
    std::array<SDL_VirtualJoystickSensorDesc, 2> sensors{};
};

namespace
{

bool SDLCALL onVirtualRumble(void* user, Uint16 low, Uint16 high)
{
    auto* outputs = static_cast<VirtualGamepad::Outputs*>(user);
    outputs->lowFrequencyRumble = static_cast<f32>(low) / 65535.0f;
    outputs->highFrequencyRumble = static_cast<f32>(high) / 65535.0f;
    return true;
}

bool SDLCALL onVirtualRumbleTriggers(void* user, Uint16 left, Uint16 right)
{
    auto* outputs = static_cast<VirtualGamepad::Outputs*>(user);
    outputs->leftTriggerRumble = static_cast<f32>(left) / 65535.0f;
    outputs->rightTriggerRumble = static_cast<f32>(right) / 65535.0f;
    return true;
}

bool SDLCALL onVirtualLed(void* user, Uint8 red, Uint8 green, Uint8 blue)
{
    static_cast<VirtualGamepad::Outputs*>(user)->led = {red, green, blue};
    return true;
}

bool SDLCALL onVirtualEffect(void* user, const void* data, int size)
{
    auto* outputs = static_cast<VirtualGamepad::Outputs*>(user);
    const auto* bytes = static_cast<const u8*>(data);
    outputs->lastEffect.assign(bytes, bytes + std::max(size, 0));
    return true;
}

bool SDLCALL onVirtualSensors(void* user, bool enabled)
{
    static_cast<VirtualGamepad::Outputs*>(user)->motionSensorsEnabled = enabled;
    return true;
}

} // namespace

std::unique_ptr<VirtualGamepad> GamepadSystem::attachVirtualGamepad(const VirtualGamepadDesc& desc)
{
    std::unique_ptr<VirtualGamepad> pad(new VirtualGamepad());
    pad->m_shared = std::make_unique<VirtualGamepad::Shared>();
    VirtualGamepad::Shared& shared = *pad->m_shared;

    const bool playStation = desc.type == GamepadType::PS4 || desc.type == GamepadType::PS5;
    const VendorProduct ids = idsFor(desc.type);

    SDL_VirtualJoystickDesc joystick;
    SDL_INIT_INTERFACE(&joystick);
    joystick.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    joystick.vendor_id = ids.vendor;
    joystick.product_id = ids.product;
    joystick.naxes = static_cast<Uint16>(GamepadAxis::Count);
    joystick.nbuttons = static_cast<Uint16>(GamepadButton::Count);
    joystick.button_mask = (1u << static_cast<u32>(GamepadButton::Count)) - 1u;
    joystick.axis_mask = (1u << static_cast<u32>(GamepadAxis::Count)) - 1u;
    joystick.name = desc.name.c_str();
    joystick.userdata = &shared.outputs;
    joystick.Rumble = &onVirtualRumble;
    joystick.RumbleTriggers =
        desc.type == GamepadType::XboxOne ? &onVirtualRumbleTriggers : nullptr; // impulse triggers
    joystick.SetLED = playStation ? &onVirtualLed : nullptr;
    joystick.SendEffect = &onVirtualEffect;
    if (playStation)
    {
        shared.touchpads[0].nfingers = static_cast<Uint16>(GamepadState::kMaxFingers);
        shared.sensors[0] = SDL_VirtualJoystickSensorDesc{SDL_SENSOR_GYRO, 250.0f};
        shared.sensors[1] = SDL_VirtualJoystickSensorDesc{SDL_SENSOR_ACCEL, 250.0f};
        joystick.ntouchpads = 1;
        joystick.touchpads = shared.touchpads.data();
        joystick.nsensors = 2;
        joystick.sensors = shared.sensors.data();
        joystick.SetSensorsEnabled = &onVirtualSensors;
    }

    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&joystick);
    if (id == 0)
    {
        ILL_LOG_ERROR("Input", "cannot attach virtual gamepad: {}", SDL_GetError());
        return nullptr;
    }
    pad->m_id = static_cast<GamepadId>(id);
    pad->m_joystick = SDL_OpenJoystick(id);
    if (pad->m_joystick == nullptr)
    {
        ILL_LOG_ERROR("Input", "cannot open virtual gamepad: {}", SDL_GetError());
        SDL_DetachVirtualJoystick(id);
        return nullptr;
    }
    // Triggers rest at the bottom of their range, not in the middle.
    pad->setAxis(GamepadAxis::LeftTrigger, 0.0f);
    pad->setAxis(GamepadAxis::RightTrigger, 0.0f);
    return pad;
}

VirtualGamepad::~VirtualGamepad()
{
    if (m_joystick != nullptr)
    {
        SDL_CloseJoystick(static_cast<SDL_Joystick*>(m_joystick));
    }
    if (m_id != GamepadId::Invalid)
    {
        SDL_DetachVirtualJoystick(static_cast<SDL_JoystickID>(m_id));
    }
}

void VirtualGamepad::setButton(GamepadButton button, bool down) noexcept
{
    SDL_SetJoystickVirtualButton(static_cast<SDL_Joystick*>(m_joystick), static_cast<int>(button), down);
}

void VirtualGamepad::setAxis(GamepadAxis axis, f32 value) noexcept
{
    SDL_SetJoystickVirtualAxis(static_cast<SDL_Joystick*>(m_joystick), static_cast<int>(axis),
                               detail::virtualAxisFromEngine(axis, value));
}

void VirtualGamepad::setTouchpadFinger(u32 index, const TouchpadFinger& finger) noexcept
{
    SDL_SetJoystickVirtualTouchpad(static_cast<SDL_Joystick*>(m_joystick), 0, static_cast<int>(index),
                                   finger.down, finger.position.x, finger.position.y, finger.pressure);
}

void VirtualGamepad::setMotion(Vec3 gyroscope, Vec3 accelerometer) noexcept
{
    auto* joystick = static_cast<SDL_Joystick*>(m_joystick);
    const Uint64 timestamp = SDL_GetTicksNS();
    SDL_SendJoystickVirtualSensorData(joystick, SDL_SENSOR_GYRO, timestamp, &gyroscope.x, 3);
    SDL_SendJoystickVirtualSensorData(joystick, SDL_SENSOR_ACCEL, timestamp, &accelerometer.x, 3);
}

const VirtualGamepad::Outputs& VirtualGamepad::outputs() const noexcept
{
    return m_shared->outputs;
}

} // namespace illusion
