#include "UI/PluginEditor.h"

#include "Plugin/PluginProcessor.h"
#include "UI/Theme.h"

namespace beatsmith
{

namespace
{
constexpr int refreshesPerSecond = 30;
constexpr float zoomStep = 1.25f;
constexpr int buttonWidth = 64;
constexpr int smallButtonWidth = 32;
constexpr int buttonHeight = 28;
constexpr int buttonGap = 6;

void styleButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, Theme::Colours::panelBackground);
    button.setColour (juce::TextButton::textColourOffId, Theme::Colours::primaryText);
    button.setColour (juce::ComboBox::outlineColourId, Theme::Colours::panelOutline);
    button.setWantsKeyboardFocus (false);
}
} // namespace

BeatsmithEditor::BeatsmithEditor (BeatsmithProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit), beatsmithProcessor (processorToEdit), grid (processorToEdit)
{
    for (auto* button : { &undoButton, &redoButton, &zoomOutButton, &zoomInButton })
    {
        styleButton (*button);
        addAndMakeVisible (*button);
    }
    undoButton.setTooltip ("Undo the last change (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Shift+Z or Ctrl+Y)");
    zoomOutButton.setTooltip ("Zoom out (or Ctrl + mouse wheel)");
    zoomInButton.setTooltip ("Zoom in (or Ctrl + mouse wheel)");

    undoButton.onClick = [this]
    {
        beatsmithProcessor.undo();
        refreshFromProcessor();
    };
    redoButton.onClick = [this]
    {
        beatsmithProcessor.redo();
        refreshFromProcessor();
    };
    zoomOutButton.onClick = [this] { grid.setZoom (grid.getZoom() / zoomStep, (float) grid.getWidth() * 0.5f); };
    zoomInButton.onClick = [this] { grid.setZoom (grid.getZoom() * zoomStep, (float) grid.getWidth() * 0.5f); };
    addAndMakeVisible (grid);

    // Read the saved size first: setResizeLimits() and setSize() call resized(), which stores the size again.
    const int savedWidth = beatsmithProcessor.getEditorWidth();
    const int savedHeight = beatsmithProcessor.getEditorHeight();

    setResizable (true, true);
    setResizeLimits (BeatsmithProcessor::minimumEditorWidth, BeatsmithProcessor::minimumEditorHeight,
                     BeatsmithProcessor::maximumEditorWidth, BeatsmithProcessor::maximumEditorHeight);
    setSize (savedWidth, savedHeight);

    refreshFromProcessor();
    startTimerHz (refreshesPerSecond);
}

void BeatsmithEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::Colours::windowBackground);

    auto header = getLocalBounds().reduced (Theme::Layout::outerMargin, 0).removeFromTop (Theme::Layout::headerHeight);
    g.setColour (Theme::Colours::accent);
    g.setFont (Theme::font (28.0f, true));
    g.drawText ("Beatsmith", header.removeFromLeft (180), juce::Justification::centredLeft);

    g.setColour (Theme::Colours::secondaryText);
    g.setFont (Theme::font (13.0f));
    g.drawText (juce::String ("Version ") + JucePlugin_VersionString, header.removeFromLeft (120),
                juce::Justification::centredLeft);
}

void BeatsmithEditor::resized()
{
    beatsmithProcessor.setEditorSize (getWidth(), getHeight());

    auto area = getLocalBounds().reduced (Theme::Layout::outerMargin, 0);
    auto header = area.removeFromTop (Theme::Layout::headerHeight);
    const int buttonTop = header.getCentreY() - buttonHeight / 2;

    auto buttons = header.removeFromRight (2 * buttonWidth + 2 * smallButtonWidth + 4 * buttonGap);
    auto place = [&] (juce::Component& button, int width)
    {
        button.setBounds (buttons.removeFromLeft (width).withY (buttonTop).withHeight (buttonHeight));
        buttons.removeFromLeft (buttonGap);
    };
    place (zoomOutButton, smallButtonWidth);
    place (zoomInButton, smallButtonWidth);
    buttons.removeFromLeft (buttonGap);
    place (undoButton, buttonWidth);
    place (redoButton, buttonWidth);

    grid.setBounds (area.withTrimmedBottom (Theme::Layout::outerMargin));
}

bool BeatsmithEditor::keyPressed (const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    if (! modifiers.isCommandDown())
        return false; // e.g. the space bar: let the host start and stop
    const bool isZ = key.getKeyCode() == 'Z' || key.getKeyCode() == 'z';
    const bool isY = key.getKeyCode() == 'Y' || key.getKeyCode() == 'y';
    if (isZ && ! modifiers.isShiftDown())
        beatsmithProcessor.undo();
    else if ((isZ && modifiers.isShiftDown()) || isY)
        beatsmithProcessor.redo();
    else
        return false;
    refreshFromProcessor();
    return true;
}

void BeatsmithEditor::refreshFromProcessor()
{
    shownPatternVersion = beatsmithProcessor.getPatternVersion();
    shownHostNumerator = beatsmithProcessor.getLastHostTimeSignatureNumerator();
    shownHostDenominator = beatsmithProcessor.getLastHostTimeSignatureDenominator();
    grid.refreshFromProcessor();
    grid.refreshPlayheads();
    updateButtons();
}

void BeatsmithEditor::timerCallback()
{
    const bool patternChanged = beatsmithProcessor.getPatternVersion() != shownPatternVersion;
    const bool meterChanged = beatsmithProcessor.getLastHostTimeSignatureNumerator() != shownHostNumerator ||
                              beatsmithProcessor.getLastHostTimeSignatureDenominator() != shownHostDenominator;
    if (patternChanged || meterChanged)
        refreshFromProcessor();
    else
        grid.refreshPlayheads();
}

void BeatsmithEditor::updateButtons()
{
    undoButton.setEnabled (beatsmithProcessor.canUndo());
    redoButton.setEnabled (beatsmithProcessor.canRedo());
}

} // namespace beatsmith
