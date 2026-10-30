#include "core/EditorCoreTestSupport.hpp"

#include <illusion/editor/core/Snap.hpp>

#include <array>
#include <format>
#include <random>
#include <vector>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

// Vertex snapping (VertexSnapGrid), surfaces from depth readbacks, aligning to normals, constraining snapped
// offsets, rulers, and the viewport.snap, gizmo.measure and gizmo.clear_ruler commands.

namespace
{

constexpr UVec2 kSize{640, 360};

GizmoCamera perspectiveCamera(Vec3 pivot, f32 distance, f32 yaw, f32 pitch)
{
    ViewportCamera camera;
    camera.pivot = pivot;
    camera.distance = distance;
    camera.yaw = yaw;
    camera.pitch = pitch;
    return gizmoCamera(camera, kSize, 1.0f);
}

GizmoCamera orthographicCamera(Vec3 pivot, f32 height, f32 yaw, f32 pitch)
{
    ViewportCamera camera;
    camera.pivot = pivot;
    camera.yaw = yaw;
    camera.pitch = pitch;
    camera.projection = ViewportProjection::Orthographic;
    camera.orthographicHeight = height;
    return gizmoCamera(camera, kSize, 1.0f);
}

std::vector<Vec3> cubeCorners(f32 half)
{
    std::vector<Vec3> out;
    for (u32 i = 0; i < 8; ++i)
    {
        out.emplace_back((i & 1u) != 0 ? half : -half, (i & 2u) != 0 ? half : -half,
                         (i & 4u) != 0 ? half : -half);
    }
    return out;
}

// The vertex every grid query must find: the brute-force search over all positions.
std::optional<VertexSnapHit> bruteForce(std::span<const Vec3> positions, const VertexSnapQuery& query,
                                        const Mat4& toWorld)
{
    std::optional<VertexSnapHit> best;
    for (const Vec3 position : positions)
    {
        const Vec3 world = Vec3(toWorld * Vec4(position, 1.0f));
        const ViewportPoint point = projectToViewport(query.viewProjection, query.size, world);
        const f32 distance = glm::length(point.pixel - query.pixel);
        if (!point.inFront || !(distance <= query.tolerance))
        {
            continue;
        }
        const VertexSnapHit hit{world, distance, point.depth};
        if (!best || hit.closerThan(*best))
        {
            best = hit;
        }
    }
    return best;
}

// An optional result member that was left out (or written as null).
bool missing(const JsonValue& value)
{
    return value.isAbsent() || value.isNull();
}

Vec2 pixelOf(const GizmoCamera& camera, Vec3 point)
{
    return projectToViewport(vertexSnapQuery(camera, Vec2(0.0f), 0.0f).viewProjection, camera.size, point)
        .pixel;
}

// A depth patch of the plane through @p point with @p normal seen through @p camera (entity 7 everywhere the
// plane is in front of the camera; nothing elsewhere).
struct PlanePatch
{
    std::vector<f32> depth;
    std::vector<Entity> entities;
    DepthPatch patch;
};

PlanePatch planePatch(const GizmoCamera& camera, Vec3 point, Vec3 normal, UVec2 origin, UVec2 extent)
{
    PlanePatch out;
    const Mat4 viewProjection = vertexSnapQuery(camera, Vec2(0.0f), 0.0f).viewProjection;
    for (u32 y = 0; y < extent.y; ++y)
    {
        for (u32 x = 0; x < extent.x; ++x)
        {
            const Vec2 pixel = Vec2(origin + UVec2(x, y)) + 0.5f;
            const GizmoRay ray = gizmoRay(camera, pixel);
            const f32 facing = glm::dot(ray.direction, normal);
            const f32 along =
                std::abs(facing) > 1e-6f ? glm::dot(point - ray.origin, normal) / facing : -1.0f;
            if (along > 0.0f)
            {
                const ViewportPoint hit =
                    projectToViewport(viewProjection, camera.size, ray.origin + ray.direction * along);
                out.depth.push_back(hit.depth);
                out.entities.push_back(Entity{7, 1});
            }
            else
            {
                out.depth.push_back(0.0f);
                out.entities.push_back(Entity{});
            }
        }
    }
    out.patch.viewProjection = viewProjection;
    out.patch.size = camera.size;
    out.patch.origin = origin;
    out.patch.extent = extent;
    out.patch.depth = out.depth;
    out.patch.entities = out.entities;
    return out;
}

} // namespace

TEST_CASE("a vertex snap grid keeps each position once and finds nothing when empty")
{
    const VertexSnapGrid empty;
    CHECK(empty.vertexCount() == 0);
    CHECK(!empty
               .nearest(vertexSnapQuery(perspectiveCamera(Vec3(0.0f), 5.0f, 0.0f, 0.0f), Vec2(320.0f, 180.0f),
                                        12.0f),
                        Mat4(1.0f))
               .has_value());

    // A cube's corners repeated three times (as the faces of a mesh split them), plus a NaN.
    std::vector<Vec3> positions;
    for (u32 i = 0; i < 3; ++i)
    {
        const std::vector<Vec3> corners = cubeCorners(0.5f);
        positions.insert(positions.end(), corners.begin(), corners.end());
    }
    positions.emplace_back(std::numeric_limits<f32>::quiet_NaN(), 0.0f, 0.0f);
    const VertexSnapGrid grid(positions);
    CHECK(grid.vertexCount() == 8);
    CHECK(grid.bounds().min == Vec3(-0.5f));
    CHECK(grid.bounds().max == Vec3(0.5f));

    // One vertex, and a flat mesh (one cell across its thickness).
    const std::array one = {Vec3(1.0f, 2.0f, 3.0f)};
    CHECK(VertexSnapGrid(one).cells() == UVec3(1));
    std::vector<Vec3> plane;
    for (u32 i = 0; i < 1000; ++i)
    {
        plane.emplace_back(static_cast<f32>(i % 40), 0.0f, static_cast<f32>(i / 40));
    }
    const VertexSnapGrid flat(plane);
    CHECK(flat.cells().y == 1);
    CHECK(flat.cells().x > 1);
    CHECK(flat.cells().x <= VertexSnapGrid::kMaxCellsPerAxis);
}

TEST_CASE("a vertex snap finds the corner under the cursor within the tolerance, nearer corners first")
{
    const std::vector<Vec3> corners = cubeCorners(0.5f);
    const VertexSnapGrid grid(corners);
    for (const GizmoCamera& camera : {perspectiveCamera(Vec3(0.0f), 6.0f, 30.0f, -20.0f),
                                      orthographicCamera(Vec3(0.0f), 4.0f, 30.0f, -20.0f)})
    {
        const Vec3 corner(0.5f, 0.5f, 0.5f);
        const Vec2 pixel = pixelOf(camera, corner) + Vec2(3.0f, -4.0f);
        const std::optional<VertexSnapHit> hit =
            grid.nearest(vertexSnapQuery(camera, pixel, 6.0f), Mat4(1.0f));
        REQUIRE(hit.has_value());
        CHECK(glm::length(hit->position - corner) < 1e-5f);
        CHECK(hit->distance == doctest::Approx(5.0f).epsilon(1e-3));
        // Just outside the tolerance there is nothing.
        CHECK(!grid.nearest(vertexSnapQuery(camera, pixel, 4.9f), Mat4(1.0f)).has_value());
    }

    // Looking straight down the x axis, the corners at x = 0.5 cover those at x = -0.5: the nearer wins.
    const GizmoCamera side = perspectiveCamera(Vec3(0.0f), 6.0f, 90.0f, 0.0f);
    const Vec2 pixel = pixelOf(side, Vec3(0.5f, 0.5f, 0.5f));
    const std::optional<VertexSnapHit> front = grid.nearest(vertexSnapQuery(side, pixel, 40.0f), Mat4(1.0f));
    REQUIRE(front.has_value());
    CHECK(glm::length(front->position - Vec3(0.5f, 0.5f, 0.5f)) < 1e-5f);

    // Vertices behind the camera never snap.
    const GizmoCamera inside = perspectiveCamera(Vec3(0.0f, 0.0f, -10.0f), 0.01f, 0.0f, 0.0f);
    CHECK(!grid.nearest(vertexSnapQuery(inside, Vec2(320.0f, 180.0f), 64.0f), Mat4(1.0f)).has_value());
}

TEST_CASE("a vertex snap places the mesh by its matrix, with rotation and non-uniform scale")
{
    const std::vector<Vec3> corners = cubeCorners(0.5f);
    const VertexSnapGrid grid(corners);
    const Mat4 toWorld = glm::translate(Mat4(1.0f), Vec3(10.0f, 2.0f, -3.0f)) *
                         glm::mat4_cast(glm::angleAxis(0.7f, glm::normalize(Vec3(1.0f, 2.0f, 0.5f)))) *
                         glm::scale(Mat4(1.0f), Vec3(4.0f, 0.25f, 1.5f));
    const GizmoCamera camera = perspectiveCamera(Vec3(10.0f, 2.0f, -3.0f), 12.0f, -40.0f, -35.0f);
    for (const Vec3 corner : corners)
    {
        const Vec3 world = Vec3(toWorld * Vec4(corner, 1.0f));
        const VertexSnapQuery query = vertexSnapQuery(camera, pixelOf(camera, world) + Vec2(1.0f), 3.0f);
        const std::optional<VertexSnapHit> hit = grid.nearest(query, toWorld);
        const std::optional<VertexSnapHit> expected = bruteForce(corners, query, toWorld);
        REQUIRE(hit.has_value());
        REQUIRE(expected.has_value());
        CHECK(glm::length(hit->position - expected->position) < 1e-4f);
    }
}

TEST_CASE("a vertex snap grid finds what a brute-force search finds at many pixels, poses and tolerances")
{
    std::mt19937 random(20261030);
    std::uniform_real_distribution<f32> unit(-1.0f, 1.0f);
    std::vector<Vec3> positions;
    // A lumpy cloud: a dense blob and sparse outliers, so that the cells are uneven.
    for (u32 i = 0; i < 4000; ++i)
    {
        const f32 spread = i % 10 == 0 ? 8.0f : 1.0f;
        positions.emplace_back(unit(random) * spread, unit(random) * spread * 0.5f, unit(random) * spread);
    }
    const VertexSnapGrid grid(positions);
    CHECK(grid.vertexCount() == positions.size());
    const std::array toWorlds = {Mat4(1.0f),
                                 glm::translate(Mat4(1.0f), Vec3(10000.0f, 0.0f, -5000.0f)) *
                                     glm::mat4_cast(glm::angleAxis(1.1f, Vec3(0.0f, 1.0f, 0.0f))) *
                                     glm::scale(Mat4(1.0f), Vec3(0.5f, 2.0f, 1.0f))};
    u32 found = 0;
    for (const Mat4& toWorld : toWorlds)
    {
        const Vec3 center = Vec3(toWorld[3]);
        const std::array cameras = {perspectiveCamera(center, 20.0f, 20.0f, -30.0f),
                                    perspectiveCamera(center, 3.0f, 200.0f, -5.0f),
                                    orthographicCamera(center, 18.0f, -60.0f, -45.0f)};
        for (const GizmoCamera& camera : cameras)
        {
            for (u32 i = 0; i < 40; ++i)
            {
                const Vec2 pixel((unit(random) * 0.5f + 0.5f) * static_cast<f32>(kSize.x),
                                 (unit(random) * 0.5f + 0.5f) * static_cast<f32>(kSize.y));
                const f32 tolerance = i % 2 == 0 ? 2.0f : 24.0f;
                const VertexSnapQuery query = vertexSnapQuery(camera, pixel, tolerance);
                const std::optional<VertexSnapHit> hit = grid.nearest(query, toWorld);
                const std::optional<VertexSnapHit> expected = bruteForce(positions, query, toWorld);
                REQUIRE(hit.has_value() == expected.has_value());
                if (hit)
                {
                    ++found;
                    CHECK(hit->distance == doctest::Approx(expected->distance).epsilon(1e-4));
                    CHECK(glm::length(hit->position - expected->position) <=
                          1e-3f * glm::length(expected->position) + 1e-4f);
                }
            }
        }
    }
    CHECK(found > 60);
}

TEST_CASE("a surface from a depth patch lands on the plane with its normal facing the camera")
{
    const Vec3 point(1.0f, 2.0f, -1.0f);
    const Vec3 normal = glm::normalize(Vec3(0.3f, 1.0f, -0.2f));
    for (const GizmoCamera& camera :
         {perspectiveCamera(point, 8.0f, 25.0f, -40.0f), orthographicCamera(point, 6.0f, 25.0f, -40.0f)})
    {
        const UVec2 origin(316, 176);
        const PlanePatch plane = planePatch(camera, point, normal, origin, UVec2(8));
        const UVec2 texel = origin + UVec2(4);
        const std::optional<SurfaceHit> hit = surfaceFromDepth(plane.patch, texel);
        REQUIRE(hit.has_value());
        CHECK(std::abs(glm::dot(hit->position - point, normal)) < 1e-3f);
        CHECK(glm::dot(hit->normal, normal) > 0.9999f);
        CHECK(hit->entity == Entity{7, 1});
        // Outside the region, or a region too small for neighbours, gives nothing.
        CHECK(!surfaceFromDepth(plane.patch, origin + UVec2(8, 0)).has_value());
        DepthPatch single = plane.patch;
        single.origin = texel;
        single.extent = UVec2(1);
        single.depth = std::span(plane.depth).subspan(4 * 8 + 4, 1);
        single.entities = std::span(plane.entities).subspan(4 * 8 + 4, 1);
        CHECK(!surfaceFromDepth(single, texel).has_value());
    }

    // Seen from below, the normal still faces the camera.
    const GizmoCamera below = perspectiveCamera(point, 8.0f, 25.0f, 40.0f);
    const PlanePatch under = planePatch(below, point, normal, UVec2(316, 176), UVec2(8));
    const std::optional<SurfaceHit> hit = surfaceFromDepth(under.patch, UVec2(320, 180));
    REQUIRE(hit.has_value());
    CHECK(glm::dot(hit->normal, normal) < -0.9999f);
}

TEST_CASE("a surface from a depth patch ignores the background and neighbours of other entities")
{
    const GizmoCamera camera = perspectiveCamera(Vec3(0.0f), 8.0f, 0.0f, -60.0f);
    PlanePatch plane = planePatch(camera, Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f), UVec2(316, 176), UVec2(8));
    const usize center = 4 * 8 + 4;
    // The neighbour to the right is another entity at another depth, the one to the left is the plane: the
    // normal comes from the left one.
    plane.entities[center + 1] = Entity{9, 1};
    plane.depth[center + 1] *= 0.5f;
    std::optional<SurfaceHit> hit = surfaceFromDepth(plane.patch, UVec2(320, 180));
    REQUIRE(hit.has_value());
    CHECK(hit->normal.y > 0.9999f);
    // Without a neighbour of the same entity along x there is no normal.
    plane.entities[center - 1] = Entity{9, 1};
    CHECK(!surfaceFromDepth(plane.patch, UVec2(320, 180)).has_value());
    // The background (depth 0) is no surface, and a short patch is refused.
    plane.depth[center] = 0.0f;
    CHECK(!surfaceFromDepth(plane.patch, UVec2(320, 180)).has_value());
    DepthPatch shortPatch = plane.patch;
    shortPatch.depth = std::span(plane.depth).first(10);
    CHECK(!surfaceFromDepth(shortPatch, UVec2(317, 176)).has_value());
}

TEST_CASE("aligning to a normal turns the +y axis onto it by the shortest arc")
{
    const Quat start = glm::angleAxis(0.4f, glm::normalize(Vec3(1.0f, 0.0f, 1.0f)));
    for (const Vec3 normal : {Vec3(0.0f, 1.0f, 0.0f), glm::normalize(Vec3(1.0f, 1.0f, 0.0f)),
                              Vec3(0.0f, 0.0f, -1.0f), -(start * Vec3(0.0f, 1.0f, 0.0f))})
    {
        const Quat aligned = alignToNormal(start, normal);
        CHECK(glm::length(aligned * Vec3(0.0f, 1.0f, 0.0f) - normal) < 1e-5f);
        CHECK(std::abs(glm::length(aligned) - 1.0f) < 1e-5f);
    }
    // Already aligned: unchanged. A zero normal changes nothing either.
    const Quat same = alignToNormal(start, start * Vec3(0.0f, 1.0f, 0.0f));
    CHECK(std::abs(std::abs(glm::dot(same, start)) - 1.0f) < 1e-6f);
    const Quat zero = alignToNormal(start, Vec3(0.0f));
    CHECK(zero == start);
}

TEST_CASE("a snapped offset follows the dragged axis or plane of the gizmo")
{
    CHECK(gizmoHandleAxes(GizmoHandle::TranslateX) == 0b001u);
    CHECK(gizmoHandleAxes(GizmoHandle::TranslateXZ) == 0b101u);
    CHECK(gizmoHandleAxes(GizmoHandle::TranslateScreen) == 0b111u);
    CHECK(gizmoHandleAxes(GizmoHandle::RotateX) == 0);
    GizmoPose pose;
    const Vec3 offset(1.0f, 2.0f, 3.0f);
    CHECK(constrainOffset(pose, 0, offset) == offset);
    CHECK(constrainOffset(pose, 0b111u, offset) == offset);
    CHECK(constrainOffset(pose, 0b010u, offset) == Vec3(0.0f, 2.0f, 0.0f));
    CHECK(constrainOffset(pose, 0b101u, offset) == Vec3(1.0f, 0.0f, 3.0f));
    // Local axes: turned a quarter about y, the x axis is the world's -z.
    pose.orientation = glm::angleAxis(glm::radians(90.0f), Vec3(0.0f, 1.0f, 0.0f));
    CHECK(glm::length(constrainOffset(pose, 0b001u, offset) - Vec3(0.0f, 0.0f, 3.0f)) < 1e-5f);
}

TEST_CASE("a ruler measures the distance, its ground-plane length and the elevation")
{
    const Measurement flat = measure(Vec3(1.0f, 0.0f, 0.0f), Vec3(4.0f, 0.0f, 4.0f));
    CHECK(flat.distance == doctest::Approx(5.0f));
    CHECK(flat.horizontal == doctest::Approx(5.0f));
    CHECK(flat.elevation == doctest::Approx(0.0f));
    CHECK(flat.delta == Vec3(3.0f, 0.0f, 4.0f));
    const Measurement up = measure(Vec3(0.0f), Vec3(0.0f, 2.0f, 0.0f));
    CHECK(up.elevation == doctest::Approx(90.0f));
    CHECK(up.horizontal == 0.0f);
    const Measurement down = measure(Vec3(0.0f), Vec3(1.0f, -1.0f, 0.0f));
    CHECK(down.elevation == doctest::Approx(-45.0f));
    CHECK(measure(Vec3(2.0f), Vec3(2.0f)).elevation == 0.0f);
}

TEST_CASE("gizmo.measure measures points or entities, shows the ruler, and gizmo.clear_ruler removes it")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const JsonDocument points = ok(editor, "gizmo.measure", R"({"from":[0,0,0],"to":[3,4,0]})");
    CHECK(points.root().find("distance").asF64().value_or(0.0) == doctest::Approx(5.0));
    CHECK(points.root().find("elevation").asF64().value_or(0.0) == doctest::Approx(53.130102));
    REQUIRE(editor.gizmo().ruler.has_value());
    CHECK(editor.gizmo().ruler->to == Vec3(3.0f, 4.0f, 0.0f));
    CHECK(ok(editor, "gizmo.get").root().find("ruler").find("to")[1].asF64().value_or(0.0) == 4.0);

    ok(editor, "transform.set", R"({"entities":["/Game/Board"],"position":[1,0,0]})");
    ok(editor, "transform.set", R"({"entities":["/Game/Player"],"position":[1,0,2]})");
    const JsonDocument entities =
        ok(editor, "gizmo.measure", R"({"entities":["/Game/Board","/Game/Player"],"show":false})");
    CHECK(entities.root().find("distance").asF64().value_or(0.0) == doctest::Approx(2.0));
    CHECK(editor.gizmo().ruler->to == Vec3(3.0f, 4.0f, 0.0f)); // show false keeps the ruler

    CHECK(fails(editor, "gizmo.measure", R"({"from":[0,0,0]})").kind == ErrorKind::ValidationFailed);
    CHECK(
        fails(editor, "gizmo.measure", R"({"from":[0,0,0],"to":[1,1,1],"entities":["/Game/Board","/Game"]})")
            .kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "gizmo.measure", R"({"entities":["/Game/Board"]})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "gizmo.measure", R"({"entities":["/Game/Board","/Nope"]})").kind ==
          ErrorKind::NotFound);
    CHECK(fails(editor, "gizmo.measure", R"({"from":[0,0,0],"to":[1e9,0,0]})").kind ==
          ErrorKind::ValidationFailed);

    ok(editor, "gizmo.clear_ruler");
    CHECK(!editor.gizmo().ruler.has_value());
    CHECK(missing(ok(editor, "gizmo.get").root().find("ruler")));
}

TEST_CASE("gizmo.set_tool chooses the Measure tool and gizmo.set_snap turns aligning to normals on")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "gizmo.set_tool", R"({"tool":"measure"})");
    CHECK(editor.gizmo().tool == GizmoTool::Measure);
    CHECK(fails(editor, "gizmo.set_tool", R"({"tool":6})").kind == ErrorKind::ValidationFailed);
    ok(editor, "gizmo.set_snap", R"({"alignToNormal":true})");
    CHECK(editor.gizmo().snap.alignToNormal);
    CHECK(ok(editor, "gizmo.get").root().find("snap").find("alignToNormal").asBool().value_or(false));

    // The Measure tool has no gizmo to draw or grab.
    const GizmoCamera camera = perspectiveCamera(Vec3(0.0f), 5.0f, 0.0f, 0.0f);
    const GizmoShape shape = gizmoShape(GizmoTool::Measure, camera, GizmoPose{});
    CHECK(!shape.visible);
    CHECK(gizmoHitTest(shape, Vec2(320.0f, 180.0f)) == GizmoHandle::None);
}

TEST_CASE("viewport.snap asks the viewport for a vertex or a surface and reports the point")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const Entity board = *editor.entities().resolve("/Game/Board", editor.selection());

    // Nothing under the pixel.
    const JsonDocument miss = ok(editor, "viewport.snap", R"({"x":10,"y":20})");
    CHECK(!miss.root().find("hit").asBool().value_or(true));
    CHECK(fixture.viewports.snapRequest.pixel == Vec2(10.0f, 20.0f));
    CHECK(fixture.viewports.snapRequest.target == ViewportSnapTarget::Any);
    CHECK(fixture.viewports.snapRequest.tolerance == kVertexSnapTolerance);
    CHECK(fixture.viewports.snapExcluded.empty());

    // A surface: its normal and the rotation that turns +y onto it.
    const Vec3 normal = glm::normalize(Vec3(1.0f, 1.0f, 0.0f));
    fixture.viewports.snapped =
        ViewportSnap{ViewportSnapTarget::Surface, Vec3(1.0f, 2.0f, 3.0f), normal, board};
    const JsonDocument surface =
        ok(editor, "viewport.snap",
           R"({"x":640,"y":360,"to":"surface","exclude":["/Game/Board"],"tolerance":5})");
    CHECK(fixture.viewports.snapRequest.target == ViewportSnapTarget::Surface);
    CHECK(fixture.viewports.snapRequest.tolerance == 5.0f);
    CHECK(fixture.viewports.snapExcluded == std::vector<Entity>{board});
    const JsonValue root = surface.root();
    CHECK(root.find("hit").asBool().value_or(false));
    CHECK(root.find("kind").asString().value_or("") == "surface");
    CHECK(root.find("position")[2].asF64().value_or(0.0) == doctest::Approx(3.0));
    CHECK(root.find("normal")[0].asF64().value_or(0.0) == doctest::Approx(normal.x));
    CHECK(root.find("entity").find("path").asString().value_or("") == "/Game/Board");
    const JsonValue rotation = root.find("rotation");
    const Quat turned(static_cast<f32>(rotation[3].asF64().value_or(0.0)),
                      static_cast<f32>(rotation[0].asF64().value_or(0.0)),
                      static_cast<f32>(rotation[1].asF64().value_or(0.0)),
                      static_cast<f32>(rotation[2].asF64().value_or(0.0)));
    CHECK(glm::length(turned * Vec3(0.0f, 1.0f, 0.0f) - normal) < 1e-5f);

    // A vertex has no normal or rotation.
    fixture.viewports.snapped = ViewportSnap{ViewportSnapTarget::Vertex, Vec3(0.5f), Vec3(0.0f), board};
    const JsonDocument vertex = ok(editor, "viewport.snap", R"({"x":640,"y":360,"to":"vertex"})");
    CHECK(vertex.root().find("kind").asString().value_or("") == "vertex");
    CHECK(missing(vertex.root().find("normal")));
    CHECK(missing(vertex.root().find("rotation")));

    CHECK(fails(editor, "viewport.snap", R"({"x":640,"y":360,"to":3})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.snap", R"({"x":640,"y":360,"tolerance":0.5})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.snap", R"({"x":1280,"y":360})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.snap", R"({"x":1,"y":1,"exclude":["/Nope"]})").kind == ErrorKind::NotFound);
    CHECK(fails(editor, "viewport.snap", R"({"viewport":"nope","x":1,"y":1})").kind == ErrorKind::NotFound);
    editor.setViewports(nullptr);
    CHECK(fails(editor, "viewport.snap", R"({"x":1,"y":1})").kind == ErrorKind::Unsupported);
}
