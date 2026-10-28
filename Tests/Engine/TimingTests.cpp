/**
 * @file TimingTests.cpp
 * @brief Tests of when steps play: straight and tuplet rates, odd meters, polymeters, realignment, meter
 *        changes, and independence from block sizes.
 */
#include "../TestFramework.h"
#include "EngineTestRig.h"

#include "Engine/TestPattern.h"

#include <random>

using namespace beatsmith;
using namespace beatsmith::testrig;

namespace
{
constexpr std::int64_t samplesPerQuarter = 24000; // 120 BPM at 48 kHz

std::vector<std::int64_t> multiplesOf (std::int64_t spacing, int count, std::int64_t offset = 0)
{
    std::vector<std::int64_t> times;
    times.reserve ((std::size_t) count);
    for (int index = 0; index < count; ++index)
        times.push_back (offset + index * spacing);
    return times;
}
} // namespace

TEST_CASE ("timing: straight 1/16 steps and their gates, sample-accurate")
{
    BeatsmithEngine engine;
    auto pattern = makeSingleLanePattern ({ 16, 1, 1 }, 16, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });
    pattern.lanes[0].steps[0].velocity = 90;
    engine.setPattern (pattern);

    const auto events = render (engine, SimulatedHost {}, 0.0, 8 * samplesPerQuarter, fixedBlockSize (512));
    EXPECT (noteOnTimesOf (events, 36) == multiplesOf (6000, 32));

    // Each note-off comes a 1/32 note (3000 samples) after its note-on.
    std::vector<std::int64_t> noteOffTimes;
    for (const auto& event : events)
        if (! event.isNoteOn)
            noteOffTimes.push_back (event.sample);
    EXPECT (noteOffTimes == multiplesOf (6000, 32, 3000));

    EXPECT (events.front().velocity == 90);
    EXPECT (events.front().channel == 10);
}

TEST_CASE ("timing: triplets, dotted notes and free tuplets")
{
    struct Case
    {
        StepRate rate;
        std::int64_t spacing;
    };
    const Case cases[] = {
        { { 8, 3, 2 }, 8000 },  // eighth triplet: 1/3 quarter
        { { 8, 2, 3 }, 18000 }, // dotted eighth: 3/4 quarter
        { { 4, 5, 4 }, 19200 }, // 5 in 4 quarters: 4/5 quarter
        { { 4, 3, 2 }, 16000 }, // quarter triplet: 2/3 quarter
    };
    for (const auto& testCase : cases)
    {
        BeatsmithEngine engine;
        auto pattern = makeSingleLanePattern (testCase.rate, 128, {});
        for (auto& step : pattern.lanes[0].steps)
            step.isOn = true;
        pattern.realignment = Realignment::songStartOnly;
        engine.setPattern (pattern);

        const auto times =
            noteOnTimesOf (render (engine, SimulatedHost {}, 0.0, 4 * samplesPerQuarter, fixedBlockSize (333)), 36);
        const int expectedCount = (int) ((4 * samplesPerQuarter + testCase.spacing - 1) / testCase.spacing);
        EXPECT (times == multiplesOf (testCase.spacing, expectedCount));
    }
}

TEST_CASE ("timing: five even notes per 4/4 bar, restarting every bar")
{
    BeatsmithEngine engine;
    engine.setPattern (makeSingleLanePattern ({ 4, 5, 4 }, 5, { 0, 1, 2, 3, 4 }));
    const auto times =
        noteOnTimesOf (render (engine, SimulatedHost {}, 0.0, 8 * samplesPerQuarter, fixedBlockSize (256)), 36);
    const std::vector<std::int64_t> expected { 0, 19200, 38400, 57600, 76800, 96000, 115200, 134400, 153600, 172800 };
    EXPECT (times == expected);
}

TEST_CASE ("timing: 7/8 from the host cuts a 16-step lane at every bar")
{
    SimulatedHost host;
    host.meterRegions = { { 0.0, 7, 8 } };
    BeatsmithEngine engine;
    auto pattern = makeSingleLanePattern ({ 16, 1, 1 }, 16, {});
    for (auto& step : pattern.lanes[0].steps)
        step.isOn = true;
    engine.setPattern (pattern);

    const auto times = noteOnTimesOf (render (engine, host, 0.0, 7 * samplesPerQuarter, fixedBlockSize (500)), 36);
    // A 7/8 bar is 3.5 quarters = 84000 samples = 14 sixteenths; steps 15 and 16 never play.
    std::vector<std::int64_t> expected;
    for (int bar = 0; bar < 2; ++bar)
        for (int step = 0; step < 14; ++step)
            expected.push_back (bar * 84000 + step * 6000);
    EXPECT (times == expected);
}

TEST_CASE ("timing: 11/8 and 5/4 bars")
{
    struct Case
    {
        int numerator;
        int denominator;
        StepRate rate;
        int stepsPerBar;
        std::int64_t stepSpacing;
    };
    const Case cases[] = { { 11, 8, { 8, 1, 1 }, 11, 12000 },
                           { 5, 4, { 4, 1, 1 }, 5, 24000 },
                           { 9, 8, { 8, 1, 1 }, 9, 12000 } };
    for (const auto& testCase : cases)
    {
        SimulatedHost host;
        host.meterRegions = { { 0.0, testCase.numerator, testCase.denominator } };
        BeatsmithEngine engine;
        auto pattern = makeSingleLanePattern (testCase.rate, 32, {});
        for (auto& step : pattern.lanes[0].steps)
            step.isOn = true;
        engine.setPattern (pattern);

        const std::int64_t barLength = testCase.stepsPerBar * testCase.stepSpacing;
        const auto times = noteOnTimesOf (render (engine, host, 0.0, 3 * barLength, fixedBlockSize (480)), 36);
        std::vector<std::int64_t> expected;
        for (int bar = 0; bar < 3; ++bar)
            for (int step = 0; step < testCase.stepsPerBar; ++step)
                expected.push_back (bar * barLength + step * testCase.stepSpacing);
        EXPECT (times == expected);
    }
}

TEST_CASE ("timing: polymeter lanes drift apart and meet again")
{
    BeatsmithEngine engine;
    Pattern pattern;
    pattern.realignment = Realignment::songStartOnly;
    pattern.numberOfLanes = 2;
    pattern.lanes[0].noteNumber = 36;
    pattern.lanes[0].length = 16;
    pattern.lanes[0].steps[0].isOn = true;
    pattern.lanes[1].noteNumber = 56;
    pattern.lanes[1].length = 5;
    pattern.lanes[1].steps[0].isOn = true;
    engine.setPattern (pattern);

    const auto events = render (engine, SimulatedHost {}, 0.0, 80 * 6000 + 1, fixedBlockSize (1024));
    EXPECT (noteOnTimesOf (events, 36) == multiplesOf (16 * 6000, 6));
    EXPECT (noteOnTimesOf (events, 56) == multiplesOf (5 * 6000, 17));
}

TEST_CASE ("timing: realignment every N bars")
{
    BeatsmithEngine engine;
    auto pattern = makeSingleLanePattern ({ 16, 1, 1 }, 3, { 0 });
    pattern.realignment = Realignment::everyNumberOfBars;
    pattern.realignEveryBars = 2;
    engine.setPattern (pattern);

    // Every third 1/16, restarting every 2 bars (32 sixteenths): steps 0, 3, … 30, then 32, 35, …
    const auto times =
        noteOnTimesOf (render (engine, SimulatedHost {}, 0.0, 16 * samplesPerQuarter, fixedBlockSize (700)), 36);
    std::vector<std::int64_t> expected;
    for (int period = 0; period < 2; ++period)
        for (int sixteenth = 0; sixteenth < 32; sixteenth += 3)
            expected.push_back ((period * 32 + sixteenth) * 6000);
    EXPECT (times == expected);
}

TEST_CASE ("timing: the pattern's own 7/8 meter while the host stays in 4/4")
{
    BeatsmithEngine engine;
    auto pattern = makeSingleLanePattern ({ 16, 1, 1 }, 128, { 0 });
    pattern.meterSource = MeterSource::ownMeter;
    pattern.ownMeter = makeMeterWithDefaultGrouping (7, 8);
    engine.setPattern (pattern);

    const auto times =
        noteOnTimesOf (render (engine, SimulatedHost {}, 0.0, 14 * samplesPerQuarter, fixedBlockSize (512)), 36);
    EXPECT (times == multiplesOf (84000, 4));
}

TEST_CASE ("timing: host meter change restarts the pattern cycle, or keeps its phase")
{
    // The host plays 4 bars of 4/4 (16 quarters), then 7/8. The pattern is 3 bars long; its lane plays the
    // first step of every cycle only.
    SimulatedHost host;
    host.meterRegions = { { 0.0, 4, 4 }, { 16.0, 7, 8 } };

    auto pattern = makeSingleLanePattern ({ 16, 1, 1 }, 128, { 0 });
    pattern.lengthInBars = 3;

    const std::int64_t totalSamples = 40 * samplesPerQuarter;

    pattern.meterChangeBehaviour = MeterChangeBehaviour::restartAtChange;
    BeatsmithEngine restartingEngine;
    restartingEngine.setPattern (pattern);
    const auto restartTimes =
        noteOnTimesOf (render (restartingEngine, host, 0.0, totalSamples, fixedBlockSize (512)), 36);
    // Cycles start at bars 0 and 3 (quarters 0 and 12), restart at the change (16), then every 3 bars of 7/8.
    const std::vector<std::int64_t> expectedRestart { 0, 12 * samplesPerQuarter, 16 * samplesPerQuarter,
                                                      (std::int64_t) (26.5 * samplesPerQuarter),
                                                      37 * samplesPerQuarter };
    EXPECT (restartTimes == expectedRestart);

    pattern.meterChangeBehaviour = MeterChangeBehaviour::keepPhase;
    BeatsmithEngine phaseKeepingEngine;
    phaseKeepingEngine.setPattern (pattern);
    const auto keepTimes =
        noteOnTimesOf (render (phaseKeepingEngine, host, 0.0, totalSamples, fixedBlockSize (512)), 36);
    // Bars are counted on: cycles at bars 0, 3, 6, 9, 12 → quarters 0, 12, 23, 33.5
    const std::vector<std::int64_t> expectedKeep { 0, 12 * samplesPerQuarter, 23 * samplesPerQuarter,
                                                   (std::int64_t) (33.5 * samplesPerQuarter) };
    EXPECT (keepTimes == expectedKeep);
}

TEST_CASE ("timing: the result does not depend on block sizes")
{
    for (const double tempo : { 120.0, 137.3, 87.0 })
    {
        for (const double sampleRate : { 48000.0, 44100.0 })
        {
            SimulatedHost host;
            host.tempoBeatsPerMinute = tempo;
            host.sampleRate = sampleRate;
            host.meterRegions = { { 0.0, 7, 8 } };
            const auto totalSamples = (std::int64_t) (6 * 3.5 * 60.0 / tempo * sampleRate);

            BeatsmithEngine referenceEngine;
            referenceEngine.setPattern (makeTimingTestPattern());
            const auto reference = render (referenceEngine, host, 0.0, totalSamples, fixedBlockSize (1));

            std::mt19937 random (42);
            std::uniform_int_distribution<int> anySize (1, 2048);
            const BlockSizeChooser choosers[] = { fixedBlockSize (64), fixedBlockSize (441), fixedBlockSize (4096),
                                                  [&] (int) { return anySize (random); } };
            for (const auto& chooser : choosers)
            {
                BeatsmithEngine engine;
                engine.setPattern (makeTimingTestPattern());
                EXPECT (haveSameEventsWithinOneSample (render (engine, host, 0.0, totalSamples, chooser), reference));
            }
        }
    }
}

TEST_CASE ("timing: a host without bar positions or time signature is treated as 4/4 from the song start")
{
    SimulatedHost host;
    host.reportsLastBarStart = false;
    host.reportsTimeSignature = false;
    BeatsmithEngine engine;
    engine.setPattern (makeSingleLanePattern ({ 16, 1, 1 }, 128, { 0 }));
    const auto times = noteOnTimesOf (render (engine, host, 0.0, 12 * samplesPerQuarter, fixedBlockSize (512)), 36);
    EXPECT (times == multiplesOf (4 * samplesPerQuarter, 3));
}

TEST_CASE ("timing: steps before the song start (pre-roll) follow the same grid")
{
    BeatsmithEngine engine;
    auto pattern = makeSingleLanePattern ({ 4, 1, 1 }, 4, { 0, 1, 2, 3 });
    engine.setPattern (pattern);
    // Start two quarters before the song start: quarters -2, -1, 0, 1 → samples 0, 24000, 48000, 72000.
    const auto times =
        noteOnTimesOf (render (engine, SimulatedHost {}, -2.0, 4 * samplesPerQuarter, fixedBlockSize (512)), 36);
    EXPECT (times == multiplesOf (samplesPerQuarter, 4));
}

TEST_CASE ("timing: nothing plays without a song position")
{
    BeatsmithEngine engine;
    engine.setPattern (makeTimingTestPattern());
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    auto context = SimulatedHost {}.makeContext (0.0, 512);
    context.hasPositionInQuarterNotes = false;
    for (int block = 0; block < 100; ++block)
    {
        engine.process (context, output);
        EXPECT (output.getEvents().empty());
    }
}

TEST_CASE ("timing: mute, solo and accents")
{
    Pattern pattern;
    pattern.numberOfLanes = 2;
    pattern.lanes[0].noteNumber = 36;
    pattern.lanes[0].steps[0].isOn = true;
    pattern.lanes[0].steps[0].isAccented = true;
    pattern.lanes[0].accentVelocity = 120;
    pattern.lanes[1].noteNumber = 38;
    pattern.lanes[1].steps[0].isOn = true;
    pattern.lanes[1].steps[0].velocity = 77;

    auto renderOneBar = [] (const Pattern& patternToPlay)
    {
        BeatsmithEngine engine;
        engine.setPattern (patternToPlay);
        return noteOnsOf (render (engine, SimulatedHost {}, 0.0, 4 * samplesPerQuarter, fixedBlockSize (512)));
    };

    auto both = renderOneBar (pattern);
    EXPECT (both.size() == 2);
    EXPECT (both.size() == 2 && both[0].noteNumber == 36 && both[0].velocity == 120);
    EXPECT (both.size() == 2 && both[1].noteNumber == 38 && both[1].velocity == 77);

    pattern.lanes[0].isMuted = true;
    auto muted = renderOneBar (pattern);
    EXPECT (muted.size() == 1 && muted[0].noteNumber == 38);

    pattern.lanes[0].isMuted = false;
    pattern.lanes[0].isSoloed = true;
    auto soloed = renderOneBar (pattern);
    EXPECT (soloed.size() == 1 && soloed[0].noteNumber == 36);
}
