/**
 * @file Theme.h
 * @brief Colours, fonts and sizes of the editor, in one place.
 *
 * The colours follow MIDI Forge's dark theme, so both plug-ins look like a family.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatsmith::Theme
{

namespace Colours
{
inline const juce::Colour windowBackground { 0xff15171c };
inline const juce::Colour panelBackground { 0xff1e2128 };
inline const juce::Colour panelOutline { 0xff2a2e37 };
inline const juce::Colour primaryText { 0xffe6e8ee };
inline const juce::Colour secondaryText { 0xff8a90a0 };
inline const juce::Colour accent { 0xffffa24a }; ///< Beatsmith's signature colour (forge orange)

// The step grid
inline const juce::Colour laneBackground { 0xff1b1e24 };
inline const juce::Colour laneBackgroundAlternate { 0xff20232b };
inline const juce::Colour afterPatternEnd { 0xff121318 }; ///< Behind steps that never play
inline const juce::Colour stepOff { 0xff2c313b };
inline const juce::Colour stepOn { 0xffd9822f };       ///< Velocity fill of a trig
inline const juce::Colour stepAccented { 0xffffc27a }; ///< An accented trig
inline const juce::Colour barLine { 0xff5a6170 };
inline const juce::Colour beatGroupLine { 0xff363b46 };
inline const juce::Colour playhead { 0xfff2f2f2 };
inline const juce::Colour rulerBackground { 0xff181a20 };
} // namespace Colours

/** The editor's font at a given size. */
inline juce::Font font (float height, bool isBold = false)
{
    return juce::Font (juce::FontOptions (height, isBold ? juce::Font::bold : juce::Font::plain));
}

namespace Layout
{
constexpr int outerMargin = 14;
constexpr int headerHeight = 56;

// The step grid
constexpr int laneHeaderWidth = 176; ///< The lane names on the left
constexpr int rulerHeight = 26;      ///< Bar numbers on top
constexpr int laneHeight = 36;
constexpr int scrollBarThickness = 12;
constexpr float cellGap = 2.0f; ///< Space around each step

/** Opacity of repeats (later passes of a short lane) and of steps after the pattern end. */
constexpr float repeatOpacity = 0.4f;
constexpr float afterPatternEndOpacity = 0.3f;
} // namespace Layout

} // namespace beatsmith::Theme
