# Beatsmith – Architecture

This document explains how the code is organised and why. It is kept true with every change
(see [CONTRIBUTING.md](../CONTRIBUTING.md)). API details live in the doc comments in the headers.

> **Status:** phase 1a. The timing engine plays a built-in test pattern; the editor (phase 1b) and the lane
> settings (phase 1c) are not built yet. Sections marked *(planned)* describe the design agreed in
> [PLAN.md](../PLAN.md) and are rewritten as the code arrives. Sections without the mark describe what is in
> the repository today.

## Overview

```
 Host (Reaper, Bitwig …)
   │  playhead: tempo, position, last bar start,           ▲ MIDI notes out
   │  time signature, loop, playing                         │ (incoming MIDI passes through)
   ▼                                                        │
 Source/Plugin   BeatsmithProcessor (JUCE AudioProcessor): playhead → BlockContext,
   │             engine output → MidiBuffer, bypass, state
   │  BlockContext
   ▼
 Source/Engine   BeatsmithEngine – plain C++17, no JUCE
                 ├─ Pattern          the data: meter, lanes, steps (validated)
                 ├─ MusicalTimeline  song position → bars → realignment periods
                 └─ note scheduling  steps → note-ons/offs at samples,
                                     sounding-note bookkeeping (no stuck notes)
```

The **engine** (`Source/Engine`) is plain C++17 without JUCE, like MIDI Forge's. It is built and tested on
any machine in seconds, also under address and undefined-behaviour sanitizers. The **plug-in layer**
(`Source/Plugin`, `Source/UI`) connects it to JUCE, which provides the VST3 wrapper, parameters, state
and the editor.

Beatsmith is built as a JUCE **MIDI effect** (`IS_MIDI_EFFECT TRUE`, `NEEDS_MIDI_OUTPUT TRUE`), the same
set-up that lets MIDI Forge send notes in Reaper and Bitwig.

## Source layout

| Path | Contents |
|------|----------|
| `Source/Engine/BlockContext.h` | `BlockContext`: block length, sample rate, tempo and everything the host reports about its transport (each optional value with a `has…` flag) |
| `Source/Engine/NoteEvent.h` | `NoteEvent` (one note-on/off at a sample position) and `NoteEventList` (fixed capacity, never allocates while running, stable insertion sort) |
| `Source/Engine/Fraction.h` | `Fraction`: exact, reduced fractions for musical durations |
| `Source/Engine/Pattern.*` | The data model – `StepRate`, `Meter` (with beat grouping), `Step`, `Lane`, `Pattern`, the stored enums, limits, default groupings and `sanitizePattern()` |
| `Source/Engine/MusicalTimeline.*` | `MusicalTimeline`: bars from the host or the pattern's own meter, remembered host meter changes, realignment periods |
| `Source/Engine/BeatsmithEngine.*` | `BeatsmithEngine`: finds the steps in each block, schedules note-ons and note-offs, handles transport changes |
| `Source/Engine/TestPattern.*` | The phase 1a test pattern and the General MIDI drum note numbers it uses |
| `Source/Plugin/PluginProcessor.*` | `BeatsmithProcessor`: the JUCE MIDI effect – playhead → `BlockContext`, engine output → `MidiBuffer`, bypass, versioned state, remembered editor size |
| `Source/UI/Theme.h` | Colours, fonts and sizes (MIDI Forge's dark theme with Beatsmith's orange accent) |
| `Source/UI/PluginEditor.*` | `BeatsmithEditor`: the resizable window. Phase 1a: explains the test pattern |
| `Tests/TestFramework.h` | The tiny test framework shared with MIDI Forge (`TEST_CASE`, `EXPECT`) |
| `Tests/Engine/EngineTestRig.h` | A simulated host: plays the engine block by block (tempo, sample rate, meter regions, loops with or without split blocks) and collects events with absolute sample times |
| `Tests/Engine/PatternTests.cpp` | Durations, meters, groupings, validation of garbage patterns |
| `Tests/Engine/TimingTests.cpp` | Rates and tuplets, odd meters, polymeters, realignment, meter changes, block-size independence |
| `Tests/Engine/NoteSafetyTests.cpp` | Stop, bypass, jumps, loops, tempo and meter changes inside blocks, retriggers, capacity floods, the randomized no-stuck-notes stress test and a no-allocation check |
| `Tests/Plugin/` | Tests of the JUCE layer; `--snapshot-dir` renders the editor to `editor.png` |
| `docs/` | This document, the user manual, ideas, images |
| `packaging/` | Files that go into the release zip (`INSTALL.txt`) |

Planned additions: trig decisions (probability, conditions) in `Source/Engine/`; parameter IDs, kit and preset
libraries in `Source/Plugin/`; the grid and lane controls in `Source/UI/`.

## MIDI routing

Beatsmith is a MIDI effect. MIDI arriving from the host **passes through unchanged** (including SysEx), so
a keyboard on the same track still plays the drum instrument; the engine's notes are added to it. While the
plug-in is bypassed, input still passes through and the engine ends any note it has started. Whether some
incoming MIDI will later be used by Beatsmith itself (e.g. to switch patterns) is open decision 4 in
PLAN.md.

## The data model

`Pattern.h` holds plain values of fixed size – no heap memory – so a pattern can be copied on the audio
thread. A pattern has a meter source (host or its own `Meter`), a length in bars, a realignment mode, a
meter-change behaviour and up to 16 lanes; a lane has a note, a channel (default 10), a length of 1–128
steps, a `StepRate`, a gate, default and accent velocities, mute, solo and 128 steps; a step is on or off,
accented or not, with a velocity and an optional gate of its own.

**Exact durations.** A `StepRate` is a note value plus a tuplet "N steps in the time of M notes"; its
duration is an exact `Fraction` of a quarter note: 1/16 = 1/4, eighth triplet = 1/3, dotted eighth = 3/4,
"5 in 4 quarters" = 4/5. Steps last between a 1/64 note and four whole notes; N and M go up to 32. Gates are
counted in 1/64 notes (default 2 = a 1/32 note).

**Validation.** `sanitizePattern()` clamps every value and replaces invalid rates (straight 1/16), meters
(4/4) and groupings (the default grouping). The engine sanitizes every pattern it receives, so data from a
damaged project or a crafted preset cannot make it misbehave.

**Default groupings** (`makeMeterWithDefaultGrouping()`): quarter- and half-note meters count every unit;
eighth and shorter units are grouped in threes when the numerator is a multiple of 3 (6/8 = 3+3), otherwise
in twos with a final three (7/8 = 2+2+3, 11/8 = 2+2+2+2+3).

## Musical time

**Computed positions.** Every lane's step *k* starts at `anchor + k × stepDuration`, and plays step
`k mod laneLength`. Nothing is counted per block: each block's steps are found from the song position, so
loops, jumps and scrubbing always land on the right step in every lane. Durations are converted to floating
point only here, and each step start is computed directly from *k*, so lanes never drift.

**Realignment periods** (`MusicalTimeline::getPeriodContaining()`): the anchor is where all lanes start
together. For *song start only* there is one period, the whole song, anchored at quarter note 0 (pre-roll
steps before it follow the same grid). For *every pattern restart* and *every N bars*, a period is that many
bars, counted from bar 0 – or, with *restart at change*, from the first bar of the current meter.

**Bars.** With the pattern's own meter, bar *n* starts at `n × barLength`. When following the host, bars come
from the host's time signature and the start of the current bar. VST3 hosts do not report bar numbers, so
the timeline remembers each meter it sees during playback as a *meter segment* (start, time signature,
number of its first bar; up to 32, then it starts over). The first segment is assumed to reach back to the
song start. Limitation: after starting playback behind a time-signature change that has not been played
through yet, the change's position is unknown; the timeline assumes the current meter since the song start
until playback passes the change. Hosts that report no time signature count as 4/4; hosts that report no bar
start get bars computed from the song start.

**Tolerances.** A step belongs to a block if it starts in `[blockStart − ε, blockEnd − ε)` with the same
ε = 10⁻⁹ quarter notes everywhere; together with continuing each block exactly where the last one ended,
neighbouring blocks and periods split the steps exactly between them – no step is lost or doubled.

## The processing path (audio thread)

1. `processBlock` reads the playhead into a `BlockContext` (every value checked; tempo clamped to 10–1000
   BPM) and calls the engine.
2. The engine decides how this block relates to the last one:
   - **continuing** (the position is within two samples of where the last block ended): it carries on
     exactly from there;
   - **drifting** (within 1/4 quarter note or two block lengths – e.g. a tempo change inside the last
     block, since hosts report one tempo per block): it carries on from where the last block ended; steps the
     host has already passed come late at sample 0, none is repeated;
   - otherwise a **start or jump**: every sounding note ends at sample 0, and a step up to half a sample
     before the new position still plays.
3. The timeline learns the host's meter. A time-signature change is reported only from the first block
   after it, so steps between the change and this block are played now, slightly late, instead of being lost.
4. If the host loops and the loop end lies inside the block (some hosts do not split blocks there), the
   block is played up to the loop end, all notes end at the wrap sample, and the rest is played from the
   loop start.
5. For each stretch, the engine walks through the realignment periods it touches, collects the trigs of all
   audible lanes (solo beats mute), sorts them by time, and starts them in order – ending due notes first.
6. Events are generated in time order; a stable insertion sort fixes rounding cases while keeping a note-off
   before the note-on that retriggers the same note. The processor writes them into the `juce::MidiBuffer`.

`prepareToPlay` marks the next block as a jump. While the transport is stopped, the engine is silent.

Everything on this path is **real-time safe**: no allocation (checked by a test that counts every
`operator new` during processing), no locks, no I/O. Lists have fixed capacities: 4096 events per block,
2048 trigs per block, 256 sounding notes.

### No stuck notes

The engine remembers every note it starts (channel, note, start, end of its gate) and guarantees its
note-off:

- at the end of its gate;
- before the same note starts again (retrigger: note-off and note-on at the same sample, in that order);
- immediately (sample 0) when the transport stops, jumps or starts, and at the wrap sample of a loop;
- when all notes are released (bypass) – also for notes whose lane meanwhile changed its note or channel,
  because the note-off uses the note and channel the note was started with.

If two lanes start the same note at the same moment, it is played once. If 256 notes sound, the oldest is
ended to make room. A note-on is only added while the output list still has room for every possible
note-off, so capacity limits can drop a note-on but never a note-off. A randomized stress test checks all of
this across thousands of blocks of random patterns, tempo and sample-rate changes, jumps, loops (split and
unsplit), stops, bypasses and pattern changes.

### The test pattern (phase 1a)

`makeTimingTestPattern()` is played until the editor exists: one bar following the host's meter, restarting
every bar, on MIDI channel 10 – kick (1/16 steps 1 and 9, step 1 accented), snare (steps 5 and 13), closed
hi-hat (every 1/8, loud and soft), side stick (quarter-note quintuplets: five per 4/4 bar) and cowbell (a
3-step 1/16 lane). It makes host sync, odd meters, tuplets and polymeters audible.

## Next steps *(planned)*

- **Hand-over of edits (1b):** the editor changes a copy of the pattern and hands it to the audio thread
  without locks (the engine's `setPattern()` is already real-time safe; the hand-over mechanism is designed
  in phase 1b).
- **Audition (1c):** while the host is stopped, an internal clock at the host's tempo drives the same engine.
- **Pattern in the project state (1c):** stored, versioned and validated with `sanitizePattern()`.

## Stored data

The project state is XML with the root element `BeatsmithState` and an integer `formatVersion` (currently
1, `BeatsmithProcessor::currentStateFormatVersion`). Version 1 holds only the editor size
(`editorWidth`, `editorHeight`). Loading:

- ignores data that is empty, larger than 16 MB, not XML, has another root element or no valid version;
- clamps every value to its allowed range;
- reads a *newer* format as far as it understands it and ignores unknown properties and children.

Plug-in layer tests cover each of these cases. Kits and presets (phases 1 and 5) follow the same rules; see
[SECURITY.md](../SECURITY.md). Property names, parameter IDs, enum values and choice label order are
append-only.

## Build and CI

Builds run only on GitHub Actions; see [CONTRIBUTING.md](../CONTRIBUTING.md#building) for the commands.
The engine is a static library (`beatsmith_engine`); `CMakeLists.txt` options switch the plug-in, the two
test programs, warnings-as-errors and sanitizers on and off. Strict warnings apply to our own code only,
not to the JUCE sources compiled into the plug-in.

**`.github/workflows/build.yml`** runs on every pull request, every push to `main` and every `v*` tag:

1. **Format, lint and engine tests** (Linux): clang-format, engine tests with warnings as errors,
   clang-tidy, engine tests under address and undefined-behaviour sanitizers.
2. **Plug-in layer tests and editor screenshot** (Linux): builds the JUCE layer, runs its tests under a
   virtual display and uploads `editor.png` as artifact `editor-screenshots` (the source of
   `docs/images/editor.png`).
3. **Plug-in (Windows)**: builds the VST3 and uploads it with `INSTALL.txt` as artifact
   `Beatsmith-Windows`. On a tag it first checks that the tag matches the version in `CMakeLists.txt`.
4. **CodeQL (C++)**: security analysis (`security-extended` queries) of Beatsmith's own code; results appear
   under the repository's *Security → Code scanning*. CodeQL analyses every file compiled while it watches the
   build, and its path filters do not apply to C++, so the job first builds everything without CodeQL, then
   deletes only our own object files and rebuilds them under CodeQL. JUCE thus stays out of the results.
5. **Publish release** (tags `v*` only, after all other jobs pass): zips the build as
   `Beatsmith-Windows-vX.Y.Z.zip`, writes `SHA256SUMS.txt`, creates a build-provenance attestation and
   publishes the GitHub release. Only this job has write permissions.

**`.github/workflows/workflow-security.yml`** audits all workflows with
[zizmor](https://github.com/zizmorcore/zizmor) on every pull request and push to `main`.

Supply chain:

- GitHub Actions are pinned to commit SHAs; workflows start with `permissions: {}` and each job asks only
  for what it needs. No build caches are used, so nothing from an earlier run can leak into a release.
- CI tools (clang-format, zizmor) are installed from `.github/requirements-ci.txt` with
  `pip --require-hashes`.
- `.github/dependabot.yml` proposes weekly updates for the actions and the CI tools.
- JUCE is fetched by CMake at the pinned tag 9.0.4 (commit `3fb5cbc0340758596dac39f7c8de47d562b4896e`),
  which Dependabot cannot track. To update JUCE: check its release notes, change `GIT_TAG` and the commit
  in the comment in `CMakeLists.txt`, and let CI prove the build in a pull request.

## Repository rules

`main` is protected by a ruleset: no deletion, no force pushes, changes only through pull requests,
squash merges only, and required status checks: *Workflow security audit*, *Format, lint and engine
tests*, *Plug-in layer tests and editor screenshot*, *Plug-in (Windows)* and *CodeQL (C++)*.

## Licensing

Beatsmith is licensed under the GNU AGPLv3 (see [LICENSE](../LICENSE)), and uses JUCE under JUCE's AGPLv3
option. Every release must make the corresponding source available, which the public repository and the
tagged release do.
