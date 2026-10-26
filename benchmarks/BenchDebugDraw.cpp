#include "Budget.hpp"

#include <illusion/render/DebugDraw.hpp>

using namespace illusion;

ILL_BENCHMARK("render/debugdraw-record", 20.0)
{
    constexpr u32 kLines = 100000;
    DebugDraw draw(kLines);
    bench.batch(kLines).unit("line").run("record reserved lines", [&] {
        draw.clear();
        for (u32 i = 0; i < kLines; ++i)
        {
            const f32 x = static_cast<f32>(i);
            ankerl::nanobench::doNotOptimizeAway(draw.line(Vec3(x, 0, 0), Vec3(x, 1, 0), Vec4(1), 2));
        }
    });
}
