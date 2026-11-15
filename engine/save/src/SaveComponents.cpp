#include <illusion/ecs/Reflection.hpp>
#include <illusion/save/SaveComponents.hpp>

namespace illusion
{

void registerSaveComponents()
{
    reflect<Saved>("illusion.Saved", 1)
        .meta({.displayName = "Saved",
               .category = "Core",
               .icon = "save",
               .description = "Saved games contain the Entity and all its descendants.",
               .docsAnchor = "manual/scenes.md#scene-deltas"});
}

} // namespace illusion
