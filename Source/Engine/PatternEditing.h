/**
 * @file PatternEditing.h
 * @brief The edits the grid makes to a pattern, and the rules for telling mouse gestures apart.
 *
 * Kept free of JUCE so the editing rules can be tested with the engine. Every function accepts any lane and
 * step index and ignores out-of-range ones, so a stray mouse position can never write outside the pattern.
 */
#pragma once

#include "Engine/Pattern.h"

namespace beatsmith
{

/** Turns a step on (at the lane's default velocity, not accented) if it is off, and off if it is on. */
void toggleStep (Pattern& pattern, int laneIndex, int stepIndex);

/**
 * Sets a step on or off, as painting does. A step that is switched on gets the lane's default velocity and
 * no accent; a step that already has the wanted state is left exactly as it is.
 */
void setStepOn (Pattern& pattern, int laneIndex, int stepIndex, bool shouldBeOn);

/**
 * Sets the velocity of a step that is on (clamped to 1 … 127) and removes its accent, so the new velocity is
 * what plays. Steps that are off are left unchanged.
 */
void setStepVelocity (Pattern& pattern, int laneIndex, int stepIndex, int velocity);

/** Toggles a step's accent. A step that is off is switched on with an accent. */
void toggleAccent (Pattern& pattern, int laneIndex, int stepIndex);

//==============================================================================
/** What a mouse drag on a step does, decided by its first movement. */
enum class DragKind
{
    undecided, ///< The mouse has not moved far enough yet; releasing it now is a click.
    paint,     ///< Mostly sideways: paint steps on or off along the lane.
    velocity,  ///< Mostly up/down on a step that is on: change its velocity.
    nothing    ///< Mostly up/down on a step that is off: no effect.
};

/** How far (in pixels) the mouse must move before a drag is decided. */
constexpr int dragDecisionDistance = 4;

/** Pixels of vertical movement for the full velocity range 1 … 127. */
constexpr int pixelsForFullVelocityRange = 200;

/**
 * @return What a drag does, given how far the mouse has moved from where it was pressed.
 * @param deltaX, deltaY Movement in pixels since the mouse was pressed (y grows downwards).
 * @param stepIsOn Whether the step under the mouse was on when it was pressed.
 */
DragKind decideDragKind (int deltaX, int deltaY, bool stepIsOn) noexcept;

/** @return The velocity after dragging vertically by deltaY pixels (up = louder), clamped to 1 … 127. */
int velocityAfterVerticalDrag (int velocityWhenPressed, int deltaY) noexcept;

} // namespace beatsmith
