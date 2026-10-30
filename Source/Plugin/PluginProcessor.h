/**
 * @file PluginProcessor.h
 * @brief The JUCE AudioProcessor: state saving and the bridge between the host and the BeatsmithEngine.
 */
#pragma once

#include "Engine/BeatsmithEngine.h"
#include "Engine/EditHistory.h"
#include "Engine/TripleBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <mutex>

namespace beatsmith
{

/**
 * Beatsmith's AudioProcessor, a MIDI effect: it has no audio inputs or outputs, passes incoming MIDI through
 * and adds the notes the engine produces.
 *
 * The processor owns the pattern the editor works on. Changes reach the audio thread through a lock-free
 * TripleBuffer; the audio thread never waits for the editor. The editor's copy, the undo history and the
 * saved state are guarded by a mutex that the audio thread never takes.
 */
class BeatsmithProcessor final : public juce::AudioProcessor
{
public:
    /** Version of the format written by getStateInformation(). Raise it when the format changes. */
    static constexpr int currentStateFormatVersion = 2;

    /** Type of the root element of the saved state; anything else is rejected when loading. */
    static inline const juce::Identifier stateType { "BeatsmithState" };

    /** Editor size limits; also used to clamp sizes read from saved state. */
    static constexpr int minimumEditorWidth = 640;
    static constexpr int minimumEditorHeight = 360;
    static constexpr int maximumEditorWidth = 3840;
    static constexpr int maximumEditorHeight = 2160;
    static constexpr int defaultEditorWidth = 960;
    static constexpr int defaultEditorHeight = 540;

    /** Grid zoom limits in pixels per quarter note; also used to clamp the zoom read from saved state. */
    static constexpr float minimumZoom = 12.0f;
    static constexpr float maximumZoom = 600.0f;
    static constexpr float defaultZoom = 120.0f;

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

    /** @return The grid zoom in pixels per quarter note (remembered in the state). */
    float getEditorZoom() const noexcept { return editorZoom.load(); }

    /** Remembers the grid zoom; values outside the limits are clamped. */
    void setEditorZoom (float pixelsPerQuarterNote) noexcept;

    //==============================================================================
    // The pattern (message thread)

    /** @return A copy of the pattern as the editor shows it. */
    Pattern getPattern() const;

    /**
     * Shows a change while the user is still editing (e.g. during a drag): the audio thread plays it at once,
     * but no undo step is recorded yet. Finish the gesture with commitPatternChange().
     */
    void showPatternChange (const Pattern& changedPattern);

    /**
     * Ends an edit: records one undo step from `patternBeforeEdit` to the current pattern (nothing if they are
     * equal) and tells the host that the project has changed.
     */
    void commitPatternChange (const Pattern& patternBeforeEdit);

    /** Replaces the pattern in one step, recording an undo step (a complete edit in one call). */
    void setPattern (const Pattern& newPattern);

    /** Undoes the last change. @return False if there was nothing to undo. */
    bool undo();

    /** Redoes the last undone change. @return False if there was nothing to redo. */
    bool redo();

    bool canUndo() const;
    bool canRedo() const;

    /** @return A number that changes whenever the pattern or its undo history changes (the editor compares it). */
    std::uint32_t getPatternVersion() const noexcept { return patternVersion.load(); }

    //==============================================================================
    // What the audio thread reports to the editor

    /** @return The last time signature the host reported (saved with the project; 4/4 before any). */
    int getLastHostTimeSignatureNumerator() const noexcept { return lastHostNumerator.load(); }
    int getLastHostTimeSignatureDenominator() const noexcept { return lastHostDenominator.load(); }

    /** @return The step a lane is playing (see BeatsmithEngine::getPlayingStepNumber()). */
    std::int64_t getPlayingStepNumber (int laneIndex) const noexcept;

private:
    /** Reads tempo and transport from the host's playhead, with safe fallbacks for missing values. */
    BlockContext readBlockContext (int numSamples) const;

    /** Adds the engine's note events to the MidiBuffer. */
    void addEngineOutputTo (juce::MidiBuffer& midi) const;

    /** Audio thread: takes a new pattern from the editor, if there is one. */
    void takePatternFromEditor() noexcept;

    /** Audio thread: publishes playheads and the host's time signature for the editor. */
    void publishReadoutsForEditor (const BlockContext& context) noexcept;

    /** Stores a pattern as the editor's copy and hands it to the audio thread. Call with patternLock held. */
    void storeAndPublish (const Pattern& newPattern);

    BeatsmithEngine engine;
    NoteEventList engineOutput { BeatsmithEngine::maximumEventsPerBlock };

    mutable std::mutex patternLock; ///< Guards editedPattern, history and the writer side of patternForAudio.
    Pattern editedPattern;
    EditHistory history;
    TripleBuffer<Pattern> patternForAudio;
    std::atomic<std::uint32_t> patternVersion { 0 };

    std::atomic<int> lastHostNumerator { 4 };
    std::atomic<int> lastHostDenominator { 4 };
    std::array<std::atomic<std::int64_t>, maximumNumberOfLanes> playingStepNumbers;

    std::atomic<int> editorWidth { defaultEditorWidth };
    std::atomic<int> editorHeight { defaultEditorHeight };
    std::atomic<float> editorZoom { defaultZoom };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatsmithProcessor)
};

} // namespace beatsmith
