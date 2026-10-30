/**
 * @file GridGeometry.h
 * @brief Where the editor draws every step: positions in quarter notes for the grid, bar and beat-group lines.
 *
 * Kept free of JUCE (and of pixels) so it can be tested with the engine. The editor multiplies the positions
 * by its zoom (pixels per quarter note).
 */
#pragma once

#include "Engine/Pattern.h"

#include <cstdint>
#include <vector>

namespace beatsmith
{

/** One drawn cell of a lane: a step at a position in the grid. */
struct GridCell
{
    int stepIndex = 0;                ///< Which of the lane's steps this cell shows (0 … length - 1).
    std::int64_t stepNumber = 0;      ///< Counted from the grid start: stepIndex = stepNumber mod length.
    double startInQuarterNotes = 0.0; ///< From the grid start.
    double endInQuarterNotes = 0.0;
    bool isRepeat = false;          ///< A later pass through the lane's steps (drawn faintly).
    bool isAfterPatternEnd = false; ///< Starts at or after the pattern end: never plays (drawn greyed out).
};

/** A vertical line of the grid. */
struct GridLine
{
    double positionInQuarterNotes = 0.0;
    bool isBarLine = false; ///< A bar line (with barNumber); otherwise the start of a beat group.
    int barNumber = 0;      ///< 1-based, for bar lines.
};

/** Upper limit of cells per lane, so absurd pattern lengths cannot make the editor draw millions of cells. */
constexpr int maximumGridCellsPerLane = 4096;

/**
 * @return The meter the grid shows: the pattern's own meter, or – when the pattern follows the host – the
 *         host's last reported time signature with its default grouping (invalid values give 4/4).
 */
Meter getDisplayedMeter (const Pattern& pattern, int hostNumerator, int hostDenominator);

/** @return The length of one pattern cycle in quarter notes: lengthInBars bars of the meter. */
double getPatternLengthInQuarterNotes (const Pattern& pattern, const Meter& meter);

/** @return How far the grid reaches: the pattern length, or further if a lane's own steps reach beyond it. */
double getGridLengthInQuarterNotes (const Pattern& pattern, const Meter& meter);

/**
 * @return The cells of one lane, in time order: its own steps (all of them, also beyond the pattern end), then
 *         repeats up to the pattern end. At most maximumGridCellsPerLane cells.
 */
std::vector<GridCell> getCellsOfLane (const Lane& lane, double patternLengthInQuarterNotes);

/**
 * @return The cell of a lane at a position in the grid, if there is one.
 * @param cells The lane's cells from getCellsOfLane().
 * @param positionInQuarterNotes A position from the grid start.
 * @param foundCell Receives the cell.
 */
bool findCellAt (const std::vector<GridCell>& cells, double positionInQuarterNotes, GridCell& foundCell);

/** @return Bar lines and beat-group lines for the pattern length, in time order. */
std::vector<GridLine> getGridLines (const Meter& meter, int lengthInBars);

} // namespace beatsmith
