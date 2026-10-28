/**
 * @file TestPattern.h
 * @brief The built-in pattern of phase 1a, which plays until the editor exists (phase 1b).
 */
#pragma once

#include "Engine/Pattern.h"

namespace beatsmith
{

/** General MIDI drum notes used by the test pattern (and later by the GM default kit). */
namespace GeneralMidiDrum
{
constexpr int bassDrum = 36;
constexpr int sideStick = 37;
constexpr int acousticSnare = 38;
constexpr int closedHiHat = 42;
constexpr int cowbell = 56;
} // namespace GeneralMidiDrum

/**
 * @return A one-bar pattern that follows the host's meter and restarts every bar, on MIDI channel 10:
 *         - kick on 1/16 steps 1 and 9 (step 1 accented),
 *         - snare on 1/16 steps 5 and 13,
 *         - closed hi-hat on every 1/8, alternating loud and soft,
 *         - side stick: five even notes per 4/4 bar (quarter-note quintuplets, "5 in the time of 4"),
 *         - cowbell every third 1/16 (a 3-step lane against the 16-step lanes: a polymeter).
 *         In odd meters the lanes are cut at the bar line, which makes the meter audible.
 */
Pattern makeTimingTestPattern();

} // namespace beatsmith
