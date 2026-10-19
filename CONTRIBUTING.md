# Contributing to OpenDT

Thank you for your interest in OpenDT. This document explains how to build and test the project, how the code
is organized, and the clean-room rules every contribution must follow.

## Building and testing

Requirements: a C++17 compiler, CMake 3.16 or later, zlib, and SDL2 (for the desktop player).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build/unit_tests
```

Tests that use game data read it from `$OPENDT_SAMPLES` (default `./samples`, laid out as
`<root>/games/<archive>/…` and `<root>/d1/<name>/…`) and write scratch files to `$OPENDT_TMP` (default
`/tmp`). No game data is included in the repository.

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

For the internals of the D2 engine (files, hooks, state, helpers, control flow), see
[docs/d2_architecture.md](docs/d2_architecture.md).

Keep changes to `src/core` and `src/app` minimal and avoid breaking the other modules.

## Clean-room rules

OpenDT is a clean-room implementation. To keep it that way, contributors must follow these rules:

1. **Do not read other reimplementations.** Do not look at the source code of, or information about, other
   reimplementations of these engines (for example OpenDante98), and do not use information about the
   internals of the original software found on the Internet.
2. **Do not disassemble the original executables.** Do not disassemble, decompile or otherwise analyze the
   binaries of RPG.EXE, MUSIC.COM or other original executables. The only exceptions are the byte reads for
   version detection and the version-string search that the specification explicitly allows.
3. **Base changes on the specification and observation.** Every behavior must be justified by the clean-room
   specification, the game data files, or black-box observation of the original running in an emulator
   (running it and watching the screen and sound is fine; memory-dump analysis and stepping through code in a
   debugger are not). Mention the relevant spec section (e.g. "spec 22 §5.4") in code or commit messages where
   helpful.
4. **Record ambiguities instead of guessing.** When the specification is ambiguous or contradictory, do not
   fill the gap by guesswork. Record it as a question, and if a stopgap implementation is needed, mark it in
   the code with `// PROVISIONAL(Qn):` referring to the question number.

If you are unsure whether a source of information is acceptable, ask before using it.

## License

By contributing, you agree that your contributions are licensed under the MIT License (see `LICENSE`).
