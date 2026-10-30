#include "Engine/Descriptions.h"

#include <array>

namespace beatsmith
{

namespace
{
constexpr int firstGeneralMidiDrum = 35;

/** General MIDI percussion key map, notes 35 … 81. */
constexpr std::array<const char*, 47> generalMidiDrumNames {
    "Acoustic Bass Drum", "Bass Drum",     "Side Stick",     "Acoustic Snare", "Hand Clap",      "Electric Snare",
    "Low Floor Tom",      "Closed Hi-Hat", "High Floor Tom", "Pedal Hi-Hat",   "Low Tom",        "Open Hi-Hat",
    "Low-Mid Tom",        "Hi-Mid Tom",    "Crash Cymbal 1", "High Tom",       "Ride Cymbal 1",  "Chinese Cymbal",
    "Ride Bell",          "Tambourine",    "Splash Cymbal",  "Cowbell",        "Crash Cymbal 2", "Vibraslap",
    "Ride Cymbal 2",      "Hi Bongo",      "Low Bongo",      "Mute Hi Conga",  "Open Hi Conga",  "Low Conga",
    "High Timbale",       "Low Timbale",   "High Agogo",     "Low Agogo",      "Cabasa",         "Maracas",
    "Short Whistle",      "Long Whistle",  "Short Guiro",    "Long Guiro",     "Claves",         "Hi Wood Block",
    "Low Wood Block",     "Mute Cuica",    "Open Cuica",     "Mute Triangle",  "Open Triangle"
};
} // namespace

std::string describeDrumNote (int noteNumber)
{
    const int index = noteNumber - firstGeneralMidiDrum;
    if (index >= 0 && index < (int) generalMidiDrumNames.size())
        return generalMidiDrumNames[(std::size_t) index];
    return "Note " + std::to_string (noteNumber);
}

std::string describeStepRate (const StepRate& rate)
{
    std::string noteValue = "1/" + std::to_string (rate.noteValue);
    if (rate.tupletSteps == rate.tupletSpan)
        return noteValue;
    if (rate.tupletSteps == 3 && rate.tupletSpan == 2)
        return noteValue + "T";
    if (rate.tupletSteps == 2 && rate.tupletSpan == 3)
        return noteValue + ".";
    return std::to_string (rate.tupletSteps) + ":" + std::to_string (rate.tupletSpan) + " " + noteValue;
}

std::string describeMeter (const Meter& meter)
{
    std::string text = std::to_string (meter.numerator) + "/" + std::to_string (meter.denominator);
    bool everyUnitIsABeat = true;
    for (int groupIndex = 0; groupIndex < meter.numberOfGroups; ++groupIndex)
        everyUnitIsABeat = everyUnitIsABeat && meter.groupSizes[(std::size_t) groupIndex] == 1;
    if (everyUnitIsABeat || meter.numberOfGroups < 1)
        return text;

    text += " (";
    for (int groupIndex = 0; groupIndex < meter.numberOfGroups; ++groupIndex)
    {
        if (groupIndex > 0)
            text += "+";
        text += std::to_string ((int) meter.groupSizes[(std::size_t) groupIndex]);
    }
    return text + ")";
}

} // namespace beatsmith
