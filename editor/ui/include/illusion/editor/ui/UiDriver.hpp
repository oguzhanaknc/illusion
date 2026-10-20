#pragma once

/// @file
/// Drives a UI from tests and automation: finds items by their stable "###id" and moves the mouse,
/// clicks, drags and types into Dear ImGui's input queue, running frames in between as a user would.
///
/// ```cpp
/// UiDriver driver(context, [&] { drawFrame(); });
/// driver.runFrames(2);
/// CHECK(driver.click("gallery.primary"));
/// driver.typeText("hello");
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/ui/UiItemRegistry.hpp>

#include <functional>
#include <optional>
#include <string_view>

namespace illusion::editor::ui
{

class UiContext;

/// Feeds input to a UiContext and runs frames. Attaches a UiItemRegistry to the context for its lifetime.
class UiDriver : NonCopyable
{
public:
    /// Runs one complete frame: ImGui::NewFrame(), the UI, ImGui::Render().
    using FrameFunction = std::function<void()>;

    /// Drives @p context (which must outlive the driver) with @p frame.
    UiDriver(UiContext& context, FrameFunction frame);

    /// Runs @p count frames.
    void runFrames(u32 count = 1);

    /// The item with @p stableId in the last frame.
    [[nodiscard]] std::optional<UiItem> find(std::string_view stableId) const;

    /// Moves the mouse to @p position (in window coordinates) and runs a frame.
    void moveMouse(Vec2 position);

    /// Moves the mouse over the item @p stableId and runs frames until hover state settles. Returns false
    /// if the item was not submitted in the last frame.
    bool hover(std::string_view stableId);

    /// Clicks the item @p stableId with @p button (0 left, 1 right, 2 middle). Returns false if the item
    /// was not submitted in the last frame.
    bool click(std::string_view stableId, int button = 0);

    /// Presses the left button on the item @p stableId, moves by @p delta in @p steps steps and releases.
    bool drag(std::string_view stableId, Vec2 delta, u32 steps = 4);

    /// Types @p utf8 as text input, then runs a frame.
    void typeText(std::string_view utf8);

    /// Presses and releases @p key (an ImGuiKey) with the modifiers in @p modifiers (ImGuiMod_* flags),
    /// running a frame after each step.
    void pressKey(int key, int modifiers = 0);

    /// The item registry, to inspect every item of the last frame.
    [[nodiscard]] const UiItemRegistry& registry() const noexcept { return m_registry; }

private:
    UiContext& m_context;
    FrameFunction m_frame;
    UiItemRegistry m_registry;
};

} // namespace illusion::editor::ui
