# Beatsmith – Architecture

This document explains how the code is organised and why. It is kept true with every change
(see [CONTRIBUTING.md](../CONTRIBUTING.md)). API details live in the doc comments in the headers.

> **Status:** phase 1b. The timing engine and the step grid exist, and the pattern is saved with the project;
> the lane settings (phase 1c) are not built yet. Sections marked *(planned)* describe the design agreed in
> [PLAN.md](../PLAN.md) and are rewritten as the code arrives. Sections without the mark describe what is in
> the repository today.

## Overview

```
 Host (Reaper, Bitwig …)
   │  playhead: tempo, position, last bar start,           ▲ MIDI notes out
   │  time signature, loop, playing                         │ (incoming MIDI passes through)
   ▼                                                        │
 Source/Plugin   BeatsmithProcessor (JUCE AudioProcessor): playhead → BlockContext,
   │  ▲          engine output → MidiBuffer, bypass, state; owns the edited pattern,
   │  │          undo history, and the lock-free hand-over to the audio thread
   │  │ edits, undo                   ▲ playheads, host meter (atomics)
   │  Source/UI  BeatsmithEditor + PatternGrid (message thread)
   │  BlockContext + newest pattern
   ▼
 Source/Engine   plain C++17, no JUCE
                 ├─ Pattern          the data: meter, lanes, steps (validated)
                 ├─ MusicalTimeline  song position → bars → realignment periods
                 ├─ BeatsmithEngine  steps → note-ons/offs at samples (no stuck notes)
                 └─ editing model    GridGeometry, PatternEditing, EditHistory, TripleBuffer
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
| `Source/Engine/TestPattern.*` | The starting pattern of new instances (`makeTimingTestPattern()`) and the General MIDI drum note numbers it uses |
| `Source/Engine/TripleBuffer.h` | `TripleBuffer<T>`: lock-free, wait-free hand-over of a value from one writer thread to one reader thread |
| `Source/Engine/GridGeometry.*` | Where the grid draws every step (in quarter notes): displayed meter, pattern and grid length, cells of a lane (own steps, repeats, after the pattern end), hit testing, bar and beat-group lines |
| `Source/Engine/PatternEditing.*` | The grid's edits (toggle, paint, velocity, accent) and the rules telling clicks, paint drags and velocity drags apart |
| `Source/Engine/EditHistory.*` | `EditHistory`: undo/redo of complete pattern versions, capped at 100 |
| `Source/Engine/Descriptions.*` | Texts: General MIDI drum names, step rates ("1/8T", "5:4 1/4"), meters ("7/8 (2+2+3)") |
| `Source/Plugin/PluginProcessor.*` | `BeatsmithProcessor`: the JUCE MIDI effect – playhead → `BlockContext`, engine output → `MidiBuffer`, bypass; the edited pattern, undo and the hand-over to the audio thread; playheads and the host's time signature for the editor; versioned state |
| `Source/Plugin/PatternState.*` | The pattern in the project state: writing it as XML and reading it back with every value checked |
| `Source/UI/Theme.h` | Colours, fonts and sizes (MIDI Forge's dark theme with Beatsmith's orange accent) |
| `Source/UI/PatternGrid.*` | `PatternGrid`: lane names, ruler, the step grid, playheads, mouse gestures, scrolling and zoom |
| `Source/UI/PluginEditor.*` | `BeatsmithEditor`: the resizable window with header (zoom, undo/redo), the grid, a 30 Hz refresh timer and the undo/redo keys |
| `Tests/TestFramework.h` | The tiny test framework shared with MIDI Forge (`TEST_CASE`, `EXPECT`) |
| `Tests/Engine/EngineTestRig.h` | A simulated host: plays the engine block by block (tempo, sample rate, meter regions, loops with or without split blocks) and collects events with absolute sample times |
| `Tests/Engine/PatternTests.cpp` | Durations, meters, groupings, validation of garbage patterns |
| `Tests/Engine/TimingTests.cpp` | Rates and tuplets, odd meters, polymeters, realignment, meter changes, block-size independence |
| `Tests/Engine/NoteSafetyTests.cpp` | Stop, bypass, jumps, loops, tempo and meter changes inside blocks, retriggers, capacity floods, the randomized no-stuck-notes stress test and a no-allocation check |
| `Tests/Engine/EditingTests.cpp` | Grid geometry, step edits, drag decisions, undo/redo, the triple buffer (also with a second thread), texts, playhead reporting |
| `Tests/Plugin/` | Tests of the JUCE layer, including simulated clicks and drags on the grid; `--snapshot-dir` renders `editor.png` and `editor-odd-meter.png` |
| `docs/` | This document, the user manual, ideas, images |
| `packaging/` | Files that go into the release zip (`INSTALL.txt`) |

Planned additions: trig decisions (probability, conditions) in `Source/Engine/`; parameter IDs, kit and preset
libraries in `Source/Plugin/`; lane controls in `Source/UI/`.

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

1. `processBlock` takes the newest pattern from the editor, if there is one (`TripleBuffer::takeNewestValue()`,
   lock-free; the engine copies and validates it), reads the playhead into a `BlockContext` (every value
   checked; tempo clamped to 10–1000 BPM) and calls the engine. Afterwards it publishes each lane's playing
   step and the host's time signature in atomics for the editor.
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

### The starting pattern

`makeTimingTestPattern()` is the pattern of every new instance (and of projects saved before format 2): one bar
following the host's meter, restarting every bar, on MIDI channel 10 – kick (1/16 steps 1 and 9, step 1
accented), snare (steps 5 and 13), closed hi-hat (every 1/8, loud and soft), side stick (quarter-note
quintuplets: five per 4/4 bar) and cowbell (a 3-step 1/16 lane). It was the phase 1a timing test and gives the
editor something to edit until lanes can be added (phase 1c).

## The editor

**Threads.** The editor runs on the message thread, the engine on the audio thread. The processor keeps the
*edited pattern* (what the editor shows), the undo history and the saved state behind a mutex the audio
thread never takes. Every change is copied into a `TripleBuffer<Pattern>` (three copies of about 13 kB); the
audio thread takes the newest copy at the start of a block without waiting. In the other direction, the audio
thread writes each lane's playing step and the host's last time signature into atomics.

**Refreshing.** The processor counts changes in `getPatternVersion()` – edits, undo/redo, committed gestures,
loaded state. The editor's 30 Hz timer compares it (and the host's time signature) and redraws only when it
changed; otherwise it only moves the playheads.

**Layout** (`PatternGrid`): one time axis for all lanes, `x = gridStart + positionInQuarterNotes × zoom`
(zoom 12–600 pixels per quarter note, default 120, saved). `GridGeometry` supplies the cells: a lane's own
steps (all of them, also beyond the pattern end), then faint repeats up to the pattern end; at most 4096 cells
per lane. Only visible cells are drawn. The grid starts at a realignment point, so for *every pattern restart*
it shows exactly one pattern cycle. A lane's playhead is the step number the engine reports, counted from the
period start; if that lies beyond the drawn cells (e.g. *song start only*), the lane's own copy of the step is
marked.

**Displayed meter.** The pattern's own meter, or when following the host the host's last time signature with
its default grouping (saved in the state; 4/4 before any).

**Gestures.** Mouse down remembers the pattern before the gesture and works on a copy. The first movement of
`dragDecisionDistance` (4) pixels decides: sideways paints (every cell between the last and the current mouse
position, so fast drags skip nothing), vertical on a trig sets its velocity (200 pixels for the full range),
vertical on an empty step does nothing; releasing without a decision is a click (toggle). Shift-click or
right-click toggles the accent at once. During a drag every change goes to the audio thread
(`showPatternChange()`); mouse up records one undo step (`commitPatternChange()`) and tells the host the
project changed (`updateHostDisplay()` with a non-parameter state change), so it offers to save.

**Keys.** The grid takes keyboard focus when clicked. Ctrl/Cmd+Z undoes, Ctrl/Cmd+Shift+Z and Ctrl/Cmd+Y
redo; every other key is returned unhandled, so the host keeps its shortcuts (e.g. the space bar). Whether
a host passes keys to a plug-in at all is up to the host, so Undo and Redo are also buttons.

## Next steps *(planned)*

- **Audition (1c):** while the host is stopped, an internal clock at the host's tempo drives the same engine.
- **Lane editing (1c):** adding and removing lanes, their note, channel, rate, length, gate, velocities, mute
  and solo; mute becomes a host parameter.

## Stored data

The project state is XML with the root element `BeatsmithState` and an integer `formatVersion` (currently
2, `BeatsmithProcessor::currentStateFormatVersion`):

- format 1: the editor size (`editorWidth`, `editorHeight`);
- format 2 adds the grid zoom (`editorZoom`), the host's last time signature (`hostNumerator`,
  `hostDenominator`) and the pattern as a `Pattern` child element. Its format is documented in
  `Source/Plugin/PatternState.h`: pattern settings as attributes, one `Lane` element per lane, and per lane a
  `steps` attribute listing only the steps that differ from an untouched step, as
  `index:on:accent:velocity:gate`.

Loading:

- ignores data that is empty, larger than 16 MB, not XML, has another root element or no valid version;
- clamps every value to its allowed range; numbers too long for an int are limited before they are used;
  at most 16 lanes and 128 steps per lane are read, malformed step entries are skipped, and the pattern is
  passed through `sanitizePattern()`;
- gives format-1 projects the starting pattern;
- clears the undo history (undo must not step back into another project);
- reads a *newer* format as far as it understands it and ignores unknown properties and children.

Plug-in layer tests cover each of these cases. Kits and presets (phases 1 and 5) follow the same rules; see
[SECURITY.md](../SECURITY.md). Property names, parameter IDs, enum values and choice label order are
append-only.

## Build and CI

Builds run only on GitHub Actions; see [CONTRIBUTING.md](../CONTRIBUTING.md#building) for the commands.
The engine is a static library (`beatsmith_engine`); `CMakeLists.txt` options switch the plug-in, the two
test programs, warnings-as-errors and sanitizers on and off. Strict warnings apply to our own code only,
not to the JUCE sources compiled into the plug-in.

The plug-in links only the JUCE modules it needs: `juce_audio_processors` (with the modules it depends on:
audio basics, events, data structures, graphics, GUI) and `juce_gui_basics`, plus the plug-in wrapper.
`juce_audio_utils` is deliberately not used, because it would bring in `juce_audio_formats` (audio file codecs
such as MP3, Ogg Vorbis, FLAC, Opus) and `juce_audio_devices` (audio and MIDI drivers), which a MIDI plug-in
never needs. Keeping them out means less third-party code to compile, scan and trust.

**`.github/workflows/build.yml`** runs on every pull request, every push to `main` and every `v*` tag:

0. **What changed** (Linux, a few seconds): decides whether the other jobs are needed. Pull requests and
   pushes to `main` that change only documentation skip all build and test jobs (see *Documentation-only
   changes* below).
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

**Documentation-only changes.** The *What changed* job compares the commit being built with its base – for a
pull request the pull request's base commit, for a push to `main` the commit `main` pointed to before the
push – using `git diff --no-renames --name-only`. If every changed file is a Markdown file (`*.md`
anywhere), lies in `docs/` or is `LICENSE`, it outputs `code=false`, and jobs 1–4 are skipped. GitHub counts
skipped jobs as passed, so the required checks stay satisfied and a documentation-only pull request can be
merged after a few seconds instead of after the full build. In every other case the output is `code=true`
and everything runs:

- any file outside those patterns changed (source, tests, `CMakeLists.txt`, workflows, images outside
  `docs/` …);
- a file was moved from elsewhere into `docs/` (thanks to `--no-renames` both paths are listed);
- nothing changed at all, or the base commit is unknown (e.g. the first push of a branch);
- a tag (release) or a manual run (`workflow_dispatch`) – releases always build everything.

The event data reaches the script only through environment variables, never written into the script
itself, so a branch name or commit message cannot inject commands. The release job is not gated: it runs
only for tags, which always build. If *What changed* itself fails, the jobs that depend on it are skipped –
which would also count as passed – so *What changed* is a required check too (see *Repository rules*).

**`.github/workflows/workflow-security.yml`** audits all workflows with
[zizmor](https://github.com/zizmorcore/zizmor) on every pull request and push to `main`. It is not gated by
*What changed*: it is fast and audits every change.

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
squash merges only, and required status checks: *What changed*, *Workflow security audit*, *Format, lint
and engine tests*, *Plug-in layer tests and editor screenshot*, *Plug-in (Windows)* and *CodeQL (C++)*. The
build and test checks are skipped (and count as passed) when only documentation changed.

## Licensing

Beatsmith is licensed under the GNU AGPLv3 (see [LICENSE](../LICENSE)), and uses JUCE under JUCE's AGPLv3
option. Every release must make the corresponding source available, which the public repository and the
tagged release do.
