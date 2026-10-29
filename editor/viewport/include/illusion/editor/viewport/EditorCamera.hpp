#pragma once

/// @file
/// The navigation of a viewport's orbit camera (GPU-free): orbit, pan, dolly and fly from the panel's input,
/// animated moves, and framing of bounds.
///
/// Mouse controls: Alt + left drag orbits around the pivot; middle drag pans; the wheel dollies towards the
/// pivot; Alt + right drag dollies; right drag looks around and W A S D Q E fly (Shift is faster; the wheel
/// changes the fly speed while flying).

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

namespace illusion::editor::viewport
{

/// What a mouse drag in a viewport does.
enum class NavigationMode : u8
{
    None,  ///< No drag.
    Orbit, ///< Turns around the pivot.
    Pan,   ///< Moves the pivot in the image plane.
    Dolly, ///< Moves towards or away from the pivot.
    Fly,   ///< Turns around the eye; the fly keys move it.
};

/// Sensitivities of the navigation.
struct NavigationSettings
{
    f32 orbitDegreesPerPixel = 0.3f; ///< Orbit speed.
    f32 lookDegreesPerPixel = 0.15f; ///< Fly look speed.
    f32 dollyPerWheelStep = 0.85f;   ///< Distance factor per wheel step towards the pivot.
    f32 dollyPerPixel = 0.005f;      ///< Exponent of the distance per pixel of an Alt + right drag.
    f32 fastMultiplier = 4.0f;       ///< Fly speed factor while Shift is held.
    f32 transitionSeconds = 0.3f;    ///< Length of animated moves.
};

/// The orbit camera of a viewport and its navigation state.
class EditorCamera
{
public:
    /// Smallest and largest fly speed in units per second.
    static constexpr f32 kMinFlySpeed = 0.05f;
    /// @copydoc kMinFlySpeed
    static constexpr f32 kMaxFlySpeed = 1000.0f;

    /// Starts at @p camera.
    explicit EditorCamera(const ViewportCamera& camera = {}, NavigationSettings settings = {}) noexcept;

    /// The camera as shown now (part way through an animated move).
    [[nodiscard]] const ViewportCamera& camera() const noexcept { return m_camera; }

    /// Where the camera is going (the camera itself when it is not animating).
    [[nodiscard]] const ViewportCamera& target() const noexcept { return m_target; }

    /// True during an animated move.
    [[nodiscard]] bool animating() const noexcept { return m_elapsed < m_duration; }

    /// The drag in progress.
    [[nodiscard]] NavigationMode mode() const noexcept { return m_mode; }

    /// Fly speed in units per second.
    [[nodiscard]] f32 flySpeed() const noexcept { return m_flySpeed; }

    /// Moves to @p camera: animated over NavigationSettings::transitionSeconds when @p animate is true, at
    /// once otherwise. Values out of range are clamped.
    void set(const ViewportCamera& camera, bool animate) noexcept;

    /// Applies one frame of panel input to an image of @p size pixels and advances an animated move.
    /// Returns true when the input moved the camera or a drag is in progress (a click then does not pick).
    /// Navigation stops an animated move.
    bool update(const ViewportInput& input, UVec2 size) noexcept;

    /// World units per pixel at the pivot for an image @p height pixels tall.
    [[nodiscard]] f32 unitsPerPixel(u32 height) const noexcept;

private:
    void advance(f32 deltaSeconds) noexcept;
    void startDrag(const ViewportInput& input) noexcept;

    NavigationSettings m_settings;
    ViewportCamera m_camera;
    ViewportCamera m_target;
    ViewportCamera m_from;
    f32 m_elapsed = 0.0f;
    f32 m_duration = 0.0f;
    NavigationMode m_mode = NavigationMode::None;
    u32 m_modeButton = 0;
    std::array<bool, 3> m_buttons{};
    f32 m_flySpeed = 5.0f;
};

/// @p camera with its values clamped into range (distance, pitch, field of view, orthographic height) and
/// non-finite values replaced by the defaults.
[[nodiscard]] ViewportCamera clampCamera(const ViewportCamera& camera) noexcept;

/// The camera between @p from and @p to at @p t (0 to 1, eased): the pivot moves linearly, the distance and
/// orthographic height geometrically and the yaw along the shorter way round.
[[nodiscard]] ViewportCamera interpolateCamera(const ViewportCamera& from, const ViewportCamera& to,
                                               f32 t) noexcept;

/// @p camera moved along its view direction so that @p bounds fills an image of @p aspect (width / height)
/// with a margin, keeping its direction. An empty box frames the origin.
[[nodiscard]] ViewportCamera frameBounds(const ViewportCamera& camera, const Aabb& bounds,
                                         f32 aspect) noexcept;

} // namespace illusion::editor::viewport
