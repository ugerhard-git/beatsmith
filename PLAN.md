# Beatsmith – Plan

Beatsmith is a drum step sequencer plug-in: it plays drum patterns in sync with the DAW and sends them
as MIDI notes into its track, where a drum plug-in or hardware drum machine turns them into sound.
It is the companion of [MIDI Forge](https://github.com/ugerhard-git/midi-forge).

This document records **what** we build, **in which order**, and the **decisions** taken so far. It is
updated whenever a decision is made or a phase is finished. Ideas that are discussed but not scheduled go
into [docs/IDEAS.md](docs/IDEAS.md); once something is scheduled it moves here.

Status: **planning** – no plug-in code yet.

---

## 1. Goals

- **The drum sequencer of the owner's dreams**: quick to program, musical, full of controlled variation,
  and pleasant to use for hours.
- **Works with any drum sound source.** Mainly VST drum plug-ins, but also hardware drum machines. No
  built-in assumptions about a specific instrument: every lane's MIDI note and channel are freely set.
- **Tight timing.** Every note is placed sample-accurately and follows the host's tempo, position, loops
  and jumps.
- **Never a stuck note.** Stopping, looping, jumping, changing patterns or editing while playing must
  never leave a note hanging.
- **Understandable.** Every feature is explained in the user manual, with examples – especially the
  conditional trigs.

Non-goals (for now): producing sound itself, macOS, CLAP, AU, standalone app.

## 2. Decided

| Topic | Decision |
|-------|----------|
| Name | Beatsmith |
| Format and platform | VST3 on Windows. Built as a MIDI effect (like MIDI Forge), which Reaper and Bitwig place before an instrument |
| Hosts used for testing | Reaper and Bitwig Studio |
| Framework | JUCE 9 with CMake, JUCE pinned to an exact release tag and commit |
| Builds | Only on GitHub Actions; the owner does not build locally |
| License | GNU AGPLv3 (JUCE is used under its AGPLv3 option) |
| Repository workflow | Every change through a pull request; required CI checks; squash merge; owner merges |
| Default drum map | **General MIDI** drum map. Users can change each lane's note and channel and **save their own kits** |
| Conditional trigs | **In scope**, with a thorough, example-rich chapter in the user manual |
| Relationship to MIDI Forge | Reuse its structure, conventions, CI and suitable code (preset library, look and feel, test framework) **as far as useful**; Beatsmith may diverge where its needs differ |
| Ground rules | Same as MIDI Forge (to be written into CONTRIBUTING.md and CLAUDE.md) |

## 3. Concepts and vocabulary

These words are used in code, UI and manual alike.

- **Lane** – one drum voice (kick, snare, closed hat …). A lane has a name, a MIDI note, a MIDI channel and
  its own row of steps.
- **Step** – one cell in a lane. A step is *on* (a **trig**) or *off*. A trig carries per-step values
  (velocity, length, micro-timing, probability, condition, ratchets …).
- **Pattern** – all lanes with all their steps, plus pattern settings (length, swing …).
- **Kit** – the lane set-up only: names, notes and channels of all lanes. GM is the default kit; users
  save their own kits (e.g. "Addictive Drums", "TR-8S") and reuse them across patterns and projects.
- **Preset** – a saved Beatsmith state (patterns and kit) the user can recall, like MIDI Forge's user
  presets.
- **Trig condition** – a rule deciding whether a trig plays on a given pass through the pattern
  (Elektron-style). See phase 3.

## 4. Architecture (planned, mirrors MIDI Forge)

```
 Host (Reaper, Bitwig …)
   │  playhead (tempo, position, loop, playing)      ▲ MIDI notes out
   ▼                                                 │
 Source/Plugin   JUCE AudioProcessor: parameters, state, kits, presets, MidiBuffer
   │  BlockContext + pattern snapshot
   ▼
 Source/Engine   plain C++17, no JUCE: clock, pattern player, trig conditions,
                 note scheduler (note-offs, ratchets), no-stuck-notes bookkeeping
```

- The **engine** is JUCE-free and tested in seconds on any machine, including under sanitizers.
- The **plug-in layer** connects it to JUCE: host parameters, state save/restore, editor.
- **Real-time safety** on the audio thread: no allocation, no locks, no I/O. The editor hands pattern
  edits to the audio thread through a lock-free mechanism (to be designed in phase 1).
- **Untrusted input**: host state, preset and kit files are validated (version field, size limits,
  bounds checks) before use. See [SECURITY.md](SECURITY.md).

## 5. Phases

Each phase ends with a working plug-in, a green CI, an updated manual and a release tag. Phases are
discussed in detail, and the owner gives an explicit **go**, before work on them starts.

### Phase 0 – Groundwork

- Documents: CONTRIBUTING.md, CLAUDE.md, docs/ARCHITECTURE.md, docs/MANUAL.md (skeleton),
  pull request template.
- Tooling: `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes` (from MIDI Forge).
- CMake skeleton: engine library, engine tests, plug-in, plug-in layer tests; JUCE pinned.
- CI (`build.yml`): format + lint + engine tests (+ sanitizers) on Linux, plug-in layer tests and editor
  screenshots, Windows VST3 build, CodeQL for C++, release job for `v*` tags with SHA-256 checksums and
  build-provenance attestations.
- Result: an empty Beatsmith that loads in Reaper and Bitwig, shows a window and outputs nothing.

### Phase 1 – A playable sequencer (first useful version)

- Lanes and steps (counts: **open decision 1**), step on/off by clicking and dragging.
- Per-step velocity, with a quick accent gesture.
- Note length (gate) per lane, optionally per step.
- GM default kit; per-lane name, note and channel editable; mute and solo per lane.
- Pattern length and step resolution (1/16 default).
- Host sync: follows tempo, position, loops and jumps; plays only while the host plays.
- Sample-accurate note output; no stuck notes (stop, loop, jump, edits, kit changes, bypass).
- State saved in the DAW project.
- Release `v0.1.0`.

### Phase 2 – Feel

- Swing (global, maybe per lane – **open decision 4**).
- Per-step micro-timing (nudge early/late).
- Per-lane length and rate for polymeters/polyrhythms (**open decision 3**).
- Humanize (random timing and velocity amount).

### Phase 3 – Variation

- Per-step probability.
- **Conditional trigs**: A:B (play on pass A of every B), FILL / NOT FILL, PRE / NOT PRE (previous
  condition result in the lane), NEI / NOT NEI (neighbour lane's result), FIRST / NOT FIRST. Fill mode
  as a momentary/latching button and a host parameter, so it can be automated or mapped.
- Ratchets (repeats within a step) and flams.
- Euclidean fill per lane (pulses, steps, rotation).
- Randomize/generate tools with amounts.
- Manual: a dedicated chapter with diagrams and worked examples for every condition.

### Phase 4 – Patterns and structure

- Pattern bank (size: **open decision 5**), copy/paste/clear, shift, reverse.
- Pattern switching: in the editor (quantised to pattern end), by host parameter, and by incoming MIDI
  notes so DAW clips can choose patterns (**open decision 6**).
- Chains / song mode (**open decision 7**).

### Phase 5 – Kits and presets

- User kit library (save, load, rename, delete, import), stored in `Documents/Beatsmith/Kits`.
- User preset library, reusing MIDI Forge's design (flat list, ◀ ▶, `*` for unsaved changes,
  Show folder, Import).
- A few factory kits beyond GM if useful.

### Phase 6 – Beyond notes (to be confirmed)

- Per-step parameter locks sending MIDI CC (e.g. decay or tune of a drum voice). VST3 MIDI CC output
  needs a special path in JUCE, so this gets a prototype first (**open decision 2**).
- Export a pattern as a MIDI file / drag it into the DAW.

### Phase 7 – Polish and 1.0

- UI polish, keyboard shortcuts, tooltips, accessibility.
- Complete manual with current screenshots; performance and stress testing in both hosts.
- Release `v1.0.0`.

## 6. Open decisions

To be settled before the phase that needs them; each answer moves into section 2.

1. **Number of lanes and steps.** E.g. 16 lanes × up to 64 steps (shown in pages of 16)? Fixed or
   user-adjustable lane count? *(next to discuss)*
2. **Parameter locks / CC per step** – in which phase, and which values can be locked?
3. **Per-lane length and rate** – needed for polymeters? Which rates (1/32 … 1/4, triplets, dotted)?
4. **Swing** – global only or per lane? Swing amount range and grid (8ths/16ths)?
5. **Pattern bank size** – e.g. 4 banks × 16 patterns?
6. **Pattern switching by MIDI** – which notes/channel, and is it quantised?
7. **Song / chain mode** – needed inside the plug-in, or is the DAW arrangement (MIDI switching) enough?
8. **Behaviour when the host is stopped** – silent, or a preview/free-run mode for auditioning?
9. **Note length defaults** – drum hits usually only need a short fixed length; which default?
10. **Editor size and layout** – fixed size or resizable; how much on one screen?

## 7. Technical notes and risks

- **MIDI output in VST3.** Note output works in Reaper and Bitwig for a JUCE MIDI-effect plug-in (proven
  by MIDI Forge). CC output uses a different VST3 mechanism and must be verified in both hosts before
  phase 6 is promised.
- **Host transport edge cases.** Loops, jumps, tempo changes, pre-roll and hosts that report no position
  must be handled; the engine gets dedicated tests for each.
- **Timing at block boundaries.** Swing, negative micro-timing and flams can move a note before its
  step; the scheduler has to look ahead and may need to report latency.
- **State compatibility.** Saved projects must keep loading after updates: versioned state, append-only
  IDs and choice lists (same rules as MIDI Forge).
- **UI performance.** A large grid redrawn at playback speed must stay light on the CPU.
