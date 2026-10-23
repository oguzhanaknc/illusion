#include <illusion/editor/core/UndoStack.hpp>

namespace illusion::editor
{
namespace
{

usize stepBytes(const UndoStep& step) noexcept
{
    usize bytes = sizeof(UndoStep) + step.label.size() + step.origin.size() + step.mergeKey.size() +
                  (step.selectionBefore.size() + step.selectionAfter.size()) * sizeof(EntityGuid);
    for (const Operation& operation : step.operations)
    {
        bytes += operationBytes(operation);
    }
    return bytes;
}

// Merges @p next into @p step: a Component changed twice keeps its first "before" and last "after".
void merge(UndoStep& step, UndoStep&& next)
{
    for (Operation& operation : next.operations)
    {
        if (auto* change = std::get_if<ComponentChange>(&operation))
        {
            bool merged = false;
            for (auto it = step.operations.rbegin(); it != step.operations.rend(); ++it)
            {
                auto* earlier = std::get_if<ComponentChange>(&*it);
                if (earlier == nullptr)
                {
                    break; // an entity or file operation in between: keep the order
                }
                if (earlier->entity == change->entity && earlier->type == change->type)
                {
                    earlier->after = std::move(change->after);
                    merged = true;
                    break;
                }
            }
            if (merged)
            {
                continue;
            }
        }
        step.operations.push_back(std::move(operation));
    }
    step.selectionAfter = std::move(next.selectionAfter);
}

} // namespace

UndoPushResult UndoStack::push(UndoStep step)
{
    UndoPushResult result;
    for (usize i = m_position; i < m_steps.size(); ++i)
    {
        m_bytes -= m_steps[i].bytes;
    }
    const bool hadRedo = m_position < m_steps.size();
    m_steps.resize(m_position);

    UndoStep* top = m_steps.empty() ? nullptr : &m_steps.back();
    if (top != nullptr && !hadRedo && !m_sealed && !step.mergeKey.empty() && top->mergeKey == step.mergeKey &&
        top->origin == step.origin)
    {
        m_bytes -= top->bytes;
        merge(*top, std::move(step));
        top->bytes = stepBytes(*top);
        m_bytes += top->bytes;
        result.merged = true;
    }
    else
    {
        step.id = m_nextId++;
        step.bytes = stepBytes(step);
        m_bytes += step.bytes;
        m_steps.push_back(std::move(step));
        ++m_position;
    }
    m_sealed = false;

    usize drop = 0;
    while (m_bytes > m_budget && m_steps.size() - drop > 1)
    {
        m_bytes -= m_steps[drop].bytes;
        m_baseId = m_steps[drop].id;
        ++drop;
    }
    if (drop > 0)
    {
        m_steps.erase(m_steps.begin(), m_steps.begin() + static_cast<std::ptrdiff_t>(drop));
        m_position -= drop;
        result.trimmed = drop;
    }
    return result;
}

const UndoStep* UndoStack::undoStep() const noexcept
{
    return m_position > 0 ? &m_steps[m_position - 1] : nullptr;
}

const UndoStep* UndoStack::redoStep() const noexcept
{
    return m_position < m_steps.size() ? &m_steps[m_position] : nullptr;
}

void UndoStack::stepBack() noexcept
{
    if (m_position > 0)
    {
        --m_position;
        m_sealed = true;
    }
}

void UndoStack::stepForward() noexcept
{
    if (m_position < m_steps.size())
    {
        ++m_position;
        m_sealed = true;
    }
}

usize UndoStack::clear() noexcept
{
    const usize count = m_steps.size();
    m_steps.clear();
    m_position = 0;
    m_bytes = 0;
    m_baseId = m_nextId++; // a new state that no earlier save can match
    m_sealed = false;
    return count;
}

u64 UndoStack::stateId() const noexcept
{
    return m_position > 0 ? m_steps[m_position - 1].id : m_baseId;
}

} // namespace illusion::editor
