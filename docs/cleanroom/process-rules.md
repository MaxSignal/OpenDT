# OpenDT development rules

A clean-room reimplementation of a player for Dante98 / Dante98 II games.

## Clean-room rules (mandatory)

1. Permitted sources of information: the clean-room specification (v1.1; v1.0 is kept for reference), the data
   files of the sample games (`$OPENDT_SAMPLES`), observations of the original running in an emulator such as
   DOSBox, and general-purpose libraries (ymfm, Shinonome font).
2. Forbidden: disassembling, decompiling or otherwise analyzing the binaries of executables such as RPG.EXE and
   MUSIC.COM (only reading bytes for version detection and searching for version strings, as permitted by the
   specification, is allowed). Searching for or retrieving information on the Internet about Dante98, MUSIC.COM
   or the reference implementation used by the specification team (do not use WebSearch/WebFetch/curl).
3. When the specification is ambiguous or contradictory, do not fill the gap by guesswork; record it as a
   "question to the spec" in `docs/questions_<module>.md` (now `docs/cleanroom/questions/`). If you add a stopgap implementation, mark it in the
   code with `// PROVISIONAL(Qn):` using the matching question number.

## Layout

| Directory | Contents | Spec |
|---|---|---|
| src/core | Screen model, text, RNG, files, PNG/WAV, Host boundary | 01 |
| src/music | MUSIC.COM-compatible driver, ymfm glue, MusicPlayer | 02, 01 §8 |
| src/d1 | D1 engine | 10, 11, 12, 19 |
| src/d2 | D2 engine | 20–24 |
| src/app | Launcher, SDL frontend, headless runner | 01 §7, §9 |
| tests | Unit tests (`tests/test_<module>.cpp`) | Acceptance-test points of each chapter |

## Building

```
cmake -S . -B build-<module> && cmake --build build-<module> -j2 --target <targets>
```

During parallel work, each owner uses their own build directory (`build-music`, `build-d1`, `build-d2`).
Changes to `src/core` and `src/app` must be additive and minimal, and must not break other modules' builds.

## Headless runner

```
build/opendt-headless GAMEDIR -k "60 shot:01_title 30 z 30 z 150 shot:02_start" -o OUTDIR [-w out.wav] [-v]
```
