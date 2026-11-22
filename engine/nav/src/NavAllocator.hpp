#pragma once

namespace illusion::detail
{

/// Routes the allocations of Recast and Detour through the engine heap. Idempotent and thread-safe; call it
/// before the first Recast or Detour call.
void installNavAllocators() noexcept;

} // namespace illusion::detail
