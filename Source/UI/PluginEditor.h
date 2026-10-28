/**
 * @file PluginEditor.h
 * @brief Beatsmith's window. Phase 0: a resizable window with the header only; the grid arrives in phase 1.
 */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace beatsmith
{

class BeatsmithProcessor;

/** The editor window. It is resizable and remembers its size in the processor's state. */
class BeatsmithEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BeatsmithEditor (BeatsmithProcessor& processorToEdit);
    ~BeatsmithEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    BeatsmithProcessor& beatsmithProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatsmithEditor)
};

} // namespace beatsmith
