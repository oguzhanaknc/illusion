#pragma once

/// @file
/// The scene being edited: its file and its saved state.

#include <illusion/core/Base.hpp>

#include <string>
#include <string_view>

namespace illusion::editor
{

/// The open scene document. The entities live in EditorContext's World; the document knows where they are
/// saved and which undo state was saved last (to tell whether there are unsaved changes).
class SceneDocument
{
public:
    /// Virtual path of the scene file ("" until it is saved for the first time).
    [[nodiscard]] const std::string& path() const noexcept { return m_path; }

    /// True if the scene has never been saved.
    [[nodiscard]] bool untitled() const noexcept { return m_path.empty(); }

    /// Display name: the file name without extension, or "Untitled".
    [[nodiscard]] std::string_view displayName() const noexcept;

    /// The undo state (UndoStack::stateId()) of the last save or load.
    [[nodiscard]] u64 savedState() const noexcept { return m_savedState; }

    /// True if the undo state @p current differs from the saved one.
    [[nodiscard]] bool dirty(u64 current) const noexcept { return current != m_savedState; }

    /// Records a load or a save at @p path in undo state @p state.
    void markSaved(std::string path, u64 state)
    {
        m_path = std::move(path);
        m_savedState = state;
    }

private:
    std::string m_path;
    u64 m_savedState = 0;
};

} // namespace illusion::editor
