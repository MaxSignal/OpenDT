# OpenDT

OpenDT is a player that runs PC-9801 games made with "RPGツクール Dante98" (RPG Tsukuru Dante98, "D1", 1992) and
"RPGツクール Dante98 II" (RPG Tsukuru Dante98 II, "D2", 1996) on modern systems. It is not an emulator: it reads
the game's data files directly and reimplements the behavior of the original game runtime (RPG.EXE) and FM
sound driver (MUSIC.COM).

## Features

- Both engines: games made with Dante98 (D1) and Dante98 II (D2).
- Title screen, field, events, menus, shops, inns and battles.
- Save/load compatible with the original save files.
- MUSIC.COM-compatible FM/SSG music and sound effects via YM2203 emulation ([ymfm](third_party/ymfm)).
- A built-in game browser: point it at a folder and pick a game.
- Developed and tested on Linux (game logic is verified with the headless runner). The SDL2 desktop player should work on any platform SDL2 supports.

## Getting started

### Dependencies

- A C++17 compiler
- CMake 3.16 or later
- zlib
- SDL2 (needed only for the desktop player; without it, only the headless tools are built)

On Debian/Ubuntu:

```
sudo apt install build-essential cmake zlib1g-dev libsdl2-dev
```

### Building

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces:

| Executable | Description |
|---|---|
| `opendt` | Desktop player (SDL2), with a game selection screen |
| `opendt-headless` | Headless runner for testing and automation (scripted input, PNG/WAV output) |
| `mmlplay` | Plays MML / SOUND.DAT music and batch-checks MML compilation |
| `unit_tests` | Unit tests |

### Running

```
build/opendt [GAME_OR_FOLDER] [--no-title] [--scale N] [--fullscreen] [--mute] [-v]
```

- If `GAME_OR_FOLDER` is a game folder (the folder containing RPG.EXE), the game starts directly.
- If it is any other folder, OpenDT searches it for games and shows a game selection screen.
- If omitted, the current directory is used.

| Option | Meaning |
|---|---|
| `--no-title` | Same as starting the original with no arguments: D1 skips the opening; D2 starts in test-play mode (the debug menu is available) |
| `--scale N` | Window scale factor (default 2) |
| `--fullscreen` | Start in fullscreen |
| `--mute` | Disable sound |
| `-v` | Verbose log output |

### Controls

| Key | Function |
|---|---|
| Cursor keys / numpad 8 2 4 6 | Move, cursor |
| Z / Enter / Space | Confirm, examine |
| X / Esc / Backspace | Cancel, menu |
| Q / Shift | Fast-forward messages (Q in D1, SHIFT in D2) |
| Ctrl | Debug (progress monitor in D1; in D2 test play, STOP is Esc+Ctrl) |
| Page Up / Page Down | PC-98 ROLL UP / ROLL DOWN (value +1 / −1 in the variable editor of the D2 debug menu) |
| Tab (hold) | Fast-forward |
| F12 | Screenshot (`opendt_NNN.png`) |
| Alt+Enter | Toggle fullscreen |

## Games

No game data is included. Supply your own game folders: each game is the folder containing its RPG.EXE and
data files. You can point `opendt` at a single game folder or at a folder that holds several games.

## Headless runner

`opendt-headless` runs a game without a window, driven by a script, and writes screenshots (PNG) and audio
(WAV). It is intended for testing and automation.

```
build/opendt-headless GAMEDIR -k "60 shot:title 30 z 30 z 150 shot:start" -o OUTDIR [-w out.wav] [-n FRAMES] [-v]
    [-s SCRIPT_FILE] [--no-title] [--no-quit] [--warp M,X,Y] [--battle G] [--gold N] [--item ID] [--seed N]
build/opendt-headless --games FOLDER -o OUTDIR
```

Script words: `N` (wait N frames); `z x u d l r q c p n` (press confirm, cancel, up, down, left, right, fast,
Ctrl, ROLL UP, ROLL DOWN; letters in one word are pressed together, and `*N` holds them for N frames);
`shot:NAME` (save `OUTDIR/NAME.png`); `quit`. `-n` sets the maximum number of frames to run (default 600).
`--games` lists the games found under a folder and renders the game browser to `OUTDIR/games.png`.

## Running tests

```
build/unit_tests                 # unit tests
tools/run_all_refs.sh            # pixel comparison against reference screens for all sample games
```

Tests that need game data read it from your own copies; configure them with environment variables:

| Variable | Used by | Meaning | Default |
|---|---|---|---|
| `OPENDT_SAMPLES` | unit tests, `run_all_refs.sh` | Root of the sample games, laid out as `<root>/games/<archive>/…` and `<root>/d1/<name>/…` | `./samples` |
| `OPENDT_TMP` | unit tests | Scratch directory | `/tmp` |
| `OPENDT_REF` | `run_all_refs.sh` | The `reference_shots` directory from the clean-room specification (required) | — |
| `BIN` | `run_all_refs.sh` | Headless runner to use | `build/opendt-headless` |
| `OUT` | `run_all_refs.sh` | Output directory for screenshots and logs | `/tmp/opendt_refs` |

## Clean-room implementation

OpenDT was written only from a natural-language specification of the original software produced by a separate
team, together with the data files of sample games and black-box observation of the original running in an
emulator. The original executables were not disassembled (beyond the byte reads for version detection that the
specification allows), and no code or information from other implementations was used. The historical records
of this process (development rules, observation notes, questions to the specification) are kept in
[docs/cleanroom/](docs/cleanroom/README.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Contributions must follow the clean-room rules described there.

## License

- OpenDT: MIT License ([LICENSE](LICENSE))
- `third_party/ymfm`: BSD-3-Clause (`third_party/ymfm/LICENSE`)
- Glyph data (`src/core/font_data.cpp`, `tools/*.pcf.gz`): Shinonome font (public domain)
- No game data or original executables are included.

## Trademarks

Dante98, Dante98 II and RPGツクール (RPG Tsukuru / RPG Maker) are trademarks of their respective owners. OpenDT
is an independent project and is not affiliated with or endorsed by them. These names are used only to describe
compatibility.

## Acknowledgements

- [ymfm](https://github.com/aaronsgiles/ymfm) by Aaron Giles, used for YM2203 emulation.
- The Shinonome bitmap fonts (public domain), used for text rendering.
