#pragma once
/// @file
/// What the UI commands (`ui.state`, `ui.show_panel`, ...) and `editor.screenshot` need from the editor's
/// user interface. The editor window (and the offscreen UI of a headless editor) implements it and registers
/// with EditorContext::setUi(); without one, those commands fail with ErrorKind::Unsupported.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/core/CommandError.hpp>
#include <illusion/editor/core/Gizmo.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor
{

/// A panel or window of the UI.
struct UiPanelInfo
{
    std::string id;      ///< Stable id, e.g. "panel.console".
    std::string title;   ///< Localized title.
    bool open = false;   ///< Shown.
    bool docked = false; ///< Part of the dock layout (windows such as Preferences are not).
};

/// A layout of the dock space.
struct UiLayoutInfo
{
    std::string name;     ///< Name, e.g. "default" or a name the user saved.
    bool builtIn = false; ///< One of the built-in layouts (cannot be deleted).
    bool active = false;  ///< The layout applied last.
};

/// Severity of a notification.
enum class UiNotice : u8
{
    Info,
    Success,
    Warning,
    Error,
};

/// A captured frame of the UI.
struct UiScreenshot
{
    UVec2 size{0};   ///< Width and height in pixels.
    std::string png; ///< The image, PNG-encoded.
};

/// The UI behind the `ui.*` commands. Called on the main thread from command handlers.
class UiHost
{
public:
    virtual ~UiHost() = default;

    /// Every panel and window.
    [[nodiscard]] virtual std::vector<UiPanelInfo> panels() const = 0;
    /// Shows (and focuses) or hides the panel @p id; NotFound for unknown ids.
    [[nodiscard]] virtual Expected<void, CommandError> showPanel(std::string_view id, bool open) = 0;

    /// The built-in and saved layouts.
    [[nodiscard]] virtual std::vector<UiLayoutInfo> layouts() const = 0;
    /// Applies the layout @p name from the next frame on; NotFound for unknown names.
    [[nodiscard]] virtual Expected<void, CommandError> applyLayout(std::string_view name) = 0;
    /// Saves the current arrangement as the user's layout @p name (letters, digits, '-', '_').
    [[nodiscard]] virtual Expected<void, CommandError> saveLayout(std::string_view name) = 0;
    /// Deletes the user's layout @p name; built-in layouts cannot be deleted.
    [[nodiscard]] virtual Expected<void, CommandError> deleteLayout(std::string_view name) = 0;
    /// Restores the built-in arrangement of the active layout (the default layout for saved ones).
    virtual void resetLayout() = 0;

    /// Opens the command palette in @p mode ("commands", "entities" or "settings") with @p query typed.
    [[nodiscard]] virtual Expected<void, CommandError> openPalette(std::string_view mode,
                                                                   std::string_view query) = 0;
    /// Shows a toast and adds it to the notification centre.
    virtual void notify(UiNotice notice, std::string_view message) = 0;
    /// Asks the editor to close (as the window's close button does).
    virtual void requestQuit() = 0;
    /// Starts a modal @p operation of the selection in the Scene view under the cursor (the main one when the
    /// cursor is over none) from the next frame on; PreconditionFailed when no Scene view is open.
    [[nodiscard]] virtual Expected<void, CommandError> beginModalTransform(GizmoModalOperation operation) = 0;

    /// Starts capturing the next frame at @p size (0, 0: the window's size). Returns a ticket for
    /// screenshot(); Unsupported without a GPU.
    [[nodiscard]] virtual Expected<u64, CommandError> requestScreenshot(UVec2 size) = 0;
    /// The capture of @p ticket once it is done (std::nullopt while pending). Each ticket is answered once.
    [[nodiscard]] virtual std::optional<Expected<UiScreenshot, CommandError>> screenshot(u64 ticket) = 0;
};

} // namespace illusion::editor
