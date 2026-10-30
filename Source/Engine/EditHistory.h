/**
 * @file EditHistory.h
 * @brief Undo and redo of pattern edits.
 */
#pragma once

#include "Engine/Pattern.h"

#include <cstddef>
#include <deque>

namespace beatsmith
{

/**
 * Remembers earlier versions of the pattern for undo and redo.
 *
 * Each recorded change stores the complete pattern from before it (about 13 kB), which keeps undo simple and
 * exact. The history is capped; the oldest versions are dropped first. Used on the message thread only.
 */
class EditHistory
{
public:
    /** Default number of undo steps kept. */
    static constexpr std::size_t defaultMaximumUndoSteps = 100;

    explicit EditHistory (std::size_t maximumUndoStepsToKeep = defaultMaximumUndoSteps)
        : maximumUndoSteps (maximumUndoStepsToKeep < 1 ? 1 : maximumUndoStepsToKeep)
    {
    }

    /**
     * Records that the pattern changed from `before` to `after`. Nothing is recorded if they are equal. A new
     * change makes the redo history disappear, as in every editor.
     */
    void recordChange (const Pattern& before, const Pattern& after);

    /** @return True if there is something to undo. */
    bool canUndo() const noexcept { return ! undoVersions.empty(); }

    /** @return True if there is something to redo. */
    bool canRedo() const noexcept { return ! redoVersions.empty(); }

    /**
     * Steps back one change.
     * @param current The pattern as it is now (kept for redo).
     * @param result  Receives the pattern before the last change.
     * @return False if there was nothing to undo (result is unchanged).
     */
    bool undo (const Pattern& current, Pattern& result);

    /**
     * Steps forward again after an undo.
     * @param current The pattern as it is now (kept for undo).
     * @param result  Receives the pattern after the undone change.
     * @return False if there was nothing to redo (result is unchanged).
     */
    bool redo (const Pattern& current, Pattern& result);

    /** Forgets all versions, e.g. when a different project state is loaded. */
    void clear() noexcept;

    /** @return How many undo steps are stored (for tests). */
    std::size_t getNumberOfUndoSteps() const noexcept { return undoVersions.size(); }

private:
    std::size_t maximumUndoSteps;
    std::deque<Pattern> undoVersions; ///< Oldest first.
    std::deque<Pattern> redoVersions; ///< Most recently undone last.
};

} // namespace beatsmith
