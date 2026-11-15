#include <illusion/audio/AudioWorldService.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Wind.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/GamepadSystem.hpp>
#include <illusion/input/InputActionsAsset.hpp>
#include <illusion/input/InputActionsService.hpp>
#include <illusion/modules/ModuleHost.hpp>
#include <illusion/particles/ParticleWorld.hpp>
#include <illusion/particles/ParticleWorldService.hpp>
#include <illusion/physics/PhysicsAssetResolver.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/physics/PhysicsWorldService.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Input.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/runtime/SceneLoaderService.hpp>
#include <illusion/store/StoreService.hpp>
#include <illusion/text/LocaleFile.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

constexpr usize kReservedGamepads = 8;
constexpr std::string_view kLocaleDirectory = "assets:/locales";
// While a modal canvas is shown, only the actions with this prefix get input.
constexpr std::string_view kUiActionPrefix = "ui.";

// The game's string table, built from its locale files; the Localization views it.
struct GameStrings
{
    std::vector<std::string> keys;
    std::vector<std::string> english;
    std::vector<std::string_view> keyViews;
    std::vector<std::string_view> englishViews;
    text::StringTableData table;
};

std::vector<text::LocaleFile> readLocaleFiles(const VirtualFileSystem& files)
{
    std::vector<text::LocaleFile> locales;
    std::vector<DirectoryEntry> entries;
    if (!files.exists(kLocaleDirectory) || files.list(kLocaleDirectory, entries) != FileError::None)
    {
        return locales;
    }
    for (const DirectoryEntry& entry : entries)
    {
        if (entry.isDirectory || !entry.name.ends_with(".json"))
        {
            continue;
        }
        const std::string path = std::string(kLocaleDirectory) + "/" + entry.name;
        std::string json;
        if (const FileError error = files.readText(path, json); error != FileError::None)
        {
            ILL_LOG_WARN("Game", "cannot read the locale file {} ({})", path, fileErrorName(error));
            continue;
        }
        Expected<text::LocaleFile, text::LocaleError> parsed = text::LocaleFile::parse(json);
        if (!parsed)
        {
            ILL_LOG_WARN("Game", "{}:{}:{}: {}", path, parsed.error().line, parsed.error().column,
                         parsed.error().message);
            continue;
        }
        locales.push_back(std::move(parsed).value());
    }
    // English first: its texts are the source of the game's table.
    std::stable_partition(locales.begin(), locales.end(),
                          [](const text::LocaleFile& file) { return file.locale.tag() == "en"; });
    return locales;
}

// Registers the keys of @p locales as the game's table in @p localization, and their translations.
void loadGameStrings(std::vector<text::LocaleFile> locales, GameStrings& strings,
                     text::Localization& localization)
{
    if (locales.empty())
    {
        return;
    }
    std::vector<std::pair<std::string, std::string>> texts;
    for (const text::LocaleFile& file : locales)
    {
        for (const text::LocaleEntry& entry : file.entries)
        {
            texts.emplace_back(entry.key, entry.text);
        }
    }
    std::stable_sort(texts.begin(), texts.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    texts.erase(std::unique(texts.begin(), texts.end(),
                            [](const auto& a, const auto& b) { return a.first == b.first; }),
                texts.end());
    for (auto& [key, english] : texts)
    {
        if (Expected<text::MessageFormat, text::MessageError> compiled =
                text::MessageFormat::compile(english);
            !compiled)
        {
            ILL_LOG_WARN("Game", "the text of \"{}\" does not compile: {}", key, compiled.error().message);
            continue;
        }
        strings.keys.push_back(std::move(key));
        strings.english.push_back(std::move(english));
    }
    strings.keyViews.assign(strings.keys.begin(), strings.keys.end());
    strings.englishViews.assign(strings.english.begin(), strings.english.end());
    strings.table = text::StringTableData{"game", strings.keyViews, strings.englishViews};
    localization.addTable(strings.table);
    for (text::LocaleFile& file : locales)
    {
        if (file.locale.tag() != "en")
        {
            localization.addLocale(std::move(file));
        }
    }
    localization.applyPendingLocale();
}

// Adds the actions the UI reads, with their default bindings, when @p actions lacks them.
void addMissingUiActions(InputActions& actions)
{
    if (!actions.findAction(kUiNavigateAction))
    {
        const ActionId navigate = actions.addAction(kUiNavigateAction, ActionType::Vector2);
        const std::array bindings{InputBinding::keyVector(Key::Up, Key::Down, Key::Left, Key::Right),
                                  InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D),
                                  InputBinding::gamepadStick(GamepadStick::Left),
                                  InputBinding::gamepadStick(GamepadStick::DPad)};
        actions.setBindings(navigate, bindings);
    }
    if (!actions.findAction(kUiSubmitAction))
    {
        const ActionId submit = actions.addAction(kUiSubmitAction, ActionType::Button);
        const std::array bindings{InputBinding::key(Key::Enter), InputBinding::key(Key::Space),
                                  InputBinding::gamepadButton(GamepadButton::South)};
        actions.setBindings(submit, bindings);
    }
    if (!actions.findAction(kUiCancelAction))
    {
        const ActionId cancel = actions.addAction(kUiCancelAction, ActionType::Button);
        const std::array bindings{InputBinding::key(Key::Escape),
                                  InputBinding::gamepadButton(GamepadButton::East)};
        actions.setBindings(cancel, bindings);
    }
}

PhysicsWorldDesc physicsDesc(const PhysicsSettings& settings) noexcept
{
    PhysicsWorldDesc desc;
    desc.gravity = settings.gravity;
    desc.stepSeconds = settings.stepSeconds;
    desc.maxStepsPerUpdate = settings.maxStepsPerUpdate;
    return desc;
}

AudioWorldDesc audioDesc(const GameSessionDesc& session) noexcept
{
    AudioWorldDesc desc;
    desc.output = session.audioOutput;
    desc.maxVoices = session.settings.audio.maxVoices;
    desc.speedOfSound = session.settings.audio.speedOfSound;
    desc.streamer = session.streamer;
    return desc;
}

// The generator of the EntityGuids made while running: seeded with fixed steps, else none (the shared one).
std::optional<GuidGenerator> seededGenerator(const GameSessionDesc& session) noexcept
{
    if (session.fixedStep)
    {
        return std::optional<GuidGenerator>(std::in_place, session.seed);
    }
    return std::nullopt;
}

} // namespace

struct GameSession::Impl
{
    explicit Impl(const GameSessionDesc& sessionDesc)
        : desc(sessionDesc)
        , seededGuids(seededGenerator(sessionDesc))
        , physics(world, *sessionDesc.jobs, physicsDesc(sessionDesc.settings.physics))
        , audio(world, *sessionDesc.files, audioDesc(sessionDesc))
        , particles(world)
        , ui(world)
        , physicsService(physics)
        , audioService(audio)
        , particleService(particles)
        , inputService(actions)
        , sceneService(world, *sessionDesc.files, &guids())
    {
        gamepads.reserve(kReservedGamepads);
        ui.setAssets(sessionDesc.uiAssets);
        ui.setLocalization(&localization);
    }

    [[nodiscard]] GuidGenerator& guids() noexcept
    {
        return seededGuids ? *seededGuids : GuidGenerator::shared();
    }

    void applySettings()
    {
        const PhysicsSettings& physicsSettings = desc.settings.physics;
        for (u32 a = 0; a < kProjectPhysicsLayers; ++a)
        {
            for (u32 b = a; b < kProjectPhysicsLayers; ++b)
            {
                const bool collide = ((physicsSettings.collisionMatrix[a] >> b) & 1u) != 0 &&
                                     ((physicsSettings.collisionMatrix[b] >> a) & 1u) != 0;
                physics.setLayersCollide(a, b, collide);
            }
        }
        audio.setMasterVolume(desc.settings.audio.masterVolume);
        for (u32 bus = 0; bus < kProjectAudioBuses; ++bus)
        {
            audio.setBusVolume(static_cast<AudioBus>(bus), desc.settings.audio.busVolumes[bus]);
        }
    }

    void updateInput()
    {
        const bool gamepadsReach = focus || desc.settings.input.gamepadsInBackground;
        gamepads.clear();
        if (desc.gamepads != nullptr && gamepadsReach)
        {
            for (const Gamepad* gamepad : desc.gamepads->gamepads())
            {
                gamepads.push_back(&gamepad->state());
            }
        }
        // The modal state of the last UI update holds for the modules of this one.
        const std::string_view prefix = ui.modalActive() ? kUiActionPrefix : std::string_view();
        if (actions.exclusivePrefix() != prefix)
        {
            actions.setExclusivePrefix(prefix);
        }
        actions.update(focus ? &input : nullptr, gamepads);
    }

    // What the UI reads this update: the cursor and primary button with the focus, and the ui.* actions.
    [[nodiscard]] UiInput uiInput(f64 deltaSeconds) const noexcept
    {
        UiInput result;
        result.deltaSeconds = deltaSeconds;
        if (focus)
        {
            result.pointerPixels = input.mousePosition() * pointerScale;
            const Vec2 size(uiFrame.targetPixels);
            result.pointerPresent = result.pointerPixels.x >= 0.0f && result.pointerPixels.y >= 0.0f &&
                                    result.pointerPixels.x < size.x && result.pointerPixels.y < size.y;
            result.pointerDown = input.isDown(MouseButton::Left);
        }
        if (const std::optional<ActionId> navigate = actions.findAction(kUiNavigateAction))
        {
            result.navigate = actions.vector(*navigate);
        }
        if (const std::optional<ActionId> submit = actions.findAction(kUiSubmitAction))
        {
            result.submitDown = actions.isDown(*submit);
        }
        if (const std::optional<ActionId> cancel = actions.findAction(kUiCancelAction))
        {
            result.cancelPressed = actions.wasPressed(*cancel);
        }
        return result;
    }

    GameSessionDesc desc;
    std::optional<GuidGenerator> seededGuids; // fixedStep: ids of Entities created while running
    GameStrings strings;                      // viewed by localization
    text::Localization localization;
    World world;
    PhysicsWorld physics;
    AudioWorld audio;
    ParticleWorld particles;
    UiWorld ui;
    UiFrame uiFrame{};
    Vec2 pointerScale{1.0f};
    InputActions actions;
    InputState input;
    HierarchyIndex hierarchy;
    PhysicsWorldService physicsService;
    AudioWorldService audioService;
    ParticleWorldService particleService;
    InputActionsService inputService;
    SceneLoaderService sceneService;
    std::unique_ptr<PhysicsAssetResolver> physicsAssets;
    std::unique_ptr<RenderAssetResolver> renderAssets;
    std::vector<const GamepadState*> gamepads;
    u64 updates = 0;
    f64 time = 0.0;
    bool started = false;
    bool focus = true;
};

Expected<std::unique_ptr<GameSession>, std::string> GameSession::create(const GameSessionDesc& desc)
{
    ILL_ASSERT(desc.jobs != nullptr, "GameSessionDesc::jobs is required");
    ILL_ASSERT(desc.files != nullptr, "GameSessionDesc::files is required");
    auto impl = std::make_unique<Impl>(desc);
    if (!desc.settings.input.actions.empty())
    {
        if (Expected<void, std::string> loaded =
                loadInputActions(*desc.files, desc.settings.input.actions.path(), impl->actions);
            !loaded)
        {
            return unexpected(std::move(loaded.error()));
        }
    }
    addMissingUiActions(impl->actions);
    impl->applySettings();
    loadGameStrings(readLocaleFiles(*desc.files), impl->strings, impl->localization);
    if (desc.streamer != nullptr)
    {
        impl->physicsAssets = std::make_unique<PhysicsAssetResolver>(impl->physics, *desc.streamer);
    }
    if (desc.renderAssets != nullptr)
    {
        impl->renderAssets = std::make_unique<RenderAssetResolver>(*desc.renderAssets);
    }
    return std::unique_ptr<GameSession>(new GameSession(std::move(impl)));
}

GameSession::GameSession(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

GameSession::~GameSession()
{
    stop();
}

World& GameSession::world() noexcept
{
    return m_impl->world;
}

const World& GameSession::world() const noexcept
{
    return m_impl->world;
}

void GameSession::start()
{
    Impl& impl = *m_impl;
    if (impl.started)
    {
        return;
    }
    impl.started = true;
    if (impl.desc.modules != nullptr && impl.desc.mode == GameSessionMode::Play)
    {
        ServiceSet services;
        services.provide<PhysicsService>(impl.physicsService);
        services.provide<AudioService>(impl.audioService);
        services.provide<ParticleService>(impl.particleService);
        services.provide<InputService>(impl.inputService);
        services.provide<SceneService>(impl.sceneService);
        if (impl.desc.store != nullptr)
        {
            services.provide<StoreService>(*impl.desc.store);
        }
        impl.desc.modules->attachWorld(impl.world, WorldRole::Simulation, services);
    }
}

void GameSession::stop()
{
    Impl& impl = *m_impl;
    if (!impl.started)
    {
        return;
    }
    impl.started = false;
    if (impl.desc.modules != nullptr && impl.desc.mode == GameSessionMode::Play)
    {
        impl.desc.modules->detachWorld(impl.world);
    }
}

bool GameSession::isStarted() const noexcept
{
    return m_impl->started;
}

void GameSession::update(f64 deltaSeconds)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    const f64 step = impl.desc.fixedStep ? impl.desc.settings.physics.stepSeconds : deltaSeconds;
    if (impl.desc.mode == GameSessionMode::Play)
    {
        impl.updateInput();
        if (impl.started && impl.desc.modules != nullptr)
        {
            impl.desc.modules->update(impl.world, step);
        }
    }
    assignMissingGuids(impl.world, impl.guids());
    impl.physics.update(step);
    updateTransformHierarchy(impl.world, impl.hierarchy, impl.desc.jobs);
    impl.localization.applyPendingLocale();
    if (impl.desc.mode == GameSessionMode::Play)
    {
        impl.ui.update(impl.uiFrame, impl.uiInput(step));
    }
    else
    {
        impl.ui.update(impl.uiFrame);
    }
    // The wind blows before particles and the renderer read it.
    advanceWind(impl.world, static_cast<f32>(step));
    // Particles fall with the physics gravity, which game code may change.
    impl.particles.setGravity(impl.physics.gravity());
    impl.particles.update(static_cast<f32>(step), impl.desc.jobs);
    impl.audio.update(step);
    if (impl.physicsAssets)
    {
        impl.physicsAssets->update();
    }
    if (impl.renderAssets)
    {
        impl.renderAssets->update(impl.world, impl.desc.jobs);
    }
    impl.input.beginFrame();
    ++impl.updates;
    impl.time += step;
}

InputState& GameSession::keyboardMouse() noexcept
{
    return m_impl->input;
}

void GameSession::setInputFocus(bool focus) noexcept
{
    m_impl->focus = focus;
}

bool GameSession::hasInputFocus() const noexcept
{
    return m_impl->focus;
}

InputActions& GameSession::actions() noexcept
{
    return m_impl->actions;
}

PhysicsWorld& GameSession::physics() noexcept
{
    return m_impl->physics;
}

ParticleWorld& GameSession::particles() noexcept
{
    return m_impl->particles;
}

AudioWorld& GameSession::audio() noexcept
{
    return m_impl->audio;
}

void GameSession::setUiFrame(const UiFrame& frame, Vec2 pointerScale) noexcept
{
    m_impl->uiFrame = frame;
    m_impl->pointerScale = pointerScale;
}

UiWorld& GameSession::ui() noexcept
{
    return m_impl->ui;
}

text::Localization& GameSession::localization() noexcept
{
    return m_impl->localization;
}

GameSessionMode GameSession::mode() const noexcept
{
    return m_impl->desc.mode;
}

const ProjectSettings& GameSession::settings() const noexcept
{
    return m_impl->desc.settings;
}

u64 GameSession::updateCount() const noexcept
{
    return m_impl->updates;
}

f64 GameSession::time() const noexcept
{
    return m_impl->time;
}

} // namespace illusion
