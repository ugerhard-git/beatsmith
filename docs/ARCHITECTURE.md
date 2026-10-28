# Beatsmith – Architecture

This document explains how the code is organised and why. It is kept true with every change
(see [CONTRIBUTING.md](../CONTRIBUTING.md)). API details live in the doc comments in the headers.

> **Status:** no plug-in code exists yet. Sections marked *(planned)* describe the design agreed in
> [PLAN.md](../PLAN.md); they are rewritten as the code arrives. Sections without the mark describe what
> is in the repository today.

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

## Source layout *(planned)*

| Path | Contents |
|------|----------|
| `Source/Engine/` | Musical time, meters, lanes, patterns, trig decisions, note scheduling – no JUCE |
| `Source/Plugin/` | `BeatsmithProcessor`, parameter IDs, state, kit and preset libraries |
| `Source/UI/` | Editor: grid, lane headers, pattern and kit controls, look and feel |
| `Tests/Engine/` | Engine tests (no JUCE), including randomized stress tests |
| `Tests/Plugin/` | Tests of the JUCE layer; renders the editor to PNGs for the manual |
| `docs/` | This document, the user manual, ideas, images |
| `packaging/` | Files that go into the release zip |

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

## Stored data *(planned)*

Project state, kits and presets carry a format version. Loading validates everything (version, sizes,
lane and step counts, value ranges) and falls back to defaults for anything invalid; see
[SECURITY.md](../SECURITY.md). Parameter IDs, enum values and choice label order are append-only.

## Build and CI

Builds run only on GitHub Actions. Today the repository has:

- **`.github/workflows/workflow-security.yml`** – audits all workflows with
  [zizmor](https://github.com/zizmorcore/zizmor) on every pull request and push to `main`. The tool is
  installed from `.github/requirements-ci.txt` with `pip --require-hashes`. Its job,
  *Workflow security audit*, is a required status check on `main`.
- **`.github/dependabot.yml`** – weekly update pull requests for GitHub Actions and the CI tools.

Planned for phase 0 (`build.yml`, modelled on MIDI Forge):

1. **Checks** (Linux): clang-format, clang-tidy, engine tests with warnings as errors, engine tests under
   sanitizers, plug-in layer tests and editor screenshots (artifact `editor-screenshots`).
2. **Plug-in build** (Windows): the VST3 as artifact `Beatsmith-Windows`.
3. **CodeQL** analysis of the C++ code.
4. **Release** (tags `v*` only): zips the build, attaches it to a GitHub Release with SHA-256 checksums
   and build-provenance attestations. Only this job gets write permission.

All GitHub Actions are pinned to commit SHAs; workflows default to no or read-only permissions.
Dependencies (JUCE 9) are fetched by CMake at a pinned tag and commit.

## Repository rules

`main` is protected by a ruleset: no deletion, no force pushes, changes only through pull requests,
squash merges only, and the required status check *Workflow security audit* (the build checks join it
in phase 0).

## Licensing

Beatsmith is licensed under the GNU AGPLv3 (see [LICENSE](../LICENSE)), and uses JUCE under JUCE's AGPLv3
option. Every release must make the corresponding source available, which the public repository and the
tagged release do.
