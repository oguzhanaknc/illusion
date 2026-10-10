# illusion API Reference

This is the reference for the public C++ API of the illusion engine, generated from the header comments.

- User guide and tutorials: https://oguzhanaknc.github.io/illusion/
- Source code: https://github.com/oguzhanaknc/illusion

Start with these entry points:

- `illusion::World` and `illusion::Query` — the Entity Component System.
- `illusion::CommandBuffer` — deferred structural changes.
- `illusion::JobSystem` — parallel work.
- `illusion::Logger` and the `ILL_LOG_*` macros — logging.
- `illusion::LinearAllocator` and `illusion::PoolAllocator` — allocation without the general heap.
