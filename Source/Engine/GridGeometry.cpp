#include "Engine/GridGeometry.h"

#include <algorithm>

namespace beatsmith
{

namespace
{
/** Positions closer than this (in quarter notes) count as equal. */
constexpr double tolerance = 1.0e-9;
} // namespace

Meter getDisplayedMeter (const Pattern& pattern, int hostNumerator, int hostDenominator)
{
    if (pattern.meterSource == MeterSource::ownMeter)
        return pattern.ownMeter;

    const bool isValidDenominator = hostDenominator == 1 || hostDenominator == 2 || hostDenominator == 4 ||
                                    hostDenominator == 8 || hostDenominator == 16 || hostDenominator == 32;
    if (hostNumerator < 1 || hostNumerator > maximumMeterNumerator || ! isValidDenominator)
        return makeMeterWithDefaultGrouping (4, 4);
    return makeMeterWithDefaultGrouping (hostNumerator, hostDenominator);
}

double getPatternLengthInQuarterNotes (const Pattern& pattern, const Meter& meter)
{
    return std::clamp (pattern.lengthInBars, 1, maximumBarsPerPattern) * meter.getBarLengthInQuarterNotes().toDouble();
}

double getGridLengthInQuarterNotes (const Pattern& pattern, const Meter& meter)
{
    double gridLength = getPatternLengthInQuarterNotes (pattern, meter);
    const int numberOfLanes = std::clamp (pattern.numberOfLanes, 0, maximumNumberOfLanes);
    for (int laneIndex = 0; laneIndex < numberOfLanes; ++laneIndex)
    {
        const auto& lane = pattern.lanes[(std::size_t) laneIndex];
        gridLength = std::max (gridLength, lane.length * lane.rate.getDurationInQuarterNotes().toDouble());
    }
    return gridLength;
}

std::vector<GridCell> getCellsOfLane (const Lane& lane, double patternLengthInQuarterNotes)
{
    std::vector<GridCell> cells;
    const double stepDuration = lane.rate.getDurationInQuarterNotes().toDouble();
    const int length = std::clamp (lane.length, 1, maximumStepsPerLane);
    if (stepDuration <= 0.0)
        return cells;

    for (std::int64_t stepNumber = 0; stepNumber < maximumGridCellsPerLane; ++stepNumber)
    {
        const double start = (double) stepNumber * stepDuration;
        const bool isRepeat = stepNumber >= length;
        const bool isAfterPatternEnd = start >= patternLengthInQuarterNotes - tolerance;
        if (isRepeat && isAfterPatternEnd)
            break; // repeats are only drawn up to the pattern end; own steps always

        GridCell cell;
        cell.stepNumber = stepNumber;
        cell.stepIndex = (int) (stepNumber % length);
        cell.startInQuarterNotes = start;
        cell.endInQuarterNotes = start + stepDuration;
        cell.isRepeat = isRepeat;
        cell.isAfterPatternEnd = isAfterPatternEnd;
        cells.push_back (cell);
    }
    return cells;
}

bool findCellAt (const std::vector<GridCell>& cells, double positionInQuarterNotes, GridCell& foundCell)
{
    // Cells are in time order and do not overlap: find the last one starting at or before the position.
    const auto after =
        std::upper_bound (cells.begin(), cells.end(), positionInQuarterNotes + tolerance,
                          [] (double position, const GridCell& cell) { return position < cell.startInQuarterNotes; });
    if (after == cells.begin())
        return false;
    const auto& candidate = *(after - 1);
    if (positionInQuarterNotes >= candidate.endInQuarterNotes - tolerance)
        return false;
    foundCell = candidate;
    return true;
}

std::vector<GridLine> getGridLines (const Meter& meter, int lengthInBars)
{
    std::vector<GridLine> lines;
    const double barLength = meter.getBarLengthInQuarterNotes().toDouble();
    const double unitLength = 4.0 / meter.denominator;
    const int bars = std::clamp (lengthInBars, 1, maximumBarsPerPattern);

    for (int barIndex = 0; barIndex < bars; ++barIndex)
    {
        const double barStart = barIndex * barLength;
        lines.push_back ({ barStart, true, barIndex + 1 });

        // Beat groups after the first one start inside the bar.
        int unitsSoFar = 0;
        for (int groupIndex = 0; groupIndex + 1 < meter.numberOfGroups; ++groupIndex)
        {
            unitsSoFar += meter.groupSizes[(std::size_t) groupIndex];
            lines.push_back ({ barStart + unitsSoFar * unitLength, false, 0 });
        }
    }
    lines.push_back ({ bars * barLength, true, bars + 1 }); // the end of the pattern
    return lines;
}

} // namespace beatsmith
