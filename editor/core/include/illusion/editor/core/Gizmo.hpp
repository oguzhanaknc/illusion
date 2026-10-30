#pragma once

/// @file
/// GPU-free math of the transform gizmos of the Scene viewports: the shapes of the handles in image pixels,
/// hit tests with a tolerance that scales with the display, drags that turn cursor positions into
/// translations, rotations and scale factors (with snapping), and how a drag moves the selected entities.
/// The Scene panel draws the shapes with Dear ImGui and runs the drags as `transform.set` calls inside one
/// transaction.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace illusion::editor
{

class EditorContext;

/// What a left drag in a Scene viewport does to the selection (Q, W, E, R and T).
enum class GizmoTool : u8
{
    Select,    ///< No gizmo: drags draw a selection box.
    Translate, ///< Move along axes, in planes or in the view plane.
    Rotate,    ///< Turn about the axes, the view direction or freely (trackball).
    Scale,     ///< Scale along the entities' own axes or uniformly.
    Universal, ///< Translate handles, rotation rings and a uniform scale handle together.
    Measure,   ///< No gizmo: drags draw a ruler between the points under the cursor.
};

/// Axes of the Translate and Rotate gizmos. The Scale gizmo always uses the active entity's own axes.
enum class GizmoSpace : u8
{
    World, ///< The scene's axes.
    Local, ///< The active entity's own axes.
    View,  ///< The camera's axes.
};

/// Where the gizmo sits and what rotations and scaling turn about.
enum class GizmoPivot : u8
{
    Median,       ///< The mean of the selected entities' positions.
    Active,       ///< The active (last selected) entity's position.
    Individual,   ///< Each entity about its own position (the gizmo sits at the median).
    BoundsCenter, ///< The centre of the box around the selected entities' positions.
};

/// Increments that drags snap to. Holding Ctrl during a drag inverts `enabled`.
struct GizmoSnap
{
    bool enabled = false; ///< Snap without Ctrl.
    f32 translate = 0.5f; ///< Distance increment in world units, 0.001 to 1,000.
    f32 rotate = 15.0f;   ///< Angle increment in degrees, 0.01 to 180.
    f32 scale = 0.1f;     ///< Scale factor increment, 0.001 to 10.
    bool alignToNormal =
        false; ///< Surface snapping also turns the entities' +y axis onto the surface normal.
};

/// A ruler drawn in the Scene viewports (`gizmo.measure`, the Measure tool).
struct GizmoRuler
{
    Vec3 from{0.0f}; ///< Start in world space.
    Vec3 to{0.0f};   ///< End in world space.
};

/// The gizmo state of the editor (`gizmo.get`; not saved, not undoable).
struct GizmoSettings
{
    GizmoTool tool = GizmoTool::Translate; ///< Tool.
    GizmoSpace space = GizmoSpace::World;  ///< Axes.
    GizmoPivot pivot = GizmoPivot::Median; ///< Pivot.
    GizmoSnap snap;                        ///< Snapping.
    std::optional<GizmoRuler> ruler{};     ///< The ruler shown in the Scene viewports, if any.
};

/// Smallest snap increment of a translation.
inline constexpr f32 kGizmoMinTranslateSnap = 0.001f;
/// Largest snap increment of a translation.
inline constexpr f32 kGizmoMaxTranslateSnap = 1000.0f;
/// Smallest snap increment of an angle in degrees.
inline constexpr f32 kGizmoMinRotateSnap = 0.01f;
/// Largest snap increment of an angle in degrees.
inline constexpr f32 kGizmoMaxRotateSnap = 180.0f;
/// Smallest snap increment of a scale factor.
inline constexpr f32 kGizmoMinScaleSnap = 0.001f;
/// Largest snap increment of a scale factor.
inline constexpr f32 kGizmoMaxScaleSnap = 10.0f;
/// Length of the gizmo's axes in display points (multiplied by GizmoCamera::pixelScale).
inline constexpr f32 kGizmoSize = 96.0f;
/// Distance in display points within which the cursor grabs a line or ring handle.
inline constexpr f32 kGizmoHitTolerance = 6.0f;
/// Points of each rotation ring.
inline constexpr u32 kGizmoRingSegments = 64;
/// Smallest scale factor a drag gives (the scale never reaches zero).
inline constexpr f32 kGizmoMinScaleFactor = 0.01f;

/// A part of a gizmo that the cursor can grab.
enum class GizmoHandle : u8
{
    None,            ///< Nothing.
    TranslateX,      ///< Move along the x axis.
    TranslateY,      ///< Move along the y axis.
    TranslateZ,      ///< Move along the z axis.
    TranslateYZ,     ///< Move in the plane of the y and z axes.
    TranslateXZ,     ///< Move in the plane of the x and z axes.
    TranslateXY,     ///< Move in the plane of the x and y axes.
    TranslateScreen, ///< Move in the view plane.
    RotateX,         ///< Turn about the x axis.
    RotateY,         ///< Turn about the y axis.
    RotateZ,         ///< Turn about the z axis.
    RotateView,      ///< Turn about the view direction.
    RotateTrackball, ///< Turn freely.
    ScaleX,          ///< Scale along the x axis.
    ScaleY,          ///< Scale along the y axis.
    ScaleZ,          ///< Scale along the z axis.
    ScaleUniform,    ///< Scale along all axes.
};

/// The axis (0 to 2) that @p handle moves along, turns about or scales, or std::nullopt for the others.
[[nodiscard]] std::optional<u32> gizmoHandleAxis(GizmoHandle handle) noexcept;

/// How a viewport sees the gizmo: its camera, image size and display scale.
struct GizmoCamera
{
    Vec3 eye{0.0f};                        ///< Eye position.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Camera rotation (it looks along its -z).
    Mat4 projection{1.0f};                 ///< View to clip (reversed-Z).
    UVec2 size{0};                         ///< Image size in pixels.
    bool orthographic = false;             ///< Parallel projection.
    f32 pixelScale = 1.0f; ///< Image pixels per display point (UI scale times framebuffer scale).
};

/// The GizmoCamera of @p camera for an image of @p size pixels at @p pixelScale.
[[nodiscard]] GizmoCamera gizmoCamera(const ViewportCamera& camera, UVec2 size, f32 pixelScale) noexcept;

/// A ray through an image pixel.
struct GizmoRay
{
    Vec3 origin{0.0f};                 ///< Start (the eye, or a point in the eye plane when orthographic).
    Vec3 direction{0.0f, 0.0f, -1.0f}; ///< Unit direction.
};

/// The ray through @p pixel (origin at the top left, y down) of @p camera.
[[nodiscard]] GizmoRay gizmoRay(const GizmoCamera& camera, Vec2 pixel) noexcept;

/// Where the gizmo is: its pivot and the orientation of its axes.
struct GizmoPose
{
    Vec3 origin{0.0f};                        ///< Pivot in world space.
    Quat orientation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Axes (x, y, z are the columns of its matrix).
};

/// The pose of the gizmo of @p tool for the world placements @p placements of the selection, of which
/// @p active is the active one, seen through @p camera. std::nullopt when @p placements is empty.
[[nodiscard]] std::optional<GizmoPose> gizmoPose(std::span<const Transform> placements, usize active,
                                                 GizmoTool tool, GizmoSpace space, GizmoPivot pivot,
                                                 const GizmoCamera& camera) noexcept;

/// Visibility states of ring segments.
enum class GizmoSegment : u8
{
    Hidden, ///< Behind the eye.
    Back,   ///< The half of the ring facing away from the camera (drawn dimmed, not grabbed).
    Front,  ///< The half facing the camera.
};

/// An axis line of a gizmo in image pixels.
struct GizmoAxisShape
{
    Vec2 start{0.0f};      ///< Inner end.
    Vec2 end{0.0f};        ///< Outer end (the arrow head or the scale box).
    f32 visibility = 0.0f; ///< 1 when seen across, fading to 0 when it points at the camera.
};

/// A plane handle of the Translate gizmo in image pixels.
struct GizmoPlaneShape
{
    std::array<Vec2, 4> corners{}; ///< Corners in order.
    f32 visibility = 0.0f;         ///< 1 when seen face on, fading to 0 when seen edge on.
    f32 facing = 0.0f;             ///< Cosine between its normal and the view direction (0 to 1).
};

/// A rotation ring of a gizmo in image pixels.
struct GizmoRingShape
{
    std::array<Vec2, kGizmoRingSegments> points{};           ///< Points; segment i joins point i and i + 1.
    std::array<GizmoSegment, kGizmoRingSegments> segments{}; ///< State of each segment.
    f32 visibility = 0.0f; ///< Whether the ring is drawn at all (0 while dragging another handle).
    Vec2 low{0.0f};        ///< Smallest corner of the box around its points.
    Vec2 high{0.0f};       ///< Largest corner of the box around its points.
};

/// Everything a viewport draws of a gizmo, in image pixels; gizmoHitTest() uses the same shapes.
struct GizmoShape
{
    GizmoTool tool = GizmoTool::Select;      ///< Tool (Select: nothing to draw).
    bool visible = false;                    ///< The pivot is in front of the camera.
    Vec2 center{0.0f};                       ///< The pivot in image pixels.
    f32 length = 0.0f;                       ///< Length of the axes in pixels (kGizmoSize * pixelScale).
    f32 tolerance = 0.0f;                    ///< Hit tolerance in pixels.
    f32 worldLength = 0.0f;                  ///< Length of the axes in world units at the pivot.
    std::array<Vec3, 3> axes{};              ///< World directions of the x, y and z axes.
    std::array<GizmoAxisShape, 3> lines{};   ///< Translate arrows or scale stems.
    std::array<GizmoPlaneShape, 3> planes{}; ///< Planes YZ, XZ and XY (Translate and Universal).
    std::array<GizmoRingShape, 3> rings{};   ///< Rings about x, y and z (Rotate and Universal).
    f32 viewRing = 0.0f;                     ///< Radius of the view ring in pixels (0: none).
    f32 trackball = 0.0f;                    ///< Radius of the trackball disc in pixels (0: none).
    f32 centerBox = 0.0f; ///< Half size of the centre box (screen move or uniform scale; 0: none).
};

/// The shape of the gizmo of @p tool at @p pose seen through @p camera.
[[nodiscard]] GizmoShape gizmoShape(GizmoTool tool, const GizmoCamera& camera,
                                    const GizmoPose& pose) noexcept;

/// The handle of @p shape under @p cursor (image pixels): the centre box, then the plane handle that faces
/// the camera best, then the nearest line or ring within the tolerance, then the trackball disc.
[[nodiscard]] GizmoHandle gizmoHitTest(const GizmoShape& shape, Vec2 cursor) noexcept;

/// What a drag does so far, relative to where it started.
struct GizmoDelta
{
    Vec3 translation{0.0f};                ///< World-space offset.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< World-space rotation about the pivot (or each origin).
    Vec3 scale{1.0f};                      ///< Factors along the gizmo's axes.
    f32 angle = 0.0f;                      ///< The rotation's angle in degrees (signed, unwrapped).
};

/// Numbers typed during a drag or a modal transform, as in Blender: up to three fields (the gizmo's x, y and
/// z; Tab moves to the next one), each a decimal number of digits and one point; '-' flips the sign.
class GizmoTypedInput
{
public:
    /// Characters a field holds, the sign included.
    static constexpr usize kMaxLength = 16;
    /// Fields.
    static constexpr u32 kFields = 3;

    /// Adds @p character to the current field: a digit, one '.', or '-' to flip the sign. Returns false (and
    /// changes nothing) for other characters or a full field.
    bool type(char character) noexcept;
    /// Removes the last character of the current field (the sign last); in an empty field, moves back to the
    /// previous one.
    void erase() noexcept;
    /// Moves to the next field (after the last, back to the first).
    void next() noexcept;
    /// Empties every field and moves back to the first.
    void clear() noexcept;

    /// Something was typed.
    [[nodiscard]] bool active() const noexcept;
    /// The field that typing goes to.
    [[nodiscard]] u32 field() const noexcept { return m_field; }
    /// The number in @p field (std::nullopt when it is empty or holds no digit).
    [[nodiscard]] std::optional<f32> value(u32 field) const noexcept;
    /// The text of @p field as typed, e.g. "-2.5" ("" for fields out of range).
    [[nodiscard]] std::string_view text(u32 field) const noexcept;

private:
    std::array<std::array<char, kMaxLength>, kFields> m_text{};
    std::array<u8, kFields> m_length{};
    u32 m_field = 0;
};

/// A drag of one gizmo handle. Its geometry (drag plane, axis) is fixed when it begins, so the result
/// depends only on the cursor, never on how many frames the drag took.
class GizmoDrag
{
public:
    /// Starts dragging @p handle of the gizmo of @p tool at @p pose seen through @p camera, grabbed at
    /// @p cursor. std::nullopt for GizmoHandle::None or when the handle points at the camera.
    [[nodiscard]] static std::optional<GizmoDrag> begin(GizmoTool tool, GizmoHandle handle,
                                                        const GizmoCamera& camera, const GizmoPose& pose,
                                                        Vec2 cursor) noexcept;

    /// The delta with the cursor at @p cursor, snapped to the increments of @p snap when it is not null.
    [[nodiscard]] GizmoDelta update(Vec2 cursor, const GizmoSnap* snap) noexcept;

    /// The dragged handle.
    [[nodiscard]] GizmoHandle handle() const noexcept { return m_handle; }

    /// The pose of the gizmo when the drag began.
    [[nodiscard]] const GizmoPose& pose() const noexcept { return m_pose; }

    /// The delta that the numbers typed in @p input give the dragged handle: distances along its axes,
    /// an angle in degrees about its axis, or scale factors (at least kGizmoMinScaleFactor). Fields that
    /// were not typed count as 0 (1 for scale factors). The trackball turns about the view direction.
    [[nodiscard]] GizmoDelta typed(const GizmoTypedInput& input) const noexcept;

private:
    GizmoDrag() = default;

    [[nodiscard]] std::optional<Vec3> hitPlane(Vec2 cursor) const noexcept;

    GizmoHandle m_handle = GizmoHandle::None;
    GizmoCamera m_camera;
    GizmoPose m_pose;
    Vec3 m_axis{0.0f};      // the handle's world axis (or the plane normal)
    Vec3 m_normal{0.0f};    // normal of the drag plane
    Vec3 m_start{0.0f};     // where the cursor hit the drag plane at the start
    Vec2 m_cursor{0.0f};    // cursor at the start
    Vec2 m_center{0.0f};    // the pivot in pixels
    Vec2 m_tangent{0.0f};   // screen direction of a positive turn for rings seen edge on
    f32 m_pixels = 1.0f;    // the gizmo's length in pixels
    f32 m_radius = 1.0f;    // radius of the dragged ring in pixels
    f32 m_grab = 1.0f;      // distance along the axis of the grab point (scale handles)
    bool m_onPlane = false; // rings: angles come from the drag plane (false: from the tangent)
    f32 m_turn = 0.0f;      // unwrapped angle in radians of the last update
    f32 m_previous = 0.0f;  // raw angle of the last update, for unwrapping
    GizmoDelta m_last;      // the result of the last update (kept when the cursor ray misses the plane)
};

/// What a modal transform does (G, R and S in Blender).
enum class GizmoModalOperation : u8
{
    Translate, ///< Move with the cursor.
    Rotate,    ///< Turn about the pivot as the cursor goes round it.
    Scale,     ///< Scale by the cursor's distance from the pivot.
};

/// A modal transform: it starts at the cursor wherever it is and follows it without a button held, until the
/// caller confirms or cancels it. X, Y and Z constrain it to an axis, or with Shift to the plane of the other
/// two; pressing the same key again uses the active entity's axes, a third time removes the constraint.
class GizmoModal
{
public:
    /// Starts @p operation about @p pivot seen through @p camera with the cursor at @p cursor; @p local is
    /// the active entity's rotation (the axes of the second press). std::nullopt when the pivot is behind the
    /// camera or the image is empty.
    [[nodiscard]] static std::optional<GizmoModal> begin(GizmoModalOperation operation,
                                                         const GizmoCamera& camera, Vec3 pivot, Quat local,
                                                         Vec2 cursor) noexcept;

    /// Applies a press of X, Y or Z (@p axis 0 to 2), with Shift when @p plane is true. Rotations constrained
    /// to a plane turn about its normal.
    void constrain(u32 axis, bool plane) noexcept;

    /// The delta with the cursor at @p cursor: the numbers of @p typed when it is active, otherwise the
    /// cursor's, snapped to the increments of @p snap when it is not null.
    [[nodiscard]] GizmoDelta update(Vec2 cursor, const GizmoSnap* snap,
                                    const GizmoTypedInput& typed) noexcept;

    /// The operation.
    [[nodiscard]] GizmoModalOperation operation() const noexcept { return m_operation; }
    /// Bits of the constrained axes (bit i: axis i; 0: free).
    [[nodiscard]] u32 axes() const noexcept { return m_axes; }
    /// The constraint uses the active entity's axes.
    [[nodiscard]] bool local() const noexcept { return m_local; }
    /// The pivot and the axes of the constraint (pose().origin is the pivot).
    [[nodiscard]] const GizmoPose& pose() const noexcept { return m_pose; }

private:
    GizmoModal() = default;

    void restart() noexcept;

    GizmoModalOperation m_operation = GizmoModalOperation::Translate;
    GizmoCamera m_camera;
    GizmoPose m_pose;
    Quat m_localRotation{1.0f, 0.0f, 0.0f, 0.0f};
    Vec2 m_cursor{0.0f};      // cursor at the start
    Vec2 m_center{0.0f};      // the pivot in pixels
    f32 m_startRadius = 1.0f; // distance of the start cursor from the pivot in pixels (scale)
    u32 m_axes = 0;
    u32 m_key = 0; // the constraint key pressed last (0: none; axis + 1, plus 3 with Shift)
    bool m_local = false;
    std::optional<GizmoDrag> m_drag; // translations and rotations
};

/// Writes to @p out (the size of @p start) the world placements of @p start after @p delta of a gizmo at
/// @p pose with @p pivot: scaling along the gizmo's axes, then the rotation, then the translation.
/// Individual pivots scale and turn each entity about its own position.
void applyGizmoDelta(std::span<const Transform> start, const GizmoPose& pose, GizmoPivot pivot,
                     const GizmoDelta& delta, std::span<Transform> out) noexcept;

/// A selected entity that a gizmo drag moves.
struct GizmoTarget
{
    EntityGuid guid{};   ///< The entity.
    Transform placement; ///< Its world placement.
};

/// Fills @p out (cleared first; its capacity is kept) with the selected entities a gizmo moves, the active
/// one last: alive entities with a Transform or LocalTransform, without the descendants of other selected
/// entities (they follow their parents).
void gizmoTargets(EditorContext& editor, std::vector<GizmoTarget>& out);

/// @p value rounded to the nearest multiple of @p increment (unchanged for increments that are not
/// positive).
[[nodiscard]] f32 snapToIncrement(f32 value, f32 increment) noexcept;

} // namespace illusion::editor
