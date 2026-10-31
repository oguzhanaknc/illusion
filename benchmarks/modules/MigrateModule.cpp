// A shared module for the modules/migrate benchmark, built twice: BENCH_MIGRATE_WIDE widens, renames and
// adds fields of bench.Migrated, so that every reload between the two builds migrates its Components.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/Reflection.hpp>

namespace illusion::bench
{

// The benchmark host reaches it by stable name only.
struct Migrated
{
#if !defined(BENCH_MIGRATE_WIDE)
    i32 count = 0;
    f32 speed = 0.0f;
    Vec3 position{0.0f};
    Entity target;
#else
    Entity target;
    i64 count = 0;
    f32 velocity = 0.0f;
    Vec3 position{0.0f};
    i32 combo = 0;
#endif
};

} // namespace illusion::bench

namespace
{

using namespace illusion;

bool loadModule(ModuleContext&)
{
#if !defined(BENCH_MIGRATE_WIDE)
    reflect<bench::Migrated>("bench.Migrated")
        .field("count", &bench::Migrated::count)
        .field("speed", &bench::Migrated::speed)
        .field("position", &bench::Migrated::position)
        .field("target", &bench::Migrated::target);
#else
    reflect<bench::Migrated>("bench.Migrated")
        .field("target", &bench::Migrated::target)
        .field("count", &bench::Migrated::count)
        .field("velocity", &bench::Migrated::velocity, {.renamedFrom = "speed"})
        .field("position", &bench::Migrated::position)
        .field("combo", &bench::Migrated::combo);
#endif
    return true;
}

constexpr ModuleApi kMigrateModule = makeModuleApi("bench_migrate", "1.0.0", {.load = &loadModule});

} // namespace

ILL_MODULE(bench_migrate, kMigrateModule)
