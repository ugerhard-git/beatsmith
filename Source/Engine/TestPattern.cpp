#include "Engine/TestPattern.h"

#include <initializer_list>

namespace beatsmith
{

namespace
{
/** Sets a lane up with a note, a rate and a length; all steps off. */
Lane makeLane (int noteNumber, StepRate rate, int length)
{
    Lane lane;
    lane.noteNumber = noteNumber;
    lane.rate = rate;
    lane.length = length;
    return lane;
}

void setTrigs (Lane& lane, std::initializer_list<int> stepIndices, int velocity)
{
    for (const int stepIndex : stepIndices)
    {
        auto& step = lane.steps[(std::size_t) stepIndex];
        step.isOn = true;
        step.velocity = (std::uint8_t) velocity;
    }
}
} // namespace

Pattern makeTimingTestPattern()
{
    Pattern pattern;
    pattern.meterSource = MeterSource::followHost;
    pattern.lengthInBars = 1;
    pattern.realignment = Realignment::everyPatternRestart;

    const StepRate sixteenth { 16, 1, 1 };
    const StepRate eighth { 8, 1, 1 };
    const StepRate quarterQuintuplet { 4, 5, 4 };

    auto kick = makeLane (GeneralMidiDrum::bassDrum, sixteenth, 16);
    setTrigs (kick, { 0, 8 }, 110);
    kick.steps[0].isAccented = true;

    auto snare = makeLane (GeneralMidiDrum::acousticSnare, sixteenth, 16);
    setTrigs (snare, { 4, 12 }, 105);

    auto hiHat = makeLane (GeneralMidiDrum::closedHiHat, eighth, 8);
    setTrigs (hiHat, { 0, 2, 4, 6 }, 100);
    setTrigs (hiHat, { 1, 3, 5, 7 }, 60);

    auto sideStick = makeLane (GeneralMidiDrum::sideStick, quarterQuintuplet, 5);
    setTrigs (sideStick, { 0, 1, 2, 3, 4 }, 80);

    auto cowbell = makeLane (GeneralMidiDrum::cowbell, sixteenth, 3);
    setTrigs (cowbell, { 0 }, 70);

    pattern.lanes[0] = kick;
    pattern.lanes[1] = snare;
    pattern.lanes[2] = hiHat;
    pattern.lanes[3] = sideStick;
    pattern.lanes[4] = cowbell;
    pattern.numberOfLanes = 5;
    return pattern;
}

} // namespace beatsmith
