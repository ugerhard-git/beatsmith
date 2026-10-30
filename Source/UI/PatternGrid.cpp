#include "UI/PatternGrid.h"

#include "Engine/BeatsmithEngine.h"
#include "Engine/Descriptions.h"
#include "Plugin/PluginProcessor.h"
#include "UI/Theme.h"

#include <cmath>

namespace beatsmith
{

namespace
{
/** Extra room after the end of the grid, in pixels, so the last step is not glued to the edge. */
constexpr float gridEndMargin = 24.0f;
} // namespace

PatternGrid::PatternGrid (BeatsmithProcessor& processorToEdit)
    : beatsmithProcessor (processorToEdit), zoom (processorToEdit.getEditorZoom())
{
    shownPlayheads.fill (BeatsmithEngine::noStepPlaying);
    for (auto* scrollBar : { &horizontalScrollBar, &verticalScrollBar })
    {
        scrollBar->setAutoHide (true);
        scrollBar->addListener (this);
        scrollBar->setColour (juce::ScrollBar::thumbColourId, Theme::Colours::barLine);
        addAndMakeVisible (*scrollBar);
    }
    setWantsKeyboardFocus (true); // for undo/redo keys, handled by the editor
    refreshFromProcessor();
}

PatternGrid::~PatternGrid()
{
    horizontalScrollBar.removeListener (this);
    verticalScrollBar.removeListener (this);
}

//==============================================================================
// Data

void PatternGrid::refreshFromProcessor()
{
    // While a gesture runs, the grid shows its own working copy.
    if (gesture.isActive)
        return;

    pattern = beatsmithProcessor.getPattern();
    displayedMeter = beatsmith::getDisplayedMeter (pattern, beatsmithProcessor.getLastHostTimeSignatureNumerator(),
                                                   beatsmithProcessor.getLastHostTimeSignatureDenominator());
    patternLength = getPatternLengthInQuarterNotes (pattern, displayedMeter);
    gridLength = getGridLengthInQuarterNotes (pattern, displayedMeter);
    gridLines = getGridLines (displayedMeter, pattern.lengthInBars);

    cellsOfLanes.clear();
    for (int laneIndex = 0; laneIndex < getNumberOfLanes(); ++laneIndex)
        cellsOfLanes.push_back (getCellsOfLane (pattern.lanes[(std::size_t) laneIndex], patternLength));

    updateScrollBars();
    repaint();
}

void PatternGrid::refreshPlayheads()
{
    bool hasChanged = false;
    for (int laneIndex = 0; laneIndex < maximumNumberOfLanes; ++laneIndex)
    {
        const auto playing = beatsmithProcessor.getPlayingStepNumber (laneIndex);
        hasChanged = hasChanged || playing != shownPlayheads[(std::size_t) laneIndex];
        shownPlayheads[(std::size_t) laneIndex] = playing;
    }
    if (hasChanged)
        repaint (getGridArea());
}

int PatternGrid::getNumberOfLanes() const noexcept
{
    return juce::jlimit (0, maximumNumberOfLanes, pattern.numberOfLanes);
}

//==============================================================================
// Geometry

juce::Rectangle<int> PatternGrid::getGridArea() const
{
    return getLocalBounds()
        .withTrimmedLeft (Theme::Layout::laneHeaderWidth)
        .withTrimmedTop (Theme::Layout::rulerHeight)
        .withTrimmedRight (Theme::Layout::scrollBarThickness)
        .withTrimmedBottom (Theme::Layout::scrollBarThickness);
}

float PatternGrid::timeToX (double positionInQuarterNotes) const noexcept
{
    return (float) (getGridArea().getX() + positionInQuarterNotes * zoom - horizontalOffset);
}

double PatternGrid::xToTime (float x) const noexcept
{
    return (x - getGridArea().getX() + horizontalOffset) / zoom;
}

float PatternGrid::laneTop (int laneIndex) const noexcept
{
    return (float) (getGridArea().getY() + laneIndex * Theme::Layout::laneHeight - verticalOffset);
}

int PatternGrid::yToLane (float y) const noexcept
{
    const double laneIndex = std::floor ((y - getGridArea().getY() + verticalOffset) / Theme::Layout::laneHeight);
    return laneIndex < 0.0 || laneIndex >= getNumberOfLanes() ? -1 : (int) laneIndex;
}

juce::Rectangle<float> PatternGrid::getCellBounds (int laneIndex, std::int64_t stepNumber) const
{
    if (laneIndex < 0 || laneIndex >= (int) cellsOfLanes.size())
        return {};
    const auto& cells = cellsOfLanes[(std::size_t) laneIndex];
    if (stepNumber < 0 || stepNumber >= (std::int64_t) cells.size())
        return {};
    const auto& cell = cells[(std::size_t) stepNumber];
    const float left = timeToX (cell.startInQuarterNotes);
    const float right = timeToX (cell.endInQuarterNotes);
    return juce::Rectangle<float> (left, laneTop (laneIndex), right - left, (float) Theme::Layout::laneHeight)
        .reduced (Theme::Layout::cellGap);
}

bool PatternGrid::findCellAtPoint (juce::Point<float> point, int& laneIndex, GridCell& cell) const
{
    if (! getGridArea().toFloat().contains (point))
        return false;
    laneIndex = yToLane (point.y);
    if (laneIndex < 0 || laneIndex >= (int) cellsOfLanes.size())
        return false;
    return findCellAt (cellsOfLanes[(std::size_t) laneIndex], xToTime (point.x), cell);
}

//==============================================================================
// Scrolling and zoom

void PatternGrid::resized()
{
    const auto area = getGridArea();
    horizontalScrollBar.setBounds (area.getX(), area.getBottom(), area.getWidth(), Theme::Layout::scrollBarThickness);
    verticalScrollBar.setBounds (area.getRight(), area.getY(), Theme::Layout::scrollBarThickness, area.getHeight());
    updateScrollBars();
}

void PatternGrid::updateScrollBars()
{
    const auto area = getGridArea();
    const double contentWidth = gridLength * zoom + gridEndMargin;
    const double contentHeight = (double) getNumberOfLanes() * Theme::Layout::laneHeight;

    horizontalOffset = juce::jlimit (0.0, std::max (0.0, contentWidth - area.getWidth()), horizontalOffset);
    verticalOffset = juce::jlimit (0.0, std::max (0.0, contentHeight - area.getHeight()), verticalOffset);

    horizontalScrollBar.setRangeLimits (0.0, std::max (contentWidth, (double) area.getWidth()),
                                        juce::dontSendNotification);
    horizontalScrollBar.setCurrentRange (horizontalOffset, area.getWidth(), juce::dontSendNotification);
    verticalScrollBar.setRangeLimits (0.0, std::max (contentHeight, (double) area.getHeight()),
                                      juce::dontSendNotification);
    verticalScrollBar.setCurrentRange (verticalOffset, area.getHeight(), juce::dontSendNotification);
}

void PatternGrid::scrollBarMoved (juce::ScrollBar* scrollBar, double newRangeStart)
{
    if (scrollBar == &horizontalScrollBar)
        horizontalOffset = newRangeStart;
    else
        verticalOffset = newRangeStart;
    repaint();
}

void PatternGrid::setZoom (float pixelsPerQuarterNote, float anchorX)
{
    const double timeAtAnchor = xToTime (anchorX);
    beatsmithProcessor.setEditorZoom (pixelsPerQuarterNote);
    zoom = beatsmithProcessor.getEditorZoom(); // clamped
    horizontalOffset = timeAtAnchor * zoom - (anchorX - getGridArea().getX());
    updateScrollBars();
    repaint();
}

void PatternGrid::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCommandDown())
    {
        setZoom (zoom * std::pow (1.25f, wheel.deltaY * 4.0f), event.position.x);
        return;
    }
    const float wheelPixels = 120.0f;
    if (event.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        horizontalScrollBar.setCurrentRangeStart (horizontalOffset - delta * wheelPixels);
    }
    else
    {
        verticalScrollBar.setCurrentRangeStart (verticalOffset - wheel.deltaY * wheelPixels);
    }
}

//==============================================================================
// Painting

void PatternGrid::paint (juce::Graphics& g)
{
    g.fillAll (Theme::Colours::panelBackground);
    {
        juce::Graphics::ScopedSaveState saveState (g);
        g.reduceClipRegion (getGridArea());
        paintLanes (g);
    }
    paintRuler (g);
    paintLaneHeaders (g);

    if (getNumberOfLanes() == 0)
    {
        g.setColour (Theme::Colours::secondaryText);
        g.setFont (Theme::font (15.0f));
        g.drawText ("No lanes yet", getGridArea(), juce::Justification::centred);
    }
}

void PatternGrid::paintRuler (juce::Graphics& g) const
{
    const auto area = getGridArea();
    const auto ruler = juce::Rectangle<int> (area.getX(), 0, area.getWidth(), Theme::Layout::rulerHeight);
    g.setColour (Theme::Colours::rulerBackground);
    g.fillRect (ruler.withX (0).withWidth (getWidth()));

    juce::Graphics::ScopedSaveState saveState (g);
    g.reduceClipRegion (ruler);
    g.setFont (Theme::font (12.0f, true));
    for (const auto& line : gridLines)
    {
        const float x = timeToX (line.positionInQuarterNotes);
        if (line.isBarLine)
        {
            g.setColour (Theme::Colours::barLine);
            g.drawVerticalLine ((int) x, (float) ruler.getY() + 4.0f, (float) ruler.getBottom());
            if (line.barNumber <= pattern.lengthInBars)
            {
                g.setColour (Theme::Colours::secondaryText);
                g.drawText (juce::String (line.barNumber), juce::Rectangle<float> (x + 4.0f, 0.0f, 40.0f, 26.0f),
                            juce::Justification::centredLeft);
            }
        }
        else
        {
            g.setColour (Theme::Colours::beatGroupLine);
            g.drawVerticalLine ((int) x, (float) ruler.getBottom() - 8.0f, (float) ruler.getBottom());
        }
    }
}

void PatternGrid::paintLaneHeaders (juce::Graphics& g) const
{
    const auto area = getGridArea();
    g.setColour (Theme::Colours::rulerBackground);
    g.fillRect (0, 0, Theme::Layout::laneHeaderWidth, Theme::Layout::rulerHeight);
    g.setColour (Theme::Colours::secondaryText);
    g.setFont (Theme::font (12.0f));
    g.drawText (juce::String (describeMeter (displayedMeter)),
                juce::Rectangle<int> (8, 0, Theme::Layout::laneHeaderWidth - 12, Theme::Layout::rulerHeight),
                juce::Justification::centredLeft);

    juce::Graphics::ScopedSaveState saveState (g);
    g.reduceClipRegion (0, area.getY(), Theme::Layout::laneHeaderWidth, area.getHeight());
    for (int laneIndex = 0; laneIndex < getNumberOfLanes(); ++laneIndex)
    {
        const auto& lane = pattern.lanes[(std::size_t) laneIndex];
        const auto row = juce::Rectangle<float> (0.0f, laneTop (laneIndex), (float) Theme::Layout::laneHeaderWidth,
                                                 (float) Theme::Layout::laneHeight);
        g.setColour (laneIndex % 2 == 0 ? Theme::Colours::laneBackground : Theme::Colours::laneBackgroundAlternate);
        g.fillRect (row);

        auto text = row.reduced (8.0f, 3.0f);
        g.setColour (lane.isMuted ? Theme::Colours::secondaryText : Theme::Colours::primaryText);
        g.setFont (Theme::font (13.0f, true));
        g.drawText (juce::String (describeDrumNote (lane.noteNumber)), text.removeFromTop (text.getHeight() * 0.55f),
                    juce::Justification::bottomLeft, true);
        g.setColour (Theme::Colours::secondaryText);
        g.setFont (Theme::font (11.0f));
        const juce::String separator (juce::CharPointer_UTF8 (" \xc2\xb7 ")); // a middle dot
        g.drawText (juce::String (describeStepRate (lane.rate)) + separator + juce::String (lane.length) + " steps" +
                        separator + "ch " + juce::String (lane.channel),
                    text, juce::Justification::topLeft, true);
    }
    g.setColour (Theme::Colours::panelOutline);
    g.drawVerticalLine (Theme::Layout::laneHeaderWidth - 1, 0.0f, (float) getHeight());
}

void PatternGrid::paintLanes (juce::Graphics& g) const
{
    const auto area = getGridArea().toFloat();
    const double visibleStart = xToTime (area.getX());
    const double visibleEnd = xToTime (area.getRight());

    for (int laneIndex = 0; laneIndex < getNumberOfLanes(); ++laneIndex)
    {
        const float top = laneTop (laneIndex);
        if (top > area.getBottom() || top + Theme::Layout::laneHeight < area.getY())
            continue;
        const auto row = juce::Rectangle<float> (area.getX(), top, area.getWidth(), (float) Theme::Layout::laneHeight);
        g.setColour (laneIndex % 2 == 0 ? Theme::Colours::laneBackground : Theme::Colours::laneBackgroundAlternate);
        g.fillRect (row);

        // Everything after the pattern end never plays.
        const float endX = timeToX (patternLength);
        if (endX < row.getRight())
        {
            g.setColour (Theme::Colours::afterPatternEnd);
            g.fillRect (row.withLeft (std::max (endX, row.getX())));
        }
    }

    // Bar and beat-group lines run through all lanes.
    const float linesBottom = std::min (area.getBottom(), laneTop (getNumberOfLanes()));
    for (const auto& line : gridLines)
    {
        g.setColour (line.isBarLine ? Theme::Colours::barLine : Theme::Colours::beatGroupLine);
        g.drawVerticalLine ((int) timeToX (line.positionInQuarterNotes), area.getY(), linesBottom);
    }

    for (int laneIndex = 0; laneIndex < getNumberOfLanes(); ++laneIndex)
    {
        const float top = laneTop (laneIndex);
        if (top > area.getBottom() || top + Theme::Layout::laneHeight < area.getY())
            continue;
        const auto& lane = pattern.lanes[(std::size_t) laneIndex];
        const auto& cells = cellsOfLanes[(std::size_t) laneIndex];

        // The playhead marks the cell playing now; beyond the drawn cells, the lane's own copy of that step.
        const auto playing = shownPlayheads[(std::size_t) laneIndex];
        std::int64_t playingCell = BeatsmithEngine::noStepPlaying;
        if (playing >= 0)
            playingCell = playing < (std::int64_t) cells.size() ? playing : playing % std::max (1, lane.length);

        for (const auto& cell : cells)
        {
            if (cell.endInQuarterNotes < visibleStart || cell.startInQuarterNotes > visibleEnd)
                continue;
            const float left = timeToX (cell.startInQuarterNotes);
            const float right = timeToX (cell.endInQuarterNotes);
            const auto bounds = juce::Rectangle<float> (left, top, right - left, (float) Theme::Layout::laneHeight)
                                    .reduced (Theme::Layout::cellGap);
            paintCell (g, lane, cell, bounds, cell.stepNumber == playingCell);
        }
    }
}

void PatternGrid::paintCell (juce::Graphics& g, const Lane& lane, const GridCell& cell, juce::Rectangle<float> bounds,
                             bool isPlaying) const
{
    const auto& step = lane.steps[(std::size_t) cell.stepIndex];
    const float opacity = cell.isAfterPatternEnd ? Theme::Layout::afterPatternEndOpacity
                          : cell.isRepeat        ? Theme::Layout::repeatOpacity
                                                 : 1.0f;
    const float corner = std::min (3.0f, bounds.getWidth() * 0.25f);

    g.setColour (Theme::Colours::stepOff.withMultipliedAlpha (opacity));
    g.fillRoundedRectangle (bounds, corner);

    if (step.isOn)
    {
        // The filled height shows the velocity; accented trigs are full and brighter.
        const int velocity = lane.getVelocityOf (step);
        const auto fill = bounds.withTop (bounds.getBottom() - bounds.getHeight() * (float) velocity / 127.0f);
        const auto colour = step.isAccented ? Theme::Colours::stepAccented : Theme::Colours::stepOn;
        g.setColour (colour.withMultipliedAlpha (opacity));
        g.fillRoundedRectangle (fill, corner);
        if (step.isAccented)
        {
            g.setColour (Theme::Colours::playhead.withMultipliedAlpha (opacity));
            g.fillRect (bounds.withHeight (3.0f));
        }
    }

    if (isPlaying)
    {
        g.setColour (Theme::Colours::playhead);
        g.drawRoundedRectangle (bounds.expanded (1.0f), corner + 1.0f, 2.0f);
    }
}

//==============================================================================
// Editing

void PatternGrid::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    int laneIndex = 0;
    GridCell cell;
    if (! findCellAtPoint (event.position, laneIndex, cell))
        return;

    gesture = {};
    gesture.patternBefore = pattern;
    gesture.workingPattern = pattern;
    gesture.laneIndex = laneIndex;
    gesture.startCell = cell;
    const auto& step = pattern.lanes[(std::size_t) laneIndex].steps[(std::size_t) cell.stepIndex];
    gesture.stepWasOn = step.isOn;
    gesture.velocityWhenPressed = step.velocity;
    gesture.lastPaintPosition = cell.startInQuarterNotes;

    if (event.mods.isPopupMenu() || event.mods.isShiftDown())
    {
        // Accents are one click: no dragging.
        toggleAccent (gesture.workingPattern, laneIndex, cell.stepIndex);
        showWorkingPattern();
        beatsmithProcessor.commitPatternChange (gesture.patternBefore);
        refreshFromProcessor();
        return;
    }
    gesture.isActive = true;
}

void PatternGrid::mouseDrag (const juce::MouseEvent& event)
{
    if (! gesture.isActive)
        return;

    const auto offset = event.getOffsetFromDragStart();
    if (gesture.kind == DragKind::undecided)
    {
        gesture.kind = decideDragKind (offset.x, offset.y, gesture.stepWasOn);
        if (gesture.kind == DragKind::paint)
        {
            gesture.paintsOn = ! gesture.stepWasOn;
            setStepOn (gesture.workingPattern, gesture.laneIndex, gesture.startCell.stepIndex, gesture.paintsOn);
            showWorkingPattern();
        }
    }

    if (gesture.kind == DragKind::paint)
    {
        paintAlongDrag (xToTime (event.position.x));
    }
    else if (gesture.kind == DragKind::velocity)
    {
        setStepVelocity (gesture.workingPattern, gesture.laneIndex, gesture.startCell.stepIndex,
                         velocityAfterVerticalDrag (gesture.velocityWhenPressed, offset.y));
        showWorkingPattern();
    }
}

void PatternGrid::paintAlongDrag (double positionInQuarterNotes)
{
    // Paint every cell between the last position and this one, so fast drags do not skip steps.
    const double from = std::min (gesture.lastPaintPosition, positionInQuarterNotes);
    const double to = std::max (gesture.lastPaintPosition, positionInQuarterNotes);
    gesture.lastPaintPosition = positionInQuarterNotes;

    bool hasChanged = false;
    for (const auto& cell : cellsOfLanes[(std::size_t) gesture.laneIndex])
    {
        if (cell.endInQuarterNotes <= from || cell.startInQuarterNotes > to)
            continue;
        const auto& step =
            gesture.workingPattern.lanes[(std::size_t) gesture.laneIndex].steps[(std::size_t) cell.stepIndex];
        if (step.isOn != gesture.paintsOn)
        {
            setStepOn (gesture.workingPattern, gesture.laneIndex, cell.stepIndex, gesture.paintsOn);
            hasChanged = true;
        }
    }
    if (hasChanged)
        showWorkingPattern();
}

void PatternGrid::mouseUp (const juce::MouseEvent& /*event*/)
{
    if (! gesture.isActive)
        return;
    if (gesture.kind == DragKind::undecided)
    {
        toggleStep (gesture.workingPattern, gesture.laneIndex, gesture.startCell.stepIndex);
        showWorkingPattern();
    }
    gesture.isActive = false;
    beatsmithProcessor.commitPatternChange (gesture.patternBefore);
    refreshFromProcessor();
}

void PatternGrid::showWorkingPattern()
{
    beatsmithProcessor.showPatternChange (gesture.workingPattern);
    pattern = gesture.workingPattern; // steps changed, the layout did not: no need to recompute cells
    repaint (getGridArea());
}

} // namespace beatsmith
