#include "Engine/BeatsmithEngine.h"

#include <algorithm>
#include <cmath>

namespace beatsmith
{

namespace
{
constexpr double tolerance = MusicalTimeline::positionTolerance;

/** Safety nets against endless loops if a host sends absurd values. */
constexpr int maximumPeriodsPerRange = 1024;
constexpr int maximumStepsPerLaneAndPart = 1 << 16;
constexpr double largestUsablePosition = 1.0e9;

/** @return value modulo divisor, always in 0 … divisor - 1. */
std::int64_t wrapIndex (std::int64_t value, std::int64_t divisor) noexcept
{
    const auto remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

} // namespace

int BeatsmithEngine::SampleMapping::toSampleOffset (double positionInQuarterNotes) const noexcept
{
    const double samplesFromFirst = (positionInQuarterNotes - positionAtFirstSample) / quarterNotesPerSample;
    const auto offset =
        firstSample + (std::int64_t) std::llround (std::isfinite (samplesFromFirst) ? samplesFromFirst : 0.0);
    return (int) std::clamp<std::int64_t> (offset, firstSample, std::max (firstSample, endSample - 1));
}

void BeatsmithEngine::setPattern (const Pattern& newPattern) noexcept
{
    pattern = newPattern;
    sanitizePattern (pattern);
}

void BeatsmithEngine::releaseAllNotes (NoteEventList& output) noexcept
{
    output.clear();
    releaseAllNotesAt (0, output);
    isContinuingPreviousBlock = false;
    playingStepNumbers.fill (noStepPlaying);
}

void BeatsmithEngine::process (const BlockContext& context, NoteEventList& output) noexcept
{
    output.clear();

    // Positions beyond about 16 years at 120 BPM lose the precision needed for short steps.
    const bool hasUsableTiming = context.hasPositionInQuarterNotes && std::isfinite (context.positionInQuarterNotes) &&
                                 std::abs (context.positionInQuarterNotes) < largestUsablePosition &&
                                 context.sampleRate > 0.0 && std::isfinite (context.sampleRate) &&
                                 std::isfinite (context.tempoBeatsPerMinute) && context.tempoBeatsPerMinute > 0.0 &&
                                 context.tempoBeatsPerMinute <= 10000.0;
    if (! context.hostIsPlaying || ! hasUsableTiming)
    {
        // Stopped (or no usable position): end everything and wait.
        releaseAllNotesAt (0, output);
        isContinuingPreviousBlock = false;
        playingStepNumbers.fill (noStepPlaying);
        return;
    }
    if (context.numSamples <= 0)
        return;

    const double quarterNotesPerSample = context.tempoBeatsPerMinute / 60.0 / context.sampleRate;
    double blockStart = context.positionInQuarterNotes;
    double firstRangeStart = blockStart - 0.5 * quarterNotesPerSample;
    bool continuesPreviousBlock = false;

    if (isContinuingPreviousBlock)
    {
        const double deviation = blockStart - expectedNextBlockStart;
        const double driftLimit = std::max (0.25, 2.0 * previousBlockLength);
        if (std::abs (deviation) <= 2.0 * quarterNotesPerSample + tolerance)
        {
            // Seamless playback: continue exactly where the last block ended, so no step falls between blocks.
            blockStart = expectedNextBlockStart;
            firstRangeStart = blockStart;
            continuesPreviousBlock = true;
        }
        else if (std::abs (deviation) <= driftLimit)
        {
            // A small deviation, e.g. a tempo change inside the last block (hosts report one tempo per block).
            // Carry on from where the last block ended: steps the host has already passed come late at sample 0,
            // steps already played are not repeated.
            firstRangeStart = expectedNextBlockStart;
            continuesPreviousBlock = true;
        }
    }
    if (! continuesPreviousBlock)
        releaseAllNotesAt (0, output); // start, jump or loop

    const double newMeterStart = timeline.observeBlockStart (context, pattern);

    const double blockEnd = blockStart + context.numSamples * quarterNotesPerSample;
    const SampleMapping wholeBlock { blockStart, 0, context.numSamples, quarterNotesPerSample };

    // The host reports a time-signature change only from the first block after it. If it happened inside the
    // last block, play the steps since the change now (slightly late) instead of losing them.
    if (continuesPreviousBlock && std::isfinite (newMeterStart) && newMeterStart < firstRangeStart - tolerance &&
        newMeterStart >= previousBlockStart - tolerance)
        processRange (newMeterStart, firstRangeStart, wholeBlock, output);

    previousBlockStart = blockStart;
    previousBlockLength = context.numSamples * quarterNotesPerSample;

    const double loopStart = context.loopStartInQuarterNotes;
    const double loopEnd = context.loopEndInQuarterNotes;
    const bool loopWrapsInsideBlock = context.isLooping && context.hasLoopPoints && std::isfinite (loopStart) &&
                                      std::isfinite (loopEnd) && loopEnd > loopStart + tolerance &&
                                      blockStart < loopEnd - tolerance && blockEnd > loopEnd + tolerance;

    if (loopWrapsInsideBlock)
    {
        // The host did not split the block at the loop end: play up to the loop end, then from the loop start.
        const auto wrapSample = (int) std::clamp<long long> (
            std::llround ((loopEnd - blockStart) / quarterNotesPerSample), 0, context.numSamples);
        const SampleMapping beforeWrap { blockStart, 0, wrapSample, quarterNotesPerSample };
        processRange (firstRangeStart, loopEnd, beforeWrap, output);
        releaseAllNotesAt (std::min (wrapSample, context.numSamples - 1), output);

        const double afterWrapEnd = loopStart + (context.numSamples - wrapSample) * quarterNotesPerSample;
        const SampleMapping afterWrap { loopStart, wrapSample, context.numSamples, quarterNotesPerSample };
        if (wrapSample < context.numSamples)
            processRange (loopStart - 0.5 * quarterNotesPerSample, afterWrapEnd, afterWrap, output);
        expectedNextBlockStart = afterWrapEnd;
    }
    else
    {
        processRange (firstRangeStart, blockEnd, wholeBlock, output);
        expectedNextBlockStart = std::max (blockEnd, firstRangeStart);
    }
    isContinuingPreviousBlock = true;
    updatePlayingStepNumbers (expectedNextBlockStart - tolerance);

    // Events are generated in musical order; the sort only fixes rare rounding cases and keeps that order for
    // events at the same sample (a note-off before the note-on that retriggers the same note).
    output.sortBySamplePosition();
}

void BeatsmithEngine::processRange (double rangeStart, double rangeEnd, const SampleMapping& mapping,
                                    NoteEventList& output) noexcept
{
    if (rangeEnd <= rangeStart || mapping.endSample <= mapping.firstSample)
        return;

    // Walk through the realignment periods this range touches and collect the trigs that start in it.
    numberOfPendingTrigs = 0;
    double periodSearchFrom = rangeStart;
    for (int periodCount = 0; periodCount < maximumPeriodsPerRange; ++periodCount)
    {
        const auto period = timeline.getPeriodContaining (periodSearchFrom + tolerance, pattern);
        const double partEnd = std::min (rangeEnd, period.endInQuarterNotes);
        collectTrigs (periodSearchFrom, partEnd, period, mapping);
        if (partEnd >= rangeEnd - tolerance || partEnd <= periodSearchFrom)
            break;
        periodSearchFrom = partEnd;
    }

    // Start the trigs in time order, ending due notes first, so retriggers and short gates come out right.
    std::sort (pendingTrigs.begin(), pendingTrigs.begin() + (std::ptrdiff_t) numberOfPendingTrigs,
               [] (const PendingTrig& first, const PendingTrig& second)
               {
                   if (first.startInQuarterNotes != second.startInQuarterNotes)
                       return first.startInQuarterNotes < second.startInQuarterNotes;
                   return first.laneIndex < second.laneIndex;
               });
    for (std::size_t trigIndex = 0; trigIndex < numberOfPendingTrigs; ++trigIndex)
    {
        const auto& trig = pendingTrigs[trigIndex];
        releaseNotesEndingBy (trig.startInQuarterNotes + tolerance, mapping, output);
        startNote (trig, output);
    }

    // Notes whose gate ends inside this range.
    releaseNotesEndingBy (rangeEnd - tolerance, mapping, output);
}

void BeatsmithEngine::collectTrigs (double rangeStart, double rangeEnd, const RealignmentPeriod& period,
                                    const SampleMapping& mapping) noexcept
{
    const int numberOfLanes = std::clamp (pattern.numberOfLanes, 0, maximumNumberOfLanes);
    bool anyLaneIsSoloed = false;
    for (int laneIndex = 0; laneIndex < numberOfLanes; ++laneIndex)
        anyLaneIsSoloed = anyLaneIsSoloed || pattern.lanes[(std::size_t) laneIndex].isSoloed;

    // A step belongs to this part if it starts in [lowerLimit, upperLimit). The same tolerance is used on both
    // ends everywhere, so neighbouring blocks and periods split the steps exactly between them.
    const double lowerLimit = std::max (rangeStart, period.beginInQuarterNotes) - tolerance;
    const double upperLimit = std::min (rangeEnd, period.endInQuarterNotes) - tolerance;
    if (upperLimit <= lowerLimit)
        return;

    for (int laneIndex = 0; laneIndex < numberOfLanes; ++laneIndex)
    {
        const auto& lane = pattern.lanes[(std::size_t) laneIndex];
        const bool isAudible = anyLaneIsSoloed ? lane.isSoloed : ! lane.isMuted;
        if (! isAudible)
            continue;

        const double stepDuration = lane.rate.getDurationInQuarterNotes().toDouble();
        const double anchor = period.anchorInQuarterNotes;

        // First step at or after the lower limit, decided by the same comparison used for the last one.
        auto stepNumber = (std::int64_t) std::ceil ((lowerLimit - anchor) / stepDuration);
        while (anchor + (double) (stepNumber - 1) * stepDuration >= lowerLimit)
            --stepNumber;
        while (anchor + (double) stepNumber * stepDuration < lowerLimit)
            ++stepNumber;

        for (int stepCount = 0; stepCount < maximumStepsPerLaneAndPart; ++stepCount, ++stepNumber)
        {
            const double stepStart = anchor + (double) stepNumber * stepDuration;
            if (stepStart >= upperLimit)
                break;
            const auto stepIndex = (int) wrapIndex (stepNumber, lane.length);
            if (! lane.steps[(std::size_t) stepIndex].isOn)
                continue;
            if (numberOfPendingTrigs == pendingTrigs.size())
                return; // more trigs than a block can hold: skip the rest (no note is left without its note-off)
            pendingTrigs[numberOfPendingTrigs++] = { stepStart, mapping.toSampleOffset (stepStart), laneIndex,
                                                     stepIndex };
        }
    }
}

void BeatsmithEngine::startNote (const PendingTrig& trig, NoteEventList& output) noexcept
{
    const auto& lane = pattern.lanes[(std::size_t) trig.laneIndex];
    const auto& step = lane.steps[(std::size_t) trig.stepIndex];

    // The same note was already started at this very moment (two lanes with one note, or a step played
    // late after a meter change): starting it again would only cut it.
    for (std::size_t index = 0; index < numberOfSoundingNotes; ++index)
        if (soundingNotes[index].channel == lane.channel && soundingNotes[index].noteNumber == lane.noteNumber &&
            std::abs (soundingNotes[index].startInQuarterNotes - trig.startInQuarterNotes) <= tolerance)
            return;

    // Keep room for every possible note-off: a retrigger, the oldest note, and all sounding notes.
    if (output.getEvents().size() + 3 + numberOfSoundingNotes > output.getCapacity())
        return;

    // The same note is still sounding: end it first, at the same sample.
    for (std::size_t index = 0; index < numberOfSoundingNotes; ++index)
    {
        if (soundingNotes[index].channel == lane.channel && soundingNotes[index].noteNumber == lane.noteNumber)
        {
            output.add ({ trig.sampleOffset, lane.channel, lane.noteNumber, 0, false });
            removeSoundingNote (index);
            break;
        }
    }

    // Too many notes sounding: end the oldest one.
    if (numberOfSoundingNotes == soundingNotes.size())
    {
        output.add ({ trig.sampleOffset, soundingNotes[0].channel, soundingNotes[0].noteNumber, 0, false });
        removeSoundingNote (0);
    }

    output.add ({ trig.sampleOffset, lane.channel, lane.noteNumber, lane.getVelocityOf (step), true });
    soundingNotes[numberOfSoundingNotes++] = { lane.channel, lane.noteNumber, trig.startInQuarterNotes,
                                               trig.startInQuarterNotes + lane.getGateOf (step).toDouble() };
}

void BeatsmithEngine::releaseNotesEndingBy (double limitInQuarterNotes, const SampleMapping& mapping,
                                            NoteEventList& output) noexcept
{
    // End the due notes in the order their gates end, so the events come out in time order.
    for (;;)
    {
        std::size_t earliest = numberOfSoundingNotes;
        for (std::size_t index = 0; index < numberOfSoundingNotes; ++index)
            if (soundingNotes[index].endInQuarterNotes <= limitInQuarterNotes &&
                (earliest == numberOfSoundingNotes ||
                 soundingNotes[index].endInQuarterNotes < soundingNotes[earliest].endInQuarterNotes))
                earliest = index;
        if (earliest == numberOfSoundingNotes)
            return;

        const auto& note = soundingNotes[earliest];
        output.add ({ mapping.toSampleOffset (note.endInQuarterNotes), note.channel, note.noteNumber, 0, false });
        removeSoundingNote (earliest);
    }
}

void BeatsmithEngine::releaseAllNotesAt (int sampleOffset, NoteEventList& output) noexcept
{
    for (std::size_t index = 0; index < numberOfSoundingNotes; ++index)
        output.add ({ sampleOffset, soundingNotes[index].channel, soundingNotes[index].noteNumber, 0, false });
    numberOfSoundingNotes = 0;
}

void BeatsmithEngine::updatePlayingStepNumbers (double positionInQuarterNotes) noexcept
{
    const int numberOfLanes = std::clamp (pattern.numberOfLanes, 0, maximumNumberOfLanes);
    const auto period = timeline.getPeriodContaining (positionInQuarterNotes, pattern);
    for (int laneIndex = 0; laneIndex < maximumNumberOfLanes; ++laneIndex)
    {
        auto& stepNumber = playingStepNumbers[(std::size_t) laneIndex];
        stepNumber = noStepPlaying;
        if (laneIndex >= numberOfLanes || positionInQuarterNotes < period.anchorInQuarterNotes)
            continue;
        const double stepDuration = pattern.lanes[(std::size_t) laneIndex].rate.getDurationInQuarterNotes().toDouble();
        stepNumber = (std::int64_t) std::floor ((positionInQuarterNotes - period.anchorInQuarterNotes) / stepDuration +
                                                tolerance);
    }
}

void BeatsmithEngine::removeSoundingNote (std::size_t index) noexcept
{
    // Shift the rest down to keep the oldest-first order.
    for (std::size_t moveIndex = index + 1; moveIndex < numberOfSoundingNotes; ++moveIndex)
        soundingNotes[moveIndex - 1] = soundingNotes[moveIndex];
    --numberOfSoundingNotes;
}

} // namespace beatsmith
