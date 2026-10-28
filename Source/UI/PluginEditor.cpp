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
    g.drawFittedText ("The step sequencer arrives with phase 1.\nThis version does not play anything yet.", area,
                      juce::Justification::centred, 2);
}

void BeatsmithEditor::resized()
{
    beatsmithProcessor.setEditorSize (getWidth(), getHeight());
}

} // namespace beatsmith
