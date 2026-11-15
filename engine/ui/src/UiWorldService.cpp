#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/text/LocaleId.hpp>
#include <illusion/text/Localization.hpp>
#include <illusion/ui/UiComponents.hpp>
#include <illusion/ui/UiWidgets.hpp>
#include <illusion/ui/UiWorld.hpp>
#include <illusion/ui/UiWorldService.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

template <class T>
[[nodiscard]] T* componentOf(World& world, Entity entity)
{
    return !entity.isNull() && world.isAlive(entity) ? world.tryGet<T>(entity) : nullptr;
}

template <class T>
[[nodiscard]] const T* componentOf(const World& world, Entity entity)
{
    return !entity.isNull() && world.isAlive(entity) ? world.tryGet<T>(entity) : nullptr;
}

[[nodiscard]] bool named(const World& world, Entity entity, std::string_view name)
{
    const Name* found = world.tryGet<Name>(entity);
    return found != nullptr && found->value == name;
}

[[nodiscard]] bool fits(std::span<const UiTextArgument> arguments) noexcept
{
    if (arguments.size() > UiTextArguments::kMaxArguments)
    {
        return false;
    }
    return std::ranges::all_of(arguments, [](const UiTextArgument& argument) {
        return !argument.name.empty() && argument.name.size() <= UiTextArguments::kMaxNameBytes &&
               argument.text.size() <= UiTextArguments::kMaxTextBytes;
    });
}

void copyInto(std::span<char> to, std::string_view from) noexcept
{
    if (!from.empty())
    {
        std::memcpy(to.data(), from.data(), from.size());
    }
}

[[nodiscard]] UiTextArgumentValue storedArgument(const UiTextArgument& argument) noexcept
{
    UiTextArgumentValue value;
    copyInto(value.name, argument.name);
    value.nameLength = static_cast<u8>(argument.name.size());
    if (argument.isNumber)
    {
        value.value = argument.value;
        value.isNumber = true;
    }
    else
    {
        copyInto(value.text, argument.text);
        value.textLength = static_cast<u8>(argument.text.size());
    }
    return value;
}

[[nodiscard]] bool sameArgument(const UiTextArgumentValue& a, const UiTextArgumentValue& b) noexcept
{
    const auto view = [](const auto& chars, u8 length) {
        return std::string_view(chars.data(), length);
    };
    if (a.isNumber != b.isNumber || view(a.name, a.nameLength) != view(b.name, b.nameLength))
    {
        return false;
    }
    // Numbers are compared bitwise, so that a NaN that stays a NaN does not re-format every frame.
    return a.isNumber ? std::memcmp(&a.value, &b.value, sizeof(f64)) == 0
                      : view(a.text, a.textLength) == view(b.text, b.textLength);
}

} // namespace

// Out of line so that the vtable is emitted in the library, which only the host links.
UiWorldService::~UiWorldService() = default;

Entity UiWorldService::findIn(Entity root, std::string_view name) const
{
    const std::span<const Entity> subtree = m_hierarchy.subtree(root);
    if (subtree.empty())
    {
        return named(m_world, root, name) ? root : Entity{};
    }
    for (const Entity entity : subtree)
    {
        if (named(m_world, entity, name))
        {
            return entity;
        }
    }
    return {};
}

Entity UiWorldService::find(Entity root, std::string_view name) const
{
    (void)m_hierarchy.update(m_world);
    if (!root.isNull())
    {
        return m_world.isAlive(root) ? findIn(root, name) : Entity{};
    }
    // The first match on the first canvas in draw order: ascending order, then Entity index.
    Entity found{};
    std::tuple<i32, u32> best{std::numeric_limits<i32>::max(), std::numeric_limits<u32>::max()};
    m_world.query<const UiCanvas>().each([&](Entity canvas, const UiCanvas& settings) {
        const std::tuple<i32, u32> key{settings.order, canvas.index};
        if (key >= best)
        {
            return;
        }
        if (const Entity match = findIn(canvas, name); !match.isNull())
        {
            found = match;
            best = key;
        }
    });
    return found;
}

bool UiWorldService::setText(Entity entity, std::string_view utf8)
{
    UiText* text = componentOf<UiText>(m_world, entity);
    if (text == nullptr)
    {
        return false;
    }
    if (text->text != utf8 || !text->key.empty())
    {
        text->text.assign(utf8);
        text->key.clear();
        m_world.markChanged<UiText>();
    }
    m_world.remove<UiTextArguments>(entity);
    return true;
}

bool UiWorldService::setTextKey(Entity entity, std::string_view key,
                                std::span<const UiTextArgument> arguments)
{
    UiText* text = componentOf<UiText>(m_world, entity);
    if (text == nullptr || key.empty() || !fits(arguments))
    {
        return false;
    }
    if (text->key != key)
    {
        text->key.assign(key);
        m_world.markChanged<UiText>();
    }
    if (arguments.empty())
    {
        m_world.remove<UiTextArguments>(entity);
        return true;
    }
    if (!m_world.has<UiTextArguments>(entity))
    {
        m_world.emplace<UiTextArguments>(entity);
    }
    UiTextArguments& stored = m_world.get<UiTextArguments>(entity);
    const u32 count = static_cast<u32>(arguments.size());
    bool changed = stored.count != count;
    for (u32 i = 0; i < count; ++i)
    {
        const UiTextArgumentValue value = storedArgument(arguments[i]);
        if (changed || !sameArgument(stored.arguments[i], value))
        {
            stored.arguments[i] = value;
            changed = true;
        }
    }
    if (changed)
    {
        stored.count = count;
        m_world.markChanged<UiTextArguments>();
    }
    return true;
}

bool UiWorldService::setVisible(Entity entity, bool visible)
{
    if (entity.isNull() || !m_world.isAlive(entity))
    {
        return false;
    }
    setEnabled(m_world, entity, visible);
    return true;
}

bool UiWorldService::isVisible(Entity entity) const
{
    return !entity.isNull() && m_world.isAlive(entity) && isEnabledInHierarchy(m_world, entity);
}

bool UiWorldService::setInteractable(Entity entity, bool enabled)
{
    UiInteractable* interactable = componentOf<UiInteractable>(m_world, entity);
    if (interactable == nullptr)
    {
        return false;
    }
    if (interactable->enabled != enabled)
    {
        interactable->enabled = enabled;
        m_world.markChanged<UiInteractable>();
    }
    return true;
}

bool UiWorldService::setToggle(Entity entity, bool on)
{
    UiToggle* toggle = componentOf<UiToggle>(m_world, entity);
    if (toggle == nullptr)
    {
        return false;
    }
    if (toggle->on != on)
    {
        toggle->on = on;
        m_world.markChanged<UiToggle>();
    }
    return true;
}

std::optional<bool> UiWorldService::toggle(Entity entity) const
{
    const UiToggle* toggle = componentOf<UiToggle>(std::as_const(m_world), entity);
    return toggle != nullptr ? std::optional<bool>(toggle->on) : std::nullopt;
}

bool UiWorldService::setSliderValue(Entity entity, f32 value)
{
    UiSlider* slider = componentOf<UiSlider>(m_world, entity);
    if (slider == nullptr)
    {
        return false;
    }
    if (const f32 next = snapSliderValue(*slider, value); next != slider->value)
    {
        slider->value = next;
        m_world.markChanged<UiSlider>();
    }
    return true;
}

std::optional<f32> UiWorldService::sliderValue(Entity entity) const
{
    const UiSlider* slider = componentOf<UiSlider>(std::as_const(m_world), entity);
    return slider != nullptr ? std::optional<f32>(slider->value) : std::nullopt;
}

bool UiWorldService::setFocus(Entity entity)
{
    if (entity.isNull())
    {
        m_ui.setFocus({});
        return true;
    }
    if (!m_world.isAlive(entity))
    {
        return false;
    }
    m_ui.setFocus(entity);
    return m_ui.focused() == entity;
}

Entity UiWorldService::focused() const
{
    return m_ui.focused();
}

std::optional<UiRect> UiWorldService::rect(Entity entity) const
{
    const UiComputedRect* computed = componentOf<UiComputedRect>(std::as_const(m_world), entity);
    return computed != nullptr ? std::optional<UiRect>(computed->rect) : std::nullopt;
}

void UiWorldService::invalidateLayout()
{
    m_ui.invalidateLayout();
}

bool UiWorldService::playTransition(Entity entity, bool towardsChanged)
{
    if (componentOf<UiTransition>(m_world, entity) == nullptr)
    {
        return false;
    }
    if (!m_world.has<UiTransitionState>(entity))
    {
        m_world.emplace<UiTransitionState>(entity);
    }
    UiTransitionState& state = m_world.get<UiTransitionState>(entity);
    // A Shown transition that has not started yet would otherwise start over this direction.
    state.started = true;
    if (state.towardsChanged != towardsChanged)
    {
        state.towardsChanged = towardsChanged;
        state.waited = 0.0f;
    }
    m_world.markChanged<UiTransitionState>();
    return true;
}

std::optional<f32> UiWorldService::transitionProgress(Entity entity) const
{
    if (componentOf<UiTransition>(std::as_const(m_world), entity) == nullptr)
    {
        return std::nullopt;
    }
    const UiTransitionState* state = m_world.tryGet<UiTransitionState>(entity);
    return state != nullptr ? state->progress : 0.0f;
}

u32 UiWorldService::pollEvents(std::span<UiEvent> events) const
{
    const std::span<const UiEvent> happened = m_ui.events();
    const usize copied = std::min(events.size(), happened.size());
    std::copy_n(happened.begin(), copied, events.begin());
    return static_cast<u32>(happened.size());
}

bool UiWorldService::setLanguage(std::string_view locale)
{
    const std::optional<text::LocaleId> parsed = text::LocaleId::parse(locale);
    if (!parsed)
    {
        return false;
    }
    const std::vector<text::LocaleId> available = m_localization.availableLocales();
    bool known = false;
    for (std::optional<text::LocaleId> candidate = parsed; candidate && !known;
         candidate = candidate->parent())
    {
        known = std::ranges::find(available, *candidate) != available.end();
    }
    if (!known)
    {
        return false;
    }
    m_localization.setLocale(*parsed);
    m_language.assign(parsed->tag());
    return true;
}

std::string_view UiWorldService::language() const
{
    return m_language.empty() ? m_localization.locale().tag() : std::string_view(m_language);
}

} // namespace illusion
