#include "Budget.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/play/PlaySession.hpp>

#include <string>

using namespace illusion;
using namespace illusion::editor;

// play.start and play.stop on a scene of 10,000 entities: the scene snapshot (scene JSON written and loaded),
// a fresh GameSession and play World, the play entity index, per entity. Budget about 7x the 3.0 us measured
// on a desktop (Release, MSVC).
ILL_BENCHMARK("play/snapshot", 20'000.0)
{
    EditorContext editor(EditorContextOptions{.seed = 1, .crashJournal = false});
    JobSystem jobs(JobSystemDesc{2, 1024});
    PlaySession play(editor, {.jobs = &jobs, .audioOutput = AudioOutput::Offline});
    const JsonDocument components =
        std::move(JsonDocument::parse(R"({"illusion.Transform":{"position":[1,2,3]}})").value());
    for (int i = 0; i < 10000; ++i)
    {
        CreateEntityOptions options;
        options.name = "Entity " + std::to_string(i);
        options.components = components.root();
        ILL_CHECK(editor.editor().create(options).hasValue(), "cannot create a benchmark entity");
    }
    bench.batch(10000).unit("entity").run("play.start + play.stop (10000 entities)", [&] {
        ILL_CHECK(play.start({}).hasValue(), "play.start failed");
        ILL_CHECK(editor.playWorld()->aliveCount() == 10000, "the play World lacks entities");
        play.stop();
    });
}
