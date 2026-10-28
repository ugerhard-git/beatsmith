# Beatsmith – User Manual

Beatsmith is a drum step sequencer plug-in (VST3) for Windows. It plays drum patterns in time with your
DAW and sends them as MIDI notes into its track, where a drum plug-in or a hardware drum machine turns
them into sound. Beatsmith itself makes no sound.

> **This manual is a skeleton.** The current version (phase 0) loads in your DAW but does not play
> anything yet. Each chapter is filled in when the feature it describes is built; the phase is noted in
> brackets.

- [Installation](#installation)
- [Where to put it in your DAW](#where-to-put-it-in-your-daw)
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

*(Phase 1)* Beatsmith ends every note it starts, also when you stop, loop or jump in the DAW, switch
patterns or edit lanes while playing. If a note ever hangs, please report it as a bug.

## Troubleshooting

To be written as questions come up.
