/**
 * @file BeatsmithEngine.h
 * @brief The complete sequencer engine: turns the host's transport and a pattern into drum notes.
 */
#pragma once

#include "Engine/BlockContext.h"
#include "Engine/MusicalTimeline.h"
#include "Engine/NoteEvent.h"
#include "Engine/Pattern.h"

#include <array>
#include <cstddef>

namespace beatsmith
{

/**
 * The JUCE-free sequencer engine.
 *
 * For every block it finds the steps that start inside it, computed from the song position (never counted),
 * and sends their notes at the right samples. It remembers every note it starts and guarantees its note-off:
 * at the end of its gate, before the same note starts again, and immediately when the transport stops, jumps
 * or loops, or when all notes are released (bypass). All methods are real-time safe: no memory is allocated
 * after construction, no locks are taken.
 */
class BeatsmithEngine
{
public:
    /** Size of the output list the plug-in passes to process(). */
    static constexpr std::size_t maximumEventsPerBlock = 4096;

    /** Notes that can sound at the same time; when full, the oldest note is ended to make room. */
    static constexpr std::size_t maximumSoundingNotes = 256;

    /** Trigs that can start in one block; further trigs in that block are skipped. */
    static constexpr std::size_t maximumTrigsPerBlock = 2048;

    BeatsmithEngine() = default;

    /** Replaces the pattern (a validated copy is kept). Sounding notes still end normally. */
    void setPattern (const Pattern& newPattern) noexcept;

    /** @return The pattern being played. */
    const Pattern& getPattern() const noexcept { return pattern; }

    /**
     * Produces the notes for one block.
     * @param context What the host reports for this block.
     * @param output  Receives the note events sorted by sample position; at the same sample they keep their
     *                musical order (a note-off before the note-on that retriggers the same note). It is cleared
     *                first. Its capacity should be maximumEventsPerBlock.
     */
    void process (const BlockContext& context, NoteEventList& output) noexcept;

    /**
     * Ends every sounding note at sample 0, e.g. while the plug-in is bypassed. The next processed block is
     * treated as a jump.
     */
    void releaseAllNotes (NoteEventList& output) noexcept;

    /** Makes the next processed block count as a jump (e.g. after the host reset the plug-in). */
    void markDiscontinuity() noexcept { isContinuingPreviousBlock = false; }

    /** @return How many notes are sounding right now. */
    std::size_t getNumberOfSoundingNotes() const noexcept { return numberOfSoundingNotes; }

    /** @return The timeline, for tests. */
    const MusicalTimeline& getTimeline() const noexcept { return timeline; }

private:
    /** A note that has been started and not yet ended. */
    struct SoundingNote
    {
        int channel = 1;
        int noteNumber = 0;
        double startInQuarterNotes = 0.0;
        double endInQuarterNotes = 0.0;
    };

    /** A trig found in the current range, waiting to be started in time order. */
    struct PendingTrig
    {
        double startInQuarterNotes = 0.0;
        int sampleOffset = 0;
        int laneIndex = 0;
        int stepIndex = 0;
    };

    /** Maps song positions inside one stretch of a block to sample offsets. */
    struct SampleMapping
    {
        double positionAtFirstSample = 0.0;
        int firstSample = 0;
        int endSample = 0; ///< One past the last sample of the stretch.
        double quarterNotesPerSample = 0.0;

        int toSampleOffset (double positionInQuarterNotes) const noexcept;
    };

    void processRange (double rangeStart, double rangeEnd, const SampleMapping& mapping,
                       NoteEventList& output) noexcept;
    void collectTrigs (double rangeStart, double rangeEnd, const RealignmentPeriod& period,
                       const SampleMapping& mapping) noexcept;
    void startNote (const PendingTrig& trig, NoteEventList& output) noexcept;
    void releaseNotesEndingBy (double limitInQuarterNotes, const SampleMapping& mapping,
                               NoteEventList& output) noexcept;
    void releaseAllNotesAt (int sampleOffset, NoteEventList& output) noexcept;
    void removeSoundingNote (std::size_t index) noexcept;

    Pattern pattern;
    MusicalTimeline timeline;

    std::array<SoundingNote, maximumSoundingNotes> soundingNotes {}; ///< Oldest first.
    std::size_t numberOfSoundingNotes = 0;

    std::array<PendingTrig, maximumTrigsPerBlock> pendingTrigs {};
    std::size_t numberOfPendingTrigs = 0;

    bool isContinuingPreviousBlock = false;
    double expectedNextBlockStart = 0.0; ///< Where the next block starts if playback runs on.
    double previousBlockStart = 0.0;     ///< Where the last processed block started.
    double previousBlockLength = 0.0;    ///< Its length in quarter notes.
};

} // namespace beatsmith
