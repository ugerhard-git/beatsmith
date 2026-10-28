/**
 * @file PluginProcessor.h
 * @brief The JUCE AudioProcessor: state saving and the bridge between the host and the BeatsmithEngine.
 */
#pragma once

#include "Engine/BeatsmithEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace beatsmith
{

/**
 * Beatsmith's AudioProcessor, a MIDI effect: it has no audio inputs or outputs, passes incoming MIDI through
 * and adds the notes the engine produces.
 */
class BeatsmithProcessor final : public juce::AudioProcessor
{
public:
    /** Version of the format written by getStateInformation(). Raise it when the format changes. */
    static constexpr int currentStateFormatVersion = 1;

    /** Type of the root element of the saved state; anything else is rejected when loading. */
    static inline const juce::Identifier stateType { "BeatsmithState" };

    /** Editor size limits; also used to clamp sizes read from saved state. */
    static constexpr int minimumEditorWidth = 640;
    static constexpr int minimumEditorHeight = 360;
    static constexpr int maximumEditorWidth = 3840;
    static constexpr int maximumEditorHeight = 2160;
    static constexpr int defaultEditorWidth = 960;
    static constexpr int defaultEditorHeight = 540;

    BeatsmithProcessor();
    ~BeatsmithProcessor() override = default;

    //==============================================================================
    // juce::AudioProcessor

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override;
    void processBlockBypassed (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int /*index*/) override {}
    const juce::String getProgramName (int /*index*/) override { return {}; }
    void changeProgramName (int /*index*/, const juce::String& /*newName*/) override {}

    /** Writes the state as versioned XML. */
    void getStateInformation (juce::MemoryBlock& destination) override;

    /**
     * Restores the state written by getStateInformation(). The data comes from project files, which may be
     * damaged or crafted: anything that is not a Beatsmith state is ignored, and every value is clamped.
     */
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Editor size (remembered in the state so the window reopens at the size the user chose)

    /** @return The editor width to use when the editor opens. */
    int getEditorWidth() const noexcept { return editorWidth.load(); }

    /** @return The editor height to use when the editor opens. */
    int getEditorHeight() const noexcept { return editorHeight.load(); }

    /** Remembers the editor size; values outside the limits are clamped. */
    void setEditorSize (int width, int height) noexcept;

private:
    /** Reads tempo and transport from the host's playhead, with safe fallbacks for missing values. */
    BlockContext readBlockContext (int numSamples) const;

    /** Adds the engine's note events to the MidiBuffer. */
    void addEngineOutputTo (juce::MidiBuffer& midi) const;

    BeatsmithEngine engine;
    NoteEventList engineOutput { BeatsmithEngine::maximumEventsPerBlock };

    std::atomic<int> editorWidth { defaultEditorWidth };
    std::atomic<int> editorHeight { defaultEditorHeight };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatsmithProcessor)
};

} // namespace beatsmith
