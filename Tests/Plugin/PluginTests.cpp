/**
 * @file PluginTests.cpp
 * @brief Tests of the JUCE plug-in layer: plug-in type, MIDI pass-through, bypass, state and editor.
 *
 * Usage: beatsmith_plugin_tests [name filter] [--snapshot-dir folder]
 * With --snapshot-dir the editor is rendered to editor.png in that folder. CI publishes it for visual
 * review, and it is used in the manual.
 */
#include "../TestFramework.h"

#include "Plugin/PluginProcessor.h"
#include "UI/PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

using namespace beatsmith;

namespace
{
constexpr double testSampleRate = 48000.0;
constexpr int testBlockSize = 512;

/** A playhead that reports a fixed tempo and transport state, as a host would. */
class FakePlayHead final : public juce::AudioPlayHead
{
public:
    explicit FakePlayHead (bool shouldBePlaying) : isPlaying (shouldBePlaying) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (120.0);
        info.setIsPlaying (isPlaying);
        info.setPpqPosition (0.0);
        info.setTimeSignature (TimeSignature { 7, 8 });
        return info;
    }

private:
    bool isPlaying;
};

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

TEST_CASE ("plugin: produces nothing on its own, playing or stopped")
{
    for (const bool hostIsPlaying : { true, false })
    {
        BeatsmithProcessor processor;
        FakePlayHead playHead (hostIsPlaying);
        processor.setPlayHead (&playHead);
        processor.prepareToPlay (testSampleRate, testBlockSize);
        for (int blockIndex = 0; blockIndex < 50; ++blockIndex)
            EXPECT (processBlock (processor, {}).isEmpty());
        processor.setPlayHead (nullptr);
    }
}

TEST_CASE ("plugin: works without a playhead")
{
    BeatsmithProcessor processor;
    processor.prepareToPlay (testSampleRate, testBlockSize);
    EXPECT (processBlock (processor, {}).isEmpty());
}

TEST_CASE ("plugin: incoming MIDI passes through unchanged")
{
    BeatsmithProcessor processor;
    FakePlayHead playHead (true);
    processor.setPlayHead (&playHead);
    processor.prepareToPlay (testSampleRate, testBlockSize);

    const auto input = exampleIncomingMidi();
    EXPECT (toEventList (processBlock (processor, input)) == toEventList (input));
    processor.setPlayHead (nullptr);
}

TEST_CASE ("plugin: incoming MIDI passes through while bypassed")
{
    BeatsmithProcessor processor;
    processor.prepareToPlay (testSampleRate, testBlockSize);
    const auto input = exampleIncomingMidi();
    EXPECT (toEventList (processBlock (processor, input, true)) == toEventList (input));
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

namespace
{
/** Renders the editor to editor.png at twice the size, for the manual and for CI review. */
void writeEditorSnapshot (const juce::File& directory)
{
    BeatsmithProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    if (editor == nullptr)
        return;

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    const auto file = directory.getChildFile ("editor.png");
    file.deleteFile();
    {
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }
    std::printf ("Editor snapshot written to %s\n", file.getFullPathName().toRawUTF8());
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
        writeEditorSnapshot (snapshotDirectory);
    }
    return result;
}
