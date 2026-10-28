# Beatsmith – Architecture

This document explains how the code is organised and why. It is kept true with every change
(see [CONTRIBUTING.md](../CONTRIBUTING.md)). API details live in the doc comments in the headers.

> **Status:** phase 0. The build, CI, an empty engine and an empty plug-in exist; the sequencer itself does
> not yet. Sections marked *(planned)* describe the design agreed in [PLAN.md](../PLAN.md) and are rewritten
> as the code arrives. Sections without the mark describe what is in the repository today.

## Overview *(planned)*

```
 Host (Reaper, Bitwig …)
   │  playhead: tempo, time signature, position,          ▲ MIDI notes out
   │  last bar start, loop, playing                        │
   ▼                                                       │
 Source/Plugin   BeatsmithProcessor (JUCE AudioProcessor): parameters, state,
   │             kits, presets, MidiBuffer, Audition clock
   │  BlockContext + current pattern snapshot
   ▼
 Source/Engine   plain C++17, no JUCE
                 ├─ Timeline      host position → exact musical time, meter, anchor
                 ├─ LanePlayer    per lane: step index from time, length, rate
                 ├─ TrigDecider   probability, trig conditions, fill (phase 3)
                 └─ NoteScheduler note-ons/offs at sample positions, look-ahead,
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
| `Source/Engine/BlockContext.h` | `BlockContext`: block length, sample rate, transport state – what the engine knows about a block |
| `Source/Engine/NoteEvent.h` | `NoteEvent` (one note-on/off at a sample position) and `NoteEventList` (fixed capacity, never allocates while running) |
| `Source/Engine/BeatsmithEngine.*` | `BeatsmithEngine`: the sequencer engine. Phase 0: silent; the pattern player arrives in phase 1 |
| `Source/Plugin/PluginProcessor.*` | `BeatsmithProcessor`: the JUCE MIDI effect – playhead → `BlockContext`, engine output → `MidiBuffer`, bypass, versioned state, remembered editor size |
| `Source/UI/Theme.h` | Colours, fonts and sizes (MIDI Forge's dark theme with Beatsmith's orange accent) |
| `Source/UI/PluginEditor.*` | `BeatsmithEditor`: the resizable window. Phase 0: header only |
| `Tests/TestFramework.h` | The tiny test framework shared with MIDI Forge (`TEST_CASE`, `EXPECT`) |
| `Tests/Engine/` | Engine tests (no JUCE) |
| `Tests/Plugin/` | Tests of the JUCE layer; `--snapshot-dir` renders the editor to `editor.png` |
| `docs/` | This document, the user manual, ideas, images |
| `packaging/` | Files that go into the release zip (`INSTALL.txt`) |

Planned additions: musical time, lanes, patterns, trig decisions and note scheduling in `Source/Engine/`;
parameter IDs, kit and preset libraries in `Source/Plugin/`; the grid and lane controls in `Source/UI/`.

## MIDI routing

Beatsmith is a MIDI effect. MIDI arriving from the host **passes through unchanged** (including SysEx), so
a keyboard on the same track still plays the drum instrument; the engine's notes are added to it. While the
plug-in is bypassed, input still passes through and the engine ends any note it has started. Whether some
incoming MIDI will later be used by Beatsmith itself (e.g. to switch patterns) is open decision 4 in
PLAN.md.

## Musical time *(planned)*

Odd meters, polymeters and polyrhythms are core features (PLAN.md, section 2), so time is modelled
carefully:

- **Exact durations.** A lane's step duration is stored as an exact fraction of a quarter note: 1/16
  note = 1/4 beat, 1/8 triplet = 1/3 beat, a free tuplet "5 in 4 beats" = 4/5 beat. Positions are only
  converted to samples at the very end, so lanes never drift against each other.
- **Computed positions.** A lane's current step is
  `floor((musicalTime - anchor) / stepDuration) mod laneLength`. Nothing is counted per block, so loops,
  jumps and scrubbing always land on the right step in every lane.
- **Anchor and realignment.** The anchor is where all lanes start together. The realignment setting
  chooses it: song start only, every pattern restart, or every N bars.
- **Meter.** Taken from the host's time signature and bar position unless the pattern overrides it. The
  beat grouping (e.g. 7/8 = 2+2+3) drives grid shading, accent helpers and swing. A host meter change
  either restarts the pattern at the next bar or keeps its phase (user setting). Hosts that report no
  bar position fall back to the pattern's own meter counted from song start.
- **Audition.** While the host is stopped, the Audition button drives the same timeline from an internal
  clock at the host's tempo.

## The processing path (audio thread) *(planned)*

1. `processBlock` reads the playhead into a `BlockContext` and picks up the latest pattern snapshot
   from the editor (lock-free hand-over, designed in phase 1).
2. The engine finds every step that starts in this block (plus a small look-ahead for notes that swing,
   micro-timing or flams move earlier), decides whether each trig plays, and schedules note-ons and
   note-offs at sample positions.
3. Scheduled events are written into the `juce::MidiBuffer`.

Everything on this path is **real-time safe**: no allocation, no locks, no I/O. Buffers are reserved up
front and queues have hard caps.

### No stuck notes

The `NoteScheduler` remembers every note it has started (note, channel, scheduled end). When anything
could orphan a note – transport stop, loop, jump, pattern change, a lane removed or its note or channel
changed, bypass, release of the plug-in – it sends the matching note-offs immediately. Randomized stress
tests check that no note is left hanging across hundreds of random edit-and-transport sequences.

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
4. **CodeQL (C++)**: security analysis (`security-extended` queries) of a full build; results appear under
   the repository's *Security → Code scanning*. Configuration in `.github/codeql/codeql-config.yml`.
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
