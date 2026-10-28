# CLAUDE.md – working rules for AI assistants

Beatsmith is a drum step sequencer plug-in (VST3, Windows) that sends MIDI notes into its DAW track. It is
built with JUCE 9 and CMake, and exclusively by GitHub Actions; the owner does not build locally.
It is the companion of [MIDI Forge](https://github.com/ugerhard-git/midi-forge), whose structure and
conventions it reuses where useful.

Read first: [PLAN.md](PLAN.md) (what is built, decisions, current phase), then
[CONTRIBUTING.md](CONTRIBUTING.md) (all ground rules apply to you), then
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The user-facing behaviour is described in
[docs/MANUAL.md](docs/MANUAL.md).

## Owner's rules (keep this list complete; add new rules here when the owner states them)

1. Code must be readable by humans: understandable, generous names for variables, classes, functions.
2. Comment what code does, in Doxygen style so IDEs show it.
3. Write documentation and keep it true after every change.
4. Write a user manual and keep it true after every change. Explain conditional trigs especially well.
5. Use code formatting tools and linters (clang-format, clang-tidy, warnings as errors).
6. Keep security and performance in mind for every change.
7. Write tests whenever possible or sensible, and keep them up to date.
8. When the owner asks about a possible new feature: first discuss and plan, then **wait for an explicit
   go** before writing code or building.
9. Record new ground rules from the owner in this file and/or CONTRIBUTING.md.
10. Features that are discussed but postponed go into [docs/IDEAS.md](docs/IDEAS.md) (agreed designs as
    *Planned*, open ideas as *Idea*), so nothing gets forgotten. Remove an entry once it is built.
11. Decisions go into [PLAN.md](PLAN.md) (section "Decided"); open questions stay in its "Open decisions"
    list until the owner settles them.

## How we work

- `main` is protected. Work on a branch, open a pull request, wait for green CI; the owner merges
  (squash). Never push to `main` directly.
- The owner may merge a pull request while you are still adding to it. Before pushing more commits to a
  branch, check that its pull request is still open; if it was merged, move the new commits onto a fresh
  branch from `main` and open a new pull request.
- Commits are made under the Claude identity, never with the owner's e-mail address.

## Checklist for every change

- [ ] Names are descriptive; new code has doc comments.
- [ ] Tests added or updated (`Tests/Engine` for engine behaviour, `Tests/Plugin` for the JUCE layer).
- [ ] No assumptions of 4/4, 16 steps per bar or equal lane lengths; odd meters and polymeters tested.
- [ ] Step positions computed from the host position with exact fractions, never counted per block.
- [ ] Every note-on has a guaranteed note-off; stress tests still pass.
- [ ] `docs/MANUAL.md` updated for anything a user can see or hear.
- [ ] `docs/ARCHITECTURE.md` updated for structural, build or CI changes; `PLAN.md` for decisions and
      finished phases.
- [ ] Audio-thread code stays real-time safe (no allocation, locks or I/O).
- [ ] Stored IDs, enum values and choice label order unchanged (append only); state versions handled.
- [ ] Loaded state, kit and preset data validated (versions, sizes, bounds).
- [ ] New GitHub Actions pinned to a commit SHA; workflow permissions minimal.
- [ ] `clang-format` applied; engine tests pass locally if a compiler is available.
- [ ] After pushing, check that the GitHub Actions run is green.

## Useful commands

Available once the phase 0 CMake skeleton exists:

```sh
cmake -S . -B build -DBEATSMITH_BUILD_PLUGIN=OFF -DBEATSMITH_WARNINGS_AS_ERRORS=ON && cmake --build build && ctest --test-dir build --output-on-failure
clang-format -i $(git ls-files '*.cpp' '*.h')
```
