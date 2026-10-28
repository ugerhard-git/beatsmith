/**
 * @file PatternTests.cpp
 * @brief Tests of the data model: exact durations, meters and groupings, and validation of untrusted patterns.
 */
#include "../TestFramework.h"

#include "Engine/Pattern.h"
#include "Engine/TestPattern.h"

#include <random>
#include <vector>

using namespace beatsmith;

namespace
{
std::vector<int> groupingOf (const Meter& meter)
{
    return std::vector<int> (meter.groupSizes.begin(), meter.groupSizes.begin() + meter.numberOfGroups);
}
} // namespace

TEST_CASE ("fraction: reduces and keeps the denominator positive")
{
    EXPECT (Fraction (6, 8) == Fraction (3, 4));
    EXPECT (Fraction (3, -4).getNumerator() == -3);
    EXPECT (Fraction (3, -4).getDenominator() == 4);
    EXPECT (Fraction (5, 0) == Fraction()); // no division by zero
    EXPECT (Fraction (1, 3) * Fraction (3, 4) == Fraction (1, 4));
    EXPECT (Fraction (1, 3) < Fraction (1, 2));
}

TEST_CASE ("step rate: durations are exact fractions of a quarter note")
{
    EXPECT ((StepRate { 16, 1, 1 }.getDurationInQuarterNotes() == Fraction (1, 4)));  // 1/16
    EXPECT ((StepRate { 8, 3, 2 }.getDurationInQuarterNotes() == Fraction (1, 3)));   // eighth triplet
    EXPECT ((StepRate { 8, 2, 3 }.getDurationInQuarterNotes() == Fraction (3, 4)));   // dotted eighth
    EXPECT ((StepRate { 4, 5, 4 }.getDurationInQuarterNotes() == Fraction (4, 5)));   // 5 in 4 quarters
    EXPECT ((StepRate { 16, 7, 4 }.getDurationInQuarterNotes() == Fraction (1, 7)));  // 7 in 4 sixteenths
    EXPECT ((StepRate { 64, 1, 1 }.getDurationInQuarterNotes() == Fraction (1, 16))); // shortest
}

TEST_CASE ("step rate: rejects invalid note values, tuplets and extreme durations")
{
    EXPECT (StepRate {}.isValid());
    EXPECT ((StepRate { 64, 1, 1 }.isValid()));
    EXPECT ((StepRate { 1, 1, 4 }.isValid()));     // four whole notes: the longest allowed
    EXPECT (! (StepRate { 3, 1, 1 }.isValid()));   // not a power of two
    EXPECT (! (StepRate { 128, 1, 1 }.isValid())); // shorter than a 1/64 note
    EXPECT (! (StepRate { 64, 2, 1 }.isValid()));  // 1/128 note
    EXPECT (! (StepRate { 16, 0, 1 }.isValid()));
    EXPECT (! (StepRate { 16, 1, 0 }.isValid()));
    EXPECT (! (StepRate { 16, 33, 1 }.isValid()));
    EXPECT (! (StepRate { 1, 1, 5 }.isValid())); // five whole notes
}

TEST_CASE ("meter: bar lengths of odd meters")
{
    EXPECT (makeMeterWithDefaultGrouping (7, 8).getBarLengthInQuarterNotes() == Fraction (7, 2));
    EXPECT (makeMeterWithDefaultGrouping (11, 8).getBarLengthInQuarterNotes() == Fraction (11, 2));
    EXPECT (makeMeterWithDefaultGrouping (5, 4).getBarLengthInQuarterNotes() == Fraction (5, 1));
    EXPECT (makeMeterWithDefaultGrouping (9, 8).getBarLengthInQuarterNotes() == Fraction (9, 2));
    EXPECT (makeMeterWithDefaultGrouping (3, 4).getBarLengthInQuarterNotes() == Fraction (3, 1));
}

TEST_CASE ("meter: default groupings")
{
    EXPECT ((groupingOf (makeMeterWithDefaultGrouping (7, 8)) == std::vector<int> { 2, 2, 3 }));
    EXPECT ((groupingOf (makeMeterWithDefaultGrouping (5, 8)) == std::vector<int> { 2, 3 }));
    EXPECT ((groupingOf (makeMeterWithDefaultGrouping (11, 8)) == std::vector<int> { 2, 2, 2, 2, 3 }));
    EXPECT ((groupingOf (makeMeterWithDefaultGrouping (6, 8)) == std::vector<int> { 3, 3 }));
    EXPECT ((groupingOf (makeMeterWithDefaultGrouping (9, 8)) == std::vector<int> { 3, 3, 3 }));
    EXPECT ((groupingOf (makeMeterWithDefaultGrouping (5, 4)) == std::vector<int> { 1, 1, 1, 1, 1 }));
    EXPECT ((groupingOf (makeMeterWithDefaultGrouping (3, 4)) == std::vector<int> { 1, 1, 1 }));
    EXPECT ((groupingOf (makeMeterWithDefaultGrouping (13, 16)) == std::vector<int> { 2, 2, 2, 2, 2, 3 }));
}

TEST_CASE ("sanitize: clamps every value of a garbage pattern into range")
{
    std::mt19937 random (1234);
    std::uniform_int_distribution<int> anyInt (-100000, 100000);

    for (int round = 0; round < 200; ++round)
    {
        Pattern pattern;
        pattern.meterSource = static_cast<MeterSource> (anyInt (random));
        pattern.realignment = static_cast<Realignment> (anyInt (random));
        pattern.meterChangeBehaviour = static_cast<MeterChangeBehaviour> (anyInt (random));
        pattern.ownMeter.numerator = anyInt (random);
        pattern.ownMeter.denominator = anyInt (random);
        pattern.ownMeter.numberOfGroups = anyInt (random);
        pattern.lengthInBars = anyInt (random);
        pattern.realignEveryBars = anyInt (random);
        pattern.numberOfLanes = anyInt (random);
        for (auto& lane : pattern.lanes)
        {
            lane.noteNumber = anyInt (random);
            lane.channel = anyInt (random);
            lane.length = anyInt (random);
            lane.rate = { anyInt (random), anyInt (random), anyInt (random) };
            lane.gateInSixtyFourths = anyInt (random);
            lane.defaultVelocity = anyInt (random);
            lane.accentVelocity = anyInt (random);
            for (auto& step : lane.steps)
            {
                step.velocity = (std::uint8_t) anyInt (random);
                step.gateOverride = (std::int16_t) anyInt (random);
            }
        }

        sanitizePattern (pattern);

        EXPECT ((pattern.meterSource == MeterSource::followHost || pattern.meterSource == MeterSource::ownMeter));
        EXPECT (pattern.ownMeter.numerator >= 1 && pattern.ownMeter.numerator <= maximumMeterNumerator);
        int groupSum = 0;
        for (int groupIndex = 0; groupIndex < pattern.ownMeter.numberOfGroups; ++groupIndex)
            groupSum += pattern.ownMeter.groupSizes[(std::size_t) groupIndex];
        EXPECT (groupSum == pattern.ownMeter.numerator);
        EXPECT (pattern.lengthInBars >= 1 && pattern.lengthInBars <= maximumBarsPerPattern);
        EXPECT (pattern.realignEveryBars >= 1 && pattern.realignEveryBars <= maximumBarsPerPattern);
        EXPECT (pattern.numberOfLanes >= 0 && pattern.numberOfLanes <= maximumNumberOfLanes);
        for (const auto& lane : pattern.lanes)
        {
            EXPECT (lane.noteNumber >= 0 && lane.noteNumber <= 127);
            EXPECT (lane.channel >= 1 && lane.channel <= 16);
            EXPECT (lane.length >= 1 && lane.length <= maximumStepsPerLane);
            EXPECT (lane.rate.isValid());
            EXPECT (lane.gateInSixtyFourths >= 1 && lane.gateInSixtyFourths <= maximumGateInSixtyFourths);
            EXPECT (lane.defaultVelocity >= 1 && lane.defaultVelocity <= 127);
            EXPECT (lane.accentVelocity >= 1 && lane.accentVelocity <= 127);
            for (const auto& step : lane.steps)
            {
                EXPECT (step.velocity >= 1 && step.velocity <= 127);
                EXPECT (step.gateOverride >= 0 && step.gateOverride <= maximumGateInSixtyFourths);
            }
        }
    }
}

TEST_CASE ("sanitize: keeps a valid pattern unchanged")
{
    auto pattern = makeTimingTestPattern();
    pattern.ownMeter = makeMeterWithDefaultGrouping (7, 8);
    pattern.ownMeter.groupSizes[0] = 3; // 3+2+2 is a valid grouping too
    pattern.ownMeter.groupSizes[2] = 2;
    auto copy = pattern;
    sanitizePattern (copy);

    EXPECT (copy.numberOfLanes == pattern.numberOfLanes);
    EXPECT ((groupingOf (copy.ownMeter) == std::vector<int> { 3, 2, 2 }));
    for (std::size_t laneIndex = 0; laneIndex < pattern.lanes.size(); ++laneIndex)
    {
        EXPECT (copy.lanes[laneIndex].rate == pattern.lanes[laneIndex].rate);
        EXPECT (copy.lanes[laneIndex].length == pattern.lanes[laneIndex].length);
        EXPECT (copy.lanes[laneIndex].noteNumber == pattern.lanes[laneIndex].noteNumber);
    }
}

TEST_CASE ("sanitize: a grouping that does not add up gets the default grouping")
{
    Pattern pattern;
    pattern.ownMeter.numerator = 7;
    pattern.ownMeter.denominator = 8;
    pattern.ownMeter.numberOfGroups = 2;
    pattern.ownMeter.groupSizes = { 3, 3 };
    sanitizePattern (pattern);
    EXPECT ((groupingOf (pattern.ownMeter) == std::vector<int> { 2, 2, 3 }));
}

TEST_CASE ("lane: accent velocity and gate overrides")
{
    Lane lane;
    lane.accentVelocity = 127;
    lane.gateInSixtyFourths = 2;
    Step step;
    step.velocity = 64;
    EXPECT (lane.getVelocityOf (step) == 64);
    step.isAccented = true;
    EXPECT (lane.getVelocityOf (step) == 127);
    EXPECT (lane.getGateOf (step) == Fraction (1, 8)); // a 1/32 note is 1/8 of a quarter
    step.gateOverride = 16;
    EXPECT (lane.getGateOf (step) == Fraction (1, 1)); // 16/64 note = one quarter
}
