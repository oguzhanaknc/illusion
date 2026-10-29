#pragma once

/// @file
/// Bounded asynchronous texture readbacks on the graphics queue, without device-idle waits.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>
#include <optional>
#include <span>

namespace illusion
{

class RenderDevice;

/// Result of ReadbackRing::read().
enum class ReadbackStatus : u8
{
    Pending, ///< The copy has not completed; the destination is unchanged.
    Ready,   ///< Pixels were copied and the ticket was consumed.
    Invalid, ///< Unknown ticket, undersized destination, or mapping failure; no pixels were copied.
};

/// Render-thread-only readback slots. Submit the producing graphics commands before request(). Each
/// request submits a copy followed by an event query. A ticket owns its slot until read() returns Ready.
/// Allocation occurs only on creation or when a slot changes size/format. The RenderDevice outlives this.
class ReadbackRing : NonCopyable
{
public:
    /// Creates a ring with @p capacity outstanding copies (1..64); null on invalid capacity or GPU failure.
    [[nodiscard]] static std::unique_ptr<ReadbackRing> create(RenderDevice& device, u32 capacity = 3);
    ~ReadbackRing();

    /// Copies an uncompressed, single-sample 2D texture region. Zero extent reads from origin to the edge.
    /// Returns nothing for a full ring, invalid region/format or GPU resource failure. Never waits.
    [[nodiscard]] std::optional<u64> request(nvrhi::ITexture* source, UVec2 origin = UVec2(0),
                                             UVec2 extent = UVec2(0));

    /// Copies tightly packed rows in the source format. On Ready the ticket becomes invalid. A too-small
    /// destination returns Invalid without consuming the ticket. With block=true, waits for this copy's
    /// event query (for headless tools); neither mode calls waitForIdle().
    [[nodiscard]] ReadbackStatus read(u64 ticket, std::span<std::byte> destination, bool block = false);

private:
    ReadbackRing();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion
