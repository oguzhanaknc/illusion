#pragma once

/// @file
/// The editor's undo history: steps of recorded operations with labels, origins and merge keys, under a
/// memory budget.

#include <illusion/core/Base.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/editor/core/Operation.hpp>

#include <span>
#include <string>
#include <vector>

namespace illusion::editor
{

/// Default memory budget of the undo history: 256 MiB.
inline constexpr usize kDefaultUndoBudget = usize{256} * 1024 * 1024;

/// One undoable step: the operations of a committed transaction.
struct UndoStep
{
    u64 id = 0;                              ///< Unique within the stack (assigned by push()).
    std::string label;                       ///< What it did, e.g. "Create entity".
    std::string origin;                      ///< Who did it: "ui", "inspector", "rpc:<client>", ...
    std::string mergeKey;                    ///< Continuous edits with the same key become one step.
    std::vector<Operation> operations;       ///< The operations in the order they were applied.
    std::vector<EntityGuid> selectionBefore; ///< Selection before the step (restored by undo).
    std::vector<EntityGuid> selectionAfter;  ///< Selection after the step (restored by redo).
    usize bytes = 0;                         ///< Memory used by the operations (assigned by push()).
};

/// What UndoStack::push() did.
struct UndoPushResult
{
    bool merged = false; ///< The step was merged into the previous one.
    usize trimmed = 0;   ///< Oldest steps dropped to stay within the budget.
};

/// A linear history of UndoSteps with a current position: steps before it are applied (undo goes back),
/// steps after it were undone (redo goes forward). The stack only stores steps; EditorContext applies them.
class UndoStack
{
public:
    /// A stack that keeps at most @p budget bytes of operations.
    explicit UndoStack(usize budget = kDefaultUndoBudget) noexcept
        : m_budget(budget)
    {
    }

    /// Adds @p step at the position and drops the steps that were undone.
    ///
    /// The step is merged into the previous one instead when both have the same non-empty merge key and
    /// origin, the previous one is the last step and it is not sealed (see seal()). Merging keeps the first
    /// "before" and the last "after" of each Component, so a drag of 100 frames undoes in one step back to
    /// the value before the drag. Then the oldest steps are dropped while the stack is over its budget (the
    /// newest step always stays).
    UndoPushResult push(UndoStep step);

    /// The step that undo() would revert, or nullptr.
    [[nodiscard]] const UndoStep* undoStep() const noexcept;

    /// The step that redo() would apply, or nullptr.
    [[nodiscard]] const UndoStep* redoStep() const noexcept;

    /// Moves the position one step back (after the caller reverted undoStep()).
    void stepBack() noexcept;

    /// Moves the position one step forward (after the caller applied redoStep()).
    void stepForward() noexcept;

    /// Prevents merging into the current last step (after a save, an undo or a selection of a new tool).
    void seal() noexcept { m_sealed = true; }

    /// Removes every step. Returns how many there were.
    usize clear() noexcept;

    /// All steps, oldest first.
    [[nodiscard]] std::span<const UndoStep> steps() const noexcept { return m_steps; }

    /// Number of applied steps (the index of the first undone one).
    [[nodiscard]] usize position() const noexcept { return m_position; }

    /// Memory used by all steps.
    [[nodiscard]] usize bytes() const noexcept { return m_bytes; }

    /// The memory budget.
    [[nodiscard]] usize budget() const noexcept { return m_budget; }

    /// Changes the budget; trims at the next push().
    void setBudget(usize budget) noexcept { m_budget = budget; }

    /// An id of the current state: the id of the last applied step, or of the newest step dropped below
    /// the stack (0 at the start). Saving remembers it to tell whether the document is dirty.
    [[nodiscard]] u64 stateId() const noexcept;

private:
    std::vector<UndoStep> m_steps;
    usize m_position = 0;
    usize m_bytes = 0;
    usize m_budget;
    u64 m_nextId = 1;
    u64 m_baseId = 0;
    bool m_sealed = false;
};

} // namespace illusion::editor
