# Observing the original (DOSBox-X, PC-98 mode)

With the user's permission, the original (RPG.EXE / MUSIC.COM) may be **run in an emulator and observed from
the outside**. Disassembly, memory-dump analysis and tracing code in a debugger are not allowed (black-box
observation only).

## Usage

```
H98_DISPLAY=101 python3 tools/dosbox/h98.py GAMEDIR OUTDIR "MUSIC -R;RPG" "5 shot:a key:Return 3 shot:b hold:Right:1.0 shot:c"
```

- Script words: a number of seconds (real) to wait / `key:NAME` (xdotool key names: Return, Escape, Up, Down,
  Left, Right, z, x, Shift_L …) / `hold:NAME:SECONDS` / `shot:NAME` (OUTDIR/NAME.png, 640×400).
- When used in parallel, give each owner a different `H98_DISPLAY` (music=101, d1=102, d2=103, others=104+).
- The FM sound board is a PC-9801-26K (I/O 0x188, IRQ 12). Make it resident with `MUSIC -R`, then start `RPG`.
- D1 waits for a key press during the opening. Timing is real time, so this is not suited to exact
  frame-by-frame comparison (use it to check screen layout, colors, wording and progression).
- Because it runs in real time, results depend on emulator speed (cycles=max).

## Known limitations

- **Start D1 from the batch file shipped with the game** (e.g. `COME` for come, which runs `music -r` →
  `edinit q` → `rpg 0` → `music -r`). Starting RPG directly with no arguments, as in `MUSIC -R;RPG`, leaves a
  black screen that never progresses. Via the batch file, come's title image, title menu and first map (solid
  red) match the reference screens (confirmed 2026-10-18).
- D2 works correctly (confirmed with dino and keep).
- Additional DOSBox-X settings can be passed with `H98_SET="section property=value;..."`.
