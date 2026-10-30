/**
 * @file PluginTests.cpp
 * @brief Tests of the JUCE plug-in layer: plug-in type, reading the host's transport, MIDI pass-through,
 *        bypass, the pattern (hand-over to the audio thread, undo, saving), playheads, and the step grid
 *        (clicks, drags, accents, keyboard undo).
 *
 * Usage: beatsmith_plugin_tests [name filter] [--snapshot-dir folder]
 * With --snapshot-dir the editor is rendered to editor.png (the starting pattern in 4/4) and
 * editor-odd-meter.png (7/8 from the host, while playing) in that folder. CI publishes them for visual review,
 * and they are used in the manual.
 */
#include "../TestFramework.h"

#include "Engine/PatternEditing.h"
#include "Engine/TestPattern.h"
#include "Plugin/PatternState.h"
#include "Plugin/PluginProcessor.h"
#include "UI/PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace beatsmith;

namespace
{
constexpr double testSampleRate = 48000.0;
constexpr int testBlockSize = 512;

/** A playhead that reports what a host would: tempo, transport, position, bar start and time signature. */
class FakePlayHead final : public juce::AudioPlayHead
{
public:
    explicit FakePlayHead (bool shouldBePlaying) : isPlaying (shouldBePlaying) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (120.0);
        info.setIsPlaying (isPlaying);
        info.setPpqPosition (positionInQuarterNotes);
        const double barLength = 4.0 * numerator / denominator;
        info.setPpqPositionOfLastBarStart (std::floor (positionInQuarterNotes / barLength + 1.0e-9) * barLength);
        info.setTimeSignature (TimeSignature { numerator, denominator });
        return info;
    }

    /** Moves the position on by one block, as the host does after each block. */
    void advanceByOneBlock() { positionInQuarterNotes += testBlockSize * 120.0 / 60.0 / testSampleRate; }

    bool isPlaying;
    double positionInQuarterNotes = 0.0;
    int numerator = 4;
    int denominator = 4;
};

/** Prepares a processor as a host does before playback. */
void prepare (BeatsmithProcessor& processor)
{
    processor.setRateAndBufferSizeDetails (testSampleRate, testBlockSize);
    processor.prepareToPlay (testSampleRate, testBlockSize);
}

/** @return The note numbers of all note-ons in a buffer. */
std::vector<int> noteOnNumbersIn (const juce::MidiBuffer& buffer)
{
    std::vector<int> noteNumbers;
    for (const auto metadata : buffer)
        if (metadata.getMessage().isNoteOn())
            noteNumbers.push_back (metadata.getMessage().getNoteNumber());
    std::sort (noteNumbers.begin(), noteNumbers.end());
    return noteNumbers;
}

/** Runs one block through the processor and returns the MIDI it produced. */
juce::MidiBuffer processBlock (BeatsmithProcessor& processor, const juce::MidiBuffer& input, bool bypassed = false)
{
    juce::AudioBuffer<float> noAudio (0, testBlockSize);
    juce::MidiBuffer midi (input);
    if (bypassed)
        processor.processBlockBypassed (noAudio, midi);
    else
        processor.processBlock (noAudio, midi);
    return midi;
}

/** Converts a MidiBuffer into a list of (sample position, raw bytes) for easy comparison. */
std::vector<std::pair<int, std::vector<juce::uint8>>> toEventList (const juce::MidiBuffer& buffer)
{
    std::vector<std::pair<int, std::vector<juce::uint8>>> events;
    for (const auto metadata : buffer)
        events.emplace_back (metadata.samplePosition,
                             std::vector<juce::uint8> (metadata.data, metadata.data + metadata.numBytes));
    return events;
}

/** Some typical incoming MIDI: a note, a controller and a SysEx message. */
juce::MidiBuffer exampleIncomingMidi()
{
    juce::MidiBuffer input;
    input.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    input.addEvent (juce::MidiMessage::controllerEvent (1, 1, 64), 100);
    const juce::uint8 sysexPayload[] = { 0x7d, 0x01, 0x02 };
    input.addEvent (juce::MidiMessage::createSysExMessage (sysexPayload, (int) sizeof (sysexPayload)), 200);
    input.addEvent (juce::MidiMessage::noteOff (1, 60), 300);
    return input;
}

/** Saves the processor's state into a memory block. */
juce::MemoryBlock saveState (BeatsmithProcessor& processor)
{
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    return state;
}

/** Loads an XML text as state, wrapped the way JUCE's copyXmlToBinary() wraps it. */
void loadStateFromXmlText (BeatsmithProcessor& processor, const juce::String& xmlText)
{
    juce::MemoryBlock state;
    if (auto xml = juce::parseXML (xmlText))
        juce::AudioProcessor::copyXmlToBinary (*xml, state);
    processor.setStateInformation (state.getData(), (int) state.getSize());
}
} // namespace

TEST_CASE ("plugin: is a MIDI effect without audio buses")
{
    BeatsmithProcessor processor;
    EXPECT (processor.isMidiEffect());
    EXPECT (processor.acceptsMidi());
    EXPECT (processor.producesMidi());
    EXPECT (processor.getTotalNumInputChannels() == 0);
    EXPECT (processor.getTotalNumOutputChannels() == 0);
    EXPECT (processor.getName() == "Beatsmith");
    EXPECT (processor.hasEditor());
}

TEST_CASE ("plugin: plays the test pattern on channel 10 while the host plays")
{
    BeatsmithProcessor processor;
    FakePlayHead playHead (true);
    processor.setPlayHead (&playHead);
    prepare (processor);

    const auto output = processBlock (processor, {});
    // Bar start: kick, closed hi-hat, side stick and cowbell; the snare comes on the second beat.
    EXPECT ((noteOnNumbersIn (output) == std::vector<int> { 36, 37, 42, 56 }));
    for (const auto metadata : output)
    {
        EXPECT (metadata.getMessage().getChannel() == 10);
        EXPECT (metadata.samplePosition == 0);
    }
    processor.setPlayHead (nullptr);
}

TEST_CASE ("plugin: follows the host's 7/8 bars")
{
    BeatsmithProcessor processor;
    FakePlayHead playHead (true);
    playHead.numerator = 7;
    playHead.denominator = 8;
    processor.setPlayHead (&playHead);
    prepare (processor);

    // Two bars of 7/8 at 120 BPM: the kick plays at the start of each bar (every 84000 samples) and on the
    // ninth 1/16 (48000 samples into the bar).
    std::vector<int> kickTimes;
    for (int blockIndex = 0; (blockIndex + 1) * testBlockSize <= 2 * 84000; ++blockIndex)
    {
        for (const auto metadata : processBlock (processor, {}))
            if (metadata.getMessage().isNoteOn() && metadata.getMessage().getNoteNumber() == 36)
                kickTimes.push_back (blockIndex * testBlockSize + metadata.samplePosition);
        playHead.advanceByOneBlock();
    }
    EXPECT ((kickTimes == std::vector<int> { 0, 48000, 84000, 132000 }));
    processor.setPlayHead (nullptr);
}

TEST_CASE ("plugin: silent while the host is stopped or without a playhead")
{
    BeatsmithProcessor stoppedProcessor;
    FakePlayHead stoppedPlayHead (false);
    stoppedProcessor.setPlayHead (&stoppedPlayHead);
    prepare (stoppedProcessor);
    for (int blockIndex = 0; blockIndex < 50; ++blockIndex)
        EXPECT (processBlock (stoppedProcessor, {}).isEmpty());
    stoppedProcessor.setPlayHead (nullptr);

    BeatsmithProcessor processorWithoutPlayHead;
    prepare (processorWithoutPlayHead);
    EXPECT (processBlock (processorWithoutPlayHead, {}).isEmpty());
}

TEST_CASE ("plugin: incoming MIDI passes through unchanged")
{
    BeatsmithProcessor processor;
    FakePlayHead playHead (false);
    processor.setPlayHead (&playHead);
    prepare (processor);
    const auto input = exampleIncomingMidi();
    EXPECT (toEventList (processBlock (processor, input)) == toEventList (input));

    // While playing, the input is still all there, with the pattern's notes added.
    playHead.isPlaying = true;
    const auto output = toEventList (processBlock (processor, input));
    for (const auto& event : toEventList (input))
        EXPECT (std::find (output.begin(), output.end(), event) != output.end());
    EXPECT (output.size() > toEventList (input).size());
    processor.setPlayHead (nullptr);
}

TEST_CASE ("plugin: bypassing ends sounding notes and passes MIDI through")
{
    BeatsmithProcessor processor;
    FakePlayHead playHead (true);
    processor.setPlayHead (&playHead);
    prepare (processor);

    const auto started = processBlock (processor, {}); // four notes start; their gates outlast the block
    const auto bypassed = processBlock (processor, exampleIncomingMidi(), true);

    int noteOffsAtStart = 0;
    for (const auto metadata : bypassed)
        if (metadata.getMessage().isNoteOff() && metadata.getMessage().getChannel() == 10 &&
            metadata.samplePosition == 0)
            ++noteOffsAtStart;
    EXPECT (noteOffsAtStart == (int) noteOnNumbersIn (started).size());
    for (const auto& event : toEventList (exampleIncomingMidi()))
    {
        const auto all = toEventList (bypassed);
        EXPECT (std::find (all.begin(), all.end(), event) != all.end());
    }

    // Bypassed again: nothing left to end.
    EXPECT (toEventList (processBlock (processor, {}, true)).empty());
    processor.setPlayHead (nullptr);
}

TEST_CASE ("state: editor size survives saving and loading")
{
    BeatsmithProcessor original;
    original.setEditorSize (1234, 777);
    const auto state = saveState (original);

    BeatsmithProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    EXPECT (restored.getEditorWidth() == 1234);
    EXPECT (restored.getEditorHeight() == 777);
}

TEST_CASE ("state: is versioned XML with Beatsmith's root element")
{
    BeatsmithProcessor processor;
    const auto state = saveState (processor);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    EXPECT (xml != nullptr);
    if (xml != nullptr)
    {
        EXPECT (xml->hasTagName ("BeatsmithState"));
        EXPECT (xml->getIntAttribute ("formatVersion") == BeatsmithProcessor::currentStateFormatVersion);
    }
}

TEST_CASE ("state: garbage, empty and foreign data are ignored")
{
    BeatsmithProcessor processor;
    processor.setEditorSize (1000, 600);

    const char garbage[] = "this is not a Beatsmith state at all";
    processor.setStateInformation (garbage, (int) sizeof (garbage));
    processor.setStateInformation (nullptr, 0);
    processor.setStateInformation (garbage, -5);
    loadStateFromXmlText (processor, R"(<MidiForgeState formatVersion="1" editorWidth="700" editorHeight="400"/>)");
    loadStateFromXmlText (processor, R"(<BeatsmithState editorWidth="700" editorHeight="400"/>)"); // no version

    EXPECT (processor.getEditorWidth() == 1000);
    EXPECT (processor.getEditorHeight() == 600);
}

TEST_CASE ("state: out-of-range and malformed values are clamped")
{
    BeatsmithProcessor processor;
    loadStateFromXmlText (processor, R"(<BeatsmithState formatVersion="1" editorWidth="99999" editorHeight="-3"/>)");
    EXPECT (processor.getEditorWidth() == BeatsmithProcessor::maximumEditorWidth);
    EXPECT (processor.getEditorHeight() == BeatsmithProcessor::minimumEditorHeight);

    loadStateFromXmlText (processor, R"(<BeatsmithState formatVersion="1" editorWidth="wide" editorHeight=""/>)");
    EXPECT (processor.getEditorWidth() == BeatsmithProcessor::minimumEditorWidth);
    EXPECT (processor.getEditorHeight() == BeatsmithProcessor::minimumEditorHeight);
}

TEST_CASE ("state: a newer format version is read as far as understood")
{
    BeatsmithProcessor processor;
    loadStateFromXmlText (processor,
                          R"(<BeatsmithState formatVersion="99" editorWidth="800" editorHeight="500" unknown="1">)"
                          R"(<SomethingFromTheFuture/></BeatsmithState>)");
    EXPECT (processor.getEditorWidth() == 800);
    EXPECT (processor.getEditorHeight() == 500);
}

TEST_CASE ("editor: opens at the remembered size and remembers new sizes")
{
    BeatsmithProcessor processor;
    processor.setEditorSize (1100, 650);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        EXPECT (editor != nullptr);
        if (editor == nullptr)
            return;
        EXPECT (editor->getWidth() == 1100);
        EXPECT (editor->getHeight() == 650);
        EXPECT (editor->isResizable());

        editor->setSize (900, 500);
        EXPECT (processor.getEditorWidth() == 900);
        EXPECT (processor.getEditorHeight() == 500);

        // Resizing below the limits (as a user dragging the corner would): the constrainer keeps the window usable.
        auto* constrainer = editor->getConstrainer();
        EXPECT (constrainer != nullptr);
        if (constrainer != nullptr)
        {
            constrainer->setBoundsForComponent (editor.get(), { 0, 0, 100, 100 }, false, false, true, true);
            EXPECT (editor->getWidth() == BeatsmithProcessor::minimumEditorWidth);
            EXPECT (editor->getHeight() == BeatsmithProcessor::minimumEditorHeight);
            EXPECT (processor.getEditorWidth() == BeatsmithProcessor::minimumEditorWidth);
        }
    }
}

//==============================================================================
// The pattern

namespace
{
/** A pattern with one lane (snare, channel 10) and no trigs. */
Pattern makeEmptySnarePattern()
{
    Pattern pattern;
    pattern.numberOfLanes = 1;
    pattern.lanes[0].noteNumber = GeneralMidiDrum::acousticSnare;
    return pattern;
}

/** Processes one block with a playing host at the song start and returns the note-on numbers. */
std::vector<int> noteOnsOfFirstBlock (BeatsmithProcessor& processor)
{
    FakePlayHead playHead (true);
    processor.setPlayHead (&playHead);
    prepare (processor);
    auto notes = noteOnNumbersIn (processBlock (processor, {}));
    processor.setPlayHead (nullptr);
    return notes;
}
} // namespace

TEST_CASE ("pattern: an edit reaches the audio thread")
{
    BeatsmithProcessor processor;
    auto pattern = makeEmptySnarePattern();
    toggleStep (pattern, 0, 0);
    processor.setPattern (pattern);
    EXPECT ((noteOnsOfFirstBlock (processor) == std::vector<int> { GeneralMidiDrum::acousticSnare }));

    processor.setPattern (makeEmptySnarePattern());
    FakePlayHead playHead (true);
    processor.setPlayHead (&playHead);
    EXPECT (noteOnNumbersIn (processBlock (processor, {})).empty());
    processor.setPlayHead (nullptr);
}

TEST_CASE ("pattern: undo and redo through the processor")
{
    BeatsmithProcessor processor;
    const auto start = processor.getPattern();
    EXPECT (! processor.canUndo());

    auto changed = start;
    toggleStep (changed, 1, 2);
    processor.setPattern (changed);
    EXPECT (processor.canUndo() && ! processor.canRedo());

    EXPECT (processor.undo());
    EXPECT (processor.getPattern() == start);
    EXPECT (processor.canRedo());
    EXPECT (processor.redo());
    EXPECT (processor.getPattern() == changed);

    // A drag shows changes as it goes, but records one undo step only when it ends.
    const auto beforeDrag = processor.getPattern();
    auto during = beforeDrag;
    toggleStep (during, 1, 3);
    processor.showPatternChange (during);
    toggleStep (during, 1, 4);
    processor.showPatternChange (during);
    processor.commitPatternChange (beforeDrag);
    EXPECT (processor.undo());
    EXPECT (processor.getPattern() == beforeDrag);
}

TEST_CASE ("pattern: the version number changes with every edit, undo and commit")
{
    BeatsmithProcessor processor;
    auto version = processor.getPatternVersion();
    auto changed = processor.getPattern();
    toggleStep (changed, 0, 1);
    processor.setPattern (changed);
    EXPECT (processor.getPatternVersion() != version);
    version = processor.getPatternVersion();
    processor.undo();
    EXPECT (processor.getPatternVersion() != version);
}

TEST_CASE ("state: the pattern survives saving and loading exactly")
{
    BeatsmithProcessor original;
    auto pattern = makeTimingTestPattern();
    pattern.meterSource = MeterSource::ownMeter;
    pattern.ownMeter = makeMeterWithDefaultGrouping (7, 8);
    pattern.ownMeter.groupSizes[0] = 3; // 3+2+2
    pattern.ownMeter.groupSizes[2] = 2;
    pattern.lengthInBars = 3;
    pattern.realignment = Realignment::everyNumberOfBars;
    pattern.realignEveryBars = 5;
    pattern.meterChangeBehaviour = MeterChangeBehaviour::keepPhase;
    pattern.lanes[1].isMuted = true;
    pattern.lanes[2].isSoloed = true;
    pattern.lanes[3].gateInSixtyFourths = 7;
    pattern.lanes[4].channel = 3;
    pattern.lanes[4].steps[127].isOn = true;
    pattern.lanes[4].steps[127].gateOverride = 33;
    pattern.lanes[0].steps[5].velocity = 17; // off, but with a changed velocity: kept too
    sanitizePattern (pattern);
    original.setPattern (pattern);

    juce::MemoryBlock state;
    original.getStateInformation (state);
    BeatsmithProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    EXPECT (restored.getPattern() == pattern);
    EXPECT (! restored.canUndo()); // loading starts a fresh history
}

TEST_CASE ("state: projects from format 1 get the starting pattern")
{
    BeatsmithProcessor processor;
    processor.setPattern (makeEmptySnarePattern());
    loadStateFromXmlText (processor, R"(<BeatsmithState formatVersion="1" editorWidth="800" editorHeight="500"/>)");
    auto expected = makeTimingTestPattern();
    sanitizePattern (expected);
    EXPECT (processor.getPattern() == expected);
}

TEST_CASE ("state: a damaged or crafted pattern is clamped, never trusted")
{
    juce::String lanes;
    for (int laneIndex = 0; laneIndex < 40; ++laneIndex) // more than 16 lanes
        lanes << R"(<Lane note="999" channel="-4" length="100000" rateNoteValue="3" rateTupletSteps="0"
                      rateTupletSpan="99999999999999999999" gate="-1" defaultVelocity="300" accentVelocity="0"
                      muted="7" steps="5:1:1:999:9999 -3:1:0:1:0 128:1:0:1:0 7:x:y:z:w 9:1:0 3abc:1:0:50:0
                                       2:1:0:64:0:extra 4:1:0:64:0"/>)";
    BeatsmithProcessor processor;
    loadStateFromXmlText (processor, R"(<BeatsmithState formatVersion="2"><Pattern meterSource="5" meterNumerator="-7"
                                        meterDenominator="9" meterGrouping="9 9 9 junk" lengthInBars="1000000"
                                        realignment="-1" realignEveryBars="0" meterChangeBehaviour="44">)" +
                                         lanes + "</Pattern></BeatsmithState>");
    const auto pattern = processor.getPattern();
    EXPECT (pattern.numberOfLanes == maximumNumberOfLanes);
    EXPECT (pattern.meterSource == MeterSource::followHost);
    EXPECT (pattern.ownMeter.numerator == 4 && pattern.ownMeter.denominator == 4);
    EXPECT (pattern.lengthInBars == maximumBarsPerPattern);
    EXPECT (pattern.realignment == Realignment::everyPatternRestart);
    EXPECT (pattern.realignEveryBars == 1);
    for (int laneIndex = 0; laneIndex < pattern.numberOfLanes; ++laneIndex)
    {
        const auto& lane = pattern.lanes[(std::size_t) laneIndex];
        EXPECT (lane.noteNumber == 127 && lane.channel == 1 && lane.length == maximumStepsPerLane);
        EXPECT (lane.rate.isValid());
        EXPECT (lane.gateInSixtyFourths == 1 && lane.defaultVelocity == 127 && lane.accentVelocity == 1);
        EXPECT (lane.isMuted);
        EXPECT (lane.steps[5].isOn && lane.steps[5].velocity == 127 &&
                lane.steps[5].gateOverride == maximumGateInSixtyFourths);
        EXPECT (! lane.steps[3].isOn); // "3abc" is not a step index
        EXPECT (! lane.steps[2].isOn); // six fields
        EXPECT (! lane.steps[9].isOn); // three fields
        EXPECT (lane.steps[4].isOn && lane.steps[4].velocity == 64);
    }

    // The pattern it plays is the clamped one: processing must work normally.
    FakePlayHead playHead (true);
    processor.setPlayHead (&playHead);
    prepare (processor);
    for (int blockIndex = 0; blockIndex < 20; ++blockIndex)
    {
        processBlock (processor, {});
        playHead.advanceByOneBlock();
    }
    processor.setPlayHead (nullptr);
}

TEST_CASE ("state: zoom and the host's last time signature are remembered and checked")
{
    BeatsmithProcessor original;
    original.setEditorZoom (250.0f);
    FakePlayHead playHead (true);
    playHead.numerator = 11;
    playHead.denominator = 8;
    original.setPlayHead (&playHead);
    prepare (original);
    processBlock (original, {});
    original.setPlayHead (nullptr);
    EXPECT (original.getLastHostTimeSignatureNumerator() == 11);
    EXPECT (original.getLastHostTimeSignatureDenominator() == 8);

    juce::MemoryBlock state;
    original.getStateInformation (state);
    BeatsmithProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    EXPECT (std::abs (restored.getEditorZoom() - 250.0f) < 0.001f);
    EXPECT (restored.getLastHostTimeSignatureNumerator() == 11);
    EXPECT (restored.getLastHostTimeSignatureDenominator() == 8);

    BeatsmithProcessor checked;
    loadStateFromXmlText (checked, R"(<BeatsmithState formatVersion="2" editorZoom="1e9" hostNumerator="0"
                                      hostDenominator="7"/>)");
    EXPECT (std::abs (checked.getEditorZoom() - BeatsmithProcessor::maximumZoom) < 0.001f);
    EXPECT (checked.getLastHostTimeSignatureNumerator() == 4);
    EXPECT (checked.getLastHostTimeSignatureDenominator() == 4);
}

TEST_CASE ("playheads: reported per lane while playing, cleared when stopped")
{
    BeatsmithProcessor processor;
    FakePlayHead playHead (true);
    processor.setPlayHead (&playHead);
    prepare (processor);
    for (int laneIndex = 0; laneIndex < maximumNumberOfLanes; ++laneIndex)
        EXPECT (processor.getPlayingStepNumber (laneIndex) == BeatsmithEngine::noStepPlaying);

    processBlock (processor, {}); // 512 samples: still the first 1/16 (6000 samples)
    EXPECT (processor.getPlayingStepNumber (0) == 0);
    EXPECT (processor.getPlayingStepNumber (5) == BeatsmithEngine::noStepPlaying); // no such lane

    playHead.isPlaying = false;
    processBlock (processor, {});
    EXPECT (processor.getPlayingStepNumber (0) == BeatsmithEngine::noStepPlaying);
    processor.setPlayHead (nullptr);
}

//==============================================================================
// The grid

namespace
{
/** Sends mouse events to a component, as a user would. */
class SimulatedMouse
{
public:
    explicit SimulatedMouse (juce::Component& componentToUse) : component (componentToUse) {}

    void press (juce::Point<float> position, juce::ModifierKeys modifiers = {})
    {
        downPosition = position;
        downTime = juce::Time::getCurrentTime();
        auto mods = modifiers.withFlags (juce::ModifierKeys::leftButtonModifier);
        if (modifiers.isPopupMenu())
            mods = modifiers.withFlags (juce::ModifierKeys::rightButtonModifier);
        buttonModifiers = mods;
        component.mouseDown (makeEvent (position, mods, false));
    }

    void dragTo (juce::Point<float> position) { component.mouseDrag (makeEvent (position, buttonModifiers, true)); }

    void release (juce::Point<float> position)
    {
        component.mouseUp (makeEvent (position, buttonModifiers, position != downPosition));
    }

    void click (juce::Point<float> position, juce::ModifierKeys modifiers = {})
    {
        press (position, modifiers);
        release (position);
    }

private:
    juce::MouseEvent makeEvent (juce::Point<float> position, juce::ModifierKeys modifiers, bool wasDragged) const
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position, modifiers, 0.0f, 0.0f,
                                 0.0f, 0.0f, 0.0f, &component, &component, juce::Time::getCurrentTime(), downPosition,
                                 downTime, 1, wasDragged);
    }

    juce::Component& component;
    juce::Point<float> downPosition;
    juce::Time downTime;
    juce::ModifierKeys buttonModifiers;
};

/** An editor on a processor with an empty one-lane pattern (snare, 1/16, 16 steps). */
struct GridTestRig
{
    GridTestRig()
    {
        processor.setPattern (makeEmptySnarePattern());
        editor.reset (dynamic_cast<BeatsmithEditor*> (processor.createEditorAndMakeActive()));
        if (editor != nullptr)
            editor->refreshFromProcessor();
    }

    PatternGrid& grid() { return editor->getGrid(); }
    /** @return A copy of a step of lane 0 (getPattern() returns a copy, so no reference may be kept). */
    Step step (int stepIndex) { return processor.getPattern().lanes[0].steps[(std::size_t) stepIndex]; }
    juce::Point<float> centreOf (std::int64_t stepNumber) { return grid().getCellBounds (0, stepNumber).getCentre(); }

    BeatsmithProcessor processor;
    std::unique_ptr<BeatsmithEditor> editor;
};
} // namespace

TEST_CASE ("grid: a click toggles a step, one undo step each")
{
    GridTestRig rig;
    EXPECT (rig.editor != nullptr);
    if (rig.editor == nullptr)
        return;
    SimulatedMouse mouse (rig.grid());

    mouse.click (rig.centreOf (3));
    EXPECT (rig.step (3).isOn);
    EXPECT (rig.step (3).velocity == 100);
    mouse.click (rig.centreOf (3));
    EXPECT (! rig.step (3).isOn);

    EXPECT (rig.processor.undo());
    EXPECT (rig.step (3).isOn);
    EXPECT (rig.processor.undo());
    EXPECT (! rig.step (3).isOn);
    EXPECT (rig.processor.canUndo()); // left: the rig's own switch to the empty pattern
}

TEST_CASE ("grid: Shift-click and right-click toggle the accent")
{
    GridTestRig rig;
    if (rig.editor == nullptr)
        return;
    SimulatedMouse mouse (rig.grid());
    mouse.click (rig.centreOf (0), juce::ModifierKeys::shiftModifier);
    EXPECT (rig.step (0).isOn && rig.step (0).isAccented);
    mouse.click (rig.centreOf (0), juce::ModifierKeys::rightButtonModifier);
    EXPECT (rig.step (0).isOn && ! rig.step (0).isAccented);
}

TEST_CASE ("grid: a sideways drag paints along the lane, as one undo step")
{
    GridTestRig rig;
    if (rig.editor == nullptr)
        return;
    SimulatedMouse mouse (rig.grid());
    const auto before = rig.processor.getPattern();

    mouse.press (rig.centreOf (2));
    mouse.dragTo (rig.centreOf (2).translated (6.0f, 0.0f)); // decides: paint
    mouse.dragTo (rig.centreOf (9));                         // one big jump: steps 3 … 8 are painted too
    mouse.release (rig.centreOf (9));
    for (int stepIndex = 0; stepIndex < 16; ++stepIndex)
        EXPECT (rig.step (stepIndex).isOn == (stepIndex >= 2 && stepIndex <= 9));

    // Painting from a step that is on erases.
    mouse.press (rig.centreOf (5));
    mouse.dragTo (rig.centreOf (5).translated (6.0f, 0.0f));
    mouse.dragTo (rig.centreOf (6));
    mouse.release (rig.centreOf (6));
    EXPECT (! rig.step (5).isOn && ! rig.step (6).isOn && rig.step (7).isOn);

    EXPECT (rig.processor.undo());
    EXPECT (rig.processor.undo());
    EXPECT (rig.processor.getPattern() == before);
}

TEST_CASE ("grid: a vertical drag on a trig sets its velocity; on an empty step it does nothing")
{
    GridTestRig rig;
    if (rig.editor == nullptr)
        return;
    SimulatedMouse mouse (rig.grid());
    mouse.click (rig.centreOf (4)); // on, velocity 100

    mouse.press (rig.centreOf (4));
    mouse.dragTo (rig.centreOf (4).translated (0.0f, 6.0f));
    mouse.dragTo (rig.centreOf (4).translated (0.0f, 80.0f)); // down: quieter
    mouse.release (rig.centreOf (4).translated (0.0f, 80.0f));
    EXPECT (rig.step (4).isOn);
    EXPECT (rig.step (4).velocity == velocityAfterVerticalDrag (100, 80));

    const auto before = rig.processor.getPattern();
    mouse.press (rig.centreOf (8));
    mouse.dragTo (rig.centreOf (8).translated (0.0f, -40.0f));
    mouse.release (rig.centreOf (8).translated (0.0f, -40.0f));
    EXPECT (rig.processor.getPattern() == before);
}

TEST_CASE ("grid: clicking a faint repeat edits the lane's own step")
{
    GridTestRig rig;
    if (rig.editor == nullptr)
        return;
    auto pattern = rig.processor.getPattern();
    pattern.lanes[0].length = 5; // in a 4/4 bar: 16 cells, steps 0 … 4 repeating
    rig.processor.setPattern (pattern);
    rig.editor->refreshFromProcessor();

    SimulatedMouse mouse (rig.grid());
    mouse.click (rig.centreOf (12)); // the third pass of step 2
    EXPECT (rig.step (2).isOn);
    EXPECT (! rig.step (12).isOn);
}

TEST_CASE ("grid: shows the host's time signature and lays out its bars")
{
    GridTestRig rig;
    if (rig.editor == nullptr)
        return;
    FakePlayHead playHead (true);
    playHead.numerator = 7;
    playHead.denominator = 8;
    rig.processor.setPlayHead (&playHead);
    prepare (rig.processor);
    processBlock (rig.processor, {});
    rig.processor.setPlayHead (nullptr);
    rig.editor->refreshFromProcessor();

    EXPECT (rig.grid().getDisplayedMeter().numerator == 7);
    EXPECT (rig.grid().getDisplayedMeter().denominator == 8);
    // 16 sixteenths in a 7/8 bar of 14: all 16 cells exist (the last two after the pattern end).
    EXPECT (! rig.grid().getCellBounds (0, 15).isEmpty());
    EXPECT (rig.grid().getCellBounds (0, 16).isEmpty());
}

TEST_CASE ("grid: zoom is clamped and remembered")
{
    GridTestRig rig;
    if (rig.editor == nullptr)
        return;
    const float before = rig.grid().getCellBounds (0, 1).getWidth();
    rig.grid().setZoom (rig.grid().getZoom() * 2.0f, 300.0f);
    EXPECT (rig.grid().getCellBounds (0, 1).getWidth() > before * 1.9f);
    EXPECT (std::abs (rig.processor.getEditorZoom() - rig.grid().getZoom()) < 0.001f);
    rig.grid().setZoom (1.0e6f, 300.0f);
    EXPECT (std::abs (rig.grid().getZoom() - BeatsmithProcessor::maximumZoom) < 0.001f);
    rig.grid().setZoom (0.0f, 300.0f);
    EXPECT (std::abs (rig.grid().getZoom() - BeatsmithProcessor::minimumZoom) < 0.001f);
}

TEST_CASE ("editor: Ctrl+Z undoes and Ctrl+Shift+Z / Ctrl+Y redo; other keys go to the host")
{
    GridTestRig rig;
    if (rig.editor == nullptr)
        return;
    SimulatedMouse mouse (rig.grid());
    mouse.click (rig.centreOf (1));
    EXPECT (rig.step (1).isOn);

    const auto command = juce::ModifierKeys::commandModifier;
    EXPECT (rig.editor->keyPressed (juce::KeyPress ('z', command, 0)));
    EXPECT (! rig.step (1).isOn);
    EXPECT (rig.editor->keyPressed (juce::KeyPress ('z', command | juce::ModifierKeys::shiftModifier, 0)));
    EXPECT (rig.step (1).isOn);
    EXPECT (rig.editor->keyPressed (juce::KeyPress ('z', command, 0)));
    EXPECT (rig.editor->keyPressed (juce::KeyPress ('y', command, 0)));
    EXPECT (rig.step (1).isOn);

    EXPECT (! rig.editor->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    EXPECT (! rig.editor->keyPressed (juce::KeyPress ('z')));
}

namespace
{
/** Renders an editor to a PNG file at twice the size. */
void writeSnapshot (juce::AudioProcessorEditor& editor, const juce::File& file)
{
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    {
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }
    std::printf ("Editor snapshot written to %s\n", file.getFullPathName().toRawUTF8());
}

/** Renders the editor for the manual and for CI review: the starting pattern, and 7/8 while playing. */
void writeEditorSnapshots (const juce::File& directory)
{
    {
        BeatsmithProcessor processor;
        std::unique_ptr<BeatsmithEditor> editor (
            dynamic_cast<BeatsmithEditor*> (processor.createEditorAndMakeActive()));
        if (editor == nullptr)
            return;
        editor->refreshFromProcessor();
        writeSnapshot (*editor, directory.getChildFile ("editor.png"));
    }
    {
        // 7/8 from the host, a lane longer than the bar, a polymeter lane, and playheads in the middle of the bar.
        BeatsmithProcessor processor;
        auto pattern = makeTimingTestPattern();
        pattern.lengthInBars = 2;
        pattern.lanes[4].length = 5;
        toggleAccent (pattern, 4, 0);
        toggleStep (pattern, 4, 3);
        setStepVelocity (pattern, 2, 1, 40);
        processor.setPattern (pattern);

        FakePlayHead playHead (true);
        playHead.numerator = 7;
        playHead.denominator = 8;
        playHead.positionInQuarterNotes = 4.6; // bar 2, a little in
        processor.setPlayHead (&playHead);
        prepare (processor);
        processBlock (processor, {});
        processor.setPlayHead (nullptr);
        processor.setEditorZoom (90.0f);

        std::unique_ptr<BeatsmithEditor> editor (
            dynamic_cast<BeatsmithEditor*> (processor.createEditorAndMakeActive()));
        if (editor == nullptr)
            return;
        editor->refreshFromProcessor();
        writeSnapshot (*editor, directory.getChildFile ("editor-odd-meter.png"));
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::String nameFilter;
    juce::File snapshotDirectory;
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const juce::String argument (argv[argumentIndex]);
        if (argument == "--snapshot-dir" && argumentIndex + 1 < argc)
            snapshotDirectory = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++argumentIndex]);
        else
            nameFilter = argument;
    }

    const int result = testing::runAllTests (nameFilter.toStdString());

    if (snapshotDirectory != juce::File())
    {
        snapshotDirectory.createDirectory();
        writeEditorSnapshots (snapshotDirectory);
    }
    return result;
}
