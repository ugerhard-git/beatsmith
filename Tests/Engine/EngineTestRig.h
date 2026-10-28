/**
 * @file EngineTestRig.h
 * @brief A simulated host for the engine tests: it plays the engine block by block, like a DAW, and collects
 *        the notes with absolute sample times.
 */
#pragma once

#include "Engine/BeatsmithEngine.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <vector>

namespace beatsmith::testrig
{

/** A note event with its absolute sample time since the start of rendering. */
struct TimedEvent
{
    std::int64_t sample = 0;
    int channel = 0;
    int noteNumber = 0;
    int velocity = 0;
    bool isNoteOn = false;

    bool operator== (const TimedEvent& other) const
    {
        return sample == other.sample && channel == other.channel && noteNumber == other.noteNumber &&
               velocity == other.velocity && isNoteOn == other.isNoteOn;
    }
};

/** One time-signature region of the simulated host's song, starting at a bar line. */
struct HostMeterRegion
{
    double startInQuarterNotes = 0.0;
    int numerator = 4;
    int denominator = 4;
};

/** A simulated host: tempo, sample rate, time signatures and what it reports to the plug-in. */
struct SimulatedHost
{
    double sampleRate = 48000.0;
    double tempoBeatsPerMinute = 120.0;
    std::vector<HostMeterRegion> meterRegions { HostMeterRegion {} }; ///< Sorted by start; first starts at 0.
    bool reportsLastBarStart = true;
    bool reportsTimeSignature = true;

    bool isLooping = false;
    double loopStartInQuarterNotes = 0.0;
    double loopEndInQuarterNotes = 0.0;
    bool splitsBlocksAtLoopEnd = true; ///< Reaper-like; false lets the engine handle the wrap inside a block.

    double getQuarterNotesPerSample() const { return tempoBeatsPerMinute / 60.0 / sampleRate; }

    /** @return The context the host would report for a block starting at this position. */
    BlockContext makeContext (double positionInQuarterNotes, int numSamples) const
    {
        BlockContext context;
        context.sampleRate = sampleRate;
        context.numSamples = numSamples;
        context.hostIsPlaying = true;
        context.tempoBeatsPerMinute = tempoBeatsPerMinute;
        context.hasPositionInQuarterNotes = true;
        context.positionInQuarterNotes = positionInQuarterNotes;

        const HostMeterRegion* region = &meterRegions.front();
        for (const auto& candidate : meterRegions)
            if (candidate.startInQuarterNotes <= positionInQuarterNotes + 1.0e-9)
                region = &candidate;
        const double barLength = 4.0 * region->numerator / region->denominator;
        const double barsIn = std::floor ((positionInQuarterNotes - region->startInQuarterNotes) / barLength + 1.0e-9);

        context.hasTimeSignature = reportsTimeSignature;
        context.timeSignatureNumerator = region->numerator;
        context.timeSignatureDenominator = region->denominator;
        context.hasLastBarStart = reportsLastBarStart;
        context.lastBarStartInQuarterNotes = region->startInQuarterNotes + barsIn * barLength;

        context.isLooping = isLooping;
        context.hasLoopPoints = isLooping;
        context.loopStartInQuarterNotes = loopStartInQuarterNotes;
        context.loopEndInQuarterNotes = loopEndInQuarterNotes;
        return context;
    }
};

/** Chooses the size of the next block; the argument is the block's index. */
using BlockSizeChooser = std::function<int (int blockIndex)>;

inline BlockSizeChooser fixedBlockSize (int size)
{
    return [size] (int) { return size; };
}

/**
 * Plays the engine from a position for a number of samples, block by block, as a host would (including loop
 * wrapping), and returns all events with absolute sample times.
 */
inline std::vector<TimedEvent> render (BeatsmithEngine& engine, const SimulatedHost& host, double startPosition,
                                       std::int64_t totalSamples, const BlockSizeChooser& chooseBlockSize)
{
    std::vector<TimedEvent> collected;
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    const double quarterNotesPerSample = host.getQuarterNotesPerSample();

    double position = startPosition;
    std::int64_t samplesDone = 0;
    for (int blockIndex = 0; samplesDone < totalSamples; ++blockIndex)
    {
        int blockSize = (int) std::min<std::int64_t> (chooseBlockSize (blockIndex), totalSamples - samplesDone);

        // A host that splits blocks ends this block exactly at the loop end.
        if (host.isLooping && host.splitsBlocksAtLoopEnd && position < host.loopEndInQuarterNotes)
        {
            const auto samplesToLoopEnd =
                (std::int64_t) std::llround ((host.loopEndInQuarterNotes - position) / quarterNotesPerSample);
            if (samplesToLoopEnd > 0 && samplesToLoopEnd < blockSize)
                blockSize = (int) samplesToLoopEnd;
        }

        engine.process (host.makeContext (position, blockSize), output);
        for (const auto& event : output.getEvents())
            collected.push_back ({ samplesDone + event.samplePosition, event.channel, event.noteNumber, event.velocity,
                                   event.isNoteOn });

        samplesDone += blockSize;
        position += blockSize * quarterNotesPerSample;
        if (host.isLooping && position >= host.loopEndInQuarterNotes - 1.0e-9)
            position -= host.loopEndInQuarterNotes - host.loopStartInQuarterNotes;
    }
    return collected;
}

/** @return Only the note-ons of a list of events. */
inline std::vector<TimedEvent> noteOnsOf (const std::vector<TimedEvent>& events)
{
    std::vector<TimedEvent> noteOns;
    for (const auto& event : events)
        if (event.isNoteOn)
            noteOns.push_back (event);
    return noteOns;
}

/** @return The sample times of the note-ons of one note. */
inline std::vector<std::int64_t> noteOnTimesOf (const std::vector<TimedEvent>& events, int noteNumber)
{
    std::vector<std::int64_t> times;
    for (const auto& event : events)
        if (event.isNoteOn && event.noteNumber == noteNumber)
            times.push_back (event.sample);
    return times;
}

/** @return True if both lists have the same events, with sample times differing by at most one sample. */
inline bool haveSameEventsWithinOneSample (const std::vector<TimedEvent>& first, const std::vector<TimedEvent>& second)
{
    if (first.size() != second.size())
        return false;
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        const auto& left = first[index];
        const auto& right = second[index];
        if (std::llabs (left.sample - right.sample) > 1 || left.channel != right.channel ||
            left.noteNumber != right.noteNumber || left.velocity != right.velocity || left.isNoteOn != right.isNoteOn)
            return false;
    }
    return true;
}

/** @return A pattern with one lane (note 36, channel 10) whose listed steps are on. */
inline Pattern makeSingleLanePattern (StepRate rate, int length, std::initializer_list<int> stepsOn)
{
    Pattern pattern;
    pattern.numberOfLanes = 1;
    auto& lane = pattern.lanes[0];
    lane.rate = rate;
    lane.length = length;
    for (const int stepIndex : stepsOn)
        lane.steps[(std::size_t) stepIndex].isOn = true;
    return pattern;
}

} // namespace beatsmith::testrig
