#include "Budget.hpp"

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <string>

using namespace illusion;

// Measured about 5.5 ns on a desktop.
ILL_BENCHMARK("reflection/field-read-write", 40.0)
{
    reflectAudioComponents();
    const TypeReflection& type = *ComponentRegistry::instance().reflection(componentId<AudioSource>());
    const FieldInfo& volume = *type.field("volume");
    AudioSource source;
    AudioSource target;
    source.volume = 0.5f;
    bench.unit("field").run("readField + writeField of an F32", [&] {
        const FieldValue value = readField(&source, volume);
        writeField(&target, volume, value);
        ankerl::nanobench::doNotOptimizeAway(target.volume);
    });
}

// Measured about 380 ns (write) and 460 ns (read) on a desktop.
ILL_BENCHMARK("reflection/component-json", 2500.0)
{
    reflectAudioComponents();
    const TypeReflection& type = *ComponentRegistry::instance().reflection(componentId<AudioSource>());
    AudioSource source;
    source.clip = AssetRef("game:/audio/footstep.ogg");
    source.bus = AudioBus::Ambience;
    source.volume = 0.75f;

    bench.unit("component").run("write an AudioSource (20 fields) to a new JSON document", [&] {
        JsonMutDocument doc;
        const auto value = writeValue(doc, type, &source);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    JsonMutDocument doc;
    doc.setRoot(*writeValue(doc, type, &source));
    std::string text;
    (void)doc.write(text);
    const auto parsed = JsonDocument::parse(text);
    AudioSource target;
    bench.unit("component").run("read an AudioSource (20 fields) from parsed JSON", [&] {
        const ValueReadResult result = readValue(type, &target, parsed->root());
        ankerl::nanobench::doNotOptimizeAway(result);
    });
}
