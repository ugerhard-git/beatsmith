#include "Plugin/PluginProcessor.h"

#include "UI/PluginEditor.h"

#include <cmath>

namespace beatsmith
{

namespace
{
/** Property names inside the saved state. Stored in projects: never rename. */
namespace StateProperties
{
const juce::Identifier formatVersion { "formatVersion" };
const juce::Identifier editorWidth { "editorWidth" };
const juce::Identifier editorHeight { "editorHeight" };
} // namespace StateProperties

/** Saved state larger than this is not even parsed; real states are a few kilobytes. */
constexpr int maximumStateSizeInBytes = 16 * 1024 * 1024;
} // namespace

BeatsmithProcessor::BeatsmithProcessor() : AudioProcessor (BusesProperties()) // MIDI only: no audio inputs or outputs
{
}

void BeatsmithProcessor::prepareToPlay (double /*sampleRate*/, int /*maximumBlockSize*/) {}

void BeatsmithProcessor::releaseResources() {}

bool BeatsmithProcessor::isBusesLayoutSupported (const BusesLayout& /*layouts*/) const
{
    // A MIDI effect has no audio buses; accept whatever empty layout the host offers.
    return true;
}

BlockContext BeatsmithProcessor::readBlockContext (int numSamples) const
{
    BlockContext context;
    context.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    context.numSamples = std::max (0, numSamples);

    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto bpm = position->getBpm(); bpm && std::isfinite (*bpm) && *bpm > 0.0)
                context.tempoBeatsPerMinute = juce::jlimit (10.0, 1000.0, *bpm); // guard against nonsense
            context.hostIsPlaying = position->getIsPlaying();
        }
    }
    return context;
}

void BeatsmithProcessor::addEngineOutputTo (juce::MidiBuffer& midi) const
{
    for (const auto& event : engineOutput.getEvents())
    {
        const int channel = juce::jlimit (1, 16, event.channel);
        const int noteNumber = juce::jlimit (0, 127, event.noteNumber);
        const auto message =
            event.isNoteOn
                ? juce::MidiMessage::noteOn (channel, noteNumber, (juce::uint8) juce::jlimit (1, 127, event.velocity))
                : juce::MidiMessage::noteOff (channel, noteNumber);
        midi.addEvent (message, std::max (0, event.samplePosition));
    }
}

void BeatsmithProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();

    // Incoming MIDI stays in the buffer (it passes through to the instrument); the engine's notes are added.
    engine.process (readBlockContext (audio.getNumSamples()), engineOutput);
    addEngineOutputTo (midi);
}

void BeatsmithProcessor::processBlockBypassed (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    audio.clear();

    // End whatever is sounding, so bypassing never leaves a note hanging; incoming MIDI passes through.
    engine.releaseAllNotes (engineOutput);
    addEngineOutputTo (midi);
}

juce::AudioProcessorEditor* BeatsmithProcessor::createEditor()
{
    return new BeatsmithEditor (*this);
}

void BeatsmithProcessor::setEditorSize (int width, int height) noexcept
{
    editorWidth.store (juce::jlimit (minimumEditorWidth, maximumEditorWidth, width));
    editorHeight.store (juce::jlimit (minimumEditorHeight, maximumEditorHeight, height));
}

void BeatsmithProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    juce::ValueTree state (stateType);
    state.setProperty (StateProperties::formatVersion, currentStateFormatVersion, nullptr);
    state.setProperty (StateProperties::editorWidth, getEditorWidth(), nullptr);
    state.setProperty (StateProperties::editorHeight, getEditorHeight(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void BeatsmithProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > maximumStateSizeInBytes)
        return;

    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateType.toString()))
        return;

    const auto state = juce::ValueTree::fromXml (*xml);
    const int formatVersion = state.getProperty (StateProperties::formatVersion, 0);
    if (formatVersion < 1)
        return; // not a state this or any later Beatsmith wrote

    // A newer format is read as far as this version understands it; unknown properties are ignored.
    setEditorSize (state.getProperty (StateProperties::editorWidth, defaultEditorWidth),
                   state.getProperty (StateProperties::editorHeight, defaultEditorHeight));
}

} // namespace beatsmith

/** Entry point JUCE's plug-in wrapper calls to create the processor. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new beatsmith::BeatsmithProcessor();
}
