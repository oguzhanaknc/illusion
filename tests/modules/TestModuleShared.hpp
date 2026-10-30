#pragma once

// Types shared by the module tests and the test modules they load.

#include <illusion/core/Base.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

#include <atomic>

namespace illusion::test
{

// Registered by the test modules. Variant 3 of the module widens it, which a reload must reject.
struct ModuleCounter
{
    i32 value = 0;
#if defined(TEST_MODULE_WIDE_COUNTER)
    i64 extra = 0;
#endif
};

// Registered only by variant 1, so that reloading variant 2 drops it.
struct LegacyComponent
{
    i32 value = 0;
};

// Used by the host too (like an engine Component): the module creates one, the host takes the type over
// when it uses it, and the Components outlive the module.
struct SharedComponent
{
    i32 value = 0;
};

// Given to some Worlds through their ServiceSet: the module adds its value to ModuleControl::tagSum.
struct WorldTag
{
    i32 value = 0;
};

// Shared through ModuleHost::provide(): the host steers the module and reads what it saw.
struct ModuleControl
{
    bool failLoad = false;
    bool assertInUpdate = false;
    i32 loads = 0;
    i32 unloads = 0;
    i32 starts = 0;
    i32 stops = 0;
    i32 updates = 0;
    bool worldInLoad = false;   // ModuleContext::hasWorld() during load() or unload()
    bool simulationOnly = true; // every start(), stop() and update() saw a Simulation World
    i32 tagSum = 0;             // WorldTag::value of the World of each update()
    i32 variant = 0;
    bool reloadingSeen = false;
    const void* logger = nullptr;     // &Logger::instance() as the module sees it
    const void* registry = nullptr;   // &ComponentRegistry::instance() as the module sees it
    const void* assetPaths = nullptr; // &AssetPathTable::instance() as the module sees it
    u64 asset = 0;                    // id of an AssetRef the module created from a path
    ComponentId counterId = kInvalidComponentId;
    ComponentId transformId = kInvalidComponentId;
    u32 threadIndex = 0;
    std::atomic<i32> workerIndex{-2}; // JobSystem::currentWorkerIndex() inside a job the module ran
    bool callServices = false;        // update() calls every function of the four service interfaces
    bool createMesh = false;          // start() builds a static mesh body through the PhysicsService
    PhysicsMeshHandle mesh;           // the geometry start() created
    Entity meshEntity;                // the body start() created
};

} // namespace illusion::test
