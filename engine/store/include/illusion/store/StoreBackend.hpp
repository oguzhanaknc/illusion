#pragma once

/// @file
/// The host's side of a store (ADR 0056): a StoreService that the host owns and advances once per frame.

#include <illusion/store/StoreService.hpp>

namespace illusion
{

/// A StoreService owned by a host (the player, Play mode), which calls update() once per frame.
class StoreBackend : public StoreService
{
public:
    ~StoreBackend() override = default;

    /// Runs the store's work of one frame (Steam's callbacks). Does not allocate.
    virtual void update() = 0;

protected:
    StoreBackend() = default;
};

} // namespace illusion
