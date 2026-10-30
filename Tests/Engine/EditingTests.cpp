/**
 * @file EditingTests.cpp
 * @brief Tests of the editing model: grid geometry, step edits, drag decisions, undo/redo, the lock-free
 *        pattern hand-over and the engine's playhead reporting.
 */
#include "../TestFramework.h"
#include "EngineTestRig.h"

#include "Engine/Descriptions.h"
#include "Engine/EditHistory.h"
#include "Engine/GridGeometry.h"
#include "Engine/PatternEditing.h"
#include "Engine/TestPattern.h"
#include "Engine/TripleBuffer.h"

#include <atomic>
#include <cmath>
#include <thread>

using namespace beatsmith;
using namespace beatsmith::testrig;

namespace
{
bool isNear (double first, double second)
{
    return std::abs (first - second) < 1.0e-9;
}
} // namespace

//==============================================================================
// Grid geometry

TEST_CASE ("grid: the displayed meter follows the host, or the pattern's own meter")
{
    Pattern pattern;
    pattern.meterSource = MeterSource::followHost;
    const auto sevenEight = getDisplayedMeter (pattern, 7, 8);
    EXPECT (sevenEight.numerator == 7 && sevenEight.denominator == 8 && sevenEight.numberOfGroups == 3);

    const auto invalid = getDisplayedMeter (pattern, 0, 3);
    EXPECT (invalid.numerator == 4 && invalid.denominator == 4);
    const auto tooBig = getDisplayedMeter (pattern, 99, 4);
    EXPECT (tooBig.numerator == 4 && tooBig.denominator == 4);

    pattern.meterSource = MeterSource::ownMeter;
    pattern.ownMeter = makeMeterWithDefaultGrouping (5, 4);
    const auto own = getDisplayedMeter (pattern, 7, 8);
    EXPECT (own.numerator == 5 && own.denominator == 4);
}

TEST_CASE ("grid: pattern and grid lengths")
{
    Pattern pattern;
    pattern.lengthInBars = 2;
    pattern.numberOfLanes = 1;
    pattern.lanes[0].length = 16; // 4 quarters of 1/16
    const auto sevenEight = makeMeterWithDefaultGrouping (7, 8);
    EXPECT (isNear (getPatternLengthInQuarterNotes (pattern, sevenEight), 7.0));
    EXPECT (isNear (getGridLengthInQuarterNotes (pattern, sevenEight), 7.0));

    pattern.lanes[0].length = 32; // 8 quarters: reaches beyond the 7-quarter pattern
    EXPECT (isNear (getGridLengthInQuarterNotes (pattern, sevenEight), 8.0));
}

TEST_CASE ("grid: a short lane shows its own steps and faint repeats up to the pattern end")
{
    Lane lane;
    lane.length = 5; // 1/16 steps in a 4-quarter pattern: 16 cells, 5 own + 11 repeats
    const auto cells = getCellsOfLane (lane, 4.0);
    EXPECT (cells.size() == 16);
    for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex)
    {
        const auto& cell = cells[cellIndex];
        EXPECT (cell.stepIndex == (int) (cellIndex % 5));
        EXPECT (cell.stepNumber == (std::int64_t) cellIndex);
        EXPECT (cell.isRepeat == (cellIndex >= 5));
        EXPECT (! cell.isAfterPatternEnd);
        EXPECT (isNear (cell.startInQuarterNotes, 0.25 * (double) cellIndex));
    }
}

TEST_CASE ("grid: a long lane shows all its steps, those after the pattern end greyed out")
{
    Lane lane;
    lane.length = 16; // 1/16 steps in a 7/8 bar of 3.5 quarters: steps 14 and 15 never play
    const auto cells = getCellsOfLane (lane, 3.5);
    EXPECT (cells.size() == 16);
    for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex)
    {
        EXPECT (! cells[cellIndex].isRepeat);
        EXPECT (cells[cellIndex].isAfterPatternEnd == (cellIndex >= 14));
    }
}

TEST_CASE ("grid: tuplet lanes are drawn at their true width")
{
    Lane lane;
    lane.rate = { 4, 5, 4 }; // five steps per 4/4 bar
    lane.length = 5;
    const auto cells = getCellsOfLane (lane, 4.0);
    EXPECT (cells.size() == 5);
    for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex)
    {
        EXPECT (isNear (cells[cellIndex].startInQuarterNotes, 0.8 * (double) cellIndex));
        EXPECT (isNear (cells[cellIndex].endInQuarterNotes, 0.8 * (double) (cellIndex + 1)));
    }
}

TEST_CASE ("grid: absurd pattern lengths are capped")
{
    Lane lane;
    lane.rate = { 64, 1, 1 };
    lane.length = 1;
    const auto cells = getCellsOfLane (lane, 64 * 128.0);
    EXPECT ((int) cells.size() == maximumGridCellsPerLane);
}

TEST_CASE ("grid: finding the cell under a position")
{
    Lane lane;
    lane.rate = { 8, 3, 2 }; // eighth triplets: 1/3 quarter each
    lane.length = 3;
    const auto cells = getCellsOfLane (lane, 2.0);
    GridCell found;
    EXPECT (findCellAt (cells, 0.0, found) && found.stepNumber == 0);
    EXPECT (findCellAt (cells, 0.34, found) && found.stepNumber == 1);
    EXPECT (findCellAt (cells, 1.0, found) && found.stepNumber == 3 && found.stepIndex == 0 && found.isRepeat);
    EXPECT (findCellAt (cells, 1.99, found) && found.stepNumber == 5);
    EXPECT (! findCellAt (cells, 2.0, found)); // after the last cell
    EXPECT (! findCellAt (cells, -0.1, found));
}

TEST_CASE ("grid: bar and beat-group lines of 7/8 as 2+2+3")
{
    const auto lines = getGridLines (makeMeterWithDefaultGrouping (7, 8), 2);
    // Per bar: bar line, then group starts after 2 and 4 eighths; plus the end line.
    EXPECT (lines.size() == 7);
    if (lines.size() == 7)
    {
        EXPECT (lines[0].isBarLine && lines[0].barNumber == 1 && isNear (lines[0].positionInQuarterNotes, 0.0));
        EXPECT (! lines[1].isBarLine && isNear (lines[1].positionInQuarterNotes, 1.0));
        EXPECT (! lines[2].isBarLine && isNear (lines[2].positionInQuarterNotes, 2.0));
        EXPECT (lines[3].isBarLine && lines[3].barNumber == 2 && isNear (lines[3].positionInQuarterNotes, 3.5));
        EXPECT (isNear (lines[4].positionInQuarterNotes, 4.5));
        EXPECT (isNear (lines[5].positionInQuarterNotes, 5.5));
        EXPECT (lines[6].isBarLine && isNear (lines[6].positionInQuarterNotes, 7.0));
    }
}

//==============================================================================
// Step edits

TEST_CASE ("editing: toggling uses the lane's default velocity and clears the accent")
{
    Pattern pattern;
    pattern.numberOfLanes = 1;
    pattern.lanes[0].defaultVelocity = 90;
    pattern.lanes[0].steps[3].velocity = 20;
    pattern.lanes[0].steps[3].isAccented = true;

    toggleStep (pattern, 0, 3);
    EXPECT (pattern.lanes[0].steps[3].isOn);
    EXPECT (pattern.lanes[0].steps[3].velocity == 90);
    EXPECT (! pattern.lanes[0].steps[3].isAccented);
    toggleStep (pattern, 0, 3);
    EXPECT (! pattern.lanes[0].steps[3].isOn);
}

TEST_CASE ("editing: painting leaves steps that already have the wanted state untouched")
{
    Pattern pattern;
    pattern.numberOfLanes = 1;
    auto& step = pattern.lanes[0].steps[2];
    step.isOn = true;
    step.velocity = 33;
    step.isAccented = true;
    setStepOn (pattern, 0, 2, true);
    EXPECT (step.velocity == 33 && step.isAccented);
    setStepOn (pattern, 0, 2, false);
    EXPECT (! step.isOn);
}

TEST_CASE ("editing: velocity only for steps that are on; it replaces the accent")
{
    Pattern pattern;
    pattern.numberOfLanes = 1;
    setStepVelocity (pattern, 0, 1, 50);
    EXPECT (! pattern.lanes[0].steps[1].isOn);
    EXPECT (pattern.lanes[0].steps[1].velocity == 100);

    toggleAccent (pattern, 0, 1);
    EXPECT (pattern.lanes[0].steps[1].isOn && pattern.lanes[0].steps[1].isAccented);
    setStepVelocity (pattern, 0, 1, 500);
    EXPECT (pattern.lanes[0].steps[1].velocity == 127 && ! pattern.lanes[0].steps[1].isAccented);
    setStepVelocity (pattern, 0, 1, -4);
    EXPECT (pattern.lanes[0].steps[1].velocity == 1);
}

TEST_CASE ("editing: accents toggle; an accent on an empty step switches it on")
{
    Pattern pattern;
    pattern.numberOfLanes = 1;
    toggleAccent (pattern, 0, 0);
    EXPECT (pattern.lanes[0].steps[0].isOn && pattern.lanes[0].steps[0].isAccented);
    toggleAccent (pattern, 0, 0);
    EXPECT (pattern.lanes[0].steps[0].isOn && ! pattern.lanes[0].steps[0].isAccented);
}

TEST_CASE ("editing: out-of-range lanes and steps are ignored")
{
    Pattern pattern;
    const auto before = pattern;
    toggleStep (pattern, -1, 0);
    toggleStep (pattern, maximumNumberOfLanes, 0);
    setStepOn (pattern, 0, maximumStepsPerLane, true);
    setStepVelocity (pattern, 0, -5, 10);
    toggleAccent (pattern, 99, 99);
    EXPECT (pattern == before);
}

TEST_CASE ("editing: drag decisions")
{
    EXPECT (decideDragKind (0, 0, true) == DragKind::undecided);
    EXPECT (decideDragKind (3, -3, true) == DragKind::undecided);
    EXPECT (decideDragKind (10, 2, false) == DragKind::paint);
    EXPECT (decideDragKind (-10, 9, true) == DragKind::paint);
    EXPECT (decideDragKind (1, -12, true) == DragKind::velocity);
    EXPECT (decideDragKind (1, 12, false) == DragKind::nothing);
}

TEST_CASE ("editing: velocity follows vertical dragging, up is louder")
{
    EXPECT (velocityAfterVerticalDrag (64, 0) == 64);
    EXPECT (velocityAfterVerticalDrag (64, -100) > 64);
    EXPECT (velocityAfterVerticalDrag (64, 100) < 64);
    EXPECT (velocityAfterVerticalDrag (64, -pixelsForFullVelocityRange) == 127);
    EXPECT (velocityAfterVerticalDrag (64, pixelsForFullVelocityRange) == 1);
}

//==============================================================================
// Undo and redo

TEST_CASE ("history: undo and redo step through the changes")
{
    EditHistory history;
    Pattern first;
    Pattern second = first;
    toggleStep (second, 0, 0);
    Pattern third = second;
    toggleStep (third, 0, 1);

    history.recordChange (first, second);
    history.recordChange (second, third);
    EXPECT (history.canUndo() && ! history.canRedo());

    Pattern result;
    EXPECT (history.undo (third, result) && result == second);
    EXPECT (history.undo (second, result) && result == first);
    EXPECT (! history.canUndo());
    EXPECT (! history.undo (first, result));
    EXPECT (history.redo (first, result) && result == second);
    EXPECT (history.redo (second, result) && result == third);
    EXPECT (! history.canRedo());
}

TEST_CASE ("history: no-op changes are not recorded; a new change clears redo")
{
    EditHistory history;
    Pattern first;
    history.recordChange (first, first);
    EXPECT (! history.canUndo());

    Pattern second = first;
    toggleStep (second, 0, 0);
    history.recordChange (first, second);
    Pattern result;
    history.undo (second, result);
    EXPECT (history.canRedo());

    Pattern other = first;
    toggleStep (other, 0, 5);
    history.recordChange (first, other);
    EXPECT (! history.canRedo());
}

TEST_CASE ("history: the oldest versions are dropped at the cap")
{
    EditHistory history (3);
    Pattern pattern;
    for (int stepIndex = 0; stepIndex < 10; ++stepIndex)
    {
        auto changed = pattern;
        toggleStep (changed, 0, stepIndex);
        history.recordChange (pattern, changed);
        pattern = changed;
    }
    EXPECT (history.getNumberOfUndoSteps() == 3);
}

//==============================================================================
// Lock-free hand-over

TEST_CASE ("triple buffer: the reader gets the newest value, once")
{
    TripleBuffer<int> buffer (1);
    EXPECT (buffer.getReadValue() == 1);
    EXPECT (! buffer.takeNewestValue());
    buffer.write (2);
    buffer.write (3);
    EXPECT (buffer.takeNewestValue());
    EXPECT (buffer.getReadValue() == 3);
    EXPECT (! buffer.takeNewestValue());
    EXPECT (buffer.getReadValue() == 3);
    buffer.write (4);
    EXPECT (buffer.takeNewestValue() && buffer.getReadValue() == 4);
}

TEST_CASE ("triple buffer: a reader on another thread never sees a half-written pattern")
{
    // The writer publishes patterns whose values all equal a generation number; the reader checks that every
    // pattern it takes is consistent and that generations never go backwards.
    TripleBuffer<Pattern> buffer;
    std::atomic<bool> writerIsDone { false };
    constexpr int numberOfGenerations = 3000;

    std::thread writer (
        [&]
        {
            Pattern pattern;
            for (int generation = 1; generation <= numberOfGenerations; ++generation)
            {
                pattern.lengthInBars = generation;
                for (auto& lane : pattern.lanes)
                {
                    lane.length = generation;
                    for (auto& step : lane.steps)
                        step.gateOverride = (std::int16_t) generation;
                }
                buffer.write (pattern);
            }
            writerIsDone = true;
        });

    int inconsistentPatterns = 0;
    int generationsGoingBack = 0;
    int lastGeneration = 0;
    for (;;)
    {
        // Read "done" before trying: if the writer had finished and nothing new is there, all values were seen.
        const bool writerWasDone = writerIsDone.load();
        if (! buffer.takeNewestValue())
        {
            if (writerWasDone)
                break;
            continue;
        }
        const auto& pattern = buffer.getReadValue();
        const int generation = pattern.lengthInBars;
        for (const auto& lane : pattern.lanes)
        {
            if (lane.length != generation)
                ++inconsistentPatterns;
            for (const auto& step : lane.steps)
                if (step.gateOverride != generation)
                    ++inconsistentPatterns;
        }
        if (generation < lastGeneration)
            ++generationsGoingBack;
        lastGeneration = generation;
    }
    writer.join();

    EXPECT (inconsistentPatterns == 0);
    EXPECT (generationsGoingBack == 0);
    EXPECT (lastGeneration == numberOfGenerations);
}

//==============================================================================
// Texts

TEST_CASE ("descriptions: drum names, rates and meters")
{
    EXPECT (describeDrumNote (36) == "Bass Drum");
    EXPECT (describeDrumNote (42) == "Closed Hi-Hat");
    EXPECT (describeDrumNote (35) == "Acoustic Bass Drum");
    EXPECT (describeDrumNote (81) == "Open Triangle");
    EXPECT (describeDrumNote (34) == "Note 34");
    EXPECT (describeDrumNote (82) == "Note 82");

    EXPECT ((describeStepRate ({ 16, 1, 1 }) == "1/16"));
    EXPECT ((describeStepRate ({ 8, 3, 2 }) == "1/8T"));
    EXPECT ((describeStepRate ({ 8, 2, 3 }) == "1/8."));
    EXPECT ((describeStepRate ({ 4, 5, 4 }) == "5:4 1/4"));
    EXPECT ((describeStepRate ({ 16, 7, 7 }) == "1/16"));

    EXPECT (describeMeter (makeMeterWithDefaultGrouping (7, 8)) == "7/8 (2+2+3)");
    EXPECT (describeMeter (makeMeterWithDefaultGrouping (4, 4)) == "4/4");
    EXPECT (describeMeter (makeMeterWithDefaultGrouping (5, 4)) == "5/4");
    EXPECT (describeMeter (makeMeterWithDefaultGrouping (6, 8)) == "6/8 (3+3)");
}

//==============================================================================
// Playheads

TEST_CASE ("playheads: each lane reports the step playing at the end of the block")
{
    BeatsmithEngine engine;
    auto pattern = makeTimingTestPattern(); // 1 bar, restarting every bar
    engine.setPattern (pattern);
    for (int laneIndex = 0; laneIndex < maximumNumberOfLanes; ++laneIndex)
        EXPECT (engine.getPlayingStepNumber (laneIndex) == BeatsmithEngine::noStepPlaying);

    // Play exactly 1.3 quarters (31200 samples at 120 BPM / 48 kHz).
    render (engine, SimulatedHost {}, 0.0, 31200, fixedBlockSize (31200));
    EXPECT (engine.getPlayingStepNumber (0) == 5); // 1/16 lane: step 5 (1.25 … 1.5)
    EXPECT (engine.getPlayingStepNumber (2) == 2); // 1/8 lane: step 2 (1.0 … 1.5)
    EXPECT (engine.getPlayingStepNumber (3) == 1); // quintuplets of 0.8: step 1 (0.8 … 1.6)
    EXPECT (engine.getPlayingStepNumber (5) == BeatsmithEngine::noStepPlaying); // not in use

    // In the second bar the count starts again (the pattern restarts every bar).
    render (engine, SimulatedHost {}, 4.0, 4800, fixedBlockSize (4800)); // 4.0 … 4.2
    EXPECT (engine.getPlayingStepNumber (0) == 0);

    // Stopping clears the playheads.
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    auto stopped = SimulatedHost {}.makeContext (4.2, 512);
    stopped.hostIsPlaying = false;
    engine.process (stopped, output);
    EXPECT (engine.getPlayingStepNumber (0) == BeatsmithEngine::noStepPlaying);
}
