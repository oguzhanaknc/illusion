#pragma once

/// @file
/// Loading state of an asset, shared by the asset streamer and the services of the engine libraries.

#include <illusion/core/Base.hpp>

namespace illusion
{

/// Loading state of an asset handle.
enum class AssetState : u8
{
    Loading, ///< Requested; not finished (or finished but not yet published by update()).
    Ready,   ///< Loaded; AssetHandle::get() returns the data.
    Failed,  ///< Loading failed; AssetHandle::error() says why.
};

} // namespace illusion
