# Contributing to OpenDT

Thank you for your interest in OpenDT. This document explains how to build and test the project, how the code
is organized, and the clean-room policy that contributions follow.

## Building and testing

Requirements: a C++17 compiler, CMake 3.16 or later, and SDL2 for the desktop player (or `-DOPENDT_VENDOR_SDL=ON` to build it from source). CI builds and tests Linux, Windows and macOS on every push (`.github/workflows/build.yml`).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build/unit_tests
```

Tests that use game data read it from `$OPENDT_SAMPLES` (default `./samples`, laid out as
`<root>/games/<archive>/…` and `<root>/d1/<name>/…`) and write scratch files to `$OPENDT_TMP` (default:
the system temporary directory). No game data is included in the repository. Without the sample games, the tests
that need them are reported as skipped; CI runs in this mode.

`tools/run_all_refs.sh` runs every sample game through the headless runner and compares the screenshots pixel by
pixel with the reference screens. It needs `OPENDT_REF` set to the `reference_shots` directory from the
clean-room specification; `BIN` and `OUT` override the runner binary and output directory.

`opendt-headless` is the main tool for reproducing and checking behavior; see the README for its script format.

## Code layout

| Directory | Contents | Spec chapters |
|---|---|---|
| `src/core` | Screen model (640×400, 16 colors, text layer, scrolling), text, RNG, files, PNG/WAV, host boundary | 01 |
| `src/music` | MUSIC.COM-compatible driver (MML compiler, sequencer, sound effects), ymfm glue, music player | 02, 01 §8 |
| `src/d1` | D1 (Dante98) engine | 10, 11, 12, 19 |
| `src/d2` | D2 (Dante98 II) engine | 20–24 |
| `src/app` | Launcher/game browser, SDL frontend, headless runner, mmlplay | 01 §7, §9 |
| `tests` | Unit tests (`tests/test_<module>.cpp`) | Acceptance points of each chapter |
| `tools` | Reference-comparison scripts, font/table generators, DOSBox-X observation harness | — |
| `third_party` | ymfm | — |

For how the pieces fit together, see the architecture documentation in [docs/architecture/](docs/architecture/README.md):
an overview, the platform layer, the music driver, and the D1 and D2 engines.

Keep changes to `src/core` and `src/app` minimal and avoid breaking the other modules.

## Clean-room policy

OpenDT was written without looking at the code of the original programs or of other reimplementations, and we
would like to keep it that way. This protects the project and its users from questions about where the code
came from.

**What you can use**

- The clean-room specification in [docs/cleanroom/spec/](docs/cleanroom/spec/README.md).
- The game data files themselves (graphics, maps, databases, music data), which are what OpenDT reads.
- Your own observations of the original games running in an emulator: what is on the screen, what you hear and
  how the game reacts to input.
- Public documentation of the hardware and the tools, such as PC-98 hardware references, YM2203 datasheets and
  the user manuals.

**What to avoid**

- Disassembling, decompiling or debugging RPG.EXE, MUSIC.COM or other original executables. Running them in an
  emulator and watching them is fine, but inspecting their code or memory is not.
- Using code or internal notes from other reimplementations that were made by reverse engineering the
  originals.

If you have already studied such material, please don't contribute changes to the parts it covered. Reports,
testing and work in other areas are still very welcome.

**In practice**

- When you change behavior, say in the pull request where it comes from: a section of the specification
  (for example "spec 22 §5.4"), a data file, or an observation (what you ran and what you saw).
- If the specification is unclear or contradicts itself, or you think it is wrong, open an issue rather than
  guessing. A temporary implementation is fine if the code says it is temporary and links to the issue.
- If you are not sure whether something is acceptable, ask in an issue first.

## Continuous integration and releases

`.github/workflows/build.yml` builds and tests every push and pull request on Linux, Windows and macOS and
publishes releases:

- Every push to `main` replaces the **latest** pre-release with the new build.
- To publish a versioned release, bump `project(OpenDT VERSION x.y.z)` in `CMakeLists.txt` and push to `main`:
  when no release `vX.Y.Z` exists yet, the workflow creates it (and its tag) for that commit.
- Pushing a `v*` tag also creates a release for that tag.

## License

By contributing, you agree that your contributions are licensed under the MIT License (see `LICENSE`).
