// The animation commands (ADR 0059-0061): the skins and clips of a model (animation.list), what the Animators
// of the scene or the running game play (animation.status), driving a graph of the running game
// (animation.set_parameter, animation.trigger), setting the time of an Animator in the open scene
// (animation.scrub, undoable), checking `.animgraph` and `.ragdoll` files (animation.validate) and the rig
// maps of ADR 0065 (animation.rig, animation.map_joint through the import settings sidecar, undoable, and
// animation.retarget). The
// runtime ones go through the AnimationWorld of the PlayHost and act on the play World; they are runtime
// state, not undoable.

#include "CommandSupport.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/animation/AnimationGraphState.hpp>
#include <illusion/animation/AnimationService.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/animation/Retarget.hpp>
#include <illusion/assets/ImportSettings.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/assets/Rig.hpp>
#include <illusion/core/AssetId.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/PlayHost.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/physics/Ragdoll.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::detail
{

namespace
{

namespace strings = illusion::editor::strings;

constexpr usize kMaxStatusAnimators = 100;
constexpr usize kMaxStatusEvents = 64;
constexpr f64 kMaxTime = 1.0e6;
constexpr i64 kMaxIntParameter = 16777216; // what the graph format stores exactly in a float

std::string_view parameterTypeName(AnimationParameterType type) noexcept
{
    switch (type)
    {
    case AnimationParameterType::Float:
        return "Float";
    case AnimationParameterType::Int:
        return "Int";
    case AnimationParameterType::Bool:
        return "Bool";
    case AnimationParameterType::Trigger:
        return "Trigger";
    }
    return "Float";
}

Expected<AnimationWorld*, CommandError> requireAnimation(CommandContext& context)
{
    PlayHost* play = context.editor().play();
    if (play == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "this editor has no game runtime"));
    }
    AnimationWorld* animation = context.onPlayWorld() ? play->animation() : nullptr;
    if (animation == nullptr)
    {
        return unexpected(CommandError::make(
            ErrorKind::PreconditionFailed,
            "animation graphs run in a play session: start one with play.start (the edit World has none)"));
    }
    return animation;
}

CommandError animationFailure(AnimationError error, std::string_view pointer)
{
    switch (error)
    {
    case AnimationError::NoAnimator:
        return CommandError::invalid(std::string(pointer), "has no illusion.Animator");
    case AnimationError::NoGraph:
        return CommandError::make(ErrorKind::PreconditionFailed, "the graph of the Animator has not loaded");
    case AnimationError::HasGraph:
        return CommandError::invalid(std::string(pointer), "is controlled by an animation graph");
    case AnimationError::UnknownName:
        return CommandError::invalid(std::string(pointer), "is not known to the Animator");
    case AnimationError::TypeMismatch:
        return CommandError::invalid(std::string(pointer), "does not fit the type of the parameter");
    case AnimationError::InvalidValue:
        return CommandError::invalid(std::string(pointer), "is not a valid value");
    }
    return CommandError::make(ErrorKind::Internal, std::string(animationErrorName(error)));
}

EntitySummary summaryOf(CommandContext& context, Entity entity)
{
    return context.world().isAlive(entity) ? context.summary(entity) : EntitySummary{};
}

// The Animator of @p entity named at @p pointer of the params.
Expected<Entity, CommandError> animatorEntity(CommandContext& context, std::string_view reference,
                                              std::string_view pointer)
{
    Expected<Entity, CommandError> entity = context.entity(reference, pointer);
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    if (!context.world().has<Animator>(*entity))
    {
        return unexpected(animationFailure(AnimationError::NoAnimator, pointer));
    }
    return entity;
}

// The first part of a validation error, "layers[0].states[1]: message".
std::string errorPathOf(const std::string& error)
{
    const usize colon = error.find(": ");
    if (colon != std::string::npos && error.find(' ') > colon)
    {
        return error.substr(0, colon);
    }
    return {};
}

// True if the file name of @p path is more than @p extension.
bool hasStem(std::string_view path, std::string_view extension)
{
    const usize slash = path.rfind('/');
    return slash != std::string_view::npos && path.size() > slash + 1 + extension.size();
}

std::string pathOf(const AssetRef& ref)
{
    return std::string(ref.path());
}

constexpr usize kMaxListedJoints = 64;

// Why @p path is no model of the assets: mount (at @p pointer), or that it does not exist.
Expected<void, CommandError> checkModelPath(CommandContext& context, const std::string& path,
                                            std::string_view pointer)
{
    const std::string extension = lowerExtension(path);
    std::filesystem::path real;
    if (!path.starts_with(kAssetsRoot) || (extension != ".gltf" && extension != ".glb") ||
        !hasStem(path, extension) || path.find('#') != std::string::npos ||
        context.editor().files().resolve(path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            std::string(pointer),
            "must be a model (.gltf or .glb) in the assets: mount, e.g. \"assets:/models/hero.glb\""));
    }
    if (!context.editor().files().exists(path))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "'" + path + "' does not exist"));
    }
    return {};
}

// A model with the rig maps its import settings sidecar gives (what the importer cooks).
struct RigModel
{
    ModelData model;
    ImportSettings settings;
};

Expected<RigModel, CommandError> loadRigModel(CommandContext& context, const std::string& path,
                                              std::string_view pointer)
{
    if (Expected<void, CommandError> valid = checkModelPath(context, path, pointer); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    std::string error;
    std::optional<ModelData> model = loadGltf(context.editor().files(), path, &error);
    if (!model)
    {
        return unexpected(
            CommandError::make(ErrorKind::IoError, "cannot read '" + path + "' (" + error + ")"));
    }
    RigModel result{.model = std::move(*model), .settings = {}};
    const std::string sidecar = importSettingsPath(path);
    std::string text;
    const FileError read = context.editor().files().readText(sidecar, text);
    if (read == FileError::None)
    {
        Expected<ImportSettings, ImportSettingsError> parsed =
            parseImportSettings(text, ImportSettingsKind::Model);
        if (!parsed)
        {
            return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                                 "'" + sidecar + "' is invalid (" + parsed.error().pointer +
                                                     ": " + parsed.error().message + ")"));
        }
        result.settings = std::move(*parsed);
    }
    else if (read != FileError::NotFound)
    {
        return unexpected(CommandError::make(ErrorKind::IoError, "cannot read '" + sidecar + "' (" +
                                                                     std::string(fileErrorName(read)) + ")"));
    }
    const std::vector<SkinImportSettings>& skins = result.settings.model.skins;
    for (usize i = 0; i < skins.size(); ++i)
    {
        if (i >= result.model.skins.size())
        {
            return unexpected(
                CommandError::make(ErrorKind::PreconditionFailed,
                                   std::format("'{}' has settings for skin {} but the model has {} skins",
                                               sidecar, i, result.model.skins.size())));
        }
        Skeleton& skin = result.model.skins[i];
        if (Expected<void, std::string> applied = applyRigOverrides(skin.rig, skin, skins[i]); !applied)
        {
            return unexpected(
                CommandError::make(ErrorKind::PreconditionFailed,
                                   std::format("'{}' skins[{}]: {}", sidecar, i, applied.error())));
        }
    }
    return result;
}

std::string skinRef(std::string_view path, usize skin)
{
    return std::format("{}#skin/{}", path, skin);
}

// The required humanoid bones @p rig has no joint for.
std::vector<std::string> missingBones(const RigMap& rig)
{
    std::vector<std::string> missing;
    for (const std::string_view bone : kRequiredHumanoidBones)
    {
        if (rig.find(bone) < 0)
        {
            missing.emplace_back(bone);
        }
    }
    return missing;
}

Expected<AnimationListResult, CommandError> animationList(CommandContext& context,
                                                          const AnimationListParams& params)
{
    Expected<RigModel, CommandError> loaded = loadRigModel(context, params.path, "/path");
    if (!loaded)
    {
        return unexpected(std::move(loaded).error());
    }
    const ModelData& model = loaded->model;
    AnimationListResult result;
    result.path = params.path;
    for (usize i = 0; i < model.skins.size(); ++i)
    {
        const Skeleton& skin = model.skins[i];
        result.skins.push_back(AnimationListSkin{.ref = skinRef(params.path, i),
                                                 .name = skin.name,
                                                 .joints = static_cast<u32>(skin.joints.size()),
                                                 .humanoid = missingBones(skin.rig).empty()});
    }
    for (const AnimationClip& clip : model.animations)
    {
        const bool known = clip.skeleton >= 0 && static_cast<usize>(clip.skeleton) < model.skins.size();
        result.clips.push_back(AnimationListClip{
            .ref = std::format("{}#animation/{}", params.path, clip.name),
            .name = clip.name,
            .duration = clip.duration,
            .channels = static_cast<u32>(clip.channels.size()),
            .source = known ? skinRef(params.path, static_cast<usize>(clip.skeleton)) : std::string()});
    }
    return result;
}

// The skin @p index of @p model, or the error at @p pointer.
Expected<const Skeleton*, CommandError> findSkin(const ModelData& model, u32 index, std::string_view pointer)
{
    if (index >= model.skins.size())
    {
        return unexpected(CommandError::invalid(
            std::string(pointer),
            std::format("is not a skin of the model, which has {} skins", model.skins.size())));
    }
    return &model.skins[index];
}

Expected<AnimationRigResult, CommandError> animationRig(CommandContext& context,
                                                        const AnimationRigParams& params)
{
    Expected<RigModel, CommandError> loaded = loadRigModel(context, params.path, "/path");
    if (!loaded)
    {
        return unexpected(std::move(loaded).error());
    }
    const u32 skinIndex = params.skin.value_or(0);
    Expected<const Skeleton*, CommandError> found = findSkin(loaded->model, skinIndex, "/skin");
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    const Skeleton& skin = **found;
    const std::vector<SkinImportSettings>& skins = loaded->settings.model.skins;
    const SkinImportSettings* settings = skinIndex < skins.size() ? &skins[skinIndex] : nullptr;
    RigMapReport report;
    (void)generateRigMap(skin, &report);
    AnimationRigResult result;
    result.path = params.path;
    result.skin = skinIndex;
    result.ref = skinRef(params.path, skinIndex);
    result.name = skin.name;
    result.generated = settings == nullptr || settings->generate;
    result.missing = missingBones(skin.rig);
    result.humanoid = result.missing.empty();
    result.warnings = report.warnings;
    const bool mapped =
        skin.rig.bones.size() == skin.joints.size() && skin.rig.translation.size() == skin.joints.size();
    for (usize i = 0; i < skin.joints.size(); ++i)
    {
        const std::string& name = skin.joints[i].name;
        const auto listed = [&](const auto& overrides) {
            return std::ranges::any_of(overrides, [&](const auto& entry) { return entry.joint == name; });
        };
        result.joints.push_back(AnimationRigJoint{
            .name = name,
            .parent = skin.joints[i].parent,
            .bone = mapped ? skin.rig.bones[i] : std::string(),
            .translation = std::string(
                retargetTranslationName(mapped ? skin.rig.translation[i] : RetargetTranslation::Skeleton)),
            .overridden = settings != nullptr && (listed(settings->bones) || listed(settings->translation))});
    }
    return result;
}

// The index of the joint of @p skin named @p name, or the error with the names that are close.
Expected<usize, CommandError> findJointByName(const Skeleton& skin, std::string_view name)
{
    std::vector<std::string_view> names;
    for (usize i = 0; i < skin.joints.size(); ++i)
    {
        if (skin.joints[i].name == name)
        {
            return i;
        }
        names.push_back(skin.joints[i].name);
    }
    CommandError error = CommandError::invalid("/joint", "is not a joint of the skin");
    error.kind = ErrorKind::NotFound;
    error.suggestions = closestMatches(name, names);
    return unexpected(std::move(error));
}

Expected<RetargetTranslation, CommandError> parseTranslation(std::string_view name)
{
    for (const RetargetTranslation mode :
         {RetargetTranslation::Skeleton, RetargetTranslation::Scaled, RetargetTranslation::Animation})
    {
        if (retargetTranslationName(mode) == name)
        {
            return mode;
        }
    }
    return unexpected(
        CommandError::invalid("/translation", "must be \"Skeleton\", \"Scaled\" or \"Animation\""));
}

// Replaces the entry of the joint of @p entry in @p entries or adds it, and keeps the list sorted by joint.
template <typename Entry>
void upsert(std::vector<Entry>& entries, Entry entry)
{
    const auto at = std::ranges::find_if(entries, [&](const Entry& e) { return e.joint == entry.joint; });
    if (at != entries.end())
    {
        *at = std::move(entry);
    }
    else
    {
        entries.push_back(std::move(entry));
    }
    std::ranges::sort(entries, [](const Entry& a, const Entry& b) { return a.joint < b.joint; });
}

Expected<AnimationMapJointResult, CommandError> animationMapJoint(CommandContext& context,
                                                                  const AnimationMapJointParams& params)
{
    if (!params.bone && !params.translation)
    {
        return unexpected(CommandError::invalid(
            "/bone", "give a bone (\"\" unmaps the joint), a translation mode or both"));
    }
    if (params.bone && !params.bone->empty() && !isValidBoneName(*params.bone))
    {
        return unexpected(CommandError::invalid(
            "/bone", "must be empty or 1 to 64 letters, digits, '_', '-' and '.', e.g. \"leftUpperArm\""));
    }
    std::optional<RetargetTranslation> mode;
    if (params.translation)
    {
        Expected<RetargetTranslation, CommandError> parsed = parseTranslation(*params.translation);
        if (!parsed)
        {
            return unexpected(std::move(parsed).error());
        }
        mode = *parsed;
    }
    Expected<RigModel, CommandError> loaded = loadRigModel(context, params.path, "/path");
    if (!loaded)
    {
        return unexpected(std::move(loaded).error());
    }
    const u32 skinIndex = params.skin.value_or(0);
    Expected<const Skeleton*, CommandError> found = findSkin(loaded->model, skinIndex, "/skin");
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    const Skeleton& skin = **found;
    Expected<usize, CommandError> joint = findJointByName(skin, params.joint);
    if (!joint)
    {
        return unexpected(std::move(joint).error());
    }
    ImportSettings settings = loaded->settings;
    std::vector<SkinImportSettings>& skins = settings.model.skins;
    while (skins.size() <= skinIndex)
    {
        skins.push_back(SkinImportSettings{});
    }
    SkinImportSettings& edited = skins[skinIndex];
    if (params.bone)
    {
        upsert(edited.bones, RigBoneOverride{.joint = params.joint, .bone = *params.bone});
    }
    if (mode)
    {
        upsert(edited.translation, RigTranslationOverride{.joint = params.joint, .mode = *mode});
    }
    RigMap rig = generateRigMap(skin);
    if (Expected<void, std::string> applied = applyRigOverrides(rig, skin, edited); !applied)
    {
        return unexpected(CommandError::invalid("/bone", applied.error()));
    }
    AnimationMapJointResult result;
    result.path = params.path;
    result.skin = skinIndex;
    result.joint = params.joint;
    result.bone = rig.bones[*joint];
    result.translation = std::string(retargetTranslationName(rig.translation[*joint]));
    result.missing = missingBones(rig);
    result.humanoid = result.missing.empty();
    const std::string sidecar = importSettingsPath(params.path);
    std::string before;
    const bool existed = context.editor().files().readText(sidecar, before) == FileError::None;
    const std::string text = writeImportSettings(settings, ImportSettingsKind::Model);
    if (text.empty())
    {
        if (existed)
        {
            if (Expected<void, CommandError> deleted = context.edit().deleteFile(sidecar); !deleted)
            {
                return unexpected(std::move(deleted).error());
            }
            result.changed = true;
        }
        return result;
    }
    result.sidecar = sidecar;
    if (existed && before == text)
    {
        return result;
    }
    if (Expected<bool, CommandError> written = context.edit().writeFile(sidecar, text); !written)
    {
        return unexpected(std::move(written).error());
    }
    result.changed = true;
    return result;
}

// The normalized @p reference with the fragment @p fragment, split; the error is reported at @p pointer.
Expected<AssetPathParts, CommandError> splitReference(const std::string& normalized, AssetFragment fragment,
                                                      std::string_view pointer)
{
    const std::optional<AssetPathParts> parts = splitAssetPath(normalized);
    if (!parts || parts->fragment != fragment)
    {
        return unexpected(CommandError::invalid(
            std::string(pointer),
            fragment == AssetFragment::Animation
                ? "must be a clip of a model, e.g. \"assets:/models/hero.glb#animation/walk\""
                : "must be a skin of a model, e.g. \"assets:/models/hero.glb#skin/0\""));
    }
    return *parts;
}

Expected<AnimationRetargetResult, CommandError> animationRetarget(CommandContext& context,
                                                                  const AnimationRetargetParams& params)
{
    const std::string clipPath = normalizeAssetPath(params.clip).value_or(std::string());
    const std::string skeletonPath = normalizeAssetPath(params.skeleton).value_or(std::string());
    Expected<AssetPathParts, CommandError> clipParts =
        splitReference(clipPath, AssetFragment::Animation, "/clip");
    if (!clipParts)
    {
        return unexpected(std::move(clipParts).error());
    }
    Expected<AssetPathParts, CommandError> skeletonParts =
        splitReference(skeletonPath, AssetFragment::Skin, "/skeleton");
    if (!skeletonParts)
    {
        return unexpected(std::move(skeletonParts).error());
    }
    const std::string clipFile(clipParts->file);
    const std::string skeletonFile(skeletonParts->file);
    const std::string clipName(clipParts->node);
    Expected<RigModel, CommandError> clipModel = loadRigModel(context, clipFile, "/clip");
    if (!clipModel)
    {
        return unexpected(std::move(clipModel).error());
    }
    const auto clipAt = std::ranges::find_if(
        clipModel->model.animations, [&](const AnimationClip& clip) { return clip.name == clipName; });
    if (clipAt == clipModel->model.animations.end())
    {
        std::vector<std::string_view> names;
        for (const AnimationClip& clip : clipModel->model.animations)
        {
            names.push_back(clip.name);
        }
        CommandError error = CommandError::invalid("/clip", "is not a clip of the model");
        error.kind = ErrorKind::NotFound;
        error.suggestions = closestMatches(clipName, names);
        return unexpected(std::move(error));
    }
    const AnimationClip& clip = *clipAt;
    std::optional<RigModel> other;
    if (skeletonFile != clipFile)
    {
        Expected<RigModel, CommandError> loaded = loadRigModel(context, skeletonFile, "/skeleton");
        if (!loaded)
        {
            return unexpected(std::move(loaded).error());
        }
        other = std::move(*loaded);
    }
    const ModelData& targetModel = other ? other->model : clipModel->model;
    Expected<const Skeleton*, CommandError> found = findSkin(targetModel, skeletonParts->index, "/skeleton");
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    const Skeleton& target = **found;
    AnimationRetargetResult result;
    result.clip = clipPath;
    result.skeleton = skeletonPath;
    result.channels = static_cast<u32>(clip.channels.size());
    const bool hasSource =
        clip.skeleton >= 0 && static_cast<usize>(clip.skeleton) < clipModel->model.skins.size();
    const Skeleton* source = hasSource ? &clipModel->model.skins[static_cast<usize>(clip.skeleton)] : nullptr;
    if (source != nullptr)
    {
        result.source = skinRef(clipFile, static_cast<usize>(clip.skeleton));
    }
    ClipBinding binding;
    if (source == nullptr)
    {
        result.reason = "NoSkeleton";
    }
    else if (result.source == skeletonPath)
    {
        result.reason = "SameSkeleton";
    }
    else if (Expected<RetargetedClip, RetargetError> bound = bindRetargetedClip(clip, *source, target); bound)
    {
        result.retargeted = true;
        result.heightRatio = bound->retarget.heightRatio;
        binding = std::move(bound->binding);
        const std::vector<i32> pairs = matchJoints(*source, target);
        for (usize i = 0; i < pairs.size(); ++i)
        {
            if (pairs[i] >= 0)
            {
                ++result.pairedJoints;
            }
            else if (result.unpairedJoints.size() < kMaxListedJoints)
            {
                result.unpairedJoints.push_back(target.joints[i].name);
            }
        }
    }
    else
    {
        result.reason = std::string(retargetErrorName(bound.error()));
    }
    if (!result.retargeted)
    {
        binding = bindClip(clip, target);
    }
    result.pairedChannels = binding.boundChannels;
    for (usize i = 0; i < binding.joints.size() && result.unpaired.size() < kMaxListedJoints; ++i)
    {
        const std::string& joint = clip.channels[i].joint;
        if (binding.joints[i] < 0 && std::ranges::find(result.unpaired, joint) == result.unpaired.end())
        {
            result.unpaired.push_back(joint);
        }
    }
    return result;
}

AnimationStatusParameter parameterOf(const AnimationGraph& graph, const AnimationGraphState& state,
                                     usize index)
{
    const AnimationParameter& parameter = graph.parameters[index];
    f32 value = 0.0f;
    if (parameter.type == AnimationParameterType::Trigger)
    {
        value = index < state.triggers.size() && state.triggers[index] == 1 ? 1.0f : 0.0f;
    }
    else if (index < state.parameters.size())
    {
        value = state.parameters[index];
    }
    return AnimationStatusParameter{
        .name = parameter.name, .type = std::string(parameterTypeName(parameter.type)), .value = value};
}

AnimationStatusAnimator animatorOf(CommandContext& context, const AnimationWorld* animation, Entity entity,
                                   const Animator& animator)
{
    AnimationStatusAnimator status;
    status.entity = context.summary(entity);
    status.skeleton = pathOf(animator.skeleton);
    status.clip = pathOf(animator.clip);
    status.graph = pathOf(animator.graph);
    status.speed = animator.speed;
    status.time = animator.time;
    status.loop = animator.loop;
    status.playing = animator.playing;
    if (animation == nullptr)
    {
        return status;
    }
    if (const AnimationPose* pose = context.world().tryGet<AnimationPose>(entity))
    {
        status.joints = pose->jointCount;
        status.posed = pose->jointCount != 0;
    }
    const std::optional<u32> slot = animation->findSlot(entity);
    const AnimationGraph* graph = slot ? animation->graph(*slot) : nullptr;
    const AnimationGraphState* state = slot ? animation->graphState(*slot) : nullptr;
    if (graph == nullptr || state == nullptr)
    {
        return status;
    }
    status.graphLoaded = true;
    for (usize i = 0; i < graph->layers.size() && i < state->layers.size(); ++i)
    {
        const AnimationLayer& layer = graph->layers[i];
        const GraphLayerState& live = state->layers[i];
        AnimationStatusLayer entry;
        entry.name = layer.name;
        if (live.state < layer.states.size())
        {
            entry.state = layer.states[live.state].name;
        }
        entry.normalizedTime = live.time;
        if (live.previous >= 0 && static_cast<usize>(live.previous) < layer.states.size())
        {
            entry.previous = layer.states[static_cast<usize>(live.previous)].name;
        }
        if (live.fadeDuration > 0.0f)
        {
            entry.fade = std::clamp(live.fade / live.fadeDuration, 0.0f, 1.0f);
        }
        status.layers.push_back(std::move(entry));
    }
    for (usize i = 0; i < graph->parameters.size(); ++i)
    {
        status.parameters.push_back(parameterOf(*graph, *state, i));
    }
    return status;
}

Expected<AnimationStatusResult, CommandError> animationStatus(CommandContext& context,
                                                              const AnimationStatusParams& params)
{
    World& world = context.world();
    const AnimationWorld* animation = nullptr;
    if (context.onPlayWorld())
    {
        if (PlayHost* play = context.editor().play())
        {
            animation = play->animation();
        }
    }
    else if (const ViewportHost* viewports = context.editor().viewports())
    {
        animation = viewports->editAnimation();
        if (animation != nullptr && &animation->world() != &world)
        {
            animation = nullptr;
        }
    }
    std::optional<Entity> one;
    if (params.entity)
    {
        Expected<Entity, CommandError> entity = animatorEntity(context, *params.entity, "/entity");
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        one = *entity;
    }
    std::vector<Entity> entities;
    if (one)
    {
        entities.push_back(*one);
    }
    else
    {
        world.query<const Animator>().each(
            [&](Entity entity, const Animator&) { entities.push_back(entity); });
        std::ranges::sort(entities, [](Entity a, Entity b) { return a.toBits() < b.toBits(); });
    }
    AnimationStatusResult result;
    result.world = context.onPlayWorld() ? "play" : "edit";
    result.live = animation != nullptr;
    if (!result.live)
    {
        result.note =
            context.onPlayWorld()
                ? "the play session has no animation runtime: only the saved Animator fields are shown"
                : "the edit World has no animation runtime here (no viewport poses it): only the saved "
                  "Animator fields are shown; start a play session (play.start) for states and parameters";
    }
    {
        u32 total = 0;
        world.query<const Animator>().each([&](Entity, const Animator&) { ++total; });
        result.total = total;
    }
    for (const Entity entity : entities)
    {
        if (result.animators.size() >= kMaxStatusAnimators)
        {
            break;
        }
        if (const Animator* animator = world.tryGet<Animator>(entity))
        {
            result.animators.push_back(animatorOf(context, animation, entity, *animator));
        }
    }
    if (animation != nullptr)
    {
        const AnimationStatistics& statistics = animation->statistics();
        result.pending = statistics.pending;
        result.failures = statistics.failures;
        for (const AnimationEvent& event : animation->events())
        {
            if (result.events.size() >= kMaxStatusEvents)
            {
                break;
            }
            result.events.push_back(AnimationStatusEvent{.entity = summaryOf(context, event.entity),
                                                         .name = std::string(event.name),
                                                         .layer = std::string(event.layer),
                                                         .state = std::string(event.state)});
        }
    }
    return result;
}

// The graph of @p entity's Animator in @p animation, or the error the commands report.
struct LiveGraph
{
    const AnimationGraph* graph = nullptr;
    const AnimationGraphState* state = nullptr;
};

Expected<LiveGraph, CommandError> liveGraph(CommandContext& context, AnimationWorld& animation, Entity entity)
{
    const Animator* animator = context.world().tryGet<Animator>(entity);
    if (animator == nullptr || animator->graph.empty())
    {
        return unexpected(
            CommandError::invalid("/entity", "has no animation graph (Animator::graph is empty)"));
    }
    const std::optional<u32> slot = animation.findSlot(entity);
    const AnimationGraph* graph = slot ? animation.graph(*slot) : nullptr;
    const AnimationGraphState* state = slot ? animation.graphState(*slot) : nullptr;
    if (graph == nullptr || state == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the graph of the Animator has not loaded: step the game and "
                                             "check animation.status (graphLoaded)"));
    }
    return LiveGraph{graph, state};
}

// The index of the parameter @p name in @p graph, or the error with the names that are close.
Expected<usize, CommandError> findParameter(const AnimationGraph& graph, std::string_view name)
{
    const i32 index = graph.findParameter(name);
    if (index >= 0)
    {
        return static_cast<usize>(index);
    }
    std::vector<std::string_view> names;
    for (const AnimationParameter& parameter : graph.parameters)
    {
        names.push_back(parameter.name);
    }
    CommandError error = CommandError::invalid("/name", "is not a parameter of the graph");
    error.kind = ErrorKind::NotFound;
    error.suggestions = closestMatches(name, names);
    return unexpected(std::move(error));
}

Expected<AnimationSetParameterResult, CommandError>
animationSetParameter(CommandContext& context, const AnimationSetParameterParams& params)
{
    Expected<AnimationWorld*, CommandError> found = requireAnimation(context);
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    AnimationWorld& animation = **found;
    Expected<Entity, CommandError> entity = animatorEntity(context, params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Expected<JsonDocument, CommandError> json = parseParam(params.value, "/value");
    if (!json)
    {
        return unexpected(std::move(json).error());
    }
    Expected<LiveGraph, CommandError> live = liveGraph(context, animation, *entity);
    if (!live)
    {
        return unexpected(std::move(live).error());
    }
    Expected<usize, CommandError> index = findParameter(*live->graph, params.name);
    if (!index)
    {
        return unexpected(std::move(index).error());
    }
    const AnimationParameterType type = live->graph->parameters[*index].type;
    const JsonValue value = json->root();
    AnimationWorldService service(animation);
    Expected<void, AnimationError> written;
    f32 number = 0.0f;
    switch (type)
    {
    case AnimationParameterType::Trigger:
        return unexpected(CommandError::invalid("/name", "is a Trigger: use animation.trigger"));
    case AnimationParameterType::Bool:
    {
        const std::optional<bool> flag = value.asBool();
        if (!flag)
        {
            return unexpected(CommandError::invalid("/value", "must be true or false for a Bool parameter"));
        }
        number = *flag ? 1.0f : 0.0f;
        if (!context.dryRun())
        {
            written = service.setBool(*entity, params.name, *flag);
        }
        break;
    }
    case AnimationParameterType::Int:
    {
        const std::optional<i64> whole = value.asI64();
        if (!whole || *whole < -kMaxIntParameter || *whole > kMaxIntParameter)
        {
            return unexpected(CommandError::invalid(
                "/value", "must be a whole number between -16777216 and 16777216 for an Int parameter"));
        }
        number = static_cast<f32>(*whole);
        if (!context.dryRun())
        {
            written = service.setInt(*entity, params.name, static_cast<i32>(*whole));
        }
        break;
    }
    case AnimationParameterType::Float:
    {
        const std::optional<f64> real = value.asF64();
        if (!real || !std::isfinite(*real) ||
            std::abs(*real) > static_cast<f64>(std::numeric_limits<f32>::max()))
        {
            return unexpected(
                CommandError::invalid("/value", "must be a finite number for a Float parameter"));
        }
        number = static_cast<f32>(*real);
        if (!context.dryRun())
        {
            written = service.setFloat(*entity, params.name, number);
        }
        break;
    }
    }
    if (!written)
    {
        return unexpected(animationFailure(written.error(), "/name"));
    }
    return AnimationSetParameterResult{
        .entity = context.summary(*entity),
        .parameter = AnimationStatusParameter{
            .name = params.name, .type = std::string(parameterTypeName(type)), .value = number}};
}

Expected<AnimationTriggerResult, CommandError> animationTrigger(CommandContext& context,
                                                                const AnimationTriggerParams& params)
{
    Expected<AnimationWorld*, CommandError> found = requireAnimation(context);
    if (!found)
    {
        return unexpected(std::move(found).error());
    }
    AnimationWorld& animation = **found;
    Expected<Entity, CommandError> entity = animatorEntity(context, params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Expected<LiveGraph, CommandError> live = liveGraph(context, animation, *entity);
    if (!live)
    {
        return unexpected(std::move(live).error());
    }
    Expected<usize, CommandError> index = findParameter(*live->graph, params.name);
    if (!index)
    {
        return unexpected(std::move(index).error());
    }
    if (live->graph->parameters[*index].type != AnimationParameterType::Trigger)
    {
        return unexpected(CommandError::invalid("/name", "is not a Trigger: use animation.set_parameter"));
    }
    if (!context.dryRun())
    {
        AnimationWorldService service(animation);
        if (const Expected<void, AnimationError> set = service.setTrigger(*entity, params.name); !set)
        {
            return unexpected(animationFailure(set.error(), "/name"));
        }
    }
    return AnimationTriggerResult{.entity = context.summary(*entity), .name = params.name};
}

Expected<AnimationScrubResult, CommandError> animationScrub(CommandContext& context,
                                                            const AnimationScrubParams& params)
{
    if (!std::isfinite(params.time) || params.time < 0.0f || static_cast<f64>(params.time) > kMaxTime)
    {
        return unexpected(CommandError::invalid("/time", "must be between 0 and 1,000,000 seconds"));
    }
    Expected<Entity, CommandError> entity = animatorEntity(context, params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Expected<ComponentId, CommandError> type = context.componentType("illusion.Animator", "/entity");
    if (!type)
    {
        return unexpected(std::move(type).error());
    }
    Animator animator = *context.world().tryGet<Animator>(*entity);
    const f32 previous = animator.time;
    animator.time = params.time;
    Expected<bool, CommandError> changed = context.edit().assignComponent(*entity, *type, &animator);
    if (!changed)
    {
        return unexpected(std::move(changed).error());
    }
    return AnimationScrubResult{
        .entity = context.summary(*entity), .previous = previous, .time = params.time, .changed = *changed};
}

Expected<AnimationValidateResult, CommandError> animationValidate(CommandContext& context,
                                                                  const AnimationValidateParams& params)
{
    const std::string extension = lowerExtension(params.path);
    const bool graph = extension == kAnimationGraphExtension;
    const bool ragdoll = extension == kRagdollProfileExtension;
    std::filesystem::path real;
    if (!params.path.starts_with(kAssetsRoot) || (!graph && !ragdoll) || !hasStem(params.path, extension) ||
        context.editor().files().resolve(params.path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            "/path", "must be an .animgraph or .ragdoll file in the assets: mount, e.g. "
                     "\"assets:/animations/hero.animgraph\""));
    }
    std::vector<std::byte> bytes;
    const FileError error = context.editor().files().readFile(params.path, bytes);
    if (error == FileError::NotFound)
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "'" + params.path + "' does not exist"));
    }
    if (error != FileError::None)
    {
        return unexpected(CommandError::make(ErrorKind::IoError, "cannot read '" + params.path + "' (" +
                                                                     std::string(fileErrorName(error)) +
                                                                     ")"));
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    AnimationValidateResult result;
    result.path = params.path;
    if (graph)
    {
        result.kind = "AnimationGraph";
        const Expected<AnimationGraph, std::string> parsed = parseAnimationGraph(text);
        if (!parsed)
        {
            result.error = parsed.error();
            result.errorPath = errorPathOf(result.error);
            return result;
        }
        result.valid = true;
        result.parameters = static_cast<u32>(parsed->parameters.size());
        result.layers = static_cast<u32>(parsed->layers.size());
        for (const AnimationLayer& layer : parsed->layers)
        {
            result.states += static_cast<u32>(layer.states.size());
        }
        return result;
    }
    result.kind = "RagdollProfile";
    const Expected<RagdollProfile, std::string> parsed = parseRagdollProfile(text);
    if (!parsed)
    {
        result.error = parsed.error();
        result.errorPath = errorPathOf(result.error);
        return result;
    }
    result.valid = true;
    result.parts = static_cast<u32>(parsed->parts.size());
    return result;
}

} // namespace

void registerAnimationCommands(CommandRegistry& registry)
{
    registry.add<&animationList>({
        .name = "animation.list",
        .title = strings::kCommandAnimationListTitle,
        .description = strings::kCommandAnimationListDescription,
        .agentSummary =
            "Lists what a glTF model of the project can animate: its skins (the value for "
            "Animator::skeleton, "
            "with the number of joints) and its animations (the value for Animator::clip, with the duration "
            "in seconds and the number of animated channels). Use it to find the names for an Animator or an "
            "animation graph. Example: {\"path\": \"assets:/models/column.gltf\"}.",
        .category = "Animation",
        .icon = "animation",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Read,
    });
    registry.add<&animationRig>({
        .name = "animation.rig",
        .title = strings::kCommandAnimationRigTitle,
        .description = strings::kCommandAnimationRigDescription,
        .agentSummary =
            "Shows the rig map of one skin of a model: for every joint its bone (a humanoid bone such as "
            "hips or leftUpperArm, a custom name, or \"\" when unmapped), how retargeted clips move it "
            "(translation: Skeleton, Scaled or Animation) and whether the model's import settings override "
            "it. humanoid is true when every required humanoid bone is mapped, else missing lists the bones "
            "without a joint; warnings are ambiguities generation resolved. Fix a joint with "
            "animation.map_joint. Example: {\"path\": \"assets:/models/hero.glb\", \"skin\": 0}.",
        .category = "Animation",
        .icon = "animation",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Read,
    });
    registry.add<&animationMapJoint>({
        .name = "animation.map_joint",
        .title = strings::kCommandAnimationMapJointTitle,
        .description = strings::kCommandAnimationMapJointDescription,
        .agentSummary =
            "Writes one override of a skin's rig map into the model's <file>.import.json sidecar and "
            "reloads the model: bone gives the joint a humanoid bone (e.g. leftUpperArm) or a custom bone "
            "name shared with other rigs, \"\" unmaps it; translation sets how retargeted clips move it "
            "(Skeleton, Scaled or Animation). Give bone, translation or both. A bone another override "
            "already uses is an error; the result has the bone and whether the rig is humanoid afterwards. "
            "Undoable. Check with animation.rig. Example: {\"path\": \"assets:/models/hero.glb\", "
            "\"skin\": 0, \"joint\": \"Bip01_Spine2\", \"bone\": \"upperChest\"}.",
        .category = "Animation",
        .icon = "tune",
        .flags = {.idempotent = true, .undoable = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Files,
    });
    registry.add<&animationRetarget>({
        .name = "animation.retarget",
        .title = strings::kCommandAnimationRetargetTitle,
        .description = strings::kCommandAnimationRetargetDescription,
        .agentSummary =
            "Reports how a clip plays on a skeleton: retargeted through the rig maps (the clip's source "
            "skin differs from the skeleton) or bound by joint name, with reason (SameSkeleton, "
            "NoSkeleton, NoRig or NoPairs) when it is by name; the channels that move a joint "
            "(pairedChannels), the joints of channels without one (unpaired), the joints of the skeleton "
            "that hold the reference pose (unpairedJoints) and the hips height ratio that scales "
            "translations. Use it after animation.map_joint to check a fix. Example: {\"clip\": "
            "\"assets:/models/hero.glb#animation/walk\", \"skeleton\": "
            "\"assets:/models/enemy.glb#skin/0\"}.",
        .category = "Animation",
        .icon = "animation",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Read,
    });
    registry.add<&animationStatus>({
        .name = "animation.status",
        .title = strings::kCommandAnimationStatusTitle,
        .description = strings::kCommandAnimationStatusDescription,
        .agentSummary =
            "Reports the Animators of the scene (entity names one, otherwise up to 100): the saved clip, "
            "graph, speed, time, loop and playing, and, in a play session (play.start), whether the pose "
            "exists and its joint count, whether the graph has loaded, for each layer the current state, its "
            "normalized time and the state a running transition fades out, every parameter with its value "
            "(a Trigger is 1 while set), and the state events of the last update. The edit World reports "
            "live:true with the poses (time, joints) of the viewports when a viewport poses it; else only "
            "the saved fields are shown and note says so. Example: "
            "{\"entity\": \"/Column\"}.",
        .category = "Animation",
        .icon = "animation",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Read,
    });
    registry.add<&animationSetParameter>({
        .name = "animation.set_parameter",
        .title = strings::kCommandAnimationSetParameterTitle,
        .description = strings::kCommandAnimationSetParameterDescription,
        .agentSummary =
            "Sets a Float, Int or Bool parameter of the animation graph of an Animator in the running play "
            "session; the graph uses it at the next update (play.step), where animation.status shows it. "
            "value is a number for a Float, a whole number up to 16777216 for an Int, true or false for a "
            "Bool; a Trigger takes animation.trigger. Not undoable: it is runtime state. Example: "
            "{\"entity\": \"/Column\", \"name\": \"lean\", \"value\": 0.8}.",
        .category = "Animation",
        .icon = "tune",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&animationTrigger>({
        .name = "animation.trigger",
        .title = strings::kCommandAnimationTriggerTitle,
        .description = strings::kCommandAnimationTriggerDescription,
        .agentSummary =
            "Sets a Trigger of the animation graph of an Animator in the running play session: it stays set "
            "until a transition consumes it or the next update ends (play.step). Not undoable: it is "
            "runtime state. Use animation.status for the parameters. Example: {\"entity\": \"/Column\", "
            "\"name\": \"bow\"}.",
        .category = "Animation",
        .icon = "bolt",
        .mode = CommandMode::Any,
        .permission = PermissionClass::Edit,
    });
    registry.add<&animationScrub>({
        .name = "animation.scrub",
        .title = strings::kCommandAnimationScrubTitle,
        .description = strings::kCommandAnimationScrubDescription,
        .agentSummary =
            "Sets Animator::time, the seconds into the clip (or into the entry states of the graph), of an "
            "Animator of the open scene; the viewport shows the pose at that time. Undoable and saved with "
            "the scene; the game starts from it. time is between 0 and 1,000,000; a looping clip wraps, a "
            "clip that does not loop holds its last pose. The result has the time before. Example: "
            "{\"entity\": \"/Column\", \"time\": 0.6}.",
        .category = "Animation",
        .icon = "timeline",
        .flags = {.idempotent = true, .undoable = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Edit,
    });
    registry.add<&animationValidate>({
        .name = "animation.validate",
        .title = strings::kCommandAnimationValidateTitle,
        .description = strings::kCommandAnimationValidateDescription,
        .agentSummary =
            "Checks an .animgraph or .ragdoll file of the project: valid is true when it parses (then "
            "parameters, layers and states count a graph, parts a ragdoll profile). Otherwise error is the "
            "first problem and errorPath the JSON path of the offending value, e.g. "
            "\"layers[0].states[2].motion\". A file that does not parse is a result, not a failed command. "
            "Use it after writing a graph with asset tools. Example: {\"path\": "
            "\"assets:/animations/column.animgraph\"}.",
        .category = "Animation",
        .icon = "fact_check",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Read,
    });
}

} // namespace illusion::editor::detail
