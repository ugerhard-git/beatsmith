/**
 * @file PatternState.h
 * @brief Saving a pattern into the project state and loading it back, safely.
 *
 * The XML format (inside the `BeatsmithState` element):
 *
 *     <Pattern meterSource="0" meterNumerator="7" meterDenominator="8" meterGrouping="2 2 3" lengthInBars="1"
 *              realignment="1" realignEveryBars="4" meterChangeBehaviour="0">
 *       <Lane note="36" channel="10" length="16" rateNoteValue="16" rateTupletSteps="1" rateTupletSpan="1"
 *             gate="2" defaultVelocity="100" accentVelocity="127" muted="0" soloed="0"
 *             steps="0:1:1:110:0 8:1:0:110:0"/>
 *       …
 *     </Pattern>
 *
 * `steps` lists only steps that differ from an untouched step, each as index:on:accent:velocity:gate.
 * Attribute names and enum values are stored in projects: never rename or renumber them, only add.
 */
#pragma once

#include "Engine/Pattern.h"

#include <juce_data_structures/juce_data_structures.h>

namespace beatsmith
{

/** Type of the pattern element in the saved state. */
inline const juce::Identifier patternStateType { "Pattern" };

/** @return The pattern as a ValueTree, ready to be added to the saved state. */
juce::ValueTree patternToValueTree (const Pattern& pattern);

/**
 * Reads a pattern written by patternToValueTree(). The data comes from project files, which may be damaged
 * or crafted, so every number is range-checked, at most 16 lanes and 128 steps per lane are read, malformed
 * step entries are skipped, and the result is passed through sanitizePattern().
 * @return False (and the pattern unchanged) if the tree is not a pattern at all.
 */
bool patternFromValueTree (const juce::ValueTree& tree, Pattern& pattern);

} // namespace beatsmith
