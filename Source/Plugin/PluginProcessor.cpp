#include "Plugin/PluginProcessor.h"

#include "Engine/TestPattern.h"
#include "Plugin/PatternState.h"
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
const juce::Identifier editorZoom { "editorZoom" };                    // since format 2
const juce::Identifier hostTimeSignatureNumerator { "hostNumerator" }; // since format 2
const juce::Identifier hostTimeSignatureDenominator { "hostDenominator" };
} // namespace StateProperties

bool isUsableTimeSignature (int numerator, int denominator) noexcept
{
    return numerator >= 1 && numerator <= maximumMeterNumerator &&
           (denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16 ||
            denominator == 32);
}

/** Saved state larger than this is not even parsed; real states are a few kilobytes. */
constexpr int maximumStateSizeInBytes = 16 * 1024 * 1024;
} // namespace

BeatsmithProcessor::BeatsmithProcessor()
    : AudioProcessor (BusesProperties()), // MIDI only: no audio inputs or outputs
      patternForAudio (makeTimingTestPattern())
{
    // New instances start with the test pattern, so there is something to edit until lanes can be added (1c).
    editedPattern = makeTimingTestPattern();
    sanitizePattern (editedPattern);
    engine.setPattern (editedPattern);
    for (auto& stepNumber : playingStepNumbers)
        stepNumber.store (BeatsmithEngine::noStepPlaying);
}

void BeatsmithProcessor::prepareToPlay (double /*sampleRate*/, int /*maximumBlockSize*/)
{
    // The host may have reset its transport: treat the next block as a jump (which also ends sounding notes).
    engine.markDiscontinuity();
}

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

    auto* hostPlayHead = getPlayHead();
    if (hostPlayHead == nullptr)
        return context;
    const auto position = hostPlayHead->getPosition();
    if (! position.hasValue())
        return context;

    // Every value is checked: hosts may leave values out or report nonsense. The engine handles missing ones.
    if (const auto bpm = position->getBpm(); bpm.hasValue() && std::isfinite (*bpm) && *bpm > 0.0)
        context.tempoBeatsPerMinute = juce::jlimit (10.0, 1000.0, *bpm);
    context.hostIsPlaying = position->getIsPlaying();

    if (const auto songPosition = position->getPpqPosition(); songPosition.hasValue() && std::isfinite (*songPosition))
    {
        context.hasPositionInQuarterNotes = true;
        context.positionInQuarterNotes = *songPosition;
    }
    if (const auto barStart = position->getPpqPositionOfLastBarStart();
        barStart.hasValue() && std::isfinite (*barStart))
    {
        context.hasLastBarStart = true;
        context.lastBarStartInQuarterNotes = *barStart;
    }
    if (const auto timeSignature = position->getTimeSignature(); timeSignature.hasValue())
    {
        context.hasTimeSignature = true;
        context.timeSignatureNumerator = timeSignature->numerator;
        context.timeSignatureDenominator = timeSignature->denominator;
    }
    context.isLooping = position->getIsLooping();
    if (const auto loop = position->getLoopPoints();
        loop.hasValue() && std::isfinite (loop->ppqStart) && std::isfinite (loop->ppqEnd))
    {
        context.hasLoopPoints = true;
        context.loopStartInQuarterNotes = loop->ppqStart;
        context.loopEndInQuarterNotes = loop->ppqEnd;
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

    takePatternFromEditor();

    // Incoming MIDI stays in the buffer (it passes through to the instrument); the engine's notes are added.
    const auto context = readBlockContext (audio.getNumSamples());
    engine.process (context, engineOutput);
    addEngineOutputTo (midi);
    publishReadoutsForEditor (context);
}

void BeatsmithProcessor::takePatternFromEditor() noexcept
{
    if (patternForAudio.takeNewestValue())
        engine.setPattern (patternForAudio.getReadValue());
}

void BeatsmithProcessor::publishReadoutsForEditor (const BlockContext& context) noexcept
{
    for (int laneIndex = 0; laneIndex < maximumNumberOfLanes; ++laneIndex)
        playingStepNumbers[(std::size_t) laneIndex].store (engine.getPlayingStepNumber (laneIndex),
                                                           std::memory_order_relaxed);
    if (context.hasTimeSignature &&
        isUsableTimeSignature (context.timeSignatureNumerator, context.timeSignatureDenominator))
    {
        lastHostNumerator.store (context.timeSignatureNumerator, std::memory_order_relaxed);
        lastHostDenominator.store (context.timeSignatureDenominator, std::memory_order_relaxed);
    }
}

std::int64_t BeatsmithProcessor::getPlayingStepNumber (int laneIndex) const noexcept
{
    if (laneIndex < 0 || laneIndex >= maximumNumberOfLanes)
        return BeatsmithEngine::noStepPlaying;
    return playingStepNumbers[(std::size_t) laneIndex].load (std::memory_order_relaxed);
}

void BeatsmithProcessor::processBlockBypassed (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    audio.clear();

    // End whatever is sounding, so bypassing never leaves a note hanging; incoming MIDI passes through.
    takePatternFromEditor();
    engine.releaseAllNotes (engineOutput);
    addEngineOutputTo (midi);
    for (auto& stepNumber : playingStepNumbers)
        stepNumber.store (BeatsmithEngine::noStepPlaying, std::memory_order_relaxed);
}

//==============================================================================
// The pattern

Pattern BeatsmithProcessor::getPattern() const
{
    const std::lock_guard<std::mutex> lock (patternLock);
    return editedPattern;
}

void BeatsmithProcessor::storeAndPublish (const Pattern& newPattern)
{
    editedPattern = newPattern;
    sanitizePattern (editedPattern);
    patternForAudio.write (editedPattern);
    ++patternVersion;
}

void BeatsmithProcessor::showPatternChange (const Pattern& changedPattern)
{
    const std::lock_guard<std::mutex> lock (patternLock);
    storeAndPublish (changedPattern);
}

void BeatsmithProcessor::commitPatternChange (const Pattern& patternBeforeEdit)
{
    bool hasChanged = false;
    {
        const std::lock_guard<std::mutex> lock (patternLock);
        hasChanged = patternBeforeEdit != editedPattern;
        history.recordChange (patternBeforeEdit, editedPattern);
        if (hasChanged)
            ++patternVersion; // what can be undone changed: the editor updates its buttons
    }
    if (hasChanged)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true)); // the project has changed
}

void BeatsmithProcessor::setPattern (const Pattern& newPattern)
{
    const auto before = getPattern();
    showPatternChange (newPattern);
    commitPatternChange (before);
}

bool BeatsmithProcessor::undo()
{
    {
        const std::lock_guard<std::mutex> lock (patternLock);
        Pattern previous;
        if (! history.undo (editedPattern, previous))
            return false;
        storeAndPublish (previous);
    }
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    return true;
}

bool BeatsmithProcessor::redo()
{
    {
        const std::lock_guard<std::mutex> lock (patternLock);
        Pattern next;
        if (! history.redo (editedPattern, next))
            return false;
        storeAndPublish (next);
    }
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    return true;
}

bool BeatsmithProcessor::canUndo() const
{
    const std::lock_guard<std::mutex> lock (patternLock);
    return history.canUndo();
}

bool BeatsmithProcessor::canRedo() const
{
    const std::lock_guard<std::mutex> lock (patternLock);
    return history.canRedo();
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

void BeatsmithProcessor::setEditorZoom (float pixelsPerQuarterNote) noexcept
{
    if (! std::isfinite (pixelsPerQuarterNote))
        pixelsPerQuarterNote = defaultZoom;
    editorZoom.store (juce::jlimit (minimumZoom, maximumZoom, pixelsPerQuarterNote));
}

void BeatsmithProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    juce::ValueTree state (stateType);
    state.setProperty (StateProperties::formatVersion, currentStateFormatVersion, nullptr);
    state.setProperty (StateProperties::editorWidth, getEditorWidth(), nullptr);
    state.setProperty (StateProperties::editorHeight, getEditorHeight(), nullptr);
    state.setProperty (StateProperties::editorZoom, (double) getEditorZoom(), nullptr);
    state.setProperty (StateProperties::hostTimeSignatureNumerator, getLastHostTimeSignatureNumerator(), nullptr);
    state.setProperty (StateProperties::hostTimeSignatureDenominator, getLastHostTimeSignatureDenominator(), nullptr);
    state.appendChild (patternToValueTree (getPattern()), nullptr);
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
    setEditorZoom ((float) (double) state.getProperty (StateProperties::editorZoom, (double) defaultZoom));

    const int hostNumerator = state.getProperty (StateProperties::hostTimeSignatureNumerator, 4);
    const int hostDenominator = state.getProperty (StateProperties::hostTimeSignatureDenominator, 4);
    if (isUsableTimeSignature (hostNumerator, hostDenominator))
    {
        lastHostNumerator.store (hostNumerator);
        lastHostDenominator.store (hostDenominator);
    }

    // Format 1 had no pattern: such projects get the starting pattern.
    Pattern loadedPattern = makeTimingTestPattern();
    patternFromValueTree (state.getChildWithName (patternStateType), loadedPattern);
    {
        const std::lock_guard<std::mutex> lock (patternLock);
        storeAndPublish (loadedPattern);
        history.clear(); // undo must not step back into a different project
    }
}

} // namespace beatsmith

/** Entry point JUCE's plug-in wrapper calls to create the processor. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new beatsmith::BeatsmithProcessor();
}
