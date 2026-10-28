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
} // namespace Layout

} // namespace beatsmith::Theme
