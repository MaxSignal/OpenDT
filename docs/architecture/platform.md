# Platform layer (core and app)

Scope: `src/core/` (shared by both engines) and `src/app/` (executables), specification chapter 01.

## `src/core`

| File | Contents | Spec |
|---|---|---|
| `host.h` | The engine–host boundary: `Host` (`vsync`, `input`, `sound`, `log`), `InputState`, `SoundRequest`, `StartOptions` (debug start options), `Engine`, `QuitGame` | 01 §10 |
| `screen.h/.cpp` | `Screen`: the PC-98 display model, plane-image decoding and drawing primitives, final composition | 01 §1 |
| `text.h/.cpp` | Shift-JIS handling, glyph lookup, D1 and D2 text rendering rules, gaiji (custom glyphs), name trimming, UTF-8 ↔ Shift-JIS | 01 §2, §6.4 |
| `font_data.cpp` | Built-in 8×16 and 16×16 glyph tables generated from the Shinonome fonts (`tools/gen_font.py`) | 01 §2.3 |
| `sjis_table.cpp` | Shift-JIS ↔ Unicode table for file-name display (`tools/gen_sjis.py`) | 01 §2.10 |
| `timing.h` | VSYNC rate and the millisecond-to-frame conversion with carry-over (`MsBalance`) | 01 §3 |
| `util.h/.cpp` | `Rng` (Turbo C `rand()` compatible), case-insensitive path resolution, portable UTF-8 file helpers, 48-byte palette files, PNG and WAV writers | 01 §5, §6, §9 |

### Screen model

`core::Screen` holds the complete display state of a PC-98 in 640×400 16-color mode:

- `pix`: one palette index (0–15) per pixel (the four bit planes combined).
- `pal`: 16 colors with 4-bit R/G/B components. They are expanded to 8 bits as ×17 when composing.
- The text layer: 80×25 cells, each with an attribute byte and an 8×16 glyph. Only D2 uses it, mostly as
  black masks around the field and for a few glyphs.
- `frameBorder`: D1's black border around the play area. `graphicsOn`: hides the graphics plane.
- `scrollLine` / `hOffset`: GDC split scroll and the 16-dot horizontal offset used for screen shaking.

The primitives (fill, XOR, transparent blit, silhouette, 1-bit bitmaps, masked plane composite) all clip to the
screen. `decodePlanes` converts the B/R/G/I plane-sequential images used by the game data into palette indices.
`compose(rgb)` produces the final 640×400 RGB frame: graphics with scrolling, then the border or blanking, then
the text layer on top.

### Text

Glyphs come from built-in tables, so no font files are needed at run time. D1 draws opaque, bold glyphs on a
fixed character grid. D2 draws transparent, bold glyphs on the graphics plane, or unbolded glyphs into
text-layer cells, and maps JIS rows 0x76–0x77 to the game's GAIJI.DAT glyphs. Game strings stay in Shift-JIS
throughout. Built-in system messages are written in UTF-8 in the source and converted with `utf8ToSjis()`.

### Files

The original data refers to files with DOS-style names in arbitrary letter case (`BGM\BGM00.MML`).
`resolvePath(root, rel)` splits on `/` and `\` and matches each component case-insensitively (ASCII only).
`readFile` / `tryReadFile` / `fileExists` build on it. All host paths are UTF-8 strings. `openFile`, `listDir`
and the other helpers go through `std::filesystem`, so non-ASCII paths also work on Windows.

## `src/app`

| File | Contents | Spec |
|---|---|---|
| `games.h/.cpp` | Game detection (D1/D2), recursive folder scan, the game browser (rendered directly in RGB), title previews | 01 §7 |
| `sdl_main.cpp` | `opendt`: SDL2 window, renderer and audio device; browser loop; one session per game on a game thread; input mapping; screenshots | 01 §4, §7.4–7.6, §8.2 |
| `headless.cpp` | `opendt-headless`: script parser, deterministic single-threaded host, PNG/WAV output, `--games` listing | 01 §9.2 |
| `mmlplay.cpp` | `mmlplay`: plays an MML/BGM/SOUND.DAT effect to WAV, or batch-compiles all MML files under a folder | — |

### Sessions

`opendt` decides between "start this game" and "browse this folder" (`detectGame`). For each game it:

1. creates a `music::MusicPlayer` (which loads SOUND.DAT and checks the MUSIC.COM version);
2. creates the engine (`engineForRun` picks D1 or D2), which loads all data, or reports the error in the browser;
3. runs the engine on a game thread while the UI thread pumps events.

Closing the window throws `QuitGame` out of the next `vsync()`. Returning to the browser keeps the selection.

### Headless scripts

The script is a list of words: frame waits, key presses (`z x u d l r q c p n`, optionally `*N` frames), `shot:NAME`
and `quit`. One step is consumed per `vsync()`. A key press is always followed by a 4-frame release. The run ends
when the script is exhausted and the frame limit (`-n`) is reached. The debug options (`--warp`, `--battle`,
`--gold`, `--item`, `--seed`) are passed to the engine through `StartOptions`.

## Portability

The code is standard C++17 plus SDL2 (desktop player only). Platform differences are confined to `util.cpp`
(UTF-8 command line and wide-character file APIs on Windows) and `sdl_main.cpp`, which uses SDL for the
executable directory. On Windows, SDL2main provides UTF-8 `argv`. The CMake option `OPENDT_VENDOR_SDL` builds SDL2
statically from source. CI uses it for the Windows and macOS builds.
