# Contributing to Beatsmith

These rules apply to every change, by people and by AI assistants alike. AI-specific working rules are in
[CLAUDE.md](CLAUDE.md). What is being built and in which order is in [PLAN.md](PLAN.md).

## Ground rules

1. **Readable code.** Use understandable, generous names for variables, functions, classes and files:
   `laneLength`, `samplesPerStep`, `releaseSoundingNotes()` – not `len`, `sps`, `rel()`. One- and
   two-letter names are only acceptable for the conventional `x`, `y` and `g` (graphics). clang-tidy
   enforces a minimum name length.
2. **Explain the code.** Comment what a piece of code does and why, in a form IDEs pick up: Doxygen
   comments (`/** ... */`, `///<`) on every class, function and non-obvious member, `@param` / `@return`
   where they help. Use ordinary `//` comments inside functions for the *why* of non-obvious steps.
3. **Documentation stays true.** Every change that affects architecture, build, behaviour or file
   layout updates [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and the doc comments in the same commit.
   A finished phase or a new decision updates [PLAN.md](PLAN.md).
4. **The manual stays true.** Every user-visible change (features, parameters, ranges, kits, presets, UI,
   installation) updates [docs/MANUAL.md](docs/MANUAL.md) in the same commit. Features that are hard to
   grasp – conditional trigs above all – are explained with worked examples, not just listed. If the
   editor looks different, update the images in `docs/images/` from the CI artifact
   `editor-screenshots`.
5. **Tests.** Write tests whenever possible or sensible, and keep them up to date with the code:
   - engine behaviour → `Tests/Engine/` (fast, no JUCE),
   - plug-in layer (parameters, kits, presets, state, editor) → `Tests/Plugin/`,
   - every bug fix gets a test that would have caught it.
6. **Formatting and linting.** Code is formatted with clang-format (`.clang-format`) and checked with
   clang-tidy (`.clang-tidy`) and compiler warnings as errors. CI rejects changes that fail them.
7. **Security.** Treat everything from outside as untrusted: project state from the host, preset and kit
   files, imported MIDI files, text typed by users, MIDI from any source. Validate and clamp, never trust
   sizes or indices. Keep dependencies pinned (exact JUCE tag, commit SHAs for other repositories and for
   GitHub Actions) and workflow permissions minimal. Never commit secrets. See [SECURITY.md](SECURITY.md).
8. **Performance.** Code on the audio thread (`processBlock` and everything the engine calls from it)
   must be real-time safe: no memory allocation, no locks, no file or network access, no unbounded
   loops. Reserve memory in constructors and cap queue sizes. The editor's grid must stay cheap to
   redraw during playback.
9. **Compatibility.** Parameter IDs, enum values and choice label order are stored in user projects,
   kits and presets: never rename, reorder or remove them – only append. Every stored format carries a
   version number, and older versions keep loading.
10. **New features are discussed first.** When the owner asks about a possible feature, discuss and plan
    it, then wait for an explicit go before writing code or building. Discussed features that are not
    built yet are recorded in [docs/IDEAS.md](docs/IDEAS.md).

## Rules specific to a sequencer

11. **No meter assumptions.** Nothing in the engine or editor may assume 4/4, 16 steps per bar, pages of
    16 or equal lane lengths. Odd meters, polymeters and polyrhythms are core features, and tests cover
    them (e.g. 7/8, 11/8 and 5-in-4 lanes running together).
12. **Positions are computed, never counted.** A lane's current step is derived from the host position,
    the anchor and the lane's exact step duration (kept as exact fractions of a beat). Never advance a
    counter per block: loops, jumps and long songs must always land on the right step.
13. **No stuck notes.** Every note-on has a guaranteed note-off, whatever happens: stop, loop, jump,
    pattern change, lane removed, note or channel changed, bypass, editor closed. The randomized stress
    tests must keep passing.

## Building

Nothing has to be built locally: every push builds the plug-in on GitHub Actions (see
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#build-and-ci)). To build locally anyway you need CMake ≥ 3.28,
a C++17 compiler and Git.

> The commands below become available with the phase 0 CMake skeleton.

```sh
# Engine tests only (fast, no JUCE download)
cmake -S . -B build -DBEATSMITH_BUILD_PLUGIN=OFF -DBEATSMITH_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure

# Plug-in (VST3) and the plug-in layer tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBEATSMITH_BUILD_PLUGIN_TESTS=ON
cmake --build build --config Release --target Beatsmith_VST3 beatsmith_plugin_tests
```

On Linux the plug-in needs the JUCE dependencies, e.g. on Ubuntu:
`libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxi-dev libxcomposite-dev libxrender-dev libgl1-mesa-dev`.

Results land in `build/Beatsmith_artefacts/Release/VST3`.

## Checks to run before pushing

```sh
pip install clang-format==19.1.7           # the version CI uses
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')
clang-tidy -p build Source/Engine/*.cpp Tests/Engine/*.cpp
ctest --test-dir build --output-on-failure
```

Format everything in place with `clang-format -i $(git ls-files '*.cpp' '*.h')`.

## Releases

Bump `project(Beatsmith VERSION x.y.z)` in `CMakeLists.txt`, update the manual and PLAN.md if needed, then
push a tag `vx.y.z`. CI builds the plug-in and publishes the release with SHA-256 checksums and
build-provenance attestations.

## Pull requests

- `main` is protected: every change arrives through a pull request, and CI must be green.
- One topic per pull request, with a description of what changed and why.
- Code, docs, manual and tests change together.
- Pull requests are squash-merged by the owner.
