# Questions for the Spec (clean side → spec-writing side)

> **Status (2026-10-18)**: All 50 questions have been answered by spec v1.1 (`spec-answers-v1.1.md`), and all answers have been reflected in the implementation. There are 0 `PROVISIONAL` markers left in the code.

A list of ambiguities and contradictions in the spec found while implementing and reviewing OpenDT (the clean-room reimplementation).
Each question number is "prefix + number within each chapter file" (e.g. D2F-Q2 = Q2 of the D2 field). `PROVISIONAL(Qn)` in the code
refers to the number within each module's file.

## Current verification status (for reference)

- Reference screens: all 200 shots (40 runs with reference_shots × 5 shots each) match pixel for pixel.
- Reference audio: samples match 100% for kokoro and 99.48% for come and agasa (the only differences are at the M-Q5 location).
- All 137 unit tests (including each chapter's acceptance criteria) pass.

## Questions we would most like answered

| No. | Kind | Summary |
|---|---|---|
| D2F-Q2 | Contradiction between chapters | 21 §7.4 and 22 §6.6 disagree on whether the hero's direction lock is released at event end. We follow chapter 22 |
| D2B-Q2 | Text vs. table contradiction | The condition for drawing a random number in the blindness check of attack magic differs between §12.10.1 and the table in §17. Affects the random sequence |
| M-Q5 | Spec vs. reference audio contradiction | Whether a fade writes the SSG volume while stopped or during a sound effect. Following the spec is off from the reference audio for 11 frames; not writing gives a 100% match |
| M-Q1 | Wrong reference value? | The SSG period 477 ≈ 131 Hz in §3.10.4 is ≈ 262 Hz (one octave higher) in the actual ymfm output and the reference audio |
| D2E-Q1 | Visible on screen | Whether the "." of the cancel item counts toward the choice window width (8-dot offset on fant1_8 map 4) |
| D1-Q7 | Message text | Whether the target's name is prefixed to 「は隠れている‥‥;」 ("…is hiding…") (chapter 12 §13.3/§13.4 are worded differently from §12.1) |

The rest concern abnormal values and boundary conditions that do not appear in the sample data, or details where several implementation choices are possible.


## Sound driver (chapter 02)

Source: `questions/music.md`

### M-Q1. SSG pitch (reference value in §3.10.4)
§3.10.4 states "with ymfm's YM2203 model and a 1/6 prescaler … SSG period 477 is calculated as ≈ 131 Hz", but
with the bundled ymfm (OPN_FIDELITY_MIN, prescaler 1/6 selected by a write to address 0x2D) the SSG engine runs at φM/16 = 249,600 Hz,
so period 477 is ≈ 262 Hz and O4 A (period 283) is 441 Hz (the same octave as FM O4 A = 440 Hz).
The spectral peaks match the reference audio exactly (SOUND.DAT @15 in reference_audio/come.wav and agasa.wav, @12 in kokoro.wav),
so we concluded that OpenDante98's actual output is also this (≈262 Hz) and use ymfm's behavior as is.
We would like to confirm whether the "≈131 Hz" in §3.10.4 is a calculation error in the text, or whether OpenDante98 uses a different setting.

### M-Q2. Power-on initial value of the vibrato direction (§3.2)
The spec says "the initial values at driver startup are all 0", but does not state whether a vibrato "direction" of 0 means up or down.
This only matters when `I` is set before key-on and a frequency write occurs while the wait is 0. Provisionally we use "up"
(PROVISIONAL: driver.h `vibUp = true`).

### M-Q3. Invalid characters in an SSGENV line (§2.8)
The implementation stops reading the line when it encounters a character that is neither a digit, a separator, nor end of line (no 0 is appended).
No sample contains such a line, so compatibility is not affected.

### M-Q4. Sign of numeric values (§2.3)
Values of 32768 or more without a minus sign (e.g. `N40000`) are also interpreted as 16-bit signed (N40000 → −25536 → magnitude 64, negative).
We would like to confirm whether "if there is a minus sign, negate, and interpret as 16-bit signed" means the signed interpretation also applies when there is no minus sign.
No sample uses such a value.

### M-Q5. SSG volume writes by a fade while stopped (no song ever played) or during a sound effect (§3.22)
- Reproduction: `opendt-headless samples/games/come/come -k "100 z 100 z 100 z 100 z 400" -n 900 -w come.wav` (same for agasa).
  At frame 209, immediately after the D1 engine requests sound effect 15, it starts a BGM switch (fade 0→15 every 20 ms → stop → BGM00).
  At this point no song has been played yet (Ver 2.00).
- Spec §3.22 says "SSG is always written, including disabled parts and stopped channels". Implemented this way, at the start of each frame
  registers 8 and 9 are written with values derived from the channel state (0 after the fix below), and the sound effect volume changes for the few chip samples
  until the sound effect rewrites them at the next audio-side VSYNC (1–4 chip samples later). The only differences from the reference audio are
  spikes in the first 1–2 output samples of frames 211, 213 and 215 (maximum difference 2502) and a ±1 tail from the DC cut (up to frame 221); 99.5% of the whole matches.
- Experiment: making the fade **write no registers at all** gave a 100% sample match with the reference audio for both come and agasa.
  So OpenDante98 apparently does not write the SSG volume registers in this situation (or the writes do not show up in the output).
  However, this data cannot distinguish which of the following rules applies:
  (a) Channels for which no song has ever been played (or that have never been initialized) are not written (like the FM rule "do not write if no voice has been loaded")
  (b) Nothing is written while stopped (playing flag off)
  (c) While a sound effect is playing, writes to SSG A/B are discarded even in Ver 2.00
- Currently the implementation follows the spec (always write). We would like to confirm which rule is correct.

### Note (fixed): Power-on initial values of the channel state
We applied §3.2's "the initial values at driver startup are all 0" also to the state that is reset at song start (volume, current soft-envelope value, octave,
default length, gate, Q, etc.). Previously the song-start values (volume 12, etc.) were held from startup, so in the situation above the fade
wrote 12 − step to registers 8 and 9, changing the sound effect amplitude (rms 5953 at frame 209 vs. 4591 in the reference), and after the sound effect ended
a tail caused by a DC step remained (up to frame 224). With power-on values of 0, the only difference left was the spikes in frames 211–221 described above.
We would like to confirm, just in case, whether the spec wording also covers the state that is reset at song start.

### M-Q6. Does the hardware envelope restart on restore at the end of a sound effect in Ver 2.21? (§4.6)
§4.6 gives the restore procedure as "frequency write → volume update (§3.9.2) → key-on if the key state is on (the soft/hard envelope starts over from the beginning)", but
the volume update in §3.9.2 clears the hardware-envelope-enabled flag, so the subsequent key-on does not write the hardware envelope (08=10, 0D=shape)
and the channel resumes at a fixed volume (only the soft envelope starts over). The implementation currently follows the procedure as written (the volume update clears the flag).
We would like to confirm whether the parenthetical statement that the "hard" envelope also restarts is wrong, or whether the order is different.

## D1 (chapters 10, 11, 12, 19)

Source: `questions/d1.md`

Points where the spec is ambiguous or undefined, found while implementing D1 (src/d1). Stopgap implementations are marked `PROVISIONAL(Qn)` in the code.

### D1-Q1. Event instructions 0x76 / 0x77 with an argument other than '0'–'7' (end of chapter 11 §8.2)
- Spec: "In OpenDante98 this is an out-of-range reference (undefined behavior) 【未確認】 (unverified)".
- Stopgap implementation: if the argument is out of range, skip the instruction without doing anything (advance by the total length 2). Does not occur in the samples.

### D1-Q2. Action bit 6 (steal MP) when the target is "none" (the attacking side is the enemy side) (chapter 12 §10.3 note, §12.12)
- Spec: the original's behavior is unknown (an out-of-range reference in OpenDante98).
- Stopgap implementation: the random numbers (2 fluctuation draws) are drawn, nobody's MP is reduced, and 「はＭＰを{量} p.奪われた！;」 ("…had {amount} MP stolen!") is output with the name as an empty string.

### D1-Q3. Fast key pressed during a key-press wait (chapter 11 §1.3)
- The spec only says "if the fast key is pressed, advance just 1 frame and finish immediately", and does not say whether the check happens only at the start of the wait or every frame while waiting.
- Implementation: the fast key is checked every frame while waiting; when it is pressed, advance 1 frame and finish.

### D1-Q4. Sound effect when the confirm key is pressed on an empty slot in hero selection (chapter 11 §3.6)
- In "Confirm (sound effect 15): if that slot is empty, ignore it and keep waiting for input", it is unclear whether sound effect 15 plays even for an empty slot.
- Implementation: sound effect 15 plays even for an empty slot.

### D1-Q5. When the system menu returns "to 1.2" after cancelling save or slot selection, is the debug key re-checked? (chapter 11 §9.9.1)
- The text reads as skipping step 1.1 (debug key check) and resuming from 1.2, but the implementation resumes from 1.1 (there is no difference in normal play without pressing the debug key).

### D1-Q6. In battle, `damage` / `showDamage` targeting an empty slot (character number 0xFF)
- On paths where the "entered target = 0" set right at battle start is used while still pointing to an empty slot (e.g. a skipped member using a stale command on their turn), the name is treated as an empty string and HP as 0. The spec does not state this explicitly.

### D1-Q7. Is the target's name prefixed to 「は隠れている‥‥;」 ("…is hiding…")? (chapter 12 §13.3 and §13.4)
- §12.1 and §13.1 write "target's name" + 「は隠れている‥‥;」, but §13.3 (when the fixed self-destruct damage hits a hiding enemy) and §13.4 (when the target is hiding during the magic-resistance check) say that only 「は隠れている‥‥;」 is output, which is written differently from the adjacent "target's name" + 「には効かなかった！#;」 ("…was not affected!"). Chapter 19 §7.3 only gives the general form 「{名前}は隠れている‥‥;」 ("{name} is hiding…").
- Is omitting the name the original's behavior (intentional wording), or an omission in the text?
- Implementation: the name is prefixed in both cases (src/d1/d1_battle.cpp `groupProc`, `effect`).

### D1-Q8. Does the key wait after 「魔法は使えません」 ("You cannot use magic") check the fast key? (chapter 11 §9.5.1 step 2, chapter 12 §6.4 step 2)
- Both chapters say "wait for key release, then wait until confirm, cancel or a direction key is pressed", and do not use the term "key-press wait" (キー押し待ち; ends after 1 frame with the fast key) defined in §1.3. It is unclear whether this is a dedicated wait that enumerates the keys to press, like the title image (§2.2) or ending (§12.1), or an ordinary key-press wait.
- Implementation: uses the ordinary key-press wait (exits after 1 frame with the fast key). There is no difference in normal play without pressing the fast key.

### About the observation tools (an environment report, not about the spec)
- When D1's RPG.EXE (come, earth) is launched with `MUSIC -R;RPG` via tools/dosbox/h98.py, the screen stays completely black (no visible reaction to xdotool keys either).
  MUSIC -R alone and RPG alone (which shows 「音楽ドライバーが登録されてません。」, "The music driver is not registered.") display normally. We were not able to observe the original D1 this time.

## D2 data and field (chapters 20 and 21)

Source: `questions/d2.md`

| No. | Chapter/section | Question | Provisional implementation |
|---|---|---|---|
| D2F-Q1 | 21 §5.6 | In "event value on the same layer as the hero" (主人公と同じ層のイベント値), "if the layer attribute of the chip underfoot (足元チップ) is 0x60 or…": is the "chip underfoot" the chip under the hero, or the chip at the queried position (the cell specified in view coordinates)? | The chip at the queried position (`PROVISIONAL(Q1)`) |
| D2F-Q2 | 21 §7.4 step 3 and 22 §6.6 | Chapter 21 says "set the move commands and direction locks of the hero and all events to 0"; chapter 22 says "the hero's display-direction lock is not cleared by the full release at event end". These contradict each other. | Following chapter 22, the hero's direction lock is not cleared |
| D2F-Q3 | 21 §2.4 / 22 §6.8 | Is the "1-VSYNC wait" of step 4 also performed when re-applying the color tone with speed −1 (set only)? (1 frame advances on map load, on load, and after battle.) | Always wait 1 VSYNC (as written) |
| D2F-Q4 | 20 §8.5 | Handling of a BKGnnn.EBD of at least 48 bytes but less than 41008 bytes (no image / missing part is 0). | Display with the missing part as 0 |
| D2F-Q5 | 20 §8.3 | Handling of an EGD of at least 50 bytes but smaller than the specified size. | Missing part is 0 |
| D2F-Q6 | 20 §7.3.1 | Handling of a map size N that is not one of the 9 levels (especially above 160). | Capped at 160 (does not occur in real data) |
| D2F-Q7 | 21 §5.5 | Handling of animation type 4 or higher. | Same as type 3 (4 frames, fast) |
| D2F-Q8 | 21 §9.4 / 22 0x46 | When a BGM name uses SJIS (e.g. 「戦闘」, "battle"), how to match it against the file name on the host (if converted to UTF-8). | Look for it as SJIS first, and if not found, look for the name converted to UTF-8 (following the recommendation in 01 §6.1) |
| D2F-Q9 | 01 §9.2 | Is "only once" for `--battle` once per program run, or once per new game/continue? | Once per program run |
| D2F-Q10 | 21 §3.3 | In the key wait after the title image (§1.3), the event-running flag is 0, so the STOP check is disabled; is that correct? | The STOP check is enabled only while an event is running |
| D2F-Q11 | 01 §9.2 | D2's `--gold`/`--item`/`--warp` are "applied only when starting a new game", but unlike `--battle` they are not described as "only once". Are they applied every time a new game is started again after returning to the title, or only once per program run? | Only once per program run (`debugApplied`) |
| D2F-Q12 | 20 §7.3 | For a map file of at least 0x2C bytes but truncated before the version (right after the area table), what is the "initial value" of the map version (0, or, as for the default map, the previous map's value)? The listed initial values (grid 0, area table 0xFF, position table 0xFFFF, event area 0) do not include the version. Does not occur in real data. | 0 |

## D2 events (chapter 22)

Source: `questions/d2-events.md`

### D2E-Q1. Choice window width and the "." of the cancel item (§8 0x09 step 4)
The display width w is defined as "the byte count of each item's string", but it is not stated whether the leading "." of the cancel item is counted.
In the nested choice of event 0 in fant1_8 MAP004 (real data), the cancel item 「.前のメニューに戻る」 (".Back to the previous menu", 19 bytes) is the longest;
counting the "." gives x = 0x200 − 152 = 360, not counting it gives 368.
Provisionally the width is computed from the stored byte count including the "." (`PROVISIONAL(Q1)`, events.cpp `Interp::choice`).

### D2E-Q2. Meaning of "field redraw request" (フィールド再描画要求) (0x11, 0x12, 0x1C, 0x1D, 0x4C, 0x52, 0x53)
It is not stated whether a full redraw happens immediately, or whether it waits for the next drawing opportunity (tick, hero update, closing a window, etc.).
Provisionally "nothing is drawn on the spot" (drawing happens in the main loop after the hero update or the end-of-event processing).
0x0A (erase event) does not mention a redraw either, so it is likewise treated as disappearing at the next draw.

### D2E-Q3. Hero specifiers 0x0C/0x0D in conditional branches and invalid party slot values
The handling of a party slot value of 12 or more (does not normally happen) is not described. Provisionally such slots are skipped
(`PROVISIONAL(Q3)`).

### D2E-Q4. Remaining bytes of the BGM field of warp points (0x2D/0x2E)
The BGM field of each point is 12 bytes (chapter 20), but 0x2D only says "the current BGM name (8 characters)".
Whether bytes 9–12 are kept or cleared when updating an existing point, and whether the last point is cleared or kept after 0x2E compacts the list,
affect byte-exact save files. Provisionally: on update only the first 8 bytes are replaced; on a new addition the 16 bytes are zeroed before writing;
on deletion the last entry after compaction is cleared with 0 (`PROVISIONAL(Q4)`).

### D2E-Q5. Layer of an event moved by 0x50
0x50 only sets the event's X and Y. It is not stated whether the event's layer (surface/underground) is changed to match the layer attribute
of the destination chip. Provisionally it is not changed (the event is placed on its original layer).

### D2E-Q6. 0x50 step 2-2 "if there is an event in the underground placement at the destination"
Is it correct to do nothing when the underground placement value is 0xFD (the erased marker) or 0xFF? Provisionally the event is temporarily erased "only when the value is 0–249
and the event exists".

### D2E-Q7. Palette when the EBD file for picture 0x3A is short or missing
The spec says "if missing, the screen stays black (the palette is not changed)", but applying T = 100 in step 5 displays black (color 0)
using the previous map's palette. On a map where color 0 is not black, the result is a solid non-black screen; is that acceptable?
Provisionally as written (the palette is not changed).

### D2E-Q8. Type of key wait in the game-over check during an event (§5.1 step 2-3)
§5.1 only says "key wait (§6.3)", so it is unclear whether it means §6.3's "key wait with wait marker" (待ち記号つきキー待ち; draws ▼ at (312,352)) or the
"key release → press" wait (キー離し→押し; no marker) (other places, e.g. 0x01 step 6 and 0x3A step 6, state it explicitly).
This determines whether ▼ appears on screen. Provisionally the wait with the marker is used (`PROVISIONAL(Q8)`, events.cpp `Interp::gameOverCheck`).

## D2 menus (chapter 23)

Source: `questions/d2-menus.md`

### D2M-Q1. STOP after the "event running" (イベント実行中) flag is set inside a menu (switch-type in §9.1 / §9.2)
Switch-type items and magic perform a "key release → press" (キー離し→押し) wait after setting the event-running flag to 1. The STOP check of this wait
(22 §6.3) looks at the event-running flag, so pressing Ctrl+Cancel during a test play can trigger an abort, but
the handling of an abort inside a menu is not described.
Provisionally: `fieldMenu` catches the abort, sets the flag to 0 and closes the menu (menus.cpp `PROVISIONAL(Q1)`).

### D2M-Q2. ROLL UP / ROLL DOWN in the variable editor (§10.3)
Chapter 01's input abstraction (8 booleans) and key buffer (6 kinds of keystrokes) have no ROLL UP / ROLL DOWN,
so value changes cannot be reached in practice (only the handling of keystroke codes 0x36/0x37 is implemented).
May we add PageUp/PageDown to the input abstraction (this would be a change to chapter 01)?

### D2M-Q3. "Empty" in the warp destination list (§2.3)
The spec says "empty check = map number 0xFFFF", but does not say whether the slots after the registered count (state block 0x081) are always 0xFFFF
(the initial values in PLAYER.DAT, and the value of the last slot after 0x2E deletes and compacts).
Provisionally: only the slots before the registered count are listed, and among them a map number of 0xFFFF is treated as empty.

### D2M-Q4. "Previous position" in the debug menu (§10.1)
It is unclear whether the scope of "the position is remembered within the debug menu" is a single display of the debug menu or the whole
program run. Provisionally: starts from 0 each time it is entered from the system menu, and returns to the previous position when coming back from a viewer.

### D2M-Q5. Key buffer when a two-column list opens (§2.3)
The generic selection (22 §6.4) clears the key buffer when it starts, but the two-column list has no such description. Provisionally: not cleared.
(Review note: the in-battle list (battle.cpp `listSelect`, which uses 23 §2.3 via 24 §8.2) clears it on open,
so the field and battle behave differently. We would like to confirm which is correct.)

### D2M-Q6. Cursor on cancel in a two-column list (§2.3)
The spec says the cursor is erased on cancel only for the equipment candidates. For items, magic and warp destinations the implementation does not
erase the cursor on cancel (the menu screen is redrawn right afterwards, so there is almost no visible effect).

### D2M-Q7. Final key wait in reordering (§7 step 5)
It is not stated whether the key buffer is cleared, or a STOP check is performed, before "wait until one key arrives from the key buffer".
Provisionally: not cleared; a STOP check is done every VSYNC, then one keystroke is taken.

## D2 battle (chapter 24)

Source: `questions/d2-battle.md`

Implementation: `src/d2/battle.cpp` (flow, screen, actions), `src/d2/battle_calc.cpp` (formulas, initialization), `src/d2/battle.h`.
Provisional implementations are marked `PROVISIONAL(Qn)` in the code.

### D2B-Q1. STOP during battle (Ctrl+Cancel in test play)
In a battle started by an event's battle command, the "event running" flag in the state block is set. The generic selection and
key waits of chapter 23 perform a STOP check, but chapter 24 says nothing about STOP during battle. Does STOP abort the event in the middle of a battle
(exiting without the battle cleanup), or is STOP not checked during battle?
Current: cursor selection, lists and key waits within battle do not check STOP (battle-specific implementations).

### D2B-Q2. Condition for drawing a random number in the blindness check of attack magic (§12.10.1 step 3 vs. the table in §17)
The body text says "if random % 100 ≥ success rate, it fails. If the caster is blind, draw one more random number…", while the table in §17 says "success 1, +1 if blind".
If the success-rate check has already failed, is the blindness random number still drawn (the table's reading) or not (short-circuit reading)?
Current: if blind, the second number is always drawn (PROVISIONAL(Q2)).

### D2B-Q3. Step 15 (checks) after cutting off in §11.3 step 14
When step 14 cuts off the remaining repetitions (sleep, paralysis or fainting after the action; action interrupted), are the step 15 checks for party wipe / all enemies defeated
performed before exiting, or does it move on to the next slot without checking? This makes a difference for the last combatant in the action order (without the check,
even with all enemies defeated, victory comes only after the end-of-round poison damage is applied).
Current: even when cutting off, the step 15 checks are performed first.

### D2B-Q4. Drawing when the transformation target's size class and the EGD size disagree (【未確認】 (unverified) in §12.3)
When the combatant's size class (record 0x46) differs from the size of the loaded EGD (0x30), which dimensions are used to interpret and draw
the image data?
Current: normal/white display is drawn with the EGD's own dimensions; the erase area uses the combatant's size class; the fill after stepping forward is determined by the EGD values.

### D2B-Q5. Negative magic damage
With a large variance value (around p > 24), the result of the variance function can be negative. §12.2 only handles
"when the damage is 0"; negative damage satisfies "damage < target's HP", so HP increases. Does the original do this too? MP damage (§12.4) is
the same (only "if value = 0, do nothing").
Current: as written (negative values are applied as is).

### D2B-Q6. List page/cursor memory (during battle)
§4.1 step 2 resets the memory of the item list and every character's magic list to 0 at battle start. Is the memory kept across turns
during battle (using the memory of 23 §2.3 as is), or does the list start from the top every time (the parenthetical in §4.1, "lists during battle always
start from the top")?
Current: reset to 0 only at battle start, and remembered across turns during battle.

### D2B-Q7. ESC check for cancelling auto battle
In §7 step 2, "wait 1 unit, and if ESC is pressed at that point", does it look at the pressed state at that instant (held down),
or at a keystroke in the key buffer?
Current: looks at the held-down state (the key buffer is not used).

### D2B-Q8. Strings of external characters and symbols during battle
The 「Ｌ」 (full-width L) in `Ｌ%2d` was taken to be SJIS 0x826B. The HP/MP fields are drawn half-width as `%4d/%4d%5d`.
Both match the example in §6.3 (` 348/ 348   32`), but we would like to confirm half-width vs. full-width in the original.
(We have already confirmed, by capturing the original battle screen in DOSBox-X, that the layout and column positions match.)

### D2B-Q9. Party record numbers above 11
If the party order in the state block contains a value of 12 or more (data corruption), that slot is treated as empty (but counted in the party size).
