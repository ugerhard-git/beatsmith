/**
 * @file NoteSafetyTests.cpp
 * @brief Tests of transport handling and note safety: jumps, loops, stops, tempo changes, retriggers, capacity
 *        limits, a randomized no-stuck-notes stress test, and no memory allocation while processing.
 */
#include "../TestFramework.h"
#include "EngineTestRig.h"

#include "Engine/TestPattern.h"

#include <atomic>
#include <cstdlib>
#include <map>
#include <new>
#include <random>

using namespace beatsmith;
using namespace beatsmith::testrig;

//==============================================================================
// Allocation counting: every operator new in the test program is counted while counting is switched on.

namespace
{
std::atomic<bool> isCountingAllocations { false };
std::atomic<long> numberOfAllocations { 0 };
} // namespace

void* operator new (std::size_t size)
{
    if (isCountingAllocations.load())
        ++numberOfAllocations;
    if (void* memory = std::malloc (size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void operator delete (void* memory) noexcept
{
    std::free (memory);
}

void operator delete (void* memory, std::size_t /*size*/) noexcept
{
    std::free (memory);
}

//==============================================================================

namespace
{
constexpr std::int64_t samplesPerQuarter = 24000; // 120 BPM at 48 kHz

/**
 * Follows the MIDI an engine sends and records every rule violation: a note started twice without a note-off
 * in between, a note-off without a sounding note, events out of time order or outside the block. Events at
 * the same sample are read in list order, as an instrument would.
 */
class NoteBalanceChecker
{
public:
    void check (const NoteEventList& output, int numSamples)
    {
        int previousSample = 0;
        for (const auto& event : output.getEvents())
        {
            const bool isInBlock = event.samplePosition >= 0 && event.samplePosition < std::max (1, numSamples);
            if (! isInBlock || event.samplePosition < previousSample)
                ++numberOfViolations;
            previousSample = event.samplePosition;

            auto& sounding = isSounding[{ event.channel, event.noteNumber }];
            if (event.isNoteOn)
            {
                if (sounding || event.velocity < 1 || event.velocity > 127)
                    ++numberOfViolations;
                sounding = true;
                ++numberOfNoteOns;
            }
            else
            {
                if (! sounding)
                    ++numberOfViolations;
                sounding = false;
            }
        }
    }

    int countSoundingNotes() const
    {
        int count = 0;
        for (const auto& entry : isSounding)
            count += entry.second ? 1 : 0;
        return count;
    }

    int numberOfViolations = 0;
    long numberOfNoteOns = 0;

private:
    std::map<std::pair<int, int>, bool> isSounding;
};

/** A pattern with every step of one long-gated lane on, so notes are sounding most of the time. */
Pattern makeLongNotesPattern()
{
    auto pattern = makeSingleLanePattern ({ 4, 1, 1 }, 4, { 0, 1, 2, 3 });
    pattern.lanes[0].gateInSixtyFourths = 64; // a whole note: notes overlap and must be retriggered
    return pattern;
}
} // namespace

TEST_CASE ("transport: stopping ends every note at once and keeps silent")
{
    BeatsmithEngine engine;
    auto pattern = makeSingleLanePattern ({ 4, 1, 1 }, 4, { 0 });
    pattern.lanes[0].gateInSixtyFourths = 64;
    engine.setPattern (pattern);

    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    SimulatedHost host;
    engine.process (host.makeContext (0.0, 512), output);
    EXPECT (output.getEvents().size() == 1 && output.getEvents()[0].isNoteOn);

    auto stopped = host.makeContext (512 * host.getQuarterNotesPerSample(), 512);
    stopped.hostIsPlaying = false;
    engine.process (stopped, output);
    EXPECT (output.getEvents().size() == 1 && ! output.getEvents()[0].isNoteOn &&
            output.getEvents()[0].samplePosition == 0);
    EXPECT (engine.getNumberOfSoundingNotes() == 0);

    for (int block = 0; block < 20; ++block)
    {
        engine.process (stopped, output);
        EXPECT (output.getEvents().empty());
    }
}

TEST_CASE ("transport: releasing all notes (bypass) ends them at sample 0")
{
    BeatsmithEngine engine;
    engine.setPattern (makeLongNotesPattern());
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    engine.process (SimulatedHost {}.makeContext (0.0, 512), output);
    EXPECT (engine.getNumberOfSoundingNotes() == 1);

    engine.releaseAllNotes (output);
    EXPECT (output.getEvents().size() == 1 && ! output.getEvents()[0].isNoteOn &&
            output.getEvents()[0].samplePosition == 0);
    EXPECT (engine.getNumberOfSoundingNotes() == 0);
}

TEST_CASE ("transport: after a jump the result is the same as starting fresh at that position")
{
    SimulatedHost host;
    host.meterRegions = { { 0.0, 7, 8 } };
    auto pattern = makeTimingTestPattern();
    pattern.lengthInBars = 3;
    pattern.lanes[4].length = 5; // the cowbell drifts over the 3-bar cycle

    std::mt19937 random (7);
    std::uniform_int_distribution<std::int64_t> anyStart (0, 40 * samplesPerQuarter);
    for (int round = 0; round < 30; ++round)
    {
        const double jumpTarget = (double) anyStart (random) / samplesPerQuarter;

        BeatsmithEngine freshEngine;
        freshEngine.setPattern (pattern);
        const auto fresh =
            noteOnsOf (render (freshEngine, host, jumpTarget, 8 * samplesPerQuarter, fixedBlockSize (512)));

        // The same, but the engine was playing somewhere else before the jump.
        BeatsmithEngine jumpingEngine;
        jumpingEngine.setPattern (pattern);
        render (jumpingEngine, host, 3.25, 5 * samplesPerQuarter, fixedBlockSize (512));
        const auto afterJump =
            noteOnsOf (render (jumpingEngine, host, jumpTarget, 8 * samplesPerQuarter, fixedBlockSize (512)));

        EXPECT (afterJump == fresh);
        EXPECT (! fresh.empty());
    }
}

TEST_CASE ("transport: a loop plays the same whether or not the host splits the block at the loop end")
{
    SimulatedHost splittingHost;
    splittingHost.isLooping = true;
    splittingHost.loopStartInQuarterNotes = 4.0;
    splittingHost.loopEndInQuarterNotes = 11.0; // 7 quarters: not a whole number of 4/4 bars
    splittingHost.splitsBlocksAtLoopEnd = true;

    auto nonSplittingHost = splittingHost;
    nonSplittingHost.splitsBlocksAtLoopEnd = false;

    BeatsmithEngine splitEngine;
    splitEngine.setPattern (makeTimingTestPattern());
    const auto split = render (splitEngine, splittingHost, 2.0, 40 * samplesPerQuarter, fixedBlockSize (1000));

    BeatsmithEngine wrapEngine;
    wrapEngine.setPattern (makeTimingTestPattern());
    const auto wrapped = render (wrapEngine, nonSplittingHost, 2.0, 40 * samplesPerQuarter, fixedBlockSize (1000));

    EXPECT (haveSameEventsWithinOneSample (noteOnsOf (wrapped), noteOnsOf (split)));

    // The loop start (quarter 4) is beat 1 of bar 2: the kick plays there on every pass.
    const auto kickTimes = noteOnTimesOf (wrapped, GeneralMidiDrum::bassDrum);
    int kicksAtLoopStart = 0;
    for (int pass = 0; pass < 5; ++pass)
    {
        const std::int64_t loopStartTime = 2 * samplesPerQuarter + pass * 7 * samplesPerQuarter;
        for (const auto time : kickTimes)
            if (std::llabs (time - loopStartTime) <= 1)
                ++kicksAtLoopStart;
    }
    EXPECT (kicksAtLoopStart == 5);
}

TEST_CASE ("transport: a host meter change inside a block delays the downbeat by at most one block")
{
    SimulatedHost host;
    host.meterRegions = { { 0.0, 4, 4 }, { 16.0, 7, 8 } };
    auto pattern = makeSingleLanePattern ({ 16, 1, 1 }, 128, { 0 });
    pattern.lengthInBars = 3;

    BeatsmithEngine engine;
    engine.setPattern (pattern);
    const int blockSize = 700; // the change at quarter 16 (sample 384000) falls inside block 548
    const auto times =
        noteOnTimesOf (render (engine, host, 0.0, 30 * samplesPerQuarter, fixedBlockSize (blockSize)), 36);

    EXPECT (times.size() == 4);
    if (times.size() == 4)
    {
        EXPECT (times[0] == 0);
        EXPECT (times[1] == 12 * samplesPerQuarter);
        EXPECT (times[2] >= 16 * samplesPerQuarter && times[2] < 16 * samplesPerQuarter + blockSize);
        EXPECT (std::llabs (times[3] - (std::int64_t) (26.5 * samplesPerQuarter)) <= 1);
    }
}

TEST_CASE ("transport: a tempo change inside a block neither loses nor repeats steps")
{
    BeatsmithEngine engine;
    engine.setPattern (
        makeSingleLanePattern ({ 16, 1, 1 }, 16, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }));
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    NoteBalanceChecker checker;

    // The host reports one tempo per block, but its position follows a tempo that changes in the middle of
    // some blocks, so each new block starts a little away from where the engine expected it.
    SimulatedHost host;
    double position = 0.0;
    const int blockSize = 1024;
    const int halfBlock = blockSize / 2;
    for (int block = 0; block < 400; ++block)
    {
        host.tempoBeatsPerMinute = (block / 50) % 2 == 0 ? 120.0 : 180.0;
        const double tempoInSecondHalf = ((block + 25) / 50) % 2 == 0 ? 120.0 : 180.0;
        engine.process (host.makeContext (position, blockSize), output);
        checker.check (output, blockSize);
        position += halfBlock * host.getQuarterNotesPerSample() + halfBlock * tempoInSecondHalf / 60.0 / 48000.0;
    }
    // Every sixteenth from 0 to the final position played exactly once.
    const long expectedNoteOns = (long) std::ceil (position * 4.0 - 1.0e-9);
    EXPECT (std::labs (checker.numberOfNoteOns - expectedNoteOns) <= 1);
    EXPECT (checker.numberOfViolations == 0);
}

TEST_CASE ("notes: a note that is still sounding is ended before it starts again")
{
    BeatsmithEngine engine;
    engine.setPattern (makeLongNotesPattern());
    const auto events = render (engine, SimulatedHost {}, 0.0, 4 * samplesPerQuarter, fixedBlockSize (512));
    // on at 0; then at every quarter: off, on (same sample)
    EXPECT (events.size() == 7);
    for (std::size_t index = 1; index + 1 < events.size(); index += 2)
    {
        EXPECT (! events[index].isNoteOn);
        EXPECT (events[index + 1].isNoteOn);
        EXPECT (events[index].sample == events[index + 1].sample);
    }
    EXPECT (engine.getNumberOfSoundingNotes() == 1);
}

TEST_CASE ("notes: two lanes with the same note at the same moment play it once")
{
    Pattern pattern;
    pattern.numberOfLanes = 2;
    pattern.lanes[0].steps[0].isOn = true;
    pattern.lanes[1].steps[0].isOn = true; // both lanes play note 36 on channel 10
    BeatsmithEngine engine;
    engine.setPattern (pattern);
    const auto noteOns = noteOnsOf (render (engine, SimulatedHost {}, 0.0, samplesPerQuarter, fixedBlockSize (512)));
    EXPECT (noteOns.size() == 1);
}

TEST_CASE ("notes: changing a lane's note while it sounds still ends the original note")
{
    BeatsmithEngine engine;
    auto pattern = makeLongNotesPattern();
    engine.setPattern (pattern);
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    NoteBalanceChecker checker;
    SimulatedHost host;

    engine.process (host.makeContext (0.0, 512), output);
    checker.check (output, 512);
    pattern.lanes[0].noteNumber = 40;
    pattern.lanes[0].channel = 3;
    engine.setPattern (pattern);
    auto stopped = host.makeContext (512 * host.getQuarterNotesPerSample(), 512);
    stopped.hostIsPlaying = false;
    engine.process (stopped, output);
    checker.check (output, 512);

    EXPECT (checker.countSoundingNotes() == 0);
    EXPECT (checker.numberOfViolations == 0);
}

TEST_CASE ("notes: a flood of trigs never overflows the output or loses a note-off")
{
    Pattern pattern;
    pattern.numberOfLanes = maximumNumberOfLanes;
    for (int laneIndex = 0; laneIndex < maximumNumberOfLanes; ++laneIndex)
    {
        auto& lane = pattern.lanes[(std::size_t) laneIndex];
        lane.noteNumber = 30 + laneIndex;
        lane.rate = { 64, 1, 1 };
        lane.length = maximumStepsPerLane;
        lane.gateInSixtyFourths = maximumGateInSixtyFourths;
        for (auto& step : lane.steps)
            step.isOn = true;
    }
    BeatsmithEngine engine;
    engine.setPattern (pattern);

    SimulatedHost host;
    host.tempoBeatsPerMinute = 1000.0;
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
    NoteBalanceChecker checker;
    double position = 0.0;
    const int blockSize = 32768;
    for (int block = 0; block < 10; ++block)
    {
        engine.process (host.makeContext (position, blockSize), output);
        EXPECT (output.getEvents().size() <= output.getCapacity());
        checker.check (output, blockSize);
        position += blockSize * host.getQuarterNotesPerSample();
    }
    engine.releaseAllNotes (output);
    checker.check (output, 1);
    EXPECT (checker.countSoundingNotes() == 0);
    EXPECT (checker.numberOfViolations == 0);
    EXPECT (checker.numberOfNoteOns > 0);
}

TEST_CASE ("notes: randomized stress test - no stuck notes, whatever the host and the user do")
{
    std::mt19937 random (2024);
    auto chance = [&random] (double probability)
    { return std::uniform_real_distribution<double> (0.0, 1.0) (random) < probability; };
    auto anyInt = [&random] (int lowest, int highest)
    { return std::uniform_int_distribution<int> (lowest, highest) (random); };

    const StepRate rates[] = { { 16, 1, 1 }, { 8, 3, 2 },  { 4, 5, 4 }, { 32, 1, 1 },
                               { 8, 2, 3 },  { 16, 7, 4 }, { 64, 1, 1 } };

    auto makeRandomPattern = [&]
    {
        Pattern pattern;
        pattern.meterSource = chance (0.3) ? MeterSource::ownMeter : MeterSource::followHost;
        pattern.ownMeter = makeMeterWithDefaultGrouping (anyInt (2, 13), chance (0.5) ? 8 : 4);
        pattern.lengthInBars = anyInt (1, 4);
        pattern.realignment = static_cast<Realignment> (anyInt (0, 2));
        pattern.realignEveryBars = anyInt (1, 3);
        pattern.meterChangeBehaviour = static_cast<MeterChangeBehaviour> (anyInt (0, 1));
        pattern.numberOfLanes = anyInt (0, maximumNumberOfLanes);
        for (auto& lane : pattern.lanes)
        {
            lane.noteNumber = anyInt (36, 40); // few notes, so lanes collide and retrigger each other
            lane.channel = anyInt (9, 10);
            lane.length = anyInt (1, maximumStepsPerLane);
            lane.rate = rates[anyInt (0, 6)];
            lane.gateInSixtyFourths = anyInt (1, maximumGateInSixtyFourths);
            lane.isMuted = chance (0.1);
            lane.isSoloed = chance (0.05);
            for (auto& step : lane.steps)
            {
                step.isOn = chance (0.4);
                step.isAccented = chance (0.1);
                step.gateOverride = (std::int16_t) (chance (0.2) ? anyInt (1, 64) : 0);
            }
        }
        return pattern;
    };

    for (int round = 0; round < 20; ++round)
    {
        BeatsmithEngine engine;
        engine.setPattern (makeRandomPattern());
        NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);
        NoteBalanceChecker checker;

        SimulatedHost host;
        double position = 0.0;
        bool isPlaying = true;

        for (int block = 0; block < 3000; ++block)
        {
            // The user and the host do things.
            if (chance (0.01))
                engine.setPattern (makeRandomPattern());
            if (chance (0.01))
                host.tempoBeatsPerMinute = anyInt (40, 300);
            if (chance (0.01))
                host.sampleRate = chance (0.5) ? 44100.0 : 96000.0;
            if (chance (0.005))
                host.meterRegions = { { 0.0, anyInt (2, 15), chance (0.5) ? 8 : 4 }, { 32.0, anyInt (2, 15), 8 } };
            if (chance (0.01))
                position = anyInt (-4, 200) + anyInt (0, 3) * 0.25; // jump
            if (chance (0.005))
            {
                host.isLooping = ! host.isLooping;
                host.loopStartInQuarterNotes = anyInt (0, 20);
                host.loopEndInQuarterNotes = host.loopStartInQuarterNotes + anyInt (1, 9) + 0.5;
                host.splitsBlocksAtLoopEnd = chance (0.5);
            }
            if (chance (0.01))
                isPlaying = ! isPlaying;

            const int blockSize = anyInt (0, 4096);
            if (chance (0.005))
            {
                engine.releaseAllNotes (output); // bypass
                checker.check (output, std::max (1, blockSize));
                continue;
            }

            auto context = host.makeContext (position, blockSize);
            context.hostIsPlaying = isPlaying;
            engine.process (context, output);
            checker.check (output, blockSize);

            if (isPlaying)
            {
                position += blockSize * host.getQuarterNotesPerSample();
                if (host.isLooping && position >= host.loopEndInQuarterNotes && position > host.loopStartInQuarterNotes)
                    position -= host.loopEndInQuarterNotes - host.loopStartInQuarterNotes;
            }
        }

        auto stopped = host.makeContext (position, 256);
        stopped.hostIsPlaying = false;
        engine.process (stopped, output);
        checker.check (output, 256);

        EXPECT (checker.numberOfViolations == 0);
        EXPECT (checker.countSoundingNotes() == 0);
        EXPECT (engine.getNumberOfSoundingNotes() == 0);
    }
}

TEST_CASE ("real-time safety: processing allocates no memory")
{
    BeatsmithEngine engine;
    auto pattern = makeTimingTestPattern();
    pattern.lanes[0].gateInSixtyFourths = 64;
    engine.setPattern (pattern);
    NoteEventList output (BeatsmithEngine::maximumEventsPerBlock);

    SimulatedHost host;
    host.meterRegions = { { 0.0, 4, 4 }, { 8.0, 7, 8 } };
    host.isLooping = true;
    host.loopStartInQuarterNotes = 2.0;
    host.loopEndInQuarterNotes = 13.5;
    host.splitsBlocksAtLoopEnd = false;

    numberOfAllocations = 0;
    isCountingAllocations = true;
    double position = 0.0;
    for (int block = 0; block < 2000; ++block)
    {
        engine.process (host.makeContext (position, 333), output);
        position += 333 * host.getQuarterNotesPerSample();
        if (position >= host.loopEndInQuarterNotes)
            position -= host.loopEndInQuarterNotes - host.loopStartInQuarterNotes;
        if (block % 500 == 0)
            engine.setPattern (pattern);
    }
    engine.releaseAllNotes (output);
    isCountingAllocations = false;

    EXPECT (numberOfAllocations == 0);
}
