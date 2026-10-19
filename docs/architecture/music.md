# Music module architecture

Scope: `src/music/` (the MUSIC.COM compatible sound driver used by both engines), specification chapter 02 and
01 §8. The module turns MML text and SOUND.DAT into YM2203 register writes, runs them through the ymfm YM2203
model, and delivers 44.1 kHz stereo PCM. Engines never touch it directly: they send `core::SoundRequest`s
through `core::Host::sound()`.

## Files

| File | Contents | Spec |
|---|---|---|
| `textscan.h` | `Cursor`: delimiter skipping, case-insensitive keyword match, number reading (16-bit wrap, `:` counts as digit 10, final reinterpretation as signed 16-bit) | 02 §2.2–2.3 |
| `mml.h/.cpp` | `compileMml()` → `Song`; line splitting shared with SOUND.DAT; length codes; `tempoToQ()`; the intermediate-code opcodes | 02 §2, §1.3 |
| `sounddat.h/.cpp` | `parseSoundDat()` → `SoundDat` (records in file order + number table 0–64) | 02 §4.1–4.3 |
| `driver.h/.cpp` | `Driver`: sequencer, volume/frequency rules, key on/off, effects, fade, Ver 2.00/2.21 behaviour; emits writes through a callback | 02 §3, §4 |
| `player.h/.cpp` | `MusicPlayer`: request queue, ymfm chip, the two clocks, resampling and DC cut; MUSIC.COM version check; SOUND.DAT load | 01 §8, 02 §1.4, §5, §6 |

New files in `src/music/` are picked up by CMake automatically.

## Compile pipeline (MML text → `Song`)

1. `splitLines`: stop at 0x1A, split on LF, drop every CR, append a CR terminator to each line (02 §2.1).
2. Each line is classified by keyword prefix (`SOUND:` … `->`, 02 §2.2). Unknown lines are ignored silently.
3. The line set is scanned in passes (02 §2.4):
   - **A** voices: `SOUND:`/`LFO:`/`OPn:` fill `Song::voices[21]`. The LFO line also precomputes the per-note
     LFO table (02 §2.6). Operator slot index `s` maps to register offset `s*4` (slot 0 = OP1, 1 = OP3, 2 = OP2, 3 = OP4).
   - **B** macros: each `STR:` body is compiled into `code`, ending with `kOpReturn`. Macros are registered
     before their body is compiled, so self-reference works and forward references are errors.
   - A `kOpPartEnd` separator follows the macros.
   - **C** envelopes: `SSGENV:`/`->` build `env` (0xFF separators). `envStart[n]` points at the period value.
   - **D** parts `1: 2: 3: D: 4: 5: 6:` in that order (channels 0–6). Each line ends with `kOpLineEnd` and each
     part with `kOpPartEnd`. `partStart[ch]` is recorded; a part whose first opcode is `kOpPartEnd` is invalid.
4. `T` only changes `Song::tempo` at compile time; the last one in pass order wins.
5. Errors never fail a compile. They are counted in `errorCount`, up to 32 messages are kept, and the rest of the
   offending line is dropped (02 §2.13).

Opcode byte values follow the reference values listed in 02 §2.11, so code dumps can be compared directly with
the spec.

## Driver runtime state (`Driver`)

- `song_`: the loaded `Song`. `load()` replaces it, and the tempo (and so the timer-B period) changes at once.
- `ch_[7]` (`Chan`): position, call/loop stacks (16 levels each), remaining ticks, octave, note, volume, gate, Q,
  detune, portamento, tremolo, vibrato, soft LFO, SSG soft/hard envelope and key state. `initChannel()` resets
  the "reset at song start" group (02 §3.2). The other fields (envelope position, LFO accumulator, tremolo and
  vibrato counters, portamento values, hard-envelope flag and shape) persist across songs.
- `lineEnd_[7]`: line-sync flags (02 §3.4). A tick runs up to 4 rounds; all valid parts must reach their line
  end before any advances.
- `shadow_[256]`: last value written per register. Register 7 read-modify-write always uses the shadow.
- `playing_` / `phase_`: the sequencer runs only while playing, on every second timer-B overflow (02 §1.2).
- `everPlayed_`: false until the first `play()` after creation (see Fade below).
- Effects: `sfxPrio_`, `sfxPending_` (−1 = none), `sfxRec_` (index into the file-order records, used for
  chaining), `sfxRemain_` (VSYNCs left), and the tone A/B, volume A/B and noise generators.
- `ver221_`: set by `MusicPlayer` when MUSIC.COM contains `Ver 2.21` (02 §4.7).

Writes go through `write()` (always) or `writeM()`. `writeM()` is used for SSG music writes and is dropped,
without updating the shadow, while `suppress_` is set (Ver 2.21, channels 4/5, effect active).

## The two clocks

Both clocks are counted in **chip samples** (166,400 Hz = 3,993,600 / 24, ymfm `OPN_FIDELITY_MIN`), never in
game frames, so tempo is independent of game speed and fast-forward (02 §1.4, 01 §8.3). Before each chip sample,
`MusicPlayer::Impl::chipSample()` does the following:

1. VSYNC counter −1. At ≤ 0 it adds 166,400 / 56.4 (≈ 2950.35) and calls `Driver::vsync()` (effects, 02 §4.5).
2. Timer counter −1. At ≤ 0 it adds `q × 48` (timer-B period × 166,400) and calls `Driver::timerB()`, which
   toggles the phase and runs `tick()` on every second call.
3. ymfm produces one sample. The mono mix is `FM + (SSG A + B + C) / 2`.

Both counters start at 0. Starting a song (`PlayNumber`/`PlayFile`) resets the timer counter to `q × 48` of the
new song. The chip is created with fidelity MIN, reset, and given an address-only write to 0x2D (prescaler 1/6).
Writes to 0x2D–0x2F are passed as address writes only.

## Request flow (game thread → audio)

`core::SoundRequest` kinds and what `MusicPlayer::apply()` does with them (01 §8.1, 02 §5.3):

| Kind | Action |
|---|---|
| `PlayNumber n` | `BGM/BGMnn.MML` (n & 0xFF). If the file is missing, nothing happens and the current song continues. Otherwise compile, `play()`, restart the timer |
| `PlayFile path` | `stop()` first. If the file is missing, stay stopped. Otherwise compile, `play()`, restart the timer (D2: `DAT_BGM/<name>.MML`) |
| `Stop` | `stop()` |
| `Fade n` | `setFade(n)` |
| `Sfx n` | `requestSfx(1, n)` (`n = −1` changes the priority only). D1 passes its own number, D2 passes the internal number + 10 |

`push()` appends to a mutex-protected queue. `generate()` takes the whole queue at its start, applies the
requests in order, and then renders. In the SDL frontend that happens per audio callback; in the headless runner,
once per game frame (44100 / 56.4 samples with carry). With `setVerbose(true)`, `takeLog()` returns missing-file
and compile-error lines.

## Effects, priorities, versions, fade

- **Priority** (02 §4.4): game effects use priority 1, D-part notes (A–G → `@0`–`@6`) use priority 0. A request
  is dropped only while an effect plays and its priority is lower. Only one request is pending; the next VSYNC
  starts it. Every effect occupies SSG A and B, and register 6 is written every VSYNC.
- **Chaining** (02 §4.6): when an effect ends, the next record in file order starts if it is a chain record
  (`SOUND:` without `@`).
- **Ver 2.00 / 2.21** (02 §4.8): under 2.21, music SSG writes for channels 4/5 are suppressed while an effect is
  active, and when the effect ends the music state of SSG A/B is restored (frequency, volume, key-on; a hard
  envelope is not restarted). Under 2.00, regs 8/9 are just set to 0. Stop also clears 0x90–0x9E under 2.21.
- **Fade** (02 §3.22): `setFade` stores the level and immediately rewrites the volume of all channels. Exception:
  until the first `play()` after the player was created (`everPlayed_` false), it records the level and writes
  nothing. Loading a song without playing it does not count. After the first play, fades write every time,
  including while stopped or during an effect. The fade also lowers priority-0 effects, but not priority-1 effects.
- Song start (02 §3.3) loads voices @1–@3 before the fade is reset to 0. A song started right after a fade to 15
  therefore keeps quiet FM carriers until the next volume update. This is intended.

## Output post-processing (02 §6)

Per output sample: step = 166,400 / rate, so on average 3.77 chip samples are averaged (the previous value is used
if there are none). Then a DC cut `y = x − x₋₁ + 0.9985·y₋₁` is applied (SSG channels output a DC level even when
their tone is disabled). The result is truncated toward zero, clamped to 16 bits and copied to both channels.
There is no other gain or filtering.

## `mmlplay` tool (`src/app/mmlplay.cpp`)

```
mmlplay --batch DIR                                  # compile every BGM/*.MML and DAT_BGM/*.MML under DIR
mmlplay GAMEDIR REL.MML [-t SEC] [-o out.wav]        # play an MML (path relative to GAMEDIR)
mmlplay GAMEDIR --bgm N | --sfx N [-t SEC] [-o out.wav]
        [--after-fade]                               # fade 15 + stop before the request (D1 BGM switch)
```

Playing prints the tempo, q, compile errors and the detected MUSIC.COM version. `--batch` reports per-file
errors; with the sample set it should report 0 errors (2530 files, 969 unique).

## Tests and reference audio

- `tests/test_music.cpp` (filter: `unit_tests music`). Most tests drive `Driver` directly with a register-write
  log (`Rig`), calling `tick()`/`vsync()` by hand. They encode the acceptance items of 02 (start sequence, gate,
  line sync, LFO, portamento, vibrato/tremolo, envelopes, agasa effects @10/@11 as exact per-VSYNC register
  lists, priorities, Ver 2.21 suppression and restore, fade carry-over, items 18–21 of v1.1). Tests that need the
  sample games use `t::samples()` (`OPENDT_SAMPLES`) and are skipped when the samples are absent.
- Reference audio: the spec's `reference_audio/{come,agasa,kokoro}.wav` were recorded with the headless runner and
  the script `100 z 100 z 100 z 100 z 400` (900 frames). To check, render the same way and compare sample by sample:

  ```
  build/opendt-headless SAMPLES/games/come/come -k "100 z 100 z 100 z 100 z 400" -n 900 -w come.wav -o /tmp/x
  ```

  All three are expected to be 100% sample-identical. A difference localised to a few frames usually points
  at register-write timing relative to the audio-side VSYNC (for example, which requests write registers).
  To find it, map each differing sample index to a frame using the 44100 / 56.4 carry rule.
- `mmlplay --sfx N` / `--bgm N` renders are useful to identify which effect or song a reference passage contains
  (compare spectra or per-VSYNC RMS).
