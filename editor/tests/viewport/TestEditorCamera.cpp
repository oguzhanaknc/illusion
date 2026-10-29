#include <illusion/editor/viewport/EditorCamera.hpp>
#include <illusion/editor/viewport/PickCandidates.hpp>
#include <illusion/editor/viewport/ResizePolicy.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <vector>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::viewport;

namespace
{

constexpr UVec2 kSize{800, 600};

bool nearlyEqual(Vec3 a, Vec3 b, f32 tolerance = 1e-3f)
{
    return glm::length(a - b) <= tolerance;
}

ViewportCamera start()
{
    ViewportCamera camera;
    camera.pivot = Vec3(1.0f, 0.0f, -2.0f);
    camera.distance = 10.0f;
    camera.yaw = 30.0f;
    camera.pitch = -20.0f;
    return camera;
}

// A frame of input with the cursor over the image.
ViewportInput hovering()
{
    ViewportInput input;
    input.hovered = true;
    input.cursor = Vec2(400.0f, 300.0f);
    input.deltaSeconds = 1.0f / 60.0f;
    return input;
}

// Presses @p button (with Alt if asked), then drags by @p delta for one frame.
void drag(EditorCamera& camera, u32 button, Vec2 delta, bool alt = false)
{
    ViewportInput input = hovering();
    input.alt = alt;
    input.buttons[button] = true;
    camera.update(input, kSize);
    input.cursorDelta = delta;
    camera.update(input, kSize);
}

void release(EditorCamera& camera)
{
    camera.update(hovering(), kSize);
}

} // namespace

TEST_CASE("orbiting turns around the pivot and keeps the distance")
{
    EditorCamera camera(start());
    drag(camera, 0, Vec2(50.0f, -20.0f), true);
    CHECK(camera.mode() == NavigationMode::Orbit);
    CHECK(nearlyEqual(camera.camera().pivot, start().pivot));
    CHECK(camera.camera().distance == doctest::Approx(10.0));
    CHECK(glm::length(viewportEye(camera.camera()) - camera.camera().pivot) == doctest::Approx(10.0));
    CHECK(camera.camera().yaw == doctest::Approx(30.0 - 50.0 * 0.3));
    CHECK(camera.camera().pitch == doctest::Approx(-20.0 + 20.0 * 0.3));
    release(camera);
    CHECK(camera.mode() == NavigationMode::None);
    // Left drags without Alt are clicks, not navigation.
    drag(camera, 0, Vec2(50.0f, 0.0f));
    CHECK(camera.mode() == NavigationMode::None);
}

TEST_CASE("orbiting clamps the pitch at straight up and down")
{
    EditorCamera camera(start());
    drag(camera, 0, Vec2(0.0f, -10000.0f), true);
    CHECK(camera.camera().pitch == doctest::Approx(90.0));
    drag(camera, 0, Vec2(0.0f, 10000.0f), true);
    CHECK(camera.camera().pitch == doctest::Approx(-90.0));
}

TEST_CASE("panning moves the pivot so that the point under the cursor follows it")
{
    for (const ViewportProjection projection :
         {ViewportProjection::Perspective, ViewportProjection::Orthographic})
    {
        ViewportCamera initial = start();
        initial.projection = projection;
        EditorCamera camera(initial);
        const ViewportPoint before = projectToViewport(camera.camera(), kSize, initial.pivot);
        drag(camera, 2, Vec2(40.0f, 25.0f));
        CHECK(camera.mode() == NavigationMode::Pan);
        const ViewportPoint after = projectToViewport(camera.camera(), kSize, initial.pivot);
        CHECK(after.pixel.x - before.pixel.x == doctest::Approx(40.0).epsilon(0.01));
        CHECK(after.pixel.y - before.pixel.y == doctest::Approx(25.0).epsilon(0.01));
        CHECK(camera.camera().distance == doctest::Approx(10.0));
    }
}

TEST_CASE("the wheel dollies towards the pivot and Alt + right drag dollies too")
{
    EditorCamera camera(start());
    ViewportInput input = hovering();
    input.wheel = 2.0f;
    CHECK(camera.update(input, kSize));
    CHECK(camera.camera().distance == doctest::Approx(10.0 * 0.85 * 0.85));
    CHECK(nearlyEqual(camera.camera().pivot, start().pivot));
    // Not over the image: nothing happens.
    input.hovered = false;
    CHECK_FALSE(camera.update(input, kSize));
    CHECK(camera.camera().distance == doctest::Approx(10.0 * 0.85 * 0.85));

    EditorCamera dolly(start());
    drag(dolly, 1, Vec2(100.0f, 0.0f), true);
    CHECK(dolly.mode() == NavigationMode::Dolly);
    CHECK(dolly.camera().distance == doctest::Approx(10.0 * std::exp(-0.5)));
    CHECK(dolly.camera().orthographicHeight ==
          doctest::Approx(static_cast<f64>(ViewportCamera{}.orthographicHeight) * std::exp(-0.5)));
}

TEST_CASE("right drag looks around the eye and the fly keys move it")
{
    EditorCamera camera(start());
    const Vec3 eye = viewportEye(start());
    drag(camera, 1, Vec2(30.0f, 10.0f));
    CHECK(camera.mode() == NavigationMode::Fly);
    CHECK(nearlyEqual(viewportEye(camera.camera()), eye));
    CHECK(camera.camera().distance == doctest::Approx(10.0));

    ViewportInput input = hovering();
    input.buttons[1] = true;
    input.move = Vec3(0.0f, 0.0f, 1.0f);
    input.deltaSeconds = 0.1f;
    const Vec3 forward = viewportForward(camera.camera());
    const Vec3 before = viewportEye(camera.camera());
    camera.update(input, kSize);
    CHECK(nearlyEqual(viewportEye(camera.camera()), before + forward * camera.flySpeed() * 0.1f));
    // Shift flies faster; the wheel changes the speed while flying instead of dollying.
    input.shift = true;
    const Vec3 slow = viewportEye(camera.camera());
    camera.update(input, kSize);
    CHECK(glm::length(viewportEye(camera.camera()) - slow) ==
          doctest::Approx(static_cast<f64>(camera.flySpeed()) * 0.4));
    input.shift = false;
    input.move = Vec3(0.0f);
    input.wheel = 1.0f;
    const f32 speed = camera.flySpeed();
    camera.update(input, kSize);
    CHECK(camera.flySpeed() == doctest::Approx(static_cast<f64>(speed) * 1.25));
    CHECK(camera.camera().distance == doctest::Approx(10.0));
}

TEST_CASE("a drag starts only over the image and continues outside it")
{
    EditorCamera camera(start());
    ViewportInput input = hovering();
    input.hovered = false;
    input.buttons[2] = true;
    CHECK_FALSE(camera.update(input, kSize));
    CHECK(camera.mode() == NavigationMode::None);
    // Holding the button while moving over the image does not start a drag either.
    input.hovered = true;
    CHECK_FALSE(camera.update(input, kSize));

    EditorCamera pan(start());
    drag(pan, 2, Vec2(5.0f, 0.0f));
    input = hovering();
    input.hovered = false;
    input.buttons[2] = true;
    input.cursorDelta = Vec2(10.0f, 0.0f);
    CHECK(pan.update(input, kSize));
    CHECK(pan.mode() == NavigationMode::Pan);
}

TEST_CASE("animated moves ease to the target and navigation takes over where they are")
{
    EditorCamera camera(start());
    ViewportCamera target = start();
    target.pivot = Vec3(11.0f, 0.0f, -2.0f);
    target.distance = 40.0f;
    camera.set(target, true);
    CHECK(camera.animating());
    CHECK(nearlyEqual(camera.camera().pivot, start().pivot));
    ViewportInput idle = hovering();
    idle.deltaSeconds = 0.15f;
    CHECK_FALSE(camera.update(idle, kSize));
    CHECK(camera.camera().pivot.x == doctest::Approx(6.0));
    CHECK(camera.camera().distance == doctest::Approx(20.0));
    camera.update(idle, kSize);
    CHECK_FALSE(camera.animating());
    CHECK(nearlyEqual(camera.camera().pivot, target.pivot));

    camera.set(start(), true);
    idle.deltaSeconds = 0.1f;
    camera.update(idle, kSize);
    const ViewportCamera midway = camera.camera();
    ViewportInput wheel = hovering();
    wheel.wheel = 1.0f;
    camera.update(wheel, kSize);
    CHECK_FALSE(camera.animating());
    CHECK(nearlyEqual(camera.camera().pivot, midway.pivot));
    CHECK(nearlyEqual(camera.target().pivot, midway.pivot));

    camera.set(target, false);
    CHECK_FALSE(camera.animating());
    CHECK(nearlyEqual(camera.camera().pivot, target.pivot));
}

TEST_CASE("interpolated cameras turn the shorter way round")
{
    ViewportCamera from;
    from.yaw = 170.0f;
    ViewportCamera to;
    to.yaw = -170.0f;
    CHECK(interpolateCamera(from, to, 0.5f).yaw == doctest::Approx(180.0));
    CHECK(interpolateCamera(from, to, 0.0f).yaw == doctest::Approx(170.0));
    CHECK(interpolateCamera(from, to, 1.0f).yaw == doctest::Approx(190.0));
}

TEST_CASE("framing puts the bounds in view with a margin and keeps the direction")
{
    const Aabb box{Vec3(-1.0f, 0.0f, -3.0f), Vec3(5.0f, 2.0f, 1.0f)};
    for (const f32 aspect : {0.5f, 1.0f, 16.0f / 9.0f})
    {
        for (const ViewportProjection projection :
             {ViewportProjection::Perspective, ViewportProjection::Orthographic})
        {
            ViewportCamera camera = start();
            camera.projection = projection;
            const ViewportCamera framed = frameBounds(camera, box, aspect);
            CHECK(nearlyEqual(framed.pivot, box.center()));
            CHECK(framed.yaw == doctest::Approx(camera.yaw));
            CHECK(framed.pitch == doctest::Approx(camera.pitch));
            const UVec2 size(static_cast<u32>(600.0f * aspect), 600u);
            for (u32 corner = 0; corner < 8; ++corner)
            {
                const Vec3 point((corner & 1) ? box.max.x : box.min.x, (corner & 2) ? box.max.y : box.min.y,
                                 (corner & 4) ? box.max.z : box.min.z);
                CHECK(projectToViewport(framed, size, point).inside);
            }
        }
    }
    // An empty box frames the origin.
    CHECK(nearlyEqual(frameBounds(start(), Aabb{}, 1.0f).pivot, Vec3(0.0f)));
    // A point keeps a sensible distance.
    const ViewportCamera point = frameBounds(start(), Aabb{Vec3(2.0f), Vec3(2.0f)}, 1.0f);
    CHECK(point.distance > 0.2f);
}

TEST_CASE("cameras are clamped into range and lose non-finite values")
{
    ViewportCamera camera;
    camera.distance = -5.0f;
    camera.pitch = 200.0f;
    camera.yaw = 725.0f;
    camera.fieldOfView = std::nanf("");
    camera.pivot = Vec3(std::numeric_limits<f32>::infinity());
    const ViewportCamera clamped = clampCamera(camera);
    CHECK(clamped.distance == doctest::Approx(kViewportMinExtent));
    CHECK(clamped.pitch == doctest::Approx(90.0));
    CHECK(clamped.yaw == doctest::Approx(5.0));
    CHECK(clamped.fieldOfView == doctest::Approx(ViewportCamera{}.fieldOfView));
    CHECK(clamped.pivot == Vec3(0.0f));
}

TEST_CASE("resizes apply at once the first time, then after 150 ms or on mouse release")
{
    ResizePolicy policy;
    CHECK(policy.update(UVec2(0, 0), 0.0, false) == UVec2(0, 0));
    CHECK(policy.update(UVec2(640, 360), 0.0, false) == UVec2(640, 360));
    CHECK(policy.update(UVec2(700, 360), 1.0, true) == UVec2(640, 360));
    CHECK(policy.update(UVec2(720, 360), 1.1, true) == UVec2(640, 360));
    CHECK(policy.update(UVec2(720, 360), 1.2, true) == UVec2(640, 360));
    CHECK(policy.update(UVec2(720, 360), 1.26, true) == UVec2(720, 360));
    CHECK(policy.update(UVec2(800, 360), 2.0, true) == UVec2(720, 360));
    CHECK(policy.update(UVec2(800, 360), 2.01, false) == UVec2(800, 360));
    CHECK(policy.update(UVec2(0, 10), 3.0, false) == UVec2(800, 360));
    CHECK(policy.current() == UVec2(800, 360));
}

TEST_CASE("pick candidates are ordered by distance from the cursor, each once")
{
    constexpr u32 kBackground = ~0u;
    // 5 x 5 texels, entity 7 at the centre, 3 next to it, 9 in a corner, 3 again further out.
    std::vector<EntityIdTexel> texels(25, EntityIdTexel{kBackground, 0});
    texels[12] = EntityIdTexel{7, 1};
    texels[13] = EntityIdTexel{3, 2};
    texels[0] = EntityIdTexel{9, 0};
    texels[24] = EntityIdTexel{3, 2};
    std::vector<Entity> out;
    appendVisibleCandidates(texels, UVec2(5, 5), UVec2(2, 2), out);
    REQUIRE(out.size() == 3);
    CHECK(out[0] == Entity{7, 1});
    CHECK(out[1] == Entity{3, 2});
    CHECK(out[2] == Entity{9, 0});

    // A window clipped at the image edge with the cursor off centre.
    std::vector<EntityIdTexel> edge(6, EntityIdTexel{kBackground, 0});
    edge[5] = EntityIdTexel{4, 0};
    edge[0] = EntityIdTexel{5, 0};
    std::vector<Entity> clipped{Entity{5, 0}};
    appendVisibleCandidates(edge, UVec2(3, 2), UVec2(0, 0), clipped);
    REQUIRE(clipped.size() == 2);
    CHECK(clipped[1] == Entity{4, 0});

    std::vector<Entity> none;
    appendVisibleCandidates(std::vector<EntityIdTexel>(9, EntityIdTexel{kBackground, 0}), UVec2(3, 3),
                            UVec2(1, 1), none);
    CHECK(none.empty());
}

TEST_CASE("cycling picks visit every candidate and wrap around")
{
    const std::vector<Entity> candidates{Entity{1, 0}, Entity{2, 0}, Entity{3, 0}};
    CHECK(chooseCandidate(candidates, Entity{}, true) == 0);
    CHECK(chooseCandidate(candidates, Entity{1, 0}, true) == 1);
    CHECK(chooseCandidate(candidates, Entity{3, 0}, true) == 0);
    CHECK(chooseCandidate(candidates, Entity{2, 0}, false) == 0);
    CHECK(chooseCandidate(candidates, Entity{8, 0}, true) == 0);
}

TEST_CASE("navigation presets map the mouse buttons to the drags of Unity, Unreal and Blender")
{
    ViewportInput plain = hovering();
    ViewportInput alt = plain;
    alt.alt = true;
    ViewportInput shift = plain;
    shift.shift = true;
    ViewportInput ctrl = plain;
    ctrl.ctrl = true;
    for (const NavigationPreset preset : {NavigationPreset::Illusion, NavigationPreset::Unity})
    {
        CHECK(navigationMode(preset, 0, plain) == NavigationMode::None);
        CHECK(navigationMode(preset, 0, alt) == NavigationMode::Orbit);
        CHECK(navigationMode(preset, 1, plain) == NavigationMode::Fly);
        CHECK(navigationMode(preset, 1, alt) == NavigationMode::Dolly);
        CHECK(navigationMode(preset, 2, plain) == NavigationMode::Pan);
    }
    CHECK(navigationMode(NavigationPreset::Unreal, 0, plain) == NavigationMode::Walk);
    CHECK(navigationMode(NavigationPreset::Unreal, 0, alt) == NavigationMode::Orbit);
    CHECK(navigationMode(NavigationPreset::Unreal, 1, plain) == NavigationMode::Fly);
    CHECK(navigationMode(NavigationPreset::Unreal, 2, plain) == NavigationMode::Pan);
    CHECK(navigationMode(NavigationPreset::Blender, 2, plain) == NavigationMode::Orbit);
    CHECK(navigationMode(NavigationPreset::Blender, 2, shift) == NavigationMode::Pan);
    CHECK(navigationMode(NavigationPreset::Blender, 2, ctrl) == NavigationMode::Dolly);
    CHECK(navigationMode(NavigationPreset::Blender, 0, plain) == NavigationMode::None);
    CHECK(navigationMode(NavigationPreset::Blender, 0, alt) == NavigationMode::Orbit);
    CHECK(navigationMode(NavigationPreset::Blender, 1, plain) == NavigationMode::None);
    // A drag that started on the view cube orbits with every preset.
    ViewportInput cube = plain;
    cube.orbit = true;
    CHECK(navigationMode(NavigationPreset::Blender, 0, cube) == NavigationMode::Orbit);
    CHECK(navigationMode(NavigationPreset::Unreal, 0, cube) == NavigationMode::Orbit);

    CHECK(navigationPresetFromName("unreal") == NavigationPreset::Unreal);
    CHECK(navigationPresetFromName("blender") == NavigationPreset::Blender);
    CHECK(navigationPresetFromName("unity") == NavigationPreset::Unity);
    CHECK(navigationPresetFromName("keymap") == NavigationPreset::Illusion);
}

TEST_CASE("an Unreal left drag walks along the ground and a Blender middle drag orbits")
{
    EditorCamera camera(start());
    camera.setPreset(NavigationPreset::Unreal);
    CHECK(camera.preset() == NavigationPreset::Unreal);
    const Vec3 eye = viewportEye(start());
    // Dragging up walks forward at the same height and keeps the pitch.
    drag(camera, 0, Vec2(0.0f, -40.0f));
    CHECK(camera.mode() == NavigationMode::Walk);
    const Vec3 moved = viewportEye(camera.camera());
    CHECK(moved.y == doctest::Approx(eye.y));
    const f32 yaw = glm::radians(start().yaw);
    CHECK(glm::dot(moved - eye, Vec3(-std::sin(yaw), 0.0f, -std::cos(yaw))) > 0.0f);
    CHECK(camera.camera().pitch == doctest::Approx(start().pitch));
    release(camera);
    // Dragging sideways turns around the eye.
    const Vec3 before = viewportEye(camera.camera());
    drag(camera, 0, Vec2(30.0f, 0.0f));
    CHECK(nearlyEqual(viewportEye(camera.camera()), before));
    CHECK(camera.camera().yaw == doctest::Approx(static_cast<double>(start().yaw) - 30.0 * 0.15));
    release(camera);

    EditorCamera blender(start());
    blender.setPreset(NavigationPreset::Blender);
    drag(blender, 2, Vec2(20.0f, 0.0f));
    CHECK(blender.mode() == NavigationMode::Orbit);
    CHECK(nearlyEqual(blender.camera().pivot, start().pivot));
    release(blender);
    drag(blender, 1, Vec2(20.0f, 0.0f));
    CHECK(blender.mode() == NavigationMode::None);
}
