#pragma once

/// @file
/// When a viewport recreates its render targets while its panel is resized (GPU-free).

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion::editor::viewport
{

/// Decides the size a viewport renders at while its panel changes size. The first size applies at once;
/// later ones apply when the panel has kept a size for kSettleSeconds or when the mouse button that drags a
/// splitter is released, so a drag does not recreate the targets every frame (the image is stretched
/// meanwhile).
class ResizePolicy
{
public:
    /// How long a new size must stay before it applies.
    static constexpr f64 kSettleSeconds = 0.15;

    /// The size to render at for a panel of @p requested pixels at time @p now (seconds). @p dragging is
    /// true while a mouse button is held. A zero size keeps the current one.
    UVec2 update(UVec2 requested, f64 now, bool dragging) noexcept;

    /// The size applied last (0, 0 before the first).
    [[nodiscard]] UVec2 current() const noexcept { return m_current; }

private:
    UVec2 m_current{0};
    UVec2 m_pending{0};
    f64 m_changed = 0.0;
    bool m_dragging = false;
};

} // namespace illusion::editor::viewport
