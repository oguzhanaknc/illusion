// The live part of the Animator's Inspector card (ADR 0059-0061): the clip or graph, a timeline scrubber that
// runs animation.scrub, the state of every layer, the parameters (set in Play mode through
// animation.set_parameter and animation.trigger) and the last events of the running game.

#include "Panels.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/animation/AnimationGraphState.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/EntityIndex.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr usize kEventCount = 8;
constexpr usize kNameLength = 48;

using Name = std::array<char, kNameLength>;

// An event kept after its update: names copied, since AnimationWorld::events() lives for one update.
struct RecentEvent
{
    Name name{};
    Name layer{};
    Name state{};
};

void copyName(Name& out, std::string_view text) noexcept
{
    const usize length = std::min(text.size(), out.size() - 1);
    std::memcpy(out.data(), text.data(), length);
    out[length] = '\0';
}

// Seconds rounded to hundredths for display.
f64 seconds(f32 value) noexcept
{
    return std::round(static_cast<f64>(value) * 100.0) / 100.0;
}

std::string_view indexText(ui::FrameText& text, usize value)
{
    char buffer[24];
    const auto [end, _] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    const usize length = static_cast<usize>(end - buffer);
    return std::string_view(text.copy(std::string_view(buffer, length)), length);
}

void hint(ui::UiKit& kit, const char* message)
{
    ImGui::PushStyleColor(ImGuiCol_Text, imvec(kit.theme.color(ui::ThemeToken::TextSecondary)));
    ImGui::TextWrapped("%s", message);
    ImGui::PopStyleColor();
}

// "{"entity":"<id>"" with the rest of a command's params appended by the caller.
std::string entityParams(EntityGuid guid)
{
    const auto hex = formatGuid(guid);
    return "{\"entity\":\"" + std::string(hex.data(), hex.size()) + "\"";
}

// The share of a state's loop that @p time (normalized) has played.
f32 progressOf(f32 time, bool loop) noexcept
{
    if (!std::isfinite(time))
    {
        return 0.0f;
    }
    return std::clamp(loop ? time - std::floor(time) : time, 0.0f, 1.0f);
}

// At most this many source skins are listed for a graph.
constexpr usize kSourceCount = 4;

// The skin @p clip was authored on ("model.gltf#skin/N", in the frame's text), where @p clipRef is its asset
// path; empty without one or when it is @p skeleton itself (the clip binds by name, ADR 0065).
std::string_view sourceSkin(ui::FrameText& text, std::string_view clipRef, const AnimationClip& clip,
                            std::string_view skeleton)
{
    if (clip.skeleton < 0 || clipRef.empty())
    {
        return {};
    }
    const std::string_view model = clipRef.substr(0, clipRef.find('#'));
    const std::string_view path(
        text.concat(model, text.concat("#skin/", indexText(text, static_cast<usize>(clip.skeleton)))));
    return path == skeleton ? std::string_view{} : path;
}

// The source skins of the retargeted clips of a graph, each once, in the graph's clip order.
struct SourceSkins
{
    std::array<std::string_view, kSourceCount> paths{};
    usize count = 0;

    void add(std::string_view path) noexcept
    {
        if (path.empty() || count == paths.size() ||
            std::find(paths.begin(), paths.begin() + static_cast<std::ptrdiff_t>(count), path) !=
                paths.begin() + static_cast<std::ptrdiff_t>(count))
        {
            return;
        }
        paths[count++] = path;
    }
};

// Adds the source skin of clip @p index of @p binding when the binding retargets it.
void addRetargeted(SourceSkins& out, ui::FrameText& text, const GraphBinding& binding, usize index,
                   const AssetRef& clipRef, std::string_view skeleton)
{
    if (index < binding.clips.size() && index < binding.clipRetargets.size() &&
        binding.clips[index] != nullptr && binding.clipRetargets[index].active())
    {
        out.add(sourceSkin(text, clipRef.path(), *binding.clips[index], skeleton));
    }
}

// The source skins of every retargeted clip of @p graph: each state's clips from GraphBinding::firstClip,
// then the layers' reference clips.
SourceSkins graphSources(ui::FrameText& text, const AnimationGraph& graph, const GraphBinding& binding,
                         std::string_view skeleton)
{
    SourceSkins out;
    for (usize layer = 0; layer < graph.layers.size() && layer < binding.firstState.size(); ++layer)
    {
        const AnimationLayer& current = graph.layers[layer];
        for (usize i = 0; i < current.states.size(); ++i)
        {
            const usize state = binding.firstState[layer] + i;
            if (state >= binding.firstClip.size())
            {
                break;
            }
            const usize first = binding.firstClip[state];
            const AnimationMotion& motion = current.states[i].motion;
            if (motion.type == AnimationMotionType::Clip)
            {
                addRetargeted(out, text, binding, first, motion.clip, skeleton);
                continue;
            }
            for (usize point = 0; point < motion.points.size(); ++point)
            {
                addRetargeted(out, text, binding, first + point, motion.points[point].clip, skeleton);
            }
        }
        if (layer < binding.referenceClips.size() && binding.referenceClips[layer] >= 0)
        {
            addRetargeted(out, text, binding, static_cast<usize>(binding.referenceClips[layer]),
                          current.reference, skeleton);
        }
    }
    return out;
}

} // namespace

struct AnimatorSection::State
{
    // The inspected Animator's Entity in the play World: looked up again only for another Entity or session,
    // or when it dies, so that the index of the play World is not rebuilt every frame.
    bool playLooked = false;
    u64 playSession = 0;
    EntityGuid playGuid;
    Entity playEntity;

    // The last events of the Animator, oldest first, and the update they were read from.
    std::array<RecentEvent, kEventCount> events{};
    usize eventCount = 0;
    u64 eventFrame = 0;
    const AnimationWorld* eventSource = nullptr;
    Entity eventEntity;

    Entity findPlayEntity(EditorContext& editor, const World& world, EntityGuid guid)
    {
        const u64 session = editor.playSessions();
        if (playLooked && playSession == session && playGuid == guid)
        {
            if (playEntity.isNull())
            {
                return playEntity;
            }
            const EntityGuid* id = world.isAlive(playEntity) ? world.tryGet<EntityGuid>(playEntity) : nullptr;
            if (id != nullptr && *id == guid)
            {
                return playEntity;
            }
        }
        playLooked = true;
        playSession = session;
        playGuid = guid;
        EntityIndex* index = editor.playEntities();
        playEntity = index != nullptr ? index->find(guid) : Entity{};
        return playEntity;
    }

    void collectEvents(const AnimationWorld& animation, Entity entity) noexcept
    {
        if (eventSource != &animation || eventEntity != entity)
        {
            eventSource = &animation;
            eventEntity = entity;
            eventCount = 0;
            eventFrame = 0;
        }
        if (animation.frame() == eventFrame)
        {
            return;
        }
        eventFrame = animation.frame();
        for (const AnimationEvent& event : animation.events())
        {
            if (event.entity != entity)
            {
                continue;
            }
            if (eventCount == kEventCount)
            {
                std::move(events.begin() + 1, events.end(), events.begin());
                --eventCount;
            }
            RecentEvent& recent = events[eventCount++];
            copyName(recent.name, event.name);
            copyName(recent.layer, event.layer);
            copyName(recent.state, event.state);
        }
    }
};

AnimatorSection::AnimatorSection()
    : m_state(std::make_unique<State>())
{
}

AnimatorSection::~AnimatorSection() = default;

void AnimatorSection::draw(Impl& shell, ui::UiKit& kit, Entity entity, EntityGuid guid,
                           std::string_view cardId, std::string_view mergeKey)
{
    ILL_PROFILE_SCOPE();
    State& state = *m_state;
    EditorContext& editor = shell.editor;
    ui::FrameText& text = shell.text;

    World* playWorld = editor.playWorld();
    const bool playing = playWorld != nullptr;
    const World* world = &editor.world();
    const AnimationWorld* animation = nullptr;
    Entity target = entity;
    if (playing)
    {
        PlayHost* play = editor.play();
        animation = play != nullptr ? play->animation() : nullptr;
        world = playWorld;
        target = animation != nullptr ? state.findPlayEntity(editor, *playWorld, guid) : Entity{};
    }
    else if (const ViewportHost* viewports = editor.viewports())
    {
        animation = viewports->editAnimation();
    }
    if (animation != nullptr && &animation->world() != world)
    {
        animation = nullptr;
    }
    const Animator* animator =
        !target.isNull() && world->isAlive(target) ? world->tryGet<Animator>(target) : nullptr;

    ui::sectionHeader(kit, text.format(playing ? s::kInspectorAnimatorLive : s::kInspectorAnimatorPreview));
    if (animation == nullptr || animator == nullptr)
    {
        hint(kit, text.format(s::kInspectorAnimatorNoRuntime));
        return;
    }
    const std::optional<u32> slot = animation->findSlot(target);
    const Skeleton* skeleton = slot ? animation->skeleton(*slot) : nullptr;
    if (skeleton == nullptr)
    {
        hint(kit, text.format(animation->statistics().pending > 0 ? s::kInspectorAnimatorLoading
                                                                  : s::kInspectorAnimatorNotPosed));
        return;
    }
    const AnimationGraph* graph = animation->graph(*slot);
    const AnimationGraphState* graphState = animation->graphState(*slot);
    const GraphBinding* binding = animation->graphBinding(*slot);
    const AnimationClip* clip = animation->clip(*slot);
    const bool hasGraph = graph != nullptr && graphState != nullptr && binding != nullptr;
    if (playing)
    {
        state.collectEvents(*animation, target);
    }

    if (!ui::beginPropertyTable(kit, text.concat(cardId, ".live")))
    {
        return;
    }
    ui::propertyLabel(kit, text.format(s::kInspectorAnimatorPose));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text.format(s::kInspectorAnimatorJoints, {{"count", skeleton->joints.size()}}));

    f32 duration = 0.0f;
    if (hasGraph)
    {
        usize states = 0;
        for (const AnimationLayer& layer : graph->layers)
        {
            states += layer.states.size();
        }
        ui::propertyLabel(kit, text.format(s::kInspectorAnimatorGraph));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(
            text.format(s::kInspectorAnimatorGraphSummary, {{"layers", graph->layers.size()},
                                                            {"states", states},
                                                            {"parameters", graph->parameters.size()}}));
        if (!graphState->layers.empty())
        {
            duration = graphStateDuration(*binding, *graphState, 0, graphState->layers[0].state);
        }
    }
    else if (clip != nullptr)
    {
        ui::propertyLabel(kit, text.format(s::kInspectorAnimatorClip));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(
            text.format(s::kInspectorAnimatorClipSummary,
                        {{"name", std::string_view(clip->name)}, {"seconds", seconds(clip->duration)}}));
        duration = clip->duration;
    }

    // The skins the clips were authored on, when they play retargeted (ADR 0065).
    SourceSkins sources;
    if (hasGraph)
    {
        sources = graphSources(text, *graph, *binding, animator->skeleton.path());
    }
    else if (clip != nullptr && !skeleton->rig.empty())
    {
        sources.add(sourceSkin(text, animator->clip.path(), *clip, animator->skeleton.path()));
    }
    for (usize i = 0; i < sources.count; ++i)
    {
        ui::propertyLabel(kit, i == 0 ? text.format(s::kInspectorAnimatorSource) : "");
        ImGui::Selectable(text.verbatim(sources.paths[i],
                                        text.concat(cardId, text.concat(".source.", indexText(text, i)))));
    }

    if (duration > 0.0f && std::isfinite(duration))
    {
        const char* timeId = text.concat(cardId, ".scrub");
        ui::propertyLabel(kit, text.format(s::kInspectorAnimatorTime),
                          playing ? nullptr : text.format(s::kInspectorAnimatorTimeHint));
        if (!playing)
        {
            f32 value = std::clamp(animator->time, 0.0f, duration);
            if (ImGui::SliderFloat(text.hidden(timeId), &value, 0.0f, duration, "%.2f",
                                   ImGuiSliderFlags_AlwaysClamp))
            {
                const std::string params = entityParams(guid) + std::format(",\"time\":{}}}", value);
                (void)shell.run("animation.scrub", params, "inspector", {}, mergeKey);
            }
        }
        else
        {
            const f32 shown =
                hasGraph && !graphState->layers.empty()
                    ? progressOf(graphState->layers[0].time, true) * duration
                    : (clip != nullptr ? clipTime(*clip, animator->time, animator->loop) : 0.0f);
            ImGui::ProgressBar(std::clamp(shown / duration, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f),
                               text.format(s::kInspectorAnimatorTimeOf,
                                           {{"time", seconds(shown)}, {"duration", seconds(duration)}}));
        }
    }

    if (hasGraph)
    {
        const f32 rowHeight = ImGui::GetFrameHeight();
        for (usize i = 0; i < graph->layers.size() && i < graphState->layers.size(); ++i)
        {
            const AnimationLayer& layer = graph->layers[i];
            const GraphLayerState& live = graphState->layers[i];
            if (live.state >= layer.states.size())
            {
                continue;
            }
            const AnimationState& current = layer.states[live.state];
            const char* shown = text.copy(current.name);
            if (live.previous >= 0 && static_cast<usize>(live.previous) < layer.states.size() &&
                live.fadeDuration > 0.0f)
            {
                shown = text.format(
                    s::kInspectorAnimatorStateFading,
                    {{"state", std::string_view(current.name)},
                     {"previous", std::string_view(layer.states[static_cast<usize>(live.previous)].name)}});
            }
            ui::propertyLabel(kit, text.copy(layer.name));
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const f32 width = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddRectFilled(
                origin, ImVec2(origin.x + width * progressOf(live.time, current.loop), origin.y + rowHeight),
                ui::tokenColor(kit, ui::ThemeToken::AccentSubtle), ImGui::GetStyle().FrameRounding);
            const char* id = text.concat(cardId, text.concat(".layer.", indexText(text, i)));
            ImGui::Selectable(text.verbatim(shown, id), false, ImGuiSelectableFlags_None,
                              ImVec2(0.0f, rowHeight));
        }

        for (usize i = 0; i < graph->parameters.size() && i < graphState->parameters.size(); ++i)
        {
            const AnimationParameter& parameter = graph->parameters[i];
            const char* id = text.concat(cardId, text.concat(".parameter.", parameter.name));
            ui::propertyLabel(kit, text.copy(parameter.name));
            const f32 value = graphState->parameters[i];
            ImGui::BeginDisabled(!playing);
            std::optional<std::string> written;
            switch (parameter.type)
            {
            case AnimationParameterType::Float:
            {
                f32 edited = value;
                if (ImGui::DragFloat(text.hidden(id), &edited, 0.01f, 0.0f, 0.0f, "%.3f"))
                {
                    written = std::format("{}", edited);
                }
                break;
            }
            case AnimationParameterType::Int:
            {
                int edited = static_cast<int>(value);
                if (ImGui::DragInt(text.hidden(id), &edited, 0.1f))
                {
                    written = std::format("{}", edited);
                }
                break;
            }
            case AnimationParameterType::Bool:
            {
                bool edited = value != 0.0f;
                if (ImGui::Checkbox(text.hidden(id), &edited))
                {
                    written = edited ? "true" : "false";
                }
                break;
            }
            case AnimationParameterType::Trigger:
                if (ImGui::Button(text.label(s::kInspectorAnimatorFire, id)))
                {
                    const std::string params = entityParams(guid) + ",\"name\":" + jsonQuote(parameter.name) +
                                               ",\"world\":\"play\"}";
                    (void)shell.run("animation.trigger", params, "inspector");
                }
                break;
            }
            ImGui::EndDisabled();
            if (written)
            {
                const std::string params = entityParams(guid) + ",\"name\":" + jsonQuote(parameter.name) +
                                           ",\"value\":" + *written + ",\"world\":\"play\"}";
                (void)shell.run("animation.set_parameter", params, "inspector");
            }
        }
    }

    if (playing)
    {
        ui::propertyLabel(kit, text.format(s::kInspectorAnimatorEvents));
        if (state.eventCount == 0)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", text.format(s::kInspectorAnimatorNoEvents));
        }
        for (usize i = 0; i < state.eventCount; ++i)
        {
            if (i > 0)
            {
                ui::propertyLabel(kit, "");
            }
            const RecentEvent& event = state.events[state.eventCount - 1 - i];
            const char* shown =
                text.format(s::kInspectorAnimatorEvent, {{"name", std::string_view(event.name.data())},
                                                         {"layer", std::string_view(event.layer.data())},
                                                         {"state", std::string_view(event.state.data())}});
            ImGui::Selectable(
                text.verbatim(shown, text.concat(cardId, text.concat(".event.", indexText(text, i)))));
        }
    }
    ui::endPropertyTable();
}

} // namespace illusion::editor::shell::detail
