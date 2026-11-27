#include "AssetTestProject.hpp"

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/AssetFormat.hpp>
#include <illusion/assets/ImportSettings.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/assets/Rig.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

using namespace illusion;

namespace
{

// tests/data/gltf/humanoid_a.gltf or humanoid_b.gltf (see generate.py).
ModelData loadModel(std::string_view file)
{
    VirtualFileSystem files;
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
    std::string error;
    std::optional<ModelData> model = loadGltf(files, std::format("test:/gltf/{}", file), &error);
    INFO(error);
    REQUIRE(model.has_value());
    REQUIRE(model->skins.size() == 1);
    return std::move(*model);
}

Skeleton loadSkeleton(std::string_view file)
{
    return loadModel(file).skins[0];
}

// The joint name that @p rig gives @p bone, or "" if no joint has it.
std::string jointOf(const Skeleton& skeleton, const RigMap& rig, std::string_view bone)
{
    const i32 joint = rig.find(bone);
    return joint >= 0 ? skeleton.joints[static_cast<usize>(joint)].name : std::string();
}

std::string boneOf(const Skeleton& skeleton, const RigMap& rig, std::string_view joint)
{
    for (usize j = 0; j < skeleton.joints.size(); ++j)
    {
        if (skeleton.joints[j].name == joint)
        {
            return rig.bones[j];
        }
    }
    FAIL("no joint ", joint);
    return {};
}

RetargetTranslation modeOf(const Skeleton& skeleton, const RigMap& rig, std::string_view joint)
{
    for (usize j = 0; j < skeleton.joints.size(); ++j)
    {
        if (skeleton.joints[j].name == joint)
        {
            return rig.translation[j];
        }
    }
    FAIL("no joint ", joint);
    return {};
}

void checkBones(const Skeleton& skeleton, const RigMap& rig,
                std::initializer_list<std::pair<std::string_view, std::string_view>> expected)
{
    for (const auto& [bone, joint] : expected)
    {
        INFO(bone);
        CHECK(jointOf(skeleton, rig, bone) == joint);
    }
}

// @p skeleton with its joints renamed joint0, joint1, ...
Skeleton anonymous(Skeleton skeleton)
{
    for (usize j = 0; j < skeleton.joints.size(); ++j)
    {
        skeleton.joints[j].name = std::format("joint{}", j);
    }
    return skeleton;
}

// @p skeleton with its joints in another order that keeps parents first: by depth, the later joints of
// the file first.
Skeleton reordered(const Skeleton& skeleton)
{
    const usize count = skeleton.joints.size();
    std::vector<u32> depth(count, 0);
    for (usize j = 0; j < count; ++j)
    {
        const i32 parent = skeleton.joints[j].parent;
        depth[j] = parent >= 0 ? depth[static_cast<usize>(parent)] + 1 : 0;
    }
    std::vector<usize> order(count);
    std::iota(order.begin(), order.end(), usize{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](usize a, usize b) { return depth[a] != depth[b] ? depth[a] < depth[b] : a > b; });
    std::vector<i32> newIndex(count, -1);
    for (usize k = 0; k < count; ++k)
    {
        newIndex[order[k]] = static_cast<i32>(k);
    }
    Skeleton result = skeleton;
    for (usize k = 0; k < count; ++k)
    {
        result.joints[k] = skeleton.joints[order[k]];
        const i32 parent = skeleton.joints[order[k]].parent;
        result.joints[k].parent = parent >= 0 ? newIndex[static_cast<usize>(parent)] : -1;
    }
    bool moved = false;
    for (usize k = 0; k < count; ++k)
    {
        moved = moved || result.joints[k].name != skeleton.joints[k].name;
    }
    REQUIRE(moved);
    return result;
}

// Every joint's bone and mode in @p skeleton equals that of the joint of the same name in @p other.
void checkSameMap(const Skeleton& skeleton, const RigMap& rig, const Skeleton& other, const RigMap& otherRig)
{
    REQUIRE(skeleton.joints.size() == other.joints.size());
    for (usize j = 0; j < skeleton.joints.size(); ++j)
    {
        const std::string& name = skeleton.joints[j].name;
        INFO(name);
        CHECK(rig.bones[j] == boneOf(other, otherRig, name));
        CHECK(rig.translation[j] == modeOf(other, otherRig, name));
    }
}

constexpr std::string_view kRigA = "humanoid_a.gltf";
constexpr std::string_view kRigB = "humanoid_b.gltf";

} // namespace

TEST_CASE("humanoid bone names are camelCase, parents first, and bone names are validated")
{
    CHECK(kHumanoidBones.front() == "root");
    CHECK(isHumanoidBone("leftUpperArm"));
    CHECK(isHumanoidBone("rightLittleDistal"));
    CHECK_FALSE(isHumanoidBone("LeftUpperArm"));
    CHECK_FALSE(isHumanoidBone("tail"));
    for (const std::string_view bone : kRequiredHumanoidBones)
    {
        CHECK(isHumanoidBone(bone));
    }
    CHECK(isValidBoneName("tail_01"));
    CHECK(isValidBoneName("weapon.R-grip"));
    CHECK(isValidBoneName(std::string(64, 'a')));
    CHECK_FALSE(isValidBoneName(""));
    CHECK_FALSE(isValidBoneName(std::string(65, 'a')));
    CHECK_FALSE(isValidBoneName("left arm"));
    CHECK_FALSE(isValidBoneName("mixamorig:Hips"));
    CHECK(retargetTranslationName(RetargetTranslation::Skeleton) == "Skeleton");
    CHECK(retargetTranslationName(RetargetTranslation::Scaled) == "Scaled");
    CHECK(retargetTranslationName(RetargetTranslation::Animation) == "Animation");

    RigMap rig;
    CHECK(rig.empty());
    CHECK(rig.find("hips") == -1);
    rig.bones = {"", "hips", ""};
    rig.translation.resize(3);
    CHECK_FALSE(rig.empty());
    CHECK(rig.find("hips") == 1);
    CHECK(rig.find("") == -1);
}

TEST_CASE("the Mixamo-style test rig maps every humanoid bone by name, fingers included")
{
    const Skeleton skeleton = loadSkeleton(kRigA);
    RigMapReport report;
    const RigMap rig = generateRigMap(skeleton, &report);
    CHECK(rig.bones.size() == skeleton.joints.size());
    CHECK(rig.translation.size() == skeleton.joints.size());
    CHECK(report.humanoid);
    CHECK(report.missing.empty());
    CHECK(report.warnings.empty());
    checkBones(skeleton, rig,
               {{"root", ""},
                {"hips", "mixamorig:Hips"},
                {"spine", "mixamorig:Spine"},
                {"chest", "mixamorig:Spine1"},
                {"upperChest", "mixamorig:Spine2"},
                {"neck", "mixamorig:Neck"},
                {"head", "mixamorig:Head"},
                {"leftEye", "mixamorig:LeftEye"},
                {"rightEye", "mixamorig:RightEye"},
                {"leftShoulder", "mixamorig:LeftShoulder"},
                {"leftUpperArm", "mixamorig:LeftArm"},
                {"leftLowerArm", "mixamorig:LeftForeArm"},
                {"leftHand", "mixamorig:LeftHand"},
                {"rightUpperArm", "mixamorig:RightArm"},
                {"rightHand", "mixamorig:RightHand"},
                {"leftUpperLeg", "mixamorig:LeftUpLeg"},
                {"leftLowerLeg", "mixamorig:LeftLeg"},
                {"leftFoot", "mixamorig:LeftFoot"},
                {"leftToes", "mixamorig:LeftToeBase"},
                {"rightUpperLeg", "mixamorig:RightUpLeg"},
                {"rightToes", "mixamorig:RightToeBase"},
                {"leftThumbProximal", "mixamorig:LeftHandThumb1"},
                {"leftThumbDistal", "mixamorig:LeftHandThumb3"},
                {"leftIndexIntermediate", "mixamorig:LeftHandIndex2"},
                {"rightMiddleProximal", "mixamorig:RightHandMiddle1"},
                {"rightRingDistal", "mixamorig:RightHandRing3"},
                {"leftLittleProximal", "mixamorig:LeftHandPinky1"},
                {"rightLittleDistal", "mixamorig:RightHandPinky3"}});
    // End joints are not bones.
    CHECK(boneOf(skeleton, rig, "mixamorig:HeadTop_End").empty());
    CHECK(boneOf(skeleton, rig, "mixamorig:LeftHandIndex4").empty());
    CHECK(boneOf(skeleton, rig, "mixamorig:RightToe_End").empty());
    // Every humanoid bone of the rig: 15 required, 9 more of the body and 30 finger bones.
    const auto mapped =
        std::count_if(rig.bones.begin(), rig.bones.end(), [](const std::string& b) { return !b.empty(); });
    CHECK(mapped == 54);
    CHECK(modeOf(skeleton, rig, "mixamorig:Hips") == RetargetTranslation::Scaled);
    CHECK(modeOf(skeleton, rig, "mixamorig:Spine") == RetargetTranslation::Skeleton);
    CHECK(modeOf(skeleton, rig, "mixamorig:LeftHand") == RetargetTranslation::Skeleton);
}

TEST_CASE("the Unreal-style test rig maps every humanoid bone by name and skips twist joints")
{
    const Skeleton skeleton = loadSkeleton(kRigB);
    RigMapReport report;
    const RigMap rig = generateRigMap(skeleton, &report);
    CHECK(report.humanoid);
    CHECK(report.warnings.empty());
    checkBones(skeleton, rig,
               {{"root", "root"},
                {"hips", "pelvis"},
                {"spine", "spine_01"},
                {"chest", "spine_02"},
                {"upperChest", "spine_03"},
                {"neck", "neck_01"},
                {"head", "head"},
                {"leftEye", ""},
                {"leftShoulder", "clavicle_l"},
                {"leftUpperArm", "upperarm_l"},
                {"leftLowerArm", "lowerarm_l"},
                {"leftHand", "hand_l"},
                {"rightShoulder", "clavicle_r"},
                {"rightLowerArm", "lowerarm_r"},
                {"leftUpperLeg", "thigh_l"},
                {"leftLowerLeg", "calf_l"},
                {"leftFoot", "foot_l"},
                {"leftToes", "ball_l"},
                {"rightLowerLeg", "calf_r"},
                {"rightToes", "ball_r"},
                {"leftThumbProximal", "thumb_01_l"},
                {"leftThumbIntermediate", "thumb_02_l"},
                {"rightIndexDistal", "index_03_r"},
                {"leftMiddleIntermediate", "middle_02_l"},
                {"rightRingProximal", "ring_01_r"},
                {"leftLittleDistal", "pinky_03_l"}});
    CHECK(boneOf(skeleton, rig, "upperarm_twist_01_l").empty());
    CHECK(modeOf(skeleton, rig, "root") == RetargetTranslation::Scaled);
    CHECK(modeOf(skeleton, rig, "pelvis") == RetargetTranslation::Scaled);
    CHECK(modeOf(skeleton, rig, "spine_01") == RetargetTranslation::Skeleton);
}

TEST_CASE("rigs without bone names map every required bone by topology")
{
    for (const std::string_view file : {kRigA, kRigB})
    {
        INFO(file);
        const Skeleton named = loadSkeleton(file);
        const RigMap byName = generateRigMap(named);
        const Skeleton skeleton = anonymous(named);
        RigMapReport report;
        const RigMap rig = generateRigMap(skeleton, &report);
        CHECK(report.humanoid);
        CHECK(report.missing.empty());
        // The bones topology finds are on the joints the names give them.
        for (usize j = 0; j < skeleton.joints.size(); ++j)
        {
            if (!rig.bones[j].empty())
            {
                INFO(named.joints[j].name);
                CHECK(rig.bones[j] == byName.bones[j]);
            }
        }
        for (const std::string_view bone : {"spine", "chest", "upperChest", "neck", "leftShoulder",
                                            "rightShoulder", "leftToes", "rightToes"})
        {
            INFO(bone);
            CHECK(rig.find(bone) == byName.find(bone));
        }
        CHECK(rig.find("leftThumbProximal") == -1); // fingers need names
    }
    // The ground joint above the hips is the root.
    const Skeleton b = anonymous(loadSkeleton(kRigB));
    CHECK(generateRigMap(b).find("root") == 0);
}

TEST_CASE("the joint order of the file does not change the rig map")
{
    for (const std::string_view file : {kRigA, kRigB})
    {
        INFO(file);
        for (const bool names : {true, false})
        {
            const Skeleton skeleton = names ? loadSkeleton(file) : anonymous(loadSkeleton(file));
            const Skeleton other = reordered(skeleton);
            RigMapReport report;
            RigMapReport otherReport;
            const RigMap rig = generateRigMap(skeleton, &report);
            const RigMap otherRig = generateRigMap(other, &otherReport);
            checkSameMap(skeleton, rig, other, otherRig);
            CHECK(report.missing == otherReport.missing);
            CHECK(report.warnings == otherReport.warnings);
            CHECK(generateRigMap(skeleton).bones == rig.bones); // and repeats
        }
    }
}

TEST_CASE("a name that says left on the right side is a warning and loses to the joint on the left")
{
    Skeleton skeleton = loadSkeleton(kRigA);
    for (SkeletonJoint& joint : skeleton.joints)
    {
        if (joint.name == "mixamorig:RightHand")
        {
            joint.name = "mixamorig:LeftHand2";
        }
    }
    RigMapReport report;
    const RigMap rig = generateRigMap(skeleton, &report);
    CHECK(jointOf(skeleton, rig, "leftHand") == "mixamorig:LeftHand");
    CHECK(jointOf(skeleton, rig, "rightHand") == "mixamorig:LeftHand2"); // by topology
    CHECK(report.humanoid);
    REQUIRE_FALSE(report.warnings.empty());
    const bool named = std::any_of(report.warnings.begin(), report.warnings.end(), [](const std::string& w) {
        return w.find("'mixamorig:LeftHand2' is named left but lies on the right side") != std::string::npos;
    });
    CHECK(named);
}

TEST_CASE("names of common conventions map without their prefixes, separators and helper joints")
{
    const std::pair<std::string_view, std::string_view> cases[] = {
        {"DEF-upper_arm.L", "leftUpperArm"},
        {"Bip01 L Thigh", "leftUpperLeg"},
        {"hips_JNT", "hips"},
        {"mixamorig_RightForeArm", "rightLowerArm"},
        {"f_index.01.R", "rightIndexProximal"},
        {"Character1:LeftShoulder", "leftShoulder"},
        {"R_Wrist", "rightHand"},
        {"NeckBase", ""},
        {"Spine", "spine"},
        {"Left_Toe_End", ""},
        {"spine_twist", ""},
        {"RightUpLeg_IK", ""},
        {"tail_01", ""},
    };
    for (const auto& [name, bone] : cases)
    {
        INFO(name);
        Skeleton skeleton;
        skeleton.joints.resize(1);
        skeleton.joints[0].name = std::string(name);
        RigMapReport report;
        const RigMap rig = generateRigMap(skeleton, &report);
        CHECK(rig.bones[0] == bone);
        CHECK(rig.translation[0] == RetargetTranslation::Scaled); // a joint without a parent
        CHECK_FALSE(report.humanoid);
    }
    RigMapReport report;
    const RigMap rig = generateRigMap(Skeleton{}, &report);
    CHECK(rig.bones.empty());
    CHECK(report.missing.size() == kRequiredHumanoidBones.size());
    CHECK(report.missing.front() == "hips");
}

TEST_CASE("rig overrides change bones and modes and fail without changing anything")
{
    const Skeleton skeleton = loadSkeleton(kRigB);
    const RigMap generated = generateRigMap(skeleton);

    RigMap rig = generated;
    SkinImportSettings settings;
    settings.bones = {{.joint = "head", .bone = ""},
                      {.joint = "spine_03", .bone = "chest"},
                      {.joint = "upperarm_twist_01_l", .bone = "leftUpperArmTwist"}};
    settings.translation = {{.joint = "pelvis", .mode = RetargetTranslation::Animation},
                            {.joint = "hand_l", .mode = RetargetTranslation::Scaled}};
    REQUIRE(applyRigOverrides(rig, skeleton, settings).hasValue());
    CHECK(jointOf(skeleton, rig, "head").empty());
    CHECK(jointOf(skeleton, rig, "chest") == "spine_03"); // taken from spine_02
    CHECK(boneOf(skeleton, rig, "spine_02").empty());
    CHECK(jointOf(skeleton, rig, "upperChest").empty());
    CHECK(jointOf(skeleton, rig, "leftUpperArmTwist") == "upperarm_twist_01_l");
    CHECK(modeOf(skeleton, rig, "pelvis") == RetargetTranslation::Animation);
    CHECK(modeOf(skeleton, rig, "hand_l") == RetargetTranslation::Scaled);
    CHECK(modeOf(skeleton, rig, "root") == RetargetTranslation::Scaled);

    // An override that maps the hips gives the joint the default mode of the hips.
    rig = generated;
    settings = {};
    settings.generate = false;
    settings.bones = {{.joint = "spine_01", .bone = "hips"}};
    REQUIRE(applyRigOverrides(rig, skeleton, settings).hasValue());
    CHECK(jointOf(skeleton, rig, "hips") == "spine_01");
    CHECK(std::count_if(rig.bones.begin(), rig.bones.end(),
                        [](const std::string& b) { return !b.empty(); }) == 1);
    CHECK(modeOf(skeleton, rig, "spine_01") == RetargetTranslation::Scaled);
    CHECK(modeOf(skeleton, rig, "pelvis") == RetargetTranslation::Skeleton);
    CHECK(modeOf(skeleton, rig, "root") == RetargetTranslation::Scaled);

    const auto fails = [&](SkinImportSettings bad, std::string_view expected) {
        RigMap copy = generated;
        const Expected<void, std::string> result = applyRigOverrides(copy, skeleton, bad);
        REQUIRE_FALSE(result.hasValue());
        INFO(result.error());
        CHECK(result.error().find(expected) != std::string::npos);
        CHECK(copy == generated);
    };
    SkinImportSettings bad;
    bad.bones = {{.joint = "pelvis", .bone = "hips"}, {.joint = "nope", .bone = "spine"}};
    fails(bad, "bones/nope: the skin 'body' has no joint 'nope'");
    bad.bones = {{.joint = "pelvis", .bone = "bad bone"}};
    fails(bad, "bones/pelvis: 'bad bone' is not a bone name");
    bad.bones = {{.joint = "pelvis", .bone = "tail"}, {.joint = "root", .bone = "tail"}};
    fails(bad, "bones/root: the bone tail is also given to the joint 'pelvis'");
    bad.bones = {{.joint = "pelvis", .bone = "hips"}, {.joint = "pelvis", .bone = "root"}};
    fails(bad, "bones/pelvis: the joint is given a bone twice");
    bad.bones = {};
    bad.translation = {{.joint = "a/b", .mode = RetargetTranslation::Skeleton}};
    fails(bad, "translation/a~1b: the skin 'body' has no joint 'a/b'");
    bad.translation = {{.joint = "head", .mode = RetargetTranslation::Skeleton},
                       {.joint = "head", .mode = RetargetTranslation::Scaled}};
    fails(bad, "translation/head: the joint is given a mode twice");
    bad.translation = {{.joint = "head", .mode = static_cast<RetargetTranslation>(3)}};
    fails(bad, "translation/head: not a translation mode");
}

TEST_CASE("model format 7 round trips rig maps and source skins and rejects invalid ones")
{
    for (const std::string_view file : {kRigA, kRigB})
    {
        const ModelData model = loadModel(file);
        REQUIRE(model.animations.size() == 1);
        CHECK(model.animations[0].skeleton == 0);
        CHECK(model.skins[0].rig == generateRigMap(model.skins[0]));
        const std::vector<std::byte> bytes = serializeModel(model);
        const std::optional<ModelData> read = deserializeModel(bytes);
        REQUIRE(read.has_value());
        CHECK(read->skins[0].rig == model.skins[0].rig);
        CHECK(read->animations[0].skeleton == 0);
        CHECK(serializeModel(*read) == bytes);
    }
    CHECK(kModelFormatVersion == 7);

    const ModelData model = loadModel(kRigB);
    const auto rejects = [&](auto change) {
        ModelData broken = model;
        change(broken);
        return !deserializeModel(serializeModel(broken)).has_value();
    };
    CHECK_FALSE(rejects([](ModelData& m) { m.skins[0].rig = {}; })); // a skeleton without a rig map
    CHECK_FALSE(rejects([](ModelData& m) { m.animations[0].skeleton = -1; }));
    CHECK(rejects([](ModelData& m) {
        m.skins[0].rig.bones.pop_back();
        m.skins[0].rig.translation.pop_back();
    }));
    CHECK(rejects([](ModelData& m) { m.skins[0].rig.bones[3] = "not a bone"; }));
    CHECK(rejects([](ModelData& m) { m.skins[0].rig.bones[3] = m.skins[0].rig.bones[1]; })); // twice
    CHECK(rejects([](ModelData& m) { m.skins[0].rig.translation[2] = static_cast<RetargetTranslation>(3); }));
    CHECK(rejects([](ModelData& m) { m.animations[0].skeleton = 1; })); // no such skin
    CHECK(rejects([](ModelData& m) { m.animations[0].skeleton = -2; }));
}

TEST_CASE("each clip names the skin its channels hit most, the first on ties")
{
    VirtualFileSystem files;
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
    const std::optional<ModelData> rigged = loadGltf(files, "test:/gltf/rigged.gltf");
    REQUIRE(rigged.has_value());
    REQUIRE(rigged->animations.size() == 3);
    for (const AnimationClip& clip : rigged->animations)
    {
        INFO(clip.name);
        CHECK(clip.skeleton == 0); // "cubic" moves only "side", a joint of both skins
    }
    CHECK(rigged->skins[1].rig.bones.size() == 1);
}

TEST_CASE("sidecar version 3 parses, merges and writes skin settings with JSON-path errors")
{
    const std::string_view text = R"({"format": "illusion.import", "version": 3, "model": {"skins": [
        {"bones": {"spine_03": "chest", "head": ""}, "translation": {"pelvis": "Animation"}},
        {"generate": false}]}})";
    const Expected<ImportSettings, ImportSettingsError> parsed =
        parseImportSettings(text, ImportSettingsKind::Model);
    REQUIRE(parsed.hasValue());
    const std::vector<SkinImportSettings>& skins = parsed->model.skins;
    REQUIRE(skins.size() == 2);
    CHECK(skins[0].generate);
    CHECK(skins[0].bones == std::vector<RigBoneOverride>{{.joint = "head", .bone = ""},
                                                         {.joint = "spine_03", .bone = "chest"}});
    CHECK(skins[0].translation ==
          std::vector<RigTranslationOverride>{{.joint = "pelvis", .mode = RetargetTranslation::Animation}});
    CHECK_FALSE(skins[1].generate);
    CHECK(skins[1].bones.empty());

    const std::string written = writeImportSettings(*parsed, ImportSettingsKind::Model);
    CHECK(written.find("\"version\": 3") != std::string::npos);
    CHECK(written.find("\"generate\": false") != std::string::npos);
    CHECK(written.find("\"generate\": true") == std::string::npos);
    CHECK(parseImportSettings(written, ImportSettingsKind::Model).value() == *parsed);
    CHECK(importSettingsValue(*parsed, ImportSettingsKind::Model) ==
          R"({"model":{"scale":1.0,"lodCount":3,"lodReduction":0.5,"compressTextures":true,)"
          R"("textureQuality":"normal","skins":[{"generate":true,"bones":{"head":"","spine_03":"chest"},)"
          R"("translation":{"pelvis":"Animation"}},{"generate":false,"bones":{},"translation":{}}]}})");

    // Merging replaces the list (JSON merge patch) and null resets it.
    ImportSettings merged = *parsed;
    Expected<JsonDocument, JsonError> patch = JsonDocument::parse(R"({"model": {"skins": [{}]}})");
    REQUIRE(patch.hasValue());
    REQUIRE(mergeImportSettings(merged, patch->root(), ImportSettingsKind::Model).hasValue());
    CHECK(merged.model.skins == std::vector<SkinImportSettings>{SkinImportSettings{}});
    patch = JsonDocument::parse(R"({"model": {"skins": null}})");
    REQUIRE(patch.hasValue());
    REQUIRE(mergeImportSettings(merged, patch->root(), ImportSettingsKind::Model).hasValue());
    CHECK(merged.isDefault());

    const auto pointerOf = [](std::string_view model, u32 version = 3) {
        const std::string sidecar =
            std::format(R"({{"format": "illusion.import", "version": {}, "model": {}}})", version, model);
        const Expected<ImportSettings, ImportSettingsError> result =
            parseImportSettings(sidecar, ImportSettingsKind::Model);
        REQUIRE_FALSE(result.hasValue());
        return result.error().pointer;
    };
    CHECK(pointerOf(R"({"skins": {}})") == "/model/skins");
    CHECK(pointerOf(R"({"skins": [3]})") == "/model/skins/0");
    CHECK(pointerOf(R"({"skins": [{}, {"bones": []}]})") == "/model/skins/1/bones");
    CHECK(pointerOf(R"({"skins": [{"bones": {"a/b": "bad bone"}}]})") == "/model/skins/0/bones/a~1b");
    CHECK(pointerOf(R"({"skins": [{"bones": {"head": 1}}]})") == "/model/skins/0/bones/head");
    CHECK(pointerOf(R"({"skins": [{"bones": {"": "head"}}]})") == "/model/skins/0/bones/");
    CHECK(pointerOf(R"({"skins": [{"translation": {"head": "scaled"}}]})") ==
          "/model/skins/0/translation/head");
    CHECK(pointerOf(R"({"skins": [{"generate": 1}]})") == "/model/skins/0/generate");
    CHECK(pointerOf(R"({"skins": [{"mirror": true}]})") == "/model/skins/0/mirror");
    CHECK(pointerOf(R"({"skins": []})", 2) == "/model/skins"); // skins need version 3
    CHECK(parseImportSettings(R"({"format": "illusion.import", "version": 2, "model": {"scale": 2}})",
                              ImportSettingsKind::Model)
              .value()
              .model.scale == 2.0f);
}

TEST_CASE("model sidecars correct the rig map, recook when they change and name themselves in errors")
{
    test::AssetProject project;
    constexpr std::string_view kModel = "game:/humanoid_b.gltf";
    const auto writeSidecar = [&](std::string_view text) {
        REQUIRE(project.files.writeText(importSettingsPath(kModel), text) == FileError::None);
    };
    AssetDatabase database(project.files, "cache");
    std::optional<ModelData> model = database.loadModel(kModel);
    REQUIRE(model.has_value());
    CHECK(jointOf(model->skins[0], model->skins[0].rig, "upperChest") == "spine_03");

    writeSidecar(R"({"format": "illusion.import", "version": 3, "model": {"skins": [
        {"bones": {"spine_03": "chest", "tail": "x"}}]}})");
    std::string error;
    CHECK_FALSE(database.loadModel(kModel, &error).has_value());
    CHECK(error.find("humanoid_b.gltf.import.json/model/skins/0/bones/tail: the skin 'body' has no joint") !=
          std::string::npos);

    writeSidecar(R"({"format": "illusion.import", "version": 3, "model": {"skins": [{}, {}]}})");
    CHECK_FALSE(database.loadModel(kModel, &error).has_value());
    CHECK(error.find("humanoid_b.gltf.import.json/model/skins/1: the model has 1 skins") !=
          std::string::npos);

    writeSidecar(R"({"format": "illusion.import", "version": 3, "model": {"skins": [
        {"bones": {"spine_03": "chest"}, "translation": {"pelvis": "Animation"}}]}})");
    const u32 imports = database.statistics().imports;
    model = database.loadModel(kModel);
    REQUIRE(model.has_value());
    CHECK(database.statistics().imports == imports + 1);
    const Skeleton& skin = model->skins[0];
    CHECK(jointOf(skin, skin.rig, "chest") == "spine_03");
    CHECK(jointOf(skin, skin.rig, "upperChest").empty());
    CHECK(modeOf(skin, skin.rig, "pelvis") == RetargetTranslation::Animation);

    // The cooked file keeps the corrected map; an unchanged sidecar does not import again.
    model = database.loadModel(kModel);
    REQUIRE(model.has_value());
    CHECK(database.statistics().imports == imports + 1);
    CHECK(modeOf(model->skins[0], model->skins[0].rig, "pelvis") == RetargetTranslation::Animation);

    writeSidecar(R"({"format": "illusion.import", "version": 3, "model": {"skins": [{"generate": false}]}})");
    model = database.loadModel(kModel);
    REQUIRE(model.has_value());
    CHECK(database.statistics().imports == imports + 2);
    CHECK(model->skins[0].rig.empty());
}
