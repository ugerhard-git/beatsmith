#include "Plugin/PatternState.h"

#include <limits>

namespace beatsmith
{

namespace
{
/** Attribute names. Stored in projects: never rename. */
namespace Attributes
{
const juce::Identifier meterSource { "meterSource" };
const juce::Identifier meterNumerator { "meterNumerator" };
const juce::Identifier meterDenominator { "meterDenominator" };
const juce::Identifier meterGrouping { "meterGrouping" };
const juce::Identifier lengthInBars { "lengthInBars" };
const juce::Identifier realignment { "realignment" };
const juce::Identifier realignEveryBars { "realignEveryBars" };
const juce::Identifier meterChangeBehaviour { "meterChangeBehaviour" };

const juce::Identifier note { "note" };
const juce::Identifier channel { "channel" };
const juce::Identifier length { "length" };
const juce::Identifier rateNoteValue { "rateNoteValue" };
const juce::Identifier rateTupletSteps { "rateTupletSteps" };
const juce::Identifier rateTupletSpan { "rateTupletSpan" };
const juce::Identifier gate { "gate" };
const juce::Identifier defaultVelocity { "defaultVelocity" };
const juce::Identifier accentVelocity { "accentVelocity" };
const juce::Identifier muted { "muted" };
const juce::Identifier soloed { "soloed" };
const juce::Identifier steps { "steps" };
} // namespace Attributes

const juce::Identifier laneType { "Lane" };

/** Fields of one step entry: index:on:accent:velocity:gate. */
constexpr int fieldsPerStepEntry = 5;

/**
 * @return A number from text, limited to the int range so that overlong digits cannot overflow. Text that is
 *         not a number gives 0; sanitizePattern() later brings it into its allowed range.
 */
int toLimitedInt (const juce::String& text)
{
    const auto value = text.trim().getLargeIntValue();
    return (int) juce::jlimit<juce::int64> (std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), value);
}

/** @return An attribute as a number, or the fallback if the attribute is missing. */
int readInt (const juce::ValueTree& tree, const juce::Identifier& name, int fallback)
{
    return tree.hasProperty (name) ? toLimitedInt (tree[name].toString()) : fallback;
}

juce::String groupingToText (const Meter& meter)
{
    juce::StringArray groups;
    for (int groupIndex = 0; groupIndex < meter.numberOfGroups; ++groupIndex)
        groups.add (juce::String ((int) meter.groupSizes[(std::size_t) groupIndex]));
    return groups.joinIntoString (" ");
}

void readGrouping (const juce::String& text, Meter& meter)
{
    const auto groups = juce::StringArray::fromTokens (text, " ", "");
    meter.numberOfGroups = 0;
    meter.groupSizes.fill (0);
    for (const auto& group : groups)
    {
        if (meter.numberOfGroups >= maximumMeterNumerator)
            break;
        meter.groupSizes[(std::size_t) meter.numberOfGroups++] =
            (std::uint8_t) juce::jlimit (0, 255, toLimitedInt (group));
    }
    // A grouping that does not add up is replaced by the default one in sanitizePattern().
}

juce::String stepsToText (const Lane& lane)
{
    const Step untouched;
    juce::StringArray entries;
    for (int stepIndex = 0; stepIndex < maximumStepsPerLane; ++stepIndex)
    {
        const auto& step = lane.steps[(std::size_t) stepIndex];
        if (step == untouched)
            continue;
        entries.add (juce::String (stepIndex) + ":" + (step.isOn ? "1" : "0") + ":" + (step.isAccented ? "1" : "0") +
                     ":" + juce::String ((int) step.velocity) + ":" + juce::String ((int) step.gateOverride));
    }
    return entries.joinIntoString (" ");
}

void readSteps (const juce::String& text, Lane& lane)
{
    const auto entries = juce::StringArray::fromTokens (text, " ", "");
    // At most one entry per step is meaningful; ignore anything beyond a generous limit.
    const int entriesToRead = std::min (entries.size(), maximumStepsPerLane * 2);
    for (int entryIndex = 0; entryIndex < entriesToRead; ++entryIndex)
    {
        const auto fields = juce::StringArray::fromTokens (entries[entryIndex], ":", "");
        if (fields.size() != fieldsPerStepEntry)
            continue;
        const int stepIndex = toLimitedInt (fields[0]);
        if (stepIndex < 0 || stepIndex >= maximumStepsPerLane || ! fields[0].trim().containsOnly ("0123456789"))
            continue;
        auto& step = lane.steps[(std::size_t) stepIndex];
        step.isOn = toLimitedInt (fields[1]) != 0;
        step.isAccented = toLimitedInt (fields[2]) != 0;
        step.velocity = (std::uint8_t) juce::jlimit (1, 127, toLimitedInt (fields[3]));
        step.gateOverride = (std::int16_t) juce::jlimit (0, maximumGateInSixtyFourths, toLimitedInt (fields[4]));
    }
}
} // namespace

juce::ValueTree patternToValueTree (const Pattern& pattern)
{
    juce::ValueTree tree (patternStateType);
    tree.setProperty (Attributes::meterSource, (int) pattern.meterSource, nullptr);
    tree.setProperty (Attributes::meterNumerator, pattern.ownMeter.numerator, nullptr);
    tree.setProperty (Attributes::meterDenominator, pattern.ownMeter.denominator, nullptr);
    tree.setProperty (Attributes::meterGrouping, groupingToText (pattern.ownMeter), nullptr);
    tree.setProperty (Attributes::lengthInBars, pattern.lengthInBars, nullptr);
    tree.setProperty (Attributes::realignment, (int) pattern.realignment, nullptr);
    tree.setProperty (Attributes::realignEveryBars, pattern.realignEveryBars, nullptr);
    tree.setProperty (Attributes::meterChangeBehaviour, (int) pattern.meterChangeBehaviour, nullptr);

    const int numberOfLanes = juce::jlimit (0, maximumNumberOfLanes, pattern.numberOfLanes);
    for (int laneIndex = 0; laneIndex < numberOfLanes; ++laneIndex)
    {
        const auto& lane = pattern.lanes[(std::size_t) laneIndex];
        juce::ValueTree laneTree (laneType);
        laneTree.setProperty (Attributes::note, lane.noteNumber, nullptr);
        laneTree.setProperty (Attributes::channel, lane.channel, nullptr);
        laneTree.setProperty (Attributes::length, lane.length, nullptr);
        laneTree.setProperty (Attributes::rateNoteValue, lane.rate.noteValue, nullptr);
        laneTree.setProperty (Attributes::rateTupletSteps, lane.rate.tupletSteps, nullptr);
        laneTree.setProperty (Attributes::rateTupletSpan, lane.rate.tupletSpan, nullptr);
        laneTree.setProperty (Attributes::gate, lane.gateInSixtyFourths, nullptr);
        laneTree.setProperty (Attributes::defaultVelocity, lane.defaultVelocity, nullptr);
        laneTree.setProperty (Attributes::accentVelocity, lane.accentVelocity, nullptr);
        laneTree.setProperty (Attributes::muted, lane.isMuted ? 1 : 0, nullptr);
        laneTree.setProperty (Attributes::soloed, lane.isSoloed ? 1 : 0, nullptr);
        laneTree.setProperty (Attributes::steps, stepsToText (lane), nullptr);
        tree.appendChild (laneTree, nullptr);
    }
    return tree;
}

bool patternFromValueTree (const juce::ValueTree& tree, Pattern& pattern)
{
    if (! tree.isValid() || ! tree.hasType (patternStateType))
        return false;

    Pattern loaded; // starts from defaults, so missing attributes get sensible values
    loaded.meterSource = static_cast<MeterSource> (readInt (tree, Attributes::meterSource, 0));
    loaded.ownMeter.numerator = readInt (tree, Attributes::meterNumerator, 4);
    loaded.ownMeter.denominator = readInt (tree, Attributes::meterDenominator, 4);
    readGrouping (tree[Attributes::meterGrouping].toString(), loaded.ownMeter);
    loaded.lengthInBars = readInt (tree, Attributes::lengthInBars, 1);
    loaded.realignment = static_cast<Realignment> (readInt (tree, Attributes::realignment, 1));
    loaded.realignEveryBars = readInt (tree, Attributes::realignEveryBars, 4);
    loaded.meterChangeBehaviour =
        static_cast<MeterChangeBehaviour> (readInt (tree, Attributes::meterChangeBehaviour, 0));

    loaded.numberOfLanes = 0;
    for (const auto& laneTree : tree)
    {
        if (loaded.numberOfLanes >= maximumNumberOfLanes)
            break;
        if (! laneTree.hasType (laneType))
            continue;
        auto& lane = loaded.lanes[(std::size_t) loaded.numberOfLanes++];
        lane.noteNumber = readInt (laneTree, Attributes::note, 36);
        lane.channel = readInt (laneTree, Attributes::channel, 10);
        lane.length = readInt (laneTree, Attributes::length, 16);
        lane.rate.noteValue = readInt (laneTree, Attributes::rateNoteValue, 16);
        lane.rate.tupletSteps = readInt (laneTree, Attributes::rateTupletSteps, 1);
        lane.rate.tupletSpan = readInt (laneTree, Attributes::rateTupletSpan, 1);
        lane.gateInSixtyFourths = readInt (laneTree, Attributes::gate, 2);
        lane.defaultVelocity = readInt (laneTree, Attributes::defaultVelocity, 100);
        lane.accentVelocity = readInt (laneTree, Attributes::accentVelocity, 127);
        lane.isMuted = readInt (laneTree, Attributes::muted, 0) != 0;
        lane.isSoloed = readInt (laneTree, Attributes::soloed, 0) != 0;
        readSteps (laneTree[Attributes::steps].toString(), lane);
    }

    sanitizePattern (loaded);
    pattern = loaded;
    return true;
}

} // namespace beatsmith
