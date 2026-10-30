#include "Engine/EditHistory.h"

namespace beatsmith
{

void EditHistory::recordChange (const Pattern& before, const Pattern& after)
{
    if (before == after)
        return;
    undoVersions.push_back (before);
    while (undoVersions.size() > maximumUndoSteps)
        undoVersions.pop_front();
    redoVersions.clear();
}

bool EditHistory::undo (const Pattern& current, Pattern& result)
{
    if (undoVersions.empty())
        return false;
    redoVersions.push_back (current);
    result = undoVersions.back();
    undoVersions.pop_back();
    return true;
}

bool EditHistory::redo (const Pattern& current, Pattern& result)
{
    if (redoVersions.empty())
        return false;
    undoVersions.push_back (current);
    while (undoVersions.size() > maximumUndoSteps)
        undoVersions.pop_front();
    result = redoVersions.back();
    redoVersions.pop_back();
    return true;
}

void EditHistory::clear() noexcept
{
    undoVersions.clear();
    redoVersions.clear();
}

} // namespace beatsmith
