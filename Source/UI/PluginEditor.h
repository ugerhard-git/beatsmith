/**
 * @file PluginEditor.h
 * @brief Beatsmith's window: a header with undo/redo and zoom, and the step grid.
 */
#pragma once

#include "UI/PatternGrid.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>

namespace beatsmith
{

class BeatsmithProcessor;

/**
 * The editor window. It is resizable and remembers its size and zoom in the processor's state.
 *
 * A timer (30 times a second) picks up changes the editor did not make itself – undo from the keyboard,
 * a loaded project, the host's time signature – and moves the playheads.
 */
class BeatsmithEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BeatsmithEditor (BeatsmithProcessor& processorToEdit);
    ~BeatsmithEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Ctrl+Z undoes, Ctrl+Shift+Z and Ctrl+Y redo (Cmd on a Mac); other keys go back to the host. */
    bool keyPressed (const juce::KeyPress& key) override;

    /** @return The grid (for tests). */
    PatternGrid& getGrid() noexcept { return grid; }

    /** Picks up changes from the processor at once, as the timer would (for tests and snapshots). */
    void refreshFromProcessor();

private:
    void timerCallback() override;
    void updateButtons();

    BeatsmithProcessor& beatsmithProcessor;
    PatternGrid grid;

    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::TextButton zoomOutButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) }; // a minus sign
    juce::TextButton zoomInButton { "+" };
    juce::TooltipWindow tooltips { this };

    std::uint32_t shownPatternVersion = 0;
    int shownHostNumerator = 0;
    int shownHostDenominator = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatsmithEditor)
};

} // namespace beatsmith
