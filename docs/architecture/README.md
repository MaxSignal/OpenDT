# Architecture overview

OpenDT reimplements, at a high level, the behavior of the original game runtime (RPG.EXE) and FM sound driver
(MUSIC.COM) of games made with Dante98 ("D1") and Dante98 II ("D2"). It does not emulate the PC-9801 CPU or
MS-DOS. Instead, it provides a small model of the PC-98 display and sound hardware that the two engines draw to
and drive.

## Layers

```
 app       opendt (SDL2 desktop player)   opendt-headless (scripted runner)   mmlplay
             │ game browser, sessions, threads, audio device, input mapping
 ─────────────────────────────────────────────────────────────────────────────────────
 engines   d1 (Dante98)                         d2 (Dante98 II)
             │ title, field, events, menus, battle, saves — independent implementations
 ─────────────────────────────────────────────────────────────────────────────────────
 common    core: screen model, text/fonts, RNG, files, timing     music: MUSIC.COM driver + YM2203
```

- **core** (`src/core`): the pseudo PC-98 environment shared by both engines: a 640×400 16-color frame buffer,
  palette, text layer, scrolling, Shift-JIS text rendering with built-in fonts, the Turbo C compatible random
  number generator, case-insensitive file access, and PNG/WAV writers. See [platform.md](platform.md).
- **music** (`src/music`): a MUSIC.COM-compatible driver (MML compiler, sequencer, SOUND.DAT effects) that
  writes registers to a YM2203 emulated by ymfm, plus the output stage. See [music.md](music.md).
- **d1** (`src/d1`) and **d2** (`src/d2`): the two game engines. They share the common layer but nothing else.
  The data formats and game rules of the two tools are different, so the engines are deliberately kept
  separate. See [d1.md](d1.md) and [d2.md](d2.md).
- **app** (`src/app`): the launcher (game detection and browser), the SDL2 desktop frontend, the headless runner
  and the `mmlplay` tool. See [platform.md](platform.md).

## The engine–host boundary

An engine talks to the outside world only through `core::Host` (`src/core/host.h`):

| Call | Meaning |
|---|---|
| `vsync(screen)` | Present the current screen state and wait for the next vertical sync (1/56.4 s). All game timing is counted in these frames. May throw `core::QuitGame` when the host wants the game to stop. |
| `input()` | The current state of the abstract keys (directions, confirm, cancel, fast, debug, ROLL UP/DOWN). |
| `sound(request)` | Queue a sound request: play BGM by number or file, stop, fade level, sound effect. |

Engines are written as ordinary sequential code that calls `vsync()` whenever the original would wait for a
vertical sync, so the original's frame-based timing carries over directly. `d1::createEngine` and
`d2::createEngine` load all game data (throwing `core::DataError` on failure) and return a `core::Engine` whose
`run()` plays the game.

## Threads

- **Desktop player:** the engine runs on a game thread. `vsync()` composes the frame to RGB, hands it to the
  UI thread and sleeps until the next frame time (Tab skips the wait). The UI thread polls SDL events, updates
  the input state and presents frames. The audio callback drains the queued sound requests and renders samples.
  Music timing is driven by the audio sample clock, so tempo does not depend on the game speed.
- **Headless runner:** everything runs on one thread. Each `vsync()` advances the input script, renders
  44100/56.4 audio samples into the WAV file and saves requested screenshots. The run is fully deterministic.

## Determinism and testing

Random numbers, waits and input sampling follow the original's order exactly, so a given script and seed always
produce the same frames and the same audio. The headless runner, unit tests (`tests/`) and
`tools/run_all_refs.sh` (pixel comparison against reference screenshots) rely on this. See
[CONTRIBUTING.md](../../CONTRIBUTING.md) for how to run them.

## Specification chapters

The code comments refer to the clean-room specification by chapter and section (for example "spec 22 §5.4"):

| Chapter | Topic | Code |
|---|---|---|
| 01 | Platform: screen, text, timing, input, RNG, files, launcher, headless runner | `src/core`, `src/app` |
| 02 | MUSIC.COM-compatible sound driver | `src/music` |
| 10, 11, 12, 19 | D1 data formats, engine, battle, system messages | `src/d1` |
| 20–24 | D2 data formats, flow/field, events, menus, battle | `src/d2` |

The specification itself is not part of this repository; see [docs/cleanroom/](../cleanroom/README.md) for how
it was used.
