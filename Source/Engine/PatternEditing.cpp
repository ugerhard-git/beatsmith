#include "Engine/PatternEditing.h"

#include <algorithm>
#include <cstdlib>

namespace beatsmith
{

namespace
{
/** @return The step, or nullptr if the lane or step index is out of range. */
Step* findStep (Pattern& pattern, int laneIndex, int stepIndex)
{
    if (laneIndex < 0 || laneIndex >= maximumNumberOfLanes || stepIndex < 0 || stepIndex >= maximumStepsPerLane)
        return nullptr;
    return &pattern.lanes[(std::size_t) laneIndex].steps[(std::size_t) stepIndex];
}

void switchOn (Step& step, const Lane& lane)
{
    step.isOn = true;
    step.isAccented = false;
    step.velocity = (std::uint8_t) std::clamp (lane.defaultVelocity, 1, 127);
}
} // namespace

void toggleStep (Pattern& pattern, int laneIndex, int stepIndex)
{
    if (auto* step = findStep (pattern, laneIndex, stepIndex))
        setStepOn (pattern, laneIndex, stepIndex, ! step->isOn);
}

void setStepOn (Pattern& pattern, int laneIndex, int stepIndex, bool shouldBeOn)
{
    auto* step = findStep (pattern, laneIndex, stepIndex);
    if (step == nullptr || step->isOn == shouldBeOn)
        return;
    if (shouldBeOn)
        switchOn (*step, pattern.lanes[(std::size_t) laneIndex]);
    else
        step->isOn = false;
}

void setStepVelocity (Pattern& pattern, int laneIndex, int stepIndex, int velocity)
{
    auto* step = findStep (pattern, laneIndex, stepIndex);
    if (step == nullptr || ! step->isOn)
        return;
    step->velocity = (std::uint8_t) std::clamp (velocity, 1, 127);
    step->isAccented = false;
}

void toggleAccent (Pattern& pattern, int laneIndex, int stepIndex)
{
    auto* step = findStep (pattern, laneIndex, stepIndex);
    if (step == nullptr)
        return;
    if (! step->isOn)
    {
        switchOn (*step, pattern.lanes[(std::size_t) laneIndex]);
        step->isAccented = true;
        return;
    }
    step->isAccented = ! step->isAccented;
}

DragKind decideDragKind (int deltaX, int deltaY, bool stepIsOn) noexcept
{
    const int horizontal = std::abs (deltaX);
    const int vertical = std::abs (deltaY);
    if (std::max (horizontal, vertical) < dragDecisionDistance)
        return DragKind::undecided;
    if (horizontal >= vertical)
        return DragKind::paint;
    return stepIsOn ? DragKind::velocity : DragKind::nothing;
}

int velocityAfterVerticalDrag (int velocityWhenPressed, int deltaY) noexcept
{
    // Up (negative deltaY) makes the step louder.
    const int change = (-deltaY * 127) / pixelsForFullVelocityRange;
    return std::clamp (velocityWhenPressed + change, 1, 127);
}

} // namespace beatsmith
