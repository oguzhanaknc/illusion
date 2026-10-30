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

/// What a viewport looks through.
enum class ViewportKind : u8
{
    Scene, ///< An editor camera ("scene" and the secondary viewports).
    Game,  ///< The scene's highest-priority active Camera ("game", the Game panel).
};

/// Shape of a viewport's image inside its panel; the rest of the panel is letterboxed.
enum class ViewportAspect : u8
{
    Free,          ///< The whole panel.
    Wide16x9,      ///< 16:9.
    Wide16x10,     ///< 16:10.
    Standard4x3,   ///< 4:3.
    Ultrawide21x9, ///< 21:9.
    Square,        ///< 1:1.
    Fixed,         ///< A fixed resolution (ViewportSettings::width and height), scaled down to fit the panel.
};

/// What a readback of ViewportHost::requestReadback() reads.
enum class ViewportChannel : u8
{
    Color,    ///< The displayed RGBA8 image (sRGB-encoded, overlays included).
    Entities, ///< The entities drawn in the region (visible, not hidden or locked).
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

/// Which entities the DebugDraw visualisers of a Scene viewport outline.
enum class ViewportVisualiserScope : u8
{
    Selected, ///< Only the selected entities.
    All,      ///< Every visible entity.
};

/// The kinds of DebugDraw visualisers a Scene viewport draws.
struct ViewportVisualisers
{
    bool lights = true;     ///< Point light ranges and directional light directions.
    bool cameras = true;    ///< Camera frusta.
    bool colliders = true;  ///< Collider shapes.
    bool characters = true; ///< CharacterController capsules.
    bool joints = true;     ///< Joint anchors, axes and limits.
    bool audio = true;      ///< AudioSource distances and cones.
};

/// What a viewport draws.
struct ViewportSettings
{
    ViewportShading shading = ViewportShading::Lit; ///< Shading mode.
    bool wireframe = false;                         ///< Wireframe drawn over opaque surfaces.
    bool grid = true;                               ///< Ground grid.
    bool selectionOutline = true;                   ///< Outline around selected entities.
    bool hover = true;                              ///< Highlight of the entity under the cursor.
    bool icons = true;               ///< Icons of lights, cameras and audio entities (Scene viewports).
    bool viewCube = true;            ///< View cube and axis navigator (Scene viewports).
    bool stats = false;              ///< Statistics of the last frame in a corner.
    bool cameraPreview = true;       ///< Picture-in-picture view through a selected Camera (Scene viewports).
    ViewportVisualisers visualisers; ///< Visualiser kinds.
    ViewportVisualiserScope visualiserScope = ViewportVisualiserScope::Selected; ///< Visualised entities.
    ViewportAspect aspect = ViewportAspect::Free;                                ///< Shape of the image.
    u32 width = 0;  ///< Image width with ViewportAspect::Fixed, 16 to 7,680 (0 otherwise).
    u32 height = 0; ///< Image height with ViewportAspect::Fixed, 16 to 4,320 (0 otherwise).
};

/// A viewport of `viewport.list`.
struct ViewportInfo
{
    std::string id;                          ///< Stable id: "scene", "game", or "scene-2" to "scene-4".
    ViewportKind kind = ViewportKind::Scene; ///< What it looks through.
    UVec2 size{0};             ///< Size of the rendered image in pixels (0, 0 before the first frame).
    bool visible = false;      ///< Drawn in the last UI frame.
    ViewportCamera camera;     ///< The editor camera (Scene viewports).
    Vec3 eye{0.0f};            ///< Position the image is seen from.
    ViewportSettings settings; ///< What it draws.
    Entity sceneCamera{};      ///< The Camera a Game viewport looks through (null without one).
    Mat4 view{1.0f};           ///< World to view of the image.
    Mat4 projection{1.0f};     ///< View to clip of the image (reversed-Z).
};

/// The pixels or entities of a viewport region (see ViewportHost::requestReadback()).
struct ViewportReadback
{
    UVec2 size{0};                ///< Size of the whole image.
    UVec2 origin{0};              ///< Top left of the region in image pixels.
    UVec2 extent{0};              ///< Size of the region.
    std::vector<u8> color;        ///< ViewportChannel::Color: RGBA8 rows of the region, top row first.
    std::vector<Entity> entities; ///< ViewportChannel::Entities: each entity once, by Entity index.
};

/// What the renderer did in a viewport's last frame (`render.stats`).
struct ViewportRenderStats
{
    u32 frames = 0;          ///< Frames rendered since the viewport was created.
    u32 candidates = 0;      ///< RenderMesh entities considered.
    u32 visible = 0;         ///< Entities that passed frustum culling.
    u32 drawCalls = 0;       ///< Instanced draw calls.
    u32 triangles = 0;       ///< Triangles submitted (saturates at 4,294,967,295).
    u32 shadowCasters = 0;   ///< Shadow-caster instances, summed over cascades.
    u32 shadowDrawCalls = 0; ///< Draw calls for shadows.
    u32 lightsDropped = 0;   ///< Active lights beyond the renderer's limits.
};

/// The GPU behind the viewports (`render.gpu_info`, `render.diagnostics`).
struct ViewportGpuInfo
{
    std::string name;         ///< Device name.
    std::string driver;       ///< Driver name.
    u32 vendorId = 0;         ///< PCI vendor id.
    u32 deviceId = 0;         ///< PCI device id.
    std::string apiVersion;   ///< Vulkan version, e.g. "1.3.280".
    bool discrete = false;    ///< A discrete GPU.
    bool software = false;    ///< A CPU implementation such as lavapipe.
    bool bindless = false;    ///< Bindless textures are used.
    bool validation = false;  ///< The Vulkan validation layer is enabled.
    u32 validationErrors = 0; ///< Validation errors reported since the device was created.
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
    bool ctrl = false;             ///< Ctrl held.
    bool orbit = false;            ///< A left drag orbits whatever the preset (it started on the view cube).
    Vec3 move{0.0f};               ///< Fly keys: x = D - A, y = E - Q, z = W - S (each -1, 0 or 1).
    f32 deltaSeconds = 0.0f;       ///< Length of the frame.
};

/// Colours of the overlays (display RGBA, sRGB-encoded like the UI theme's colours).
struct ViewportStyle
{
    Vec4 selection{1.0f, 0.62f, 0.1f, 1.0f}; ///< Selection outline.
    Vec4 hover{1.0f, 0.82f, 0.54f, 1.0f};    ///< Hover highlight.
    Vec4 grid{0.5f, 0.5f, 0.5f, 0.5f};       ///< Grid lines.
    Vec4 axisX{0.94f, 0.32f, 0.31f, 1.0f};   ///< The x axis of the grid.
    Vec4 axisY{0.48f, 0.8f, 0.16f, 1.0f};    ///< The y axis (orthographic side views).
    Vec4 axisZ{0.24f, 0.55f, 0.96f, 1.0f};   ///< The z axis of the grid.
};

/// What an icon billboard stands for.
enum class ViewportIconKind : u8
{
    Camera,           ///< A Camera.
    DirectionalLight, ///< A DirectionalLight.
    PointLight,       ///< A PointLight.
    SkyLight,         ///< A SkyLight or ProceduralSky.
    AudioSource,      ///< An AudioSource.
    AudioListener,    ///< An AudioListener.
};

/// An icon billboard of a Scene viewport: where an entity without a visible surface is.
struct ViewportIcon
{
    Entity entity{};                                  ///< The entity.
    ViewportIconKind kind = ViewportIconKind::Camera; ///< What it is (the first match in the enum order).
    Vec2 pixel{0.0f};                                 ///< Its position in image pixels.
    f32 depth = 0.0f;                                 ///< Reversed-Z depth (larger is nearer).
    bool selected = false;                            ///< The entity is selected.
};

/// The world-space box around the Mesh an entity draws (ViewportHost::meshBounds()).
struct ViewportBounds
{
    Entity entity{}; ///< The entity.
    Aabb bounds;     ///< Its box.
};

/// What ViewportHost::requestSnap() looks for.
enum class ViewportSnapTarget : u8
{
    Any,     ///< A vertex within the tolerance, otherwise the surface under the pixel.
    Vertex,  ///< The mesh vertex that appears nearest to the pixel.
    Surface, ///< The opaque surface under the pixel.
};

/// A request of ViewportHost::requestSnap().
struct ViewportSnapRequest
{
    Vec2 pixel{0.0f};                                    ///< Image pixel (origin at the top left, y down).
    ViewportSnapTarget target = ViewportSnapTarget::Any; ///< What to look for.
    f32 tolerance = 12.0f; ///< Largest distance of a vertex on screen in pixels.
    /// Entities left out with their descendants, e.g. what is being moved (borrowed for the call).
    std::span<const Entity> exclude;
};

/// A point that ViewportHost::requestSnap() found.
struct ViewportSnap
{
    ViewportSnapTarget kind = ViewportSnapTarget::Surface; ///< Vertex or Surface.
    Vec3 position{0.0f};                                   ///< World position.
    Vec3 normal{0.0f}; ///< Unit surface normal facing the camera (surfaces; zero for vertices).
    Entity entity{};   ///< The entity whose mesh or surface it is.
};

/// What a viewport panel draws this frame.
struct ViewportFrame
{
    u64 texture = 0;           ///< ImGui texture id of the image (0: nothing to draw yet).
    UVec2 size{0};             ///< Size of the image (it differs from the panel while a resize settles).
    bool navigating = false;   ///< The input moved the camera (a click then does not pick).
    ViewportSettings settings; ///< What the viewport draws (for the panel's menus).
    ViewportProjection projection = ViewportProjection::Perspective; ///< Projection of its camera.
    ViewportKind kind = ViewportKind::Scene;                         ///< What the viewport looks through.
    bool hasCamera = true; ///< A Game viewport found a Camera (Scene viewports always have one).
    ViewportCamera camera; ///< The editor camera as shown now (Scene viewports; for the view cube).
    Entity hovered{};      ///< The pickable entity under the cursor (null for none or while navigating).
    /// Icon billboards, farthest first (Scene viewports with ViewportSettings::icons); valid until the next
    /// frame() of any viewport.
    std::span<const ViewportIcon> icons;
    u64 preview = 0;        ///< ImGui texture id of the picture-in-picture camera view (0: none).
    UVec2 previewSize{0};   ///< Its size in pixels.
    Entity previewCamera{}; ///< The Camera it looks through.
};

/// Smallest camera distance and orthographic height.
inline constexpr f32 kViewportMinExtent = 0.01f;
/// Largest camera distance and orthographic height (and distance of the pivot from the origin).
inline constexpr f32 kViewportMaxExtent = 100000.0f;
/// Secondary Scene viewports ("scene-2" ...) that can be open at once.
inline constexpr u32 kMaxSecondaryViewports = 3;
/// Smallest fixed viewport resolution.
inline constexpr u32 kViewportMinResolution = 16;
/// Largest fixed viewport width.
inline constexpr u32 kViewportMaxWidth = 7680;
/// Largest fixed viewport height.
inline constexpr u32 kViewportMaxHeight = 4320;

/// Size of the image of a viewport drawing @p settings in a panel of @p panel pixels: the panel for Free, the
/// largest rectangle of the aspect for the presets, and the fixed resolution for Fixed (whatever the panel).
/// Never 0 in a dimension when @p panel is not empty.
[[nodiscard]] UVec2 viewportImageSize(UVec2 panel, const ViewportSettings& settings) noexcept;

/// Where @p point appears in an image of @p size pixels rendered with @p viewProjection.
[[nodiscard]] ViewportPoint projectToViewport(const Mat4& viewProjection, UVec2 size, Vec3 point) noexcept;

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
    /// previous one returns the next candidate. The ids are read once the assets of the viewport have
    /// loaded. Returns a ticket for pick().
    [[nodiscard]] virtual Expected<u64, CommandError> requestPick(std::string_view id, Vec2 pixel,
                                                                  bool cycle) = 0;

    /// The result of @p ticket once it is read (std::nullopt while pending): the hit, or nothing for the
    /// background. Each ticket is answered once.
    [[nodiscard]] virtual std::optional<Expected<std::optional<ViewportHit>, CommandError>>
    pick(u64 ticket) = 0;

    /// Draws @p id in a panel of @p size pixels this frame after applying @p input to its camera (each
    /// viewport panel calls it once per frame). The image has viewportImageSize() and is centred in the
    /// panel. Unknown ids return an empty frame.
    [[nodiscard]] virtual ViewportFrame frame(std::string_view id, UVec2 size, const ViewportInput& input,
                                              const ViewportStyle& style) = 0;

    /// Opens a secondary Scene viewport with the camera and settings of "scene" and returns its id
    /// ("scene-2" to "scene-4"); LimitExceeded when kMaxSecondaryViewports are open. Secondary viewports
    /// render fewer and smaller shadow cascades.
    [[nodiscard]] virtual Expected<std::string, CommandError> createViewport() = 0;

    /// Grows whenever a viewport opens or closes (panels refresh their list of viewports then).
    [[nodiscard]] virtual u64 viewportsVersion() const noexcept = 0;

    /// Closes the secondary viewport @p id; NotFound for unknown ids, PreconditionFailed for "scene" and
    /// "game".
    [[nodiscard]] virtual Expected<void, CommandError> closeViewport(std::string_view id) = 0;

    /// Starts reading @p channel in the region at @p origin of @p extent image pixels of @p id (validated by
    /// the command: inside the image). The viewport is rendered for it even if its panel is hidden, and read
    /// once the assets it shows have loaded. Returns a ticket for readback().
    [[nodiscard]] virtual Expected<u64, CommandError>
    requestReadback(std::string_view id, ViewportChannel channel, UVec2 origin, UVec2 extent) = 0;

    /// The result of @p ticket once it is read (std::nullopt while pending). Each ticket is answered once.
    [[nodiscard]] virtual std::optional<Expected<ViewportReadback, CommandError>> readback(u64 ticket) = 0;

    /// What the renderer did in the last frame of @p id; NotFound for unknown ids.
    [[nodiscard]] virtual Expected<ViewportRenderStats, CommandError>
    renderStats(std::string_view id) const = 0;

    /// The GPU the viewports render with.
    [[nodiscard]] virtual ViewportGpuInfo gpuInfo() const = 0;

    /// Fills @p out (cleared first) with the world-space box of every entity that draws a Mesh whose asset
    /// has loaded and that is not hidden or inactive, once each, with the placements of the World as it is
    /// now (the arranging commands and drop to ground use them).
    virtual void meshBounds(std::vector<ViewportBounds>& out) = 0;

    /// Starts looking for the point of @p request in @p id (validated by the command: inside the image):
    /// vertices of the meshes that MeshRenderers draw, from CPU copies loaded on first use, and the surface
    /// from a depth readback of a small view around the pixel. Hidden and inactive entities, and those of
    /// ViewportSnapRequest::exclude, are left out. Returns a ticket for snap().
    [[nodiscard]] virtual Expected<u64, CommandError> requestSnap(std::string_view id,
                                                                  const ViewportSnapRequest& request) = 0;

    /// The result of @p ticket once it is found (std::nullopt while pending): the point, or nothing when no
    /// vertex is within the tolerance and no surface is under the pixel. Each ticket is answered once.
    [[nodiscard]] virtual std::optional<Expected<std::optional<ViewportSnap>, CommandError>>
    snap(u64 ticket) = 0;
};

} // namespace illusion::editor
