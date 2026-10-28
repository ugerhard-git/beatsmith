/**
 * @file BeatsmithEngine.h
 * @brief The complete sequencer engine: turns the host's transport into drum notes.
 *
 * Phase 0: the engine exists so the plug-in, the tests and CI are wired up end to end, but it produces no
 * notes yet. The pattern player arrives in phase 1 (see PLAN.md).
 */
#pragma once

#include "Engine/BlockContext.h"
#include "Engine/NoteEvent.h"

#include <cstddef>

namespace beatsmith
{

/** The JUCE-free sequencer engine. All methods except the constructor are real-time safe. */
class BeatsmithEngine
{
public:
    /** Upper limit of note events one block can produce; protects the host from runaway output. */
    static constexpr std::size_t maximumEventsPerBlock = 1024;

    BeatsmithEngine() = default;

    /**
     * Produces the notes for one block.
     * @param context What the host reports for this block.
     * @param output  Receives the note events, sorted by sample position. It is cleared first.
     */
    void process (const BlockContext& context, NoteEventList& output);

    /**
     * Ends every note the engine has started, e.g. when the plug-in is bypassed or released.
     * @param output Receives the note-offs at sample position 0.
     */
    void releaseAllNotes (NoteEventList& output);
};

} // namespace beatsmith
