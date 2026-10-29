#include "Budget.hpp"

#include <illusion/editor/viewport/EditorCamera.hpp>
#include <illusion/editor/viewport/PickCandidates.hpp>

#include <vector>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::editor::viewport;

// One frame of viewport navigation (an orbit drag) through EditorCamera::update(). Budget about 10x the
// 20 ns measured on a desktop (Release, MSVC).
ILL_BENCHMARK("viewport/camera-update", 200.0)
{
    EditorCamera camera;
    ViewportInput input;
    input.hovered = true;
    input.alt = true;
    input.buttons[0] = true;
    input.cursorDelta = Vec2(1.5f, -0.5f);
    input.deltaSeconds = 1.0f / 60.0f;
    bench.run("orbit frame",
              [&] { ankerl::nanobench::doNotOptimizeAway(camera.update(input, UVec2(1920, 1080))); });
}

// Ordering the entities of a 5 x 5 pick window with four distinct entities. Budget about 8x the 127 ns
// measured on a desktop (Release, MSVC).
ILL_BENCHMARK("viewport/pick-candidates", 1'000.0)
{
    std::vector<EntityIdTexel> texels(25);
    for (u32 i = 0; i < 25; ++i)
    {
        texels[i] = EntityIdTexel{i % 5 == 0 ? ~0u : i % 4, 1};
    }
    std::vector<Entity> candidates;
    candidates.reserve(25);
    bench.run("5x5 window", [&] {
        candidates.clear();
        appendVisibleCandidates(texels, UVec2(5, 5), UVec2(2, 2), candidates);
        ankerl::nanobench::doNotOptimizeAway(candidates.data());
    });
}
