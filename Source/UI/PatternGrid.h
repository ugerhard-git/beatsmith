/**
 * @file PatternGrid.h
 * @brief The step grid: lane names on the left, bar numbers on top, and the steps of every lane laid out on
 *        one time axis, so lanes of different rates and lengths line up as they sound.
 */
#pragma once

#include "Engine/GridGeometry.h"
#include "Engine/PatternEditing.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <vector>

namespace beatsmith
{

class BeatsmithProcessor;

/**
 * Draws and edits the pattern.
 *
 * Horizontal positions are song time from the grid start (a realignment point) multiplied by the zoom, in
 * pixels per quarter note; see GridGeometry.h. Mouse gestures follow the rules in PatternEditing.h: a click
 * toggles a step, a sideways drag paints, a vertical drag on a trig sets its velocity, Shift-click or a
 * right-click toggles the accent. Every gesture is one undo step.
 *
 * Mouse wheel: scrolls the lanes; with Shift (or a sideways wheel) scrolls in time; with Ctrl (Cmd on a Mac)
 * zooms around the mouse position.
 */
class PatternGrid final : public juce::Component, private juce::ScrollBar::Listener
{
public:
    explicit PatternGrid (BeatsmithProcessor& processorToEdit);
    ~PatternGrid() override;

    /** Re-reads the pattern and the meter from the processor and repaints. */
    void refreshFromProcessor();

    /** Repaints if a playhead moved. Called by the editor's timer. */
    void refreshPlayheads();

    /** Sets the zoom (pixels per quarter note), keeping the given x position (in this component) in place. */
    void setZoom (float pixelsPerQuarterNote, float anchorX);

    /** @return The zoom in pixels per quarter note. */
    float getZoom() const noexcept { return zoom; }

    /** @return The meter the grid shows (the pattern's own or the host's last one). */
    const Meter& getDisplayedMeter() const noexcept { return displayedMeter; }

    /**
     * @return Where a cell is drawn, in this component's coordinates (empty if the lane or cell does not
     *         exist). Used by the tests to click on steps.
     */
    juce::Rectangle<float> getCellBounds (int laneIndex, std::int64_t stepNumber) const;

    //==============================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    /** An edit in progress, from mouse down to mouse up. */
    struct Gesture
    {
        bool isActive = false;
        int laneIndex = 0;
        GridCell startCell;
        DragKind kind = DragKind::undecided;
        bool paintsOn = true;        ///< For painting: the state painted onto every step touched
        bool stepWasOn = false;      ///< The start step's state when the mouse was pressed
        int velocityWhenPressed = 0; ///< The start step's velocity when the mouse was pressed
        double lastPaintPosition = 0.0;
        Pattern patternBefore;  ///< For the undo step
        Pattern workingPattern; ///< The pattern with the gesture's changes so far
    };

    void scrollBarMoved (juce::ScrollBar* scrollBar, double newRangeStart) override;
    void updateScrollBars();

    juce::Rectangle<int> getGridArea() const;
    int getNumberOfLanes() const noexcept;
    float timeToX (double positionInQuarterNotes) const noexcept;
    double xToTime (float x) const noexcept;
    int yToLane (float y) const noexcept;
    float laneTop (int laneIndex) const noexcept;
    bool findCellAtPoint (juce::Point<float> point, int& laneIndex, GridCell& cell) const;

    void paintRuler (juce::Graphics& g) const;
    void paintLaneHeaders (juce::Graphics& g) const;
    void paintLanes (juce::Graphics& g) const;
    void paintCell (juce::Graphics& g, const Lane& lane, const GridCell& cell, juce::Rectangle<float> bounds,
                    bool isPlaying) const;

    void paintAlongDrag (double positionInQuarterNotes);
    void showWorkingPattern();

    BeatsmithProcessor& beatsmithProcessor;

    Pattern pattern;
    Meter displayedMeter;
    double patternLength = 4.0; ///< In quarter notes
    double gridLength = 4.0;    ///< In quarter notes
    std::vector<std::vector<GridCell>> cellsOfLanes;
    std::vector<GridLine> gridLines;
    std::array<std::int64_t, maximumNumberOfLanes> shownPlayheads {};

    float zoom;                    ///< Pixels per quarter note
    double horizontalOffset = 0.0; ///< Pixels scrolled in time
    double verticalOffset = 0.0;   ///< Pixels scrolled through the lanes

    juce::ScrollBar horizontalScrollBar { false };
    juce::ScrollBar verticalScrollBar { true };

    Gesture gesture;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternGrid)
};

} // namespace beatsmith
