#pragma once

/// @file
/// What the viewport commands (`viewport.list`, `viewport.pick`, ...) and the Scene panel need from the
/// editor's viewports. The GPU viewport service of the editor process (`illusion::editor_viewport`)
/// implements it and registers with EditorContext::setViewports(); without one (an editor without a GPU), the
/// commands fail with ErrorKind::Unsupported.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/editor/core/CommandError.hpp>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor
{

/// Projection of a viewport camera.
enum class ViewportProjection : u8
{
    Perspective,  ///< Perspective with an infinite far plane.
    Orthographic, ///< Parallel projection (the axis views use it).
};

/// What a viewport shows (the renderer's diagnostic views).
enum class ViewportShading : u8
{
    Lit,              ///< Physically based shading.
    Unlit,            ///< Base colour plus emission.
    Albedo,           ///< Base colour.
    Normals,          ///< World normals.
    Roughness,        ///< Material roughness.
    Metallic,         ///< Material metallic factor.
    AmbientOcclusion, ///< Material and screen-space occlusion.
    ShadowCascades,   ///< The shadow cascade of each surface.
    MotionVectors,    ///< Screen-space velocity.
};

/// A standard view of `viewport.view`.
enum class ViewportView : u8
{
    Perspective, ///< Back to perspective, keeping the direction.
    Top,         ///< Orthographic, looking down -y.
    Bottom,      ///< Orthographic, looking up +y.
    Front,       ///< Orthographic, looking along -z.
    Back,        ///< Orthographic, looking along +z.
    Left,        ///< Orthographic, looking along +x.
    Right,       ///< Orthographic, looking along -x.
};

/// The orbit camera of a viewport: it looks at `pivot` from `distance` away, in the direction given by `yaw`
/// and `pitch` (degrees). The rotation is `yaw` about +y, then `pitch` about the camera's x axis, so yaw 0
/// and pitch 0 look along -z and a negative pitch looks down.
struct ViewportCamera
{
    Vec3 pivot{0.0f};        ///< Point the camera orbits around and looks at.
    f32 distance = 10.0f;    ///< Distance of the eye from the pivot, 0.01 to 100,000.
    f32 yaw = 45.0f;         ///< Degrees about +y.
    f32 pitch = -30.0f;      ///< Degrees about the camera's x axis, -90 to 90.
    f32 fieldOfView = 60.0f; ///< Vertical field of view in degrees (perspective), 1 to 170.
    ViewportProjection projection = ViewportProjection::Perspective; ///< Projection.
    f32 orthographicHeight = 10.0f; ///< Vertical extent in world units (orthographic), 0.01 to 100,000.
};

/// What a viewport draws.
struct ViewportSettings
{
    ViewportShading shading = ViewportShading::Lit; ///< Shading mode.
    bool wireframe = false;                         ///< Wireframe drawn over opaque surfaces.
    bool grid = true;                               ///< Ground grid.
    bool selectionOutline = true;                   ///< Outline around selected entities.
};

/// A viewport of `viewport.list`.
struct ViewportInfo
{
    std::string id;            ///< Stable id, e.g. "scene".
    UVec2 size{0};             ///< Size of the rendered image in pixels (0, 0 before the first frame).
    bool visible = false;      ///< Drawn in the last UI frame.
    ViewportCamera camera;     ///< The camera.
    Vec3 eye{0.0f};            ///< The camera's position (derived from the camera).
    ViewportSettings settings; ///< What it draws.
};

/// Where a world-space point appears in a viewport.
struct ViewportPoint
{
    Vec2 pixel{0.0f};     ///< Pixel coordinates (origin at the top left, y down).
    f32 depth = 0.0f;     ///< Reversed-Z depth: 1 at the near plane, towards 0 far away.
    bool inFront = false; ///< The point is in front of the camera.
    bool inside = false;  ///< In front of the camera and within the image.
};

/// What a click in a viewport hit.
struct ViewportHit
{
    Entity entity{};    ///< The entity (alive when the pick completes).
    u32 index = 0;      ///< Position of the entity among the candidates (repeated clicks cycle).
    u32 candidates = 0; ///< Entities under the cursor: the visible ones near it, then those behind.
};

/// Input of a viewport panel for one frame (navigation).
struct ViewportInput
{
    Vec2 cursor{0.0f};             ///< Cursor position in image pixels (origin at the top left, y down).
    Vec2 cursorDelta{0.0f};        ///< Cursor movement since the last frame in pixels.
    f32 wheel = 0.0f;              ///< Wheel steps (positive: away from the user).
    bool hovered = false;          ///< The cursor is over the image and nothing covers it.
    bool focused = false;          ///< Keyboard input goes to the viewport.
    std::array<bool, 3> buttons{}; ///< Left, right and middle mouse buttons held.
    bool alt = false;              ///< Alt held.
    bool shift = false;            ///< Shift held.
    Vec3 move{0.0f};               ///< Fly keys: x = D - A, y = E - Q, z = W - S (each -1, 0 or 1).
    f32 deltaSeconds = 0.0f;       ///< Length of the frame.
};

/// Colours of the overlays (display RGBA, sRGB-encoded like the UI theme's colours).
struct ViewportStyle
{
    Vec4 selection{1.0f, 0.62f, 0.1f, 1.0f}; ///< Selection outline.
    Vec4 grid{0.5f, 0.5f, 0.5f, 0.5f};       ///< Grid lines.
    Vec4 axisX{0.94f, 0.32f, 0.31f, 1.0f};   ///< The x axis of the grid.
    Vec4 axisY{0.48f, 0.8f, 0.16f, 1.0f};    ///< The y axis (orthographic side views).
    Vec4 axisZ{0.24f, 0.55f, 0.96f, 1.0f};   ///< The z axis of the grid.
};

/// What a viewport panel draws this frame.
struct ViewportFrame
{
    u64 texture = 0;           ///< ImGui texture id of the image (0: nothing to draw yet).
    UVec2 size{0};             ///< Size of the image (it differs from the panel while a resize settles).
    bool navigating = false;   ///< The input moved the camera (a click then does not pick).
    ViewportSettings settings; ///< What the viewport draws (for the panel's menus).
    ViewportProjection projection = ViewportProjection::Perspective; ///< Projection of its camera.
};

/// Smallest camera distance and orthographic height.
inline constexpr f32 kViewportMinExtent = 0.01f;
/// Largest camera distance and orthographic height (and distance of the pivot from the origin).
inline constexpr f32 kViewportMaxExtent = 100000.0f;

/// The rotation of @p camera: yaw about +y, then pitch about the camera's x axis.
[[nodiscard]] Quat viewportRotation(const ViewportCamera& camera) noexcept;

/// The unit view direction of @p camera.
[[nodiscard]] Vec3 viewportForward(const ViewportCamera& camera) noexcept;

/// The eye position of @p camera: `pivot - forward * distance`.
[[nodiscard]] Vec3 viewportEye(const ViewportCamera& camera) noexcept;

/// The world-to-view matrix of @p camera.
[[nodiscard]] Mat4 viewportViewMatrix(const ViewportCamera& camera) noexcept;

/// The reversed-Z projection of @p camera for an image of @p aspect (width / height). Perspective views
/// have an infinite far plane and a near plane that scales with the distance; orthographic views also show
/// what lies behind the eye, so that a framed object is never cut.
[[nodiscard]] Mat4 viewportProjectionMatrix(const ViewportCamera& camera, f32 aspect) noexcept;

/// @p camera turned to the standard view @p view around the same pivot. The axis views are orthographic
/// with the apparent size of the perspective view at the pivot; Perspective goes back from them.
[[nodiscard]] ViewportCamera viewportPreset(const ViewportCamera& camera, ViewportView view) noexcept;

/// @p camera with its eye moved to @p eye, still looking at the pivot (std::nullopt when they coincide).
[[nodiscard]] std::optional<ViewportCamera> aimViewportCamera(const ViewportCamera& camera,
                                                              Vec3 eye) noexcept;

/// Where @p point appears in an image of @p size pixels seen through @p camera.
[[nodiscard]] ViewportPoint projectToViewport(const ViewportCamera& camera, UVec2 size, Vec3 point) noexcept;

/// The viewports behind the `viewport.*` commands and the Scene panel. Called on the main thread.
class ViewportHost
{
public:
    virtual ~ViewportHost() = default;

    /// Every viewport.
    [[nodiscard]] virtual std::vector<ViewportInfo> viewports() const = 0;

    /// Moves the camera of @p id to @p camera (validated by the command), animated unless the user asked
    /// for reduced motion or @p animate is false; NotFound for unknown ids.
    [[nodiscard]] virtual Expected<void, CommandError>
    setCamera(std::string_view id, const ViewportCamera& camera, bool animate) = 0;

    /// Changes what @p id draws; NotFound for unknown ids.
    [[nodiscard]] virtual Expected<void, CommandError> setSettings(std::string_view id,
                                                                   const ViewportSettings& settings) = 0;

    /// The camera of @p id moved so that @p entities fill the image (every visible entity when empty).
    [[nodiscard]] virtual Expected<ViewportCamera, CommandError>
    framing(std::string_view id, std::span<const Entity> entities) = 0;

    /// Starts reading which entity is under @p pixel of @p id. With @p cycle, a pick at the pixel of the
    /// previous one returns the next candidate. Returns a ticket for pick().
    [[nodiscard]] virtual Expected<u64, CommandError> requestPick(std::string_view id, Vec2 pixel,
                                                                  bool cycle) = 0;

    /// The result of @p ticket once it is read (std::nullopt while pending): the hit, or nothing for the
    /// background. Each ticket is answered once.
    [[nodiscard]] virtual std::optional<Expected<std::optional<ViewportHit>, CommandError>>
    pick(u64 ticket) = 0;

    /// Draws @p id at @p size pixels this frame after applying @p input to its camera (the Scene panel calls
    /// it once per frame). Unknown ids return an empty frame.
    [[nodiscard]] virtual ViewportFrame frame(std::string_view id, UVec2 size, const ViewportInput& input,
                                              const ViewportStyle& style) = 0;
};

} // namespace illusion::editor
