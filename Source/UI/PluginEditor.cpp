#include "UI/PluginEditor.h"

#include "Plugin/PluginProcessor.h"
#include "UI/Theme.h"

namespace beatsmith
{

BeatsmithEditor::BeatsmithEditor (BeatsmithProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit), beatsmithProcessor (processorToEdit)
{
    // Read the saved size first: setResizeLimits() and setSize() call resized(), which stores the size again.
    const int savedWidth = beatsmithProcessor.getEditorWidth();
    const int savedHeight = beatsmithProcessor.getEditorHeight();

    setResizable (true, true);
    setResizeLimits (BeatsmithProcessor::minimumEditorWidth, BeatsmithProcessor::minimumEditorHeight,
                     BeatsmithProcessor::maximumEditorWidth, BeatsmithProcessor::maximumEditorHeight);
    setSize (savedWidth, savedHeight);
}

void BeatsmithEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::Colours::windowBackground);

    auto area = getLocalBounds().reduced (Theme::Layout::outerMargin);
    auto header = area.removeFromTop (Theme::Layout::headerHeight);

    g.setColour (Theme::Colours::accent);
    g.setFont (Theme::font (28.0f, true));
    g.drawText ("Beatsmith", header.removeFromLeft (220), juce::Justification::centredLeft);

    g.setColour (Theme::Colours::secondaryText);
    g.setFont (Theme::font (14.0f));
    g.drawText (juce::String ("Version ") + JucePlugin_VersionString, header, juce::Justification::centredRight);

    area.removeFromTop (Theme::Layout::outerMargin);
    g.setColour (Theme::Colours::panelBackground);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (Theme::Colours::panelOutline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);

    g.setColour (Theme::Colours::primaryText);
    g.setFont (Theme::font (16.0f));
    g.drawFittedText ("Timing test version (phase 1a)\n\n"
                      "A built-in test pattern plays while your DAW plays:\n"
                      "kick, snare, closed hi-hat, side stick (5 per 4/4 bar) and cowbell (every 3rd 1/16),\n"
                      "General MIDI notes on MIDI channel 10.\n\n"
                      "The step grid arrives with phase 1b.",
                      area.reduced (Theme::Layout::outerMargin), juce::Justification::centred, 8);
}

void BeatsmithEditor::resized()
{
    beatsmithProcessor.setEditorSize (getWidth(), getHeight());
}

} // namespace beatsmith
