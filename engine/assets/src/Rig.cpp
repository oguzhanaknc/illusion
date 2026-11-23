#include <illusion/assets/Animation.hpp>
#include <illusion/assets/Rig.hpp>
#include <illusion/core/Assert.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

constexpr usize kMaxBoneNameLength = 64;

enum class Side : u8
{
    None,
    Left,
    Right,
};

// What a joint's name says it is: a bone without a side, a sided part ("UpperArm", "Eye"), a spine joint or
// a finger joint.
enum class Kind : u8
{
    None,
    Bone,
    Sided,
    Spine,
    Finger,
};

struct NameMatch
{
    Kind kind = Kind::None;
    std::string_view part; // the bone (Bone), the part after the side (Sided), or the finger (Finger)
    Side side = Side::None;
};

struct Alias
{
    std::string_view key;
    Kind kind;
    std::string_view part;
};

// Names after separators, case, side tokens and prefixes are removed, joined without separators.
constexpr std::array kAliases{
    Alias{"root", Kind::Bone, "root"},
    Alias{"hips", Kind::Bone, "hips"},
    Alias{"hip", Kind::Bone, "hips"},
    Alias{"pelvis", Kind::Bone, "hips"},
    Alias{"spine", Kind::Spine, ""},
    Alias{"chest", Kind::Spine, ""},
    Alias{"upperchest", Kind::Spine, ""},
    Alias{"torso", Kind::Spine, ""},
    Alias{"abdomen", Kind::Spine, ""},
    Alias{"stomach", Kind::Spine, ""},
    Alias{"waist", Kind::Spine, ""},
    Alias{"neck", Kind::Bone, "neck"},
    Alias{"head", Kind::Bone, "head"},
    Alias{"jaw", Kind::Bone, "jaw"},
    Alias{"eye", Kind::Sided, "Eye"},
    Alias{"shoulder", Kind::Sided, "Shoulder"},
    Alias{"clavicle", Kind::Sided, "Shoulder"},
    Alias{"clav", Kind::Sided, "Shoulder"},
    Alias{"collar", Kind::Sided, "Shoulder"},
    Alias{"collarbone", Kind::Sided, "Shoulder"},
    Alias{"arm", Kind::Sided, "UpperArm"},
    Alias{"upperarm", Kind::Sided, "UpperArm"},
    Alias{"uparm", Kind::Sided, "UpperArm"},
    Alias{"humerus", Kind::Sided, "UpperArm"},
    Alias{"forearm", Kind::Sided, "LowerArm"},
    Alias{"lowerarm", Kind::Sided, "LowerArm"},
    Alias{"loarm", Kind::Sided, "LowerArm"},
    Alias{"elbow", Kind::Sided, "LowerArm"},
    Alias{"hand", Kind::Sided, "Hand"},
    Alias{"wrist", Kind::Sided, "Hand"},
    Alias{"upleg", Kind::Sided, "UpperLeg"},
    Alias{"upperleg", Kind::Sided, "UpperLeg"},
    Alias{"thigh", Kind::Sided, "UpperLeg"},
    Alias{"femur", Kind::Sided, "UpperLeg"},
    Alias{"leg", Kind::Sided, "LowerLeg"},
    Alias{"lowerleg", Kind::Sided, "LowerLeg"},
    Alias{"loleg", Kind::Sided, "LowerLeg"},
    Alias{"calf", Kind::Sided, "LowerLeg"},
    Alias{"shin", Kind::Sided, "LowerLeg"},
    Alias{"knee", Kind::Sided, "LowerLeg"},
    Alias{"foot", Kind::Sided, "Foot"},
    Alias{"ankle", Kind::Sided, "Foot"},
    Alias{"toe", Kind::Sided, "Toes"},
    Alias{"toes", Kind::Sided, "Toes"},
    Alias{"toebase", Kind::Sided, "Toes"},
    Alias{"ball", Kind::Sided, "Toes"},
};

constexpr std::array<std::pair<std::string_view, std::string_view>, 7> kFingers{{
    {"thumb", "Thumb"},
    {"index", "Index"},
    {"middle", "Middle"},
    {"ring", "Ring"},
    {"pinky", "Little"},
    {"pinkie", "Little"},
    {"little", "Little"},
}};

constexpr std::array<std::string_view, 3> kPhalanges{"Proximal", "Intermediate", "Distal"};

// Tokens that make a joint a helper, not a humanoid bone (end joints, twist and IK joints, ...).
constexpr std::array<std::string_view, 19> kExcluded{
    "end",     "nub",    "tip",        "twist",      "roll", "ik",    "fk",     "pole", "target", "ctrl",
    "control", "helper", "corrective", "metacarpal", "meta", "dummy", "socket", "null", "palm",
};

// Prefix tokens of common exporters and rigging tools, dropped before matching.
constexpr std::array<std::string_view, 12> kIgnored{
    "mixamorig", "bip", "biped", "def", "org", "mch", "jnt", "joint", "bind", "bn", "sk", "armature",
};

template <usize N>
bool contains(const std::array<std::string_view, N>& list, std::string_view token) noexcept
{
    return std::find(list.begin(), list.end(), token) != list.end();
}

bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

bool isUpper(char c) noexcept
{
    return c >= 'A' && c <= 'Z';
}

bool isLower(char c) noexcept
{
    return c >= 'a' && c <= 'z';
}

bool isAlnum(char c) noexcept
{
    return isDigit(c) || isUpper(c) || isLower(c);
}

// Lower-case tokens of @p name: the text after the last ':' or '|' (namespaces), split at separators, at
// lower-to-upper case changes ("LeftUpLeg") and between letters and digits ("Spine2").
std::vector<std::string> nameTokens(std::string_view name)
{
    if (const usize colon = name.find_last_of(":|"); colon != std::string_view::npos)
    {
        name = name.substr(colon + 1);
    }
    std::vector<std::string> tokens;
    std::string token;
    const auto flush = [&] {
        if (!token.empty())
        {
            tokens.push_back(std::move(token));
            token.clear();
        }
    };
    for (usize i = 0; i < name.size(); ++i)
    {
        const char c = name[i];
        if (!isAlnum(c))
        {
            flush();
            continue;
        }
        if (!token.empty())
        {
            const char previous = name[i - 1];
            const bool caseBreak = isUpper(c) && isLower(previous);
            const bool acronymBreak =
                isUpper(c) && isUpper(previous) && i + 1 < name.size() && isLower(name[i + 1]);
            const bool digitBreak = isDigit(c) != isDigit(previous);
            if (caseBreak || acronymBreak || digitBreak)
            {
                flush();
            }
        }
        token += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    flush();
    return tokens;
}

NameMatch matchName(std::string_view name)
{
    const std::vector<std::string> tokens = nameTokens(name);
    NameMatch match;
    std::string key;
    std::string_view finger;
    for (usize i = 0; i < tokens.size(); ++i)
    {
        const std::string& token = tokens[i];
        if (contains(kExcluded, token))
        {
            return {};
        }
        if (token == "left" || token == "l" || token == "lft")
        {
            match.side = Side::Left;
            continue;
        }
        if (token == "right" || token == "r" || token == "rgt")
        {
            match.side = Side::Right;
            continue;
        }
        if (contains(kIgnored, token) || isDigit(token.front()))
        {
            continue;
        }
        for (const auto& [alias, part] : kFingers)
        {
            if (token == alias)
            {
                finger = part;
            }
        }
        key += token;
    }
    if (!finger.empty())
    {
        match.kind = Kind::Finger;
        match.part = finger;
        return match;
    }
    for (const Alias& alias : kAliases)
    {
        if (key == alias.key)
        {
            match.kind = alias.kind;
            match.part = alias.part;
            return match;
        }
    }
    return {};
}

// Model-space positions, depths and the subtree extents of every joint of a skeleton.
struct Geometry
{
    std::vector<Vec3> position;
    std::vector<u32> depth;
    std::vector<u32> descendants; // joints below, the joint itself included
    std::vector<Vec3> subtreeMin; // per axis, over the joint and the joints below
    std::vector<Vec3> subtreeMax;
    std::vector<f32> subtreeMeanX;
    std::vector<std::vector<u32>> children; // sorted by name, then position (independent of joint order)
    f32 epsilon = 1e-6f;                    // 5 % of the skeleton's height
};

// Strict order of two joints that does not depend on their indices: name, then position.
bool jointBefore(const Skeleton& skeleton, const Geometry& geometry, u32 a, u32 b)
{
    const std::string& nameA = skeleton.joints[a].name;
    const std::string& nameB = skeleton.joints[b].name;
    if (nameA != nameB)
    {
        return nameA < nameB;
    }
    const Vec3 pa = geometry.position[a];
    const Vec3 pb = geometry.position[b];
    if (pa.x != pb.x)
    {
        return pa.x < pb.x;
    }
    if (pa.y != pb.y)
    {
        return pa.y < pb.y;
    }
    if (pa.z != pb.z)
    {
        return pa.z < pb.z;
    }
    return a < b;
}

Geometry measure(const Skeleton& skeleton)
{
    const usize count = skeleton.joints.size();
    Geometry geometry;
    geometry.position.resize(count);
    geometry.depth.assign(count, 0);
    geometry.descendants.assign(count, 1);
    geometry.subtreeMin.resize(count);
    geometry.subtreeMax.resize(count);
    geometry.subtreeMeanX.assign(count, 0.0f);
    geometry.children.resize(count);
    std::vector<Mat4> model(count, Mat4(1.0f));
    for (usize j = 0; j < count; ++j)
    {
        const SkeletonJoint& joint = skeleton.joints[j];
        const Mat4 local = glm::translate(Mat4(1.0f), joint.translation) * glm::mat4_cast(joint.rotation) *
                           glm::scale(Mat4(1.0f), joint.scale);
        if (joint.parent >= 0)
        {
            const auto parent = static_cast<usize>(joint.parent);
            model.at(j) = model[parent] * local;
            geometry.depth.at(j) = geometry.depth[parent] + 1;
            geometry.children[parent].push_back(static_cast<u32>(j));
        }
        else
        {
            model.at(j) = skeleton.rootTransform * local;
        }
        geometry.position.at(j) = Vec3(model[j][3]);
        geometry.subtreeMin.at(j) = geometry.position[j];
        geometry.subtreeMax.at(j) = geometry.position[j];
    }
    std::vector<f32> sumX(count, 0.0f);
    for (usize j = count; j-- > 0;)
    {
        sumX.at(j) += geometry.position[j].x;
        const i32 parent = skeleton.joints[j].parent;
        if (parent >= 0)
        {
            const auto p = static_cast<usize>(parent);
            geometry.descendants.at(p) += geometry.descendants[j];
            geometry.subtreeMin.at(p) = glm::min(geometry.subtreeMin[p], geometry.subtreeMin[j]);
            geometry.subtreeMax.at(p) = glm::max(geometry.subtreeMax[p], geometry.subtreeMax[j]);
            sumX.at(p) += sumX[j];
        }
    }
    f32 low = 0.0f;
    f32 high = 0.0f;
    for (usize j = 0; j < count; ++j)
    {
        geometry.subtreeMeanX.at(j) = sumX[j] / static_cast<f32>(geometry.descendants[j]);
        low = j == 0 ? geometry.position[j].y : std::min(low, geometry.position[j].y);
        high = j == 0 ? geometry.position[j].y : std::max(high, geometry.position[j].y);
    }
    geometry.epsilon = std::max((high - low) * 0.05f, 1e-6f);
    for (std::vector<u32>& children : geometry.children)
    {
        std::sort(children.begin(), children.end(),
                  [&](u32 a, u32 b) { return jointBefore(skeleton, geometry, a, b); });
    }
    return geometry;
}

// Order of two joints competing for one bone: the shallower, then jointBefore().
bool preferred(const Skeleton& skeleton, const Geometry& geometry, u32 a, u32 b)
{
    if (geometry.depth[a] != geometry.depth[b])
    {
        return geometry.depth[a] < geometry.depth[b];
    }
    return jointBefore(skeleton, geometry, a, b);
}

std::string sidedBone(Side side, std::string_view part)
{
    return std::format("{}{}", side == Side::Left ? "left" : "right", part);
}

// Gives bones to joints, keeping one joint per bone.
class Assigner
{
public:
    Assigner(const Skeleton& skeleton, const Geometry& geometry, RigMap& rig, RigMapReport& report)
        : m_skeleton(skeleton)
        , m_geometry(geometry)
        , m_rig(rig)
        , m_report(report)
    {
    }

    [[nodiscard]] bool mapped(std::string_view bone) const noexcept { return m_rig.find(bone) >= 0; }
    [[nodiscard]] bool isFree(u32 joint) const noexcept { return m_rig.bones[joint].empty(); }

    // Gives @p bone to the preferred joint of @p joints (all free): one whose side agrees with its name
    // (@p misplaced is false), then preferred(); warns when there are several.
    void assignBest(std::string_view bone, std::vector<u32>& joints, const std::vector<bool>& misplaced)
    {
        if (joints.empty() || mapped(bone))
        {
            return;
        }
        std::sort(joints.begin(), joints.end(), [&](u32 a, u32 b) {
            if (misplaced[a] != misplaced[b])
            {
                return !misplaced[a];
            }
            return preferred(m_skeleton, m_geometry, a, b);
        });
        m_rig.bones.at(joints.front()) = std::string(bone);
        for (usize i = 1; i < joints.size(); ++i)
        {
            m_report.warnings.push_back(std::format("joints '{}' and '{}' both name the bone {}; '{}' has it",
                                                    m_skeleton.joints[joints.front()].name,
                                                    m_skeleton.joints[joints[i]].name, bone,
                                                    m_skeleton.joints[joints.front()].name));
        }
    }

    // Gives @p bone to @p joint if neither is mapped yet.
    void assignIfFree(std::string_view bone, i64 joint)
    {
        if (joint >= 0 && !mapped(bone) && isFree(static_cast<u32>(joint)))
        {
            m_rig.bones.at(static_cast<usize>(joint)) = std::string(bone);
        }
    }

private:
    const Skeleton& m_skeleton;
    const Geometry& m_geometry;
    RigMap& m_rig;
    RigMapReport& m_report;
};

bool isAncestor(const Skeleton& skeleton, u32 ancestor, u32 joint) noexcept
{
    for (i32 j = skeleton.joints[joint].parent; j >= 0; j = skeleton.joints[static_cast<usize>(j)].parent)
    {
        if (static_cast<u32>(j) == ancestor)
        {
            return true;
        }
    }
    return false;
}

// The joints strictly between @p top and its ancestor @p bottom, from @p bottom up.
std::vector<u32> pathBetween(const Skeleton& skeleton, u32 bottom, u32 top)
{
    std::vector<u32> path;
    for (i32 j = skeleton.joints[top].parent; j >= 0 && static_cast<u32>(j) != bottom;
         j = skeleton.joints[static_cast<usize>(j)].parent)
    {
        path.push_back(static_cast<u32>(j));
    }
    std::reverse(path.begin(), path.end());
    return path;
}

constexpr std::array<std::string_view, 3> kSpineBones{"spine", "chest", "upperChest"};

// Bones by name: simple and sided bones, then the spine in order, then the finger chains.
void mapByName(const Skeleton& skeleton, const Geometry& geometry, RigMap& rig, RigMapReport& report)
{
    const usize count = skeleton.joints.size();
    Assigner assign(skeleton, geometry, rig, report);
    std::vector<NameMatch> matches(count);
    std::vector<bool> misplaced(count, false);
    for (usize j = 0; j < count; ++j)
    {
        NameMatch& match = matches[j];
        match = matchName(skeleton.joints[j].name);
        if (match.kind != Kind::Sided && match.kind != Kind::Finger)
        {
            continue;
        }
        const f32 x = geometry.position[j].x;
        const Side placed = x > geometry.epsilon * 0.1f
                                ? Side::Left
                                : (x < -geometry.epsilon * 0.1f ? Side::Right : Side::None);
        if (match.side == Side::None)
        {
            match.side = placed;
        }
        else if (placed != Side::None && placed != match.side)
        {
            misplaced.at(j) = true;
            report.warnings.push_back(std::format(
                "joint '{}' is named {} but lies on the {} side (x = {})", skeleton.joints[j].name,
                match.side == Side::Left ? "left" : "right", placed == Side::Left ? "left" : "right", x));
        }
        if (match.side == Side::None)
        {
            match.kind = Kind::None;
        }
    }

    for (const std::string_view bone : kHumanoidBones)
    {
        std::vector<u32> joints;
        for (usize j = 0; j < count; ++j)
        {
            const NameMatch& match = matches[j];
            if ((match.kind == Kind::Bone && match.part == bone) ||
                (match.kind == Kind::Sided && sidedBone(match.side, match.part) == bone))
            {
                joints.push_back(static_cast<u32>(j));
            }
        }
        assign.assignBest(bone, joints, misplaced);
    }

    std::vector<u32> spine;
    const i32 hips = rig.find("hips");
    i32 top = rig.find("neck");
    top = top >= 0 ? top : rig.find("head");
    if (hips >= 0 && top >= 0 && isAncestor(skeleton, static_cast<u32>(hips), static_cast<u32>(top)))
    {
        spine = pathBetween(skeleton, static_cast<u32>(hips), static_cast<u32>(top));
    }
    else
    {
        for (usize j = 0; j < count; ++j)
        {
            if (matches[j].kind == Kind::Spine)
            {
                spine.push_back(static_cast<u32>(j));
            }
        }
        std::sort(spine.begin(), spine.end(),
                  [&](u32 a, u32 b) { return preferred(skeleton, geometry, a, b); });
    }
    usize next = 0;
    for (const u32 joint : spine)
    {
        if (next < kSpineBones.size() && assign.isFree(joint))
        {
            assign.assignIfFree(kSpineBones[next++], joint);
        }
    }

    for (const Side side : {Side::Left, Side::Right})
    {
        for (const auto& [alias, finger] : kFingers)
        {
            if (alias == "pinkie" || alias == "little")
            {
                continue; // the same finger as "pinky"
            }
            std::vector<u32> chain;
            for (usize j = 0; j < count; ++j)
            {
                if (matches[j].kind == Kind::Finger && matches[j].side == side && matches[j].part == finger &&
                    assign.isFree(static_cast<u32>(j)))
                {
                    chain.push_back(static_cast<u32>(j));
                }
            }
            std::sort(chain.begin(), chain.end(),
                      [&](u32 a, u32 b) { return preferred(skeleton, geometry, a, b); });
            for (usize i = 0; i < chain.size() && i < kPhalanges.size(); ++i)
            {
                assign.assignIfFree(sidedBone(side, std::format("{}{}", finger, kPhalanges[i])), chain[i]);
            }
        }
    }
}

// The child of @p joint along its main chain (most joints below, then lowest, then jointBefore()), or -1.
i64 mainChild(const Geometry& geometry, u32 joint)
{
    i64 best = -1;
    for (const u32 child : geometry.children[joint])
    {
        if (best < 0)
        {
            best = child;
            continue;
        }
        const auto b = static_cast<u32>(best);
        if (geometry.descendants[child] > geometry.descendants[b] ||
            (geometry.descendants[child] == geometry.descendants[b] &&
             geometry.subtreeMin[child].y < geometry.subtreeMin[b].y))
        {
            best = child;
        }
    }
    return best;
}

// The child of @p joint whose subtree reaches highest, if it reaches @p margin above @p joint; or -1.
i64 upChild(const Geometry& geometry, u32 joint, f32 margin)
{
    i64 best = -1;
    for (const u32 child : geometry.children[joint])
    {
        if (geometry.subtreeMax[child].y > geometry.position[joint].y + margin &&
            (best < 0 || geometry.subtreeMax[child].y > geometry.subtreeMax[static_cast<usize>(best)].y))
        {
            best = child;
        }
    }
    return best;
}

// The hips by topology: where two chains go down and one goes up; the largest such subtree wins.
i64 findHips(const Skeleton& skeleton, const Geometry& geometry)
{
    i64 best = -1;
    for (usize j = 0; j < skeleton.joints.size(); ++j)
    {
        u32 down = 0;
        for (const u32 child : geometry.children[j])
        {
            if (geometry.subtreeMin[child].y < geometry.position[j].y - geometry.epsilon)
            {
                ++down;
            }
        }
        if (down < 2 || upChild(geometry, static_cast<u32>(j), geometry.epsilon) < 0)
        {
            continue;
        }
        const auto joint = static_cast<u32>(j);
        if (best < 0)
        {
            best = joint;
            continue;
        }
        const auto b = static_cast<u32>(best);
        if (geometry.descendants[joint] > geometry.descendants[b] ||
            (geometry.descendants[joint] == geometry.descendants[b] &&
             preferred(skeleton, geometry, joint, b)))
        {
            best = joint;
        }
    }
    return best;
}

// Assigns @p bones (from the end of the chain backwards) to the chain from @p start to its first joint with
// three or more children (a hand with fingers), or to its end.
void mapLimb(const Geometry& geometry, Assigner& assign, u32 start, std::span<const std::string> bones,
             bool stopAtBranch)
{
    std::vector<u32> chain;
    for (i64 j = start; j >= 0; j = mainChild(geometry, static_cast<u32>(j)))
    {
        chain.push_back(static_cast<u32>(j));
        if (stopAtBranch && geometry.children[static_cast<usize>(j)].size() >= 3)
        {
            break;
        }
    }
    if (!stopAtBranch)
    {
        for (usize i = 0; i < bones.size() && i < chain.size(); ++i)
        {
            assign.assignIfFree(bones[i], chain[i]);
        }
        return;
    }
    // Arms: hand last, then lower arm, upper arm and shoulder backwards.
    if (chain.size() < 3)
    {
        return;
    }
    for (usize i = 0; i < bones.size() && i < chain.size(); ++i)
    {
        assign.assignIfFree(bones[i], chain[chain.size() - 1 - i]);
    }
}

// Bones by topology for what names did not find.
void mapByTopology(const Skeleton& skeleton, const Geometry& geometry, RigMap& rig, RigMapReport& report)
{
    Assigner assign(skeleton, geometry, rig, report);
    i64 hips = rig.find("hips");
    if (hips < 0)
    {
        hips = findHips(skeleton, geometry);
        assign.assignIfFree("hips", hips);
        hips = rig.find("hips");
    }
    if (hips < 0)
    {
        return;
    }
    const auto h = static_cast<u32>(hips);

    // Root: the parentless ancestor of the hips, if it stands near the ground.
    i32 top = skeleton.joints[h].parent;
    while (top >= 0 && skeleton.joints[static_cast<usize>(top)].parent >= 0)
    {
        top = skeleton.joints[static_cast<usize>(top)].parent;
    }
    if (top >= 0)
    {
        f32 ground = geometry.position[0].y;
        for (const Vec3& position : geometry.position)
        {
            ground = std::min(ground, position.y);
        }
        if (geometry.position[static_cast<usize>(top)].y <= ground + geometry.epsilon)
        {
            assign.assignIfFree("root", top);
        }
    }

    // Legs: the two chains going down, left by larger mean x.
    std::vector<u32> legs;
    for (const u32 child : geometry.children[h])
    {
        if (geometry.subtreeMin[child].y < geometry.position[h].y - geometry.epsilon)
        {
            legs.push_back(child);
        }
    }
    if (legs.size() >= 2)
    {
        const auto [right, left] = std::minmax_element(legs.begin(), legs.end(), [&](u32 a, u32 b) {
            return geometry.subtreeMeanX[a] < geometry.subtreeMeanX[b];
        });
        if (*left != *right)
        {
            for (const Side side : {Side::Left, Side::Right})
            {
                const std::array<std::string, 4> bones{sidedBone(side, "UpperLeg"),
                                                       sidedBone(side, "LowerLeg"), sidedBone(side, "Foot"),
                                                       sidedBone(side, "Toes")};
                mapLimb(geometry, assign, side == Side::Left ? *left : *right, bones, false);
            }
        }
    }

    // Spine up to the joint where the arms branch off, then neck and head.
    std::vector<u32> spine;
    i64 branch = -1;
    i64 leftArm = -1;
    i64 rightArm = -1;
    for (i64 j = upChild(geometry, h, geometry.epsilon); j >= 0;
         j = upChild(geometry, static_cast<u32>(j), geometry.epsilon))
    {
        const auto joint = static_cast<u32>(j);
        const i64 up = upChild(geometry, joint, geometry.epsilon);
        i64 left = -1;
        i64 right = -1;
        const f32 x = geometry.position[joint].x;
        const f32 reach = geometry.epsilon * 2.0f;
        for (const u32 child : geometry.children[joint])
        {
            if (child == up)
            {
                continue;
            }
            if (geometry.subtreeMax[child].x > x + reach &&
                (left < 0 || geometry.subtreeMax[child].x > geometry.subtreeMax[static_cast<usize>(left)].x))
            {
                left = child;
            }
            if (geometry.subtreeMin[child].x < x - reach &&
                (right < 0 ||
                 geometry.subtreeMin[child].x < geometry.subtreeMin[static_cast<usize>(right)].x))
            {
                right = child;
            }
        }
        spine.push_back(joint);
        if (left >= 0 && right >= 0 && left != right)
        {
            branch = joint;
            leftArm = left;
            rightArm = right;
            break;
        }
    }
    if (branch < 0)
    {
        return;
    }
    for (usize i = 0; i < spine.size() && i < kSpineBones.size(); ++i)
    {
        assign.assignIfFree(kSpineBones[i], spine[i]);
    }
    // Neck and head may be only a few centimetres apart.
    const f32 margin = geometry.epsilon * 0.1f;
    const i64 neck = upChild(geometry, static_cast<u32>(branch), margin);
    if (neck >= 0)
    {
        const i64 head = upChild(geometry, static_cast<u32>(neck), margin);
        if (head >= 0)
        {
            assign.assignIfFree("neck", neck);
            assign.assignIfFree("head", head);
        }
        else
        {
            assign.assignIfFree("head", neck);
        }
    }
    for (const Side side : {Side::Left, Side::Right})
    {
        const std::array<std::string, 4> bones{sidedBone(side, "Hand"), sidedBone(side, "LowerArm"),
                                               sidedBone(side, "UpperArm"), sidedBone(side, "Shoulder")};
        mapLimb(geometry, assign, static_cast<u32>(side == Side::Left ? leftArm : rightArm), bones, true);
    }
}

RetargetTranslation defaultTranslation(const Skeleton& skeleton, const RigMap& rig, usize joint) noexcept
{
    const std::string& bone = rig.bones[joint];
    return bone == "root" || bone == "hips" || skeleton.joints[joint].parent < 0
               ? RetargetTranslation::Scaled
               : RetargetTranslation::Skeleton;
}

// @p text as a JSON pointer token (RFC 6901).
std::string pointerToken(std::string_view text)
{
    std::string token;
    token.reserve(text.size());
    for (const char c : text)
    {
        if (c == '~')
        {
            token += "~0";
        }
        else if (c == '/')
        {
            token += "~1";
        }
        else
        {
            token += c;
        }
    }
    return token;
}

i32 findJoint(const Skeleton& skeleton, std::string_view name) noexcept
{
    for (usize j = 0; j < skeleton.joints.size(); ++j)
    {
        if (skeleton.joints[j].name == name)
        {
            return static_cast<i32>(j);
        }
    }
    return -1;
}

} // namespace

i32 RigMap::find(std::string_view bone) const noexcept
{
    if (bone.empty())
    {
        return -1;
    }
    for (usize j = 0; j < bones.size(); ++j)
    {
        if (bones[j] == bone)
        {
            return static_cast<i32>(j);
        }
    }
    return -1;
}

bool RigMap::empty() const noexcept
{
    return std::all_of(bones.begin(), bones.end(), [](const std::string& bone) { return bone.empty(); });
}

bool isHumanoidBone(std::string_view bone) noexcept
{
    return std::find(kHumanoidBones.begin(), kHumanoidBones.end(), bone) != kHumanoidBones.end();
}

bool isValidBoneName(std::string_view bone) noexcept
{
    return !bone.empty() && bone.size() <= kMaxBoneNameLength &&
           std::all_of(bone.begin(), bone.end(),
                       [](char c) { return isAlnum(c) || c == '_' || c == '-' || c == '.'; });
}

RigMap generateRigMap(const Skeleton& skeleton, RigMapReport* report)
{
    RigMap rig;
    rig.bones.resize(skeleton.joints.size());
    rig.translation.resize(skeleton.joints.size());
    RigMapReport local;
    RigMapReport& out = report != nullptr ? *report : local;
    out = {};
    if (!skeleton.joints.empty())
    {
        const Geometry geometry = measure(skeleton);
        mapByName(skeleton, geometry, rig, out);
        mapByTopology(skeleton, geometry, rig, out);
    }
    for (usize j = 0; j < skeleton.joints.size(); ++j)
    {
        rig.translation.at(j) = defaultTranslation(skeleton, rig, j);
    }
    for (const std::string_view bone : kRequiredHumanoidBones)
    {
        if (rig.find(bone) < 0)
        {
            out.missing.emplace_back(bone);
        }
    }
    out.humanoid = out.missing.empty();
    std::sort(out.warnings.begin(), out.warnings.end());
    return rig;
}

Expected<void, std::string> applyRigOverrides(RigMap& rig, const Skeleton& skeleton,
                                              const SkinImportSettings& settings)
{
    const usize count = skeleton.joints.size();
    ILL_ASSERT(rig.bones.size() == count && rig.translation.size() == count,
               "applyRigOverrides needs a rig map of the skeleton");
    RigMap result;
    if (settings.generate)
    {
        result = rig;
    }
    else
    {
        result.bones.resize(count);
        result.translation.resize(count);
    }

    std::vector<i32> overridden(count, -1); // the bone override of each joint
    for (usize i = 0; i < settings.bones.size(); ++i)
    {
        const RigBoneOverride& entry = settings.bones[i];
        const std::string at = std::format("bones/{}", pointerToken(entry.joint));
        const i32 joint = findJoint(skeleton, entry.joint);
        if (joint < 0)
        {
            return unexpected(
                std::format("{}: the skin '{}' has no joint '{}'", at, skeleton.name, entry.joint));
        }
        if (!entry.bone.empty() && !isValidBoneName(entry.bone))
        {
            return unexpected(std::format(
                "{}: '{}' is not a bone name (1 to 64 letters, digits, '_', '-', '.')", at, entry.bone));
        }
        const auto j = static_cast<usize>(joint);
        if (overridden[j] >= 0)
        {
            return unexpected(std::format("{}: the joint is given a bone twice", at));
        }
        overridden.at(j) = static_cast<i32>(i);
    }
    // A bone named by an override leaves the joint generation gave it to; two overrides may not share one.
    for (usize i = 0; i < settings.bones.size(); ++i)
    {
        const RigBoneOverride& entry = settings.bones[i];
        if (entry.bone.empty())
        {
            continue;
        }
        for (usize k = 0; k < i; ++k)
        {
            if (settings.bones[k].bone == entry.bone)
            {
                return unexpected(std::format("bones/{}: the bone {} is also given to the joint '{}'",
                                              pointerToken(entry.joint), entry.bone,
                                              settings.bones[k].joint));
            }
        }
        for (std::string& bone : result.bones)
        {
            if (bone == entry.bone)
            {
                bone.clear();
            }
        }
    }
    for (usize j = 0; j < count; ++j)
    {
        if (overridden[j] >= 0)
        {
            result.bones.at(j) = settings.bones[static_cast<usize>(overridden[j])].bone;
        }
    }
    for (usize j = 0; j < count; ++j)
    {
        result.translation.at(j) = defaultTranslation(skeleton, result, j);
    }

    std::vector<bool> moded(count, false);
    for (const RigTranslationOverride& entry : settings.translation)
    {
        const std::string at = std::format("translation/{}", pointerToken(entry.joint));
        const i32 joint = findJoint(skeleton, entry.joint);
        if (joint < 0)
        {
            return unexpected(
                std::format("{}: the skin '{}' has no joint '{}'", at, skeleton.name, entry.joint));
        }
        if (entry.mode > RetargetTranslation::Animation)
        {
            return unexpected(std::format("{}: not a translation mode", at));
        }
        const auto j = static_cast<usize>(joint);
        if (moded[j])
        {
            return unexpected(std::format("{}: the joint is given a mode twice", at));
        }
        moded.at(j) = true;
        result.translation.at(j) = entry.mode;
    }
    rig = std::move(result);
    return {};
}

std::string_view retargetTranslationName(RetargetTranslation mode) noexcept
{
    switch (mode)
    {
    case RetargetTranslation::Skeleton:
        return "Skeleton";
    case RetargetTranslation::Scaled:
        return "Scaled";
    case RetargetTranslation::Animation:
        return "Animation";
    }
    return "Skeleton";
}

} // namespace illusion
