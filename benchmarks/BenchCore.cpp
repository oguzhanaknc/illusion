#include "Budget.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/core/Random.hpp>

#include <string>

using namespace illusion;

namespace
{

// A scene-like document of about 64 KiB: entities with a transform, a name, a few scalars and an asset
// reference, written in the canonical pretty format.
void buildScene(JsonMutDocument& doc)
{
    Random random(7);
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    root.add("format", doc.makeString("illusion.scene")).add("version", doc.makeU64(1));
    root.addObject("types").add("illusion.Transform", doc.makeU64(1)).add("game.Crate", doc.makeU64(2));
    JsonMutValue entities = root.addArray("entities");
    for (int i = 0; i < 158; ++i)
    {
        JsonMutValue entity = entities.pushObject();
        JsonMutValue transform = entity.addObject("illusion.Transform");
        JsonMutValue position = transform.addArray("position");
        JsonMutValue rotation = transform.addArray("rotation");
        JsonMutValue scale = transform.addArray("scale");
        for (int c = 0; c < 3; ++c)
        {
            position.push(doc.makeF32(static_cast<f32>(random.range(-1000, 1000)) * 0.125f));
            scale.push(doc.makeF32(1.0f));
        }
        for (int c = 0; c < 4; ++c)
        {
            rotation.push(doc.makeF32(random.uniformF32()));
        }
        JsonMutValue crate = entity.addObject("game.Crate");
        crate.add("name", doc.makeString("crate_" + std::to_string(i)))
            .add("mass", doc.makeF64(random.uniform01() * 100.0))
            .add("health", doc.makeI64(random.range(1, 100)))
            .add("breakable", doc.makeBool(random.boolean()))
            .add("mesh", doc.makeString("meshes/props/crate_large.mesh"))
            .add("target", random.chance(0.5) ? doc.makeU64(random.below(158)) : doc.makeNull());
    }
}

std::string sceneText()
{
    JsonMutDocument doc;
    buildScene(doc);
    std::string text;
    (void)doc.write(text);
    return text;
}

} // namespace

// Budgets are roughly 5-10x the result measured on a 4-core cloud machine (Release, clang); see Budget.hpp.

ILL_BENCHMARK("json/parse", 10.0)
{
    const std::string text = sceneText();
    LinearAllocator arena(4 * text.size());
    bench.batch(text.size()).unit("byte").run("parse a 64 KiB scene into an arena", [&] {
        arena.reset();
        auto doc = JsonDocument::parse(text, arena);
        ankerl::nanobench::doNotOptimizeAway(doc);
    });
}

ILL_BENCHMARK("json/write", 16.0)
{
    LinearAllocator arena(1024 * 1024);
    JsonMutDocument doc(arena);
    buildScene(doc);
    std::string out;
    (void)doc.write(out);
    bench.batch(out.size()).unit("byte").run("write a 64 KiB scene (pretty)", [&] {
        (void)doc.write(out);
        ankerl::nanobench::doNotOptimizeAway(out);
    });
}

ILL_BENCHMARK("random/next", 10.0)
{
    Random random(42);
    constexpr int kValues = 10000;
    u64 sum = 0;
    bench.batch(kValues).unit("value").run("Random::next", [&] {
        for (int i = 0; i < kValues; ++i)
        {
            sum += random.next();
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

ILL_BENCHMARK("random/below", 12.0)
{
    Random random(42);
    constexpr int kValues = 10000;
    u64 sum = 0;
    bench.batch(kValues).unit("value").run("Random::below(1000)", [&] {
        for (int i = 0; i < kValues; ++i)
        {
            sum += random.below(1000);
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

ILL_BENCHMARK("log/ring-write", 200.0)
{
    LogRingSink sink(4096);
    constexpr int kMessages = 1000;
    const LogMessage message{LogLevel::Info,
                             "Render",
                             "created 128 buffers (12.5 MiB) for the shadow cascades of view 0",
                             "engine/render/src/SceneRenderer.cpp",
                             412,
                             0,
                             1.25};
    bench.batch(kMessages).unit("message").run("LogRingSink::write", [&] {
        for (int i = 0; i < kMessages; ++i)
        {
            sink.write(message);
        }
        ankerl::nanobench::doNotOptimizeAway(sink);
    });
}
