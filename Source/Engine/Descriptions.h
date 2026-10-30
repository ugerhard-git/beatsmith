/**
 * @file Descriptions.h
 * @brief Texts the editor shows: General MIDI drum names, step rates and meters.
 */
#pragma once

#include "Engine/Pattern.h"

#include <string>

namespace beatsmith
{

/** @return The General MIDI drum name of a note (35 … 81), or "Note n" for other notes. */
std::string describeDrumNote (int noteNumber);

/**
 * @return A short text for a step rate: "1/16", "1/8T" (triplet), "1/8." (dotted), otherwise the tuplet in
 *         front of the note value, e.g. "5:4 1/4" for five steps in the time of four quarters.
 */
std::string describeStepRate (const StepRate& rate);

/** @return A meter with its grouping, e.g. "7/8 (2+2+3)"; the grouping is left out when every unit is a beat. */
std::string describeMeter (const Meter& meter);

} // namespace beatsmith
