# Beatsmith – Plan

Beatsmith is a drum step sequencer plug-in: it plays drum patterns in sync with the DAW and sends them
as MIDI notes into its track, where a drum plug-in or hardware drum machine turns them into sound.
It is the companion of [MIDI Forge](https://github.com/ugerhard-git/midi-forge).

This document records **what** we build, **in which order**, and the **decisions** taken so far. It is
updated whenever a decision is made or a phase is finished. Ideas that are discussed but not scheduled go
into [docs/IDEAS.md](docs/IDEAS.md); once something is scheduled it moves here.

Status: **planning done for phases 0–1** – no plug-in code yet.

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
| Odd meters | **Core feature.** Any meter (3/4, 5/4, 7/8, 9/8, 11/8 …) with a beat grouping (e.g. 7/8 = 2+2+3). Nothing in engine or editor assumes 16 steps per bar |
| Meter source | **Follow the host's time signature** by default, with an **override per pattern** |
| Polymeters | **Core feature.** Every lane has its own length (1–128 steps) |
| Polyrhythms | **Core feature.** Every lane has its own rate: common values (1/32 … 1/2, triplet, dotted) one click away, plus **free tuplets** ("N steps in the time of M beats") |
| Lane realignment | **User setting**: at song start only, at every pattern restart, or every N bars |
| Maximum length | **128 steps** per lane and per pattern |
| Lanes | **Up to 16** per pattern. Lanes are added and removed as needed; only lanes in use are shown |
| Note length | Short fixed gate per lane by default (1/32 note or about 50 ms – the exact value is set when building phase 1), adjustable per lane and per step |
| Host stopped | **Silent** by default; an **Audition** button plays the pattern on its own (at the host's tempo) for programming without starting the DAW |
| Editor | **Resizable** window; the grid scrolls and zooms horizontally |
| Host meter changes | **User setting**: when the host's time signature changes mid-pattern, either restart the pattern at the next bar or keep its own phase |
| Step position | Computed from the host position (anchor + step duration + lane length), never counted, so loops and jumps always land on the right step in every lane |

## 3. Concepts and vocabulary

These words are used in code, UI and manual alike.

- **Lane** – one drum voice (kick, snare, closed hat …). A lane has a name, a MIDI note, a MIDI channel,
  its own **length** and **rate**, and its own row of steps.
- **Meter** – the time signature a pattern is played in (e.g. 7/8) together with its **beat grouping**
  (e.g. 2+2+3). Taken from the host unless the pattern overrides it. The grouping drives grid shading,
  accent helpers and swing.
- **Rate** – how long one step of a lane lasts: a note value (1/16, 1/8 triplet, dotted 1/8 …) or a free
  tuplet "N steps in the time of M beats" (e.g. 5 in 4).
- **Realignment** – the moment all lanes jump back to their first step together: song start only, every
  pattern restart, or every N bars.
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

The data model and the editor are designed for odd meters, polymeters and polyrhythms from the start,
because adding them later would mean reworking both.

- Up to 16 lanes, added and removed as needed (only lanes in use are shown), each with up to 128 steps;
  step on/off by clicking and dragging.
- Per-step velocity, with a quick accent gesture.
- Note length (gate) per lane, optionally per step; short fixed default.
- GM default kit; per-lane name, note and channel editable; mute and solo per lane.
- **Meter**: follows the host's time signature; per-pattern override with beat grouping (common
  groupings offered per meter, e.g. 7/8 → 2+2+3, 3+2+2, 2+3+2).
- **Per-lane length** (1–128) and **per-lane rate**: common rates in a quick menu, free tuplets
  "N in M" for everything else.
- **Realignment** setting: song start, every pattern restart, or every N bars.
- **Host meter change** setting: restart at the next bar or keep the pattern's phase.
- Editor laid out by bars and beat groups (not fixed pages of 16); steps beyond a lane's length greyed
  out; lanes with other rates drawn at their true time width so they line up visually.
- Host sync: follows tempo, time signature, position, loops and jumps; silent while the host is stopped.
- Audition button: plays the pattern without the host transport, for programming.
- Resizable editor; the grid scrolls and zooms horizontally.
- Sample-accurate note output; no stuck notes (stop, loop, jump, edits, kit changes, bypass).
- State saved in the DAW project.
- Release `v0.1.0`.

### Phase 2 – Feel

- Swing that follows the beat grouping (**open decision 3**).
- Per-step micro-timing (nudge early/late).
- Humanize (random timing and velocity amount).
- Accent helpers that follow the beat grouping (e.g. "accent every group start").

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

- Pattern bank (size: **open decision 2**), copy/paste/clear, shift, reverse.
- Pattern switching: in the editor (quantised to pattern end), by host parameter, and by incoming MIDI
  notes so DAW clips can choose patterns (**open decision 4**).
- Chains / song mode (**open decision 5**).

### Phase 5 – Kits and presets

- User kit library (save, load, rename, delete, import), stored in `Documents/Beatsmith/Kits`.
- User preset library, reusing MIDI Forge's design (flat list, ◀ ▶, `*` for unsaved changes,
  Show folder, Import).
- A few factory kits beyond GM if useful.

### Phase 6 – Beyond notes (to be confirmed)

- Per-step parameter locks sending MIDI CC (e.g. decay or tune of a drum voice). VST3 MIDI CC output
  needs a special path in JUCE, so this gets a prototype first (**open decision 1**).
- Export a pattern as a MIDI file / drag it into the DAW.

### Phase 7 – Polish and 1.0

- UI polish, keyboard shortcuts, tooltips, accessibility.
- Complete manual with current screenshots; performance and stress testing in both hosts.
- Release `v1.0.0`.

## 6. Open decisions

To be settled before the phase that needs them; each answer moves into section 2.

1. **Parameter locks / CC per step** – in which phase, and which values can be locked?
2. **Pattern bank size** – e.g. 4 banks × 16 patterns?
3. **Swing in odd and compound meters** – how swing applies within 2- and 3-groups; global or per lane?
4. **Pattern switching by MIDI** – which notes/channel, and is it quantised?
5. **Song / chain mode** – needed inside the plug-in, or is the DAW arrangement (MIDI switching) enough?

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
- **Time signatures from the host.** The host reports the current time signature and the position of the
  last bar line. Both are used to anchor patterns to bars; hosts that report neither fall back to the
  pattern's own meter counted from song start.
- **Free tuplets and rounding.** Step times for rates like 5-in-4 are computed from exact fractions of a
  beat, never accumulated, so lanes don't drift against each other over long songs.
- **UI performance.** A large grid redrawn at playback speed must stay light on the CPU.
