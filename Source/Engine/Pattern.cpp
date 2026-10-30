#include "Engine/Pattern.h"

#include <algorithm>

namespace beatsmith
{

namespace
{
bool isPowerOfTwoUpTo (int value, int largestAllowed)
{
    for (int candidate = 1; candidate <= largestAllowed; candidate *= 2)
        if (value == candidate)
            return true;
    return false;
}

/** @return True if the grouping has 1 … numerator groups, each at least 1, adding up to the numerator. */
bool hasValidGrouping (const Meter& meter)
{
    if (meter.numberOfGroups < 1 || meter.numberOfGroups > meter.numerator)
        return false;
    int sum = 0;
    for (int groupIndex = 0; groupIndex < meter.numberOfGroups; ++groupIndex)
    {
        if (meter.groupSizes[(std::size_t) groupIndex] < 1)
            return false;
        sum += meter.groupSizes[(std::size_t) groupIndex];
    }
    return sum == meter.numerator;
}

void sanitizeMeter (Meter& meter)
{
    if (meter.numerator < 1 || meter.numerator > maximumMeterNumerator || ! isPowerOfTwoUpTo (meter.denominator, 32))
    {
        meter = makeMeterWithDefaultGrouping (4, 4);
        return;
    }
    if (! hasValidGrouping (meter))
    {
        meter = makeMeterWithDefaultGrouping (meter.numerator, meter.denominator);
        return;
    }
    // Clear unused group slots, so equal meters compare and store equally.
    for (auto groupIndex = (std::size_t) meter.numberOfGroups; groupIndex < meter.groupSizes.size(); ++groupIndex)
        meter.groupSizes[groupIndex] = 0;
}

int clampVelocity (int velocity)
{
    return std::clamp (velocity, 1, 127);
}

void sanitizeLane (Lane& lane)
{
    lane.noteNumber = std::clamp (lane.noteNumber, 0, 127);
    lane.channel = std::clamp (lane.channel, 1, 16);
    lane.length = std::clamp (lane.length, 1, maximumStepsPerLane);
    if (! lane.rate.isValid())
        lane.rate = StepRate {};
    lane.gateInSixtyFourths = std::clamp (lane.gateInSixtyFourths, 1, maximumGateInSixtyFourths);
    lane.defaultVelocity = clampVelocity (lane.defaultVelocity);
    lane.accentVelocity = clampVelocity (lane.accentVelocity);

    for (auto& step : lane.steps)
    {
        step.velocity = (std::uint8_t) clampVelocity (step.velocity);
        step.gateOverride = (std::int16_t) std::clamp ((int) step.gateOverride, 0, maximumGateInSixtyFourths);
    }
}
} // namespace

bool StepRate::isValid() const
{
    if (! isPowerOfTwoUpTo (noteValue, 64))
        return false;
    if (tupletSteps < 1 || tupletSteps > maximumTupletNumber || tupletSpan < 1 || tupletSpan > maximumTupletNumber)
        return false;
    const auto duration = getDurationInQuarterNotes();
    return ! (duration < shortestStepDuration) && ! (longestStepDuration < duration);
}

Meter makeMeterWithDefaultGrouping (int numerator, int denominator)
{
    Meter meter;
    meter.numerator = std::clamp (numerator, 1, maximumMeterNumerator);
    meter.denominator = isPowerOfTwoUpTo (denominator, 32) ? denominator : 4;
    meter.groupSizes.fill (0);
    meter.numberOfGroups = 0;

    auto addGroup = [&meter] (int size)
    { meter.groupSizes[(std::size_t) meter.numberOfGroups++] = (std::uint8_t) size; };

    if (meter.denominator <= 4 || meter.numerator <= 2)
    {
        for (int unitIndex = 0; unitIndex < meter.numerator; ++unitIndex)
            addGroup (1);
    }
    else if (meter.numerator % 3 == 0)
    {
        for (int groupIndex = 0; groupIndex < meter.numerator / 3; ++groupIndex)
            addGroup (3);
    }
    else
    {
        // Twos, with a three at the end for odd numerators: 5 → 2+3, 7 → 2+2+3, 11 → 2+2+2+2+3.
        int remainingUnits = meter.numerator;
        while (remainingUnits > 0)
        {
            const int groupSize = (remainingUnits == 3) ? 3 : 2;
            addGroup (groupSize);
            remainingUnits -= groupSize;
        }
    }
    return meter;
}

void sanitizePattern (Pattern& pattern)
{
    if (pattern.meterSource != MeterSource::followHost && pattern.meterSource != MeterSource::ownMeter)
        pattern.meterSource = MeterSource::followHost;
    sanitizeMeter (pattern.ownMeter);
    pattern.lengthInBars = std::clamp (pattern.lengthInBars, 1, maximumBarsPerPattern);
    if (pattern.realignment != Realignment::songStartOnly && pattern.realignment != Realignment::everyPatternRestart &&
        pattern.realignment != Realignment::everyNumberOfBars)
        pattern.realignment = Realignment::everyPatternRestart;
    pattern.realignEveryBars = std::clamp (pattern.realignEveryBars, 1, maximumBarsPerPattern);
    if (pattern.meterChangeBehaviour != MeterChangeBehaviour::restartAtChange &&
        pattern.meterChangeBehaviour != MeterChangeBehaviour::keepPhase)
        pattern.meterChangeBehaviour = MeterChangeBehaviour::restartAtChange;
    pattern.numberOfLanes = std::clamp (pattern.numberOfLanes, 0, maximumNumberOfLanes);

    for (auto& lane : pattern.lanes)
        sanitizeLane (lane);
}

bool operator== (const Meter& first, const Meter& second) noexcept
{
    if (first.numerator != second.numerator || first.denominator != second.denominator ||
        first.numberOfGroups != second.numberOfGroups)
        return false;
    const auto numberOfGroups = (std::size_t) std::clamp (first.numberOfGroups, 0, maximumMeterNumerator);
    return std::equal (first.groupSizes.begin(), first.groupSizes.begin() + (std::ptrdiff_t) numberOfGroups,
                       second.groupSizes.begin());
}

bool operator== (const Step& first, const Step& second) noexcept
{
    return first.isOn == second.isOn && first.isAccented == second.isAccented && first.velocity == second.velocity &&
           first.gateOverride == second.gateOverride;
}

bool operator== (const Lane& first, const Lane& second) noexcept
{
    return first.noteNumber == second.noteNumber && first.channel == second.channel && first.length == second.length &&
           first.rate == second.rate && first.gateInSixtyFourths == second.gateInSixtyFourths &&
           first.defaultVelocity == second.defaultVelocity && first.accentVelocity == second.accentVelocity &&
           first.isMuted == second.isMuted && first.isSoloed == second.isSoloed && first.steps == second.steps;
}

bool operator== (const Pattern& first, const Pattern& second) noexcept
{
    return first.meterSource == second.meterSource && first.ownMeter == second.ownMeter &&
           first.lengthInBars == second.lengthInBars && first.realignment == second.realignment &&
           first.realignEveryBars == second.realignEveryBars &&
           first.meterChangeBehaviour == second.meterChangeBehaviour && first.numberOfLanes == second.numberOfLanes &&
           first.lanes == second.lanes;
}

} // namespace beatsmith
