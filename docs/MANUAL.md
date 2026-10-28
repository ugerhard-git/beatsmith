# Beatsmith – User Manual

Beatsmith is a drum step sequencer plug-in (VST3) for Windows. It plays drum patterns in time with your
DAW and sends them as MIDI notes into its track, where a drum plug-in or a hardware drum machine turns
them into sound. Beatsmith itself makes no sound.

> **This manual is a skeleton.** The current version (phase 1a) plays a built-in test pattern so its timing
> can be checked; you cannot edit patterns yet. Each chapter is filled in when the feature it describes is
> built; the phase is noted in brackets.

- [Installation](#installation)
- [Where to put it in your DAW](#where-to-put-it-in-your-daw)
- [Test version: the built-in pattern](#test-version-the-built-in-pattern)
- [Basic ideas: lanes, steps, patterns, kits](#basic-ideas-lanes-steps-patterns-kits)
- [The window](#the-window)
- [Programming a pattern](#programming-a-pattern)
- [Meters, polymeters and polyrhythms](#meters-polymeters-and-polyrhythms)
- [Feel: swing, micro-timing, humanize](#feel-swing-micro-timing-humanize)
- [Variation: probability and conditional trigs](#variation-probability-and-conditional-trigs)
- [Patterns and switching](#patterns-and-switching)
- [Kits](#kits)
- [Presets](#presets)
- [Automation](#automation)
- [Stuck notes](#stuck-notes)
- [Troubleshooting](#troubleshooting)

---

## Installation

Download `Beatsmith-Windows-vX.Y.Z.zip` from the project's **Releases** page on GitHub (or, for test
builds, from the **Actions** tab → latest successful run → *Artifacts*) and copy `Beatsmith.vst3` to
`C:\Program Files\Common Files\VST3\`. Then let your DAW rescan its plug-ins.

How to check that a download is genuine is described in [SECURITY.md](../SECURITY.md#verifying-a-release).

## Where to put it in your DAW

- **Reaper:** add Beatsmith to a track's FX chain *above* the drum instrument. MIDI flows top to bottom.
- **Bitwig Studio:** put Beatsmith in the drum instrument's **Note FX** section, or as a device before
  the instrument.

To drive a hardware drum machine, send the track's MIDI output to the machine's MIDI port.

MIDI that arrives at Beatsmith (from a keyboard or a MIDI clip on the same track) passes through unchanged,
so you can still play the drum instrument directly. Beatsmith adds its own notes to it.

## Test version: the built-in pattern

*This chapter is only for the phase 1a test version and disappears when the editor arrives.*

This version plays one fixed pattern whenever your DAW plays. It uses General MIDI drum notes on **MIDI
channel 10**, so any GM-compatible drum plug-in plays it (set the plug-in to receive channel 10, or all
channels):

| Sound | GM note | What it plays |
|-------|---------|---------------|
| Kick | 36 (C1 in Bitwig and Cubase, C2 in Reaper) | 1st and 9th sixteenth of every bar, the first one louder (accent) |
| Snare | 38 | 5th and 13th sixteenth |
| Closed hi-hat | 42 | every eighth, alternating loud and soft |
| Side stick | 37 | five even notes per 4/4 bar – a quintuplet against the beat |
| Cowbell | 56 | every third sixteenth – three against four |

The pattern is one bar long, follows your DAW's time signature and starts again at every bar line. In
4/4 it sounds like a plain rock beat with two cross-rhythms on top. Things worth checking:

- **Timing:** render the track or record the MIDI: the notes should sit exactly on the grid.
- **Odd meters:** set the DAW to 7/8, 5/4 or 11/8 – the kick still lands on every bar line, and everything
  after the end of the shorter bar is cut off.
- **Loops and jumps:** loop a region (also one that is not a whole number of bars) and click around while
  playing – the pattern always continues where it should, as if it had played from the start.
- **Tempo changes:** change the tempo while playing – nothing stumbles or doubles.
- **Time-signature changes:** put a change from 4/4 to 7/8 in the middle of the song – the pattern restarts
  on the first bar of 7/8.
- **No stuck notes:** stop, loop, jump and bypass while playing – no note keeps sounding.

## Basic ideas: lanes, steps, patterns, kits

*(Phase 1)* To be written: a lane is one drum sound; a step is one cell in a lane, a trig is a step
that plays; a pattern is all lanes together; a kit is the set of lane names, notes and channels.

## The window

![The Beatsmith window](images/editor.png)

The window can be resized by dragging its bottom-right corner; Beatsmith remembers the size in your
project. *(Phase 1: the step grid and lane controls fill the panel.)*

## Programming a pattern

*(Phase 1)* To be written: adding and removing lanes (up to 16), setting trigs, velocity and accents,
note length, mute and solo, the Audition button.

## Meters, polymeters and polyrhythms

*(Phase 1)* To be written, with examples for:

- playing in the DAW's time signature, and overriding it per pattern;
- beat groupings (e.g. 7/8 as 2+2+3 or 3+2+2) and what they change;
- lanes of different length (a 5-step hi-hat against a 16-step kick);
- lanes of different rate (triplets, dotted values, free tuplets such as 5 in 4);
- when lanes realign (song start, pattern restart, every N bars);
- what happens when the DAW's time signature changes during a pattern.

## Feel: swing, micro-timing, humanize

*(Phase 2)* To be written.

## Variation: probability and conditional trigs

*(Phase 3)* This chapter will explain every condition with a short description, a diagram of which
passes play, and a musical example. Planned structure:

1. Probability – a chance per trig.
2. What a trig condition is, and how passes are counted per lane.
3. A:B – play on pass A of every B passes (1:2, 2:4, 4:4 …).
4. FILL and NOT FILL – the Fill button, momentary and latched.
5. FIRST and NOT FIRST.
6. PRE and NOT PRE – depend on the previous condition in the same lane.
7. NEI and NOT NEI – depend on the neighbouring lane.
8. Combining conditions with probability; recipes (e.g. a crash on the first bar of every four, a
   snare roll only in fills, hi-hat variations that never repeat exactly).
9. Ratchets and flams.

## Patterns and switching

*(Phase 4)* To be written.

## Kits

*(Phases 1 and 5)* To be written: the General MIDI default kit, changing a lane's note and channel,
saving and loading your own kits, where kit files are stored.

## Presets

*(Phase 5)* To be written.

## Automation

*(Phase 1 onwards)* To be written: which controls the DAW can automate.

## Stuck notes

Beatsmith ends every note it starts: at the end of its note length, before the same note plays again, and
at once when you stop, loop or jump in the DAW or bypass the plug-in – also when a lane's note or channel
was changed in between. If a note ever hangs, please report it as a bug.

## Troubleshooting

- **Nothing plays.** Beatsmith plays only while the DAW's transport runs. Check that the drum instrument
  receives MIDI channel 10 (or all channels) and that Beatsmith sits before it (see
  [Where to put it in your DAW](#where-to-put-it-in-your-daw)).
- **After a time-signature change, the first bar sounds slightly late.** DAWs tell plug-ins about a new time
  signature only from the next audio block on; Beatsmith then plays the missed notes at once, a few
  milliseconds late. A smaller audio buffer makes this shorter.
- **Starting playback behind a time-signature change, the pattern starts in the wrong place.** Beatsmith
  learns where time-signature changes are while playing through them. If you start behind a change it has
  not played through yet, it counts bars as if the current time signature had applied since the song start,
  until playback passes the change once.
