Translated from the Japanese original supplied by the specification team.

# Answers to the Specification Questions (spec team → clean-room team)

These are the answers to all 50 questions and the 1 supplementary item in spec-questions.md. Each answer comes from checking the behavior of the existing reference implementation. The 4 sound-driver questions were also backed by register-write logs from the verification harness or by measurements with ymfm on its own. The specification has been updated to v1.1. Changed passages carry "(fixed in v1.1)" or "(added in v1.1)" at the end of the sentence.

## Summary: items that require implementation changes on the clean-room side

| ID | Change | Impact |
|---|---|---|
| M-Q5 | Until playback has been started at least once after startup, setting the fade only records the fade level and writes nothing to FM or SSG (rule (a)). Once playback has been started at least once, it writes every time, even while stopped or during a sound effect (both 2.00 and 2.21) | The come and agasa reference audio is expected to match 100% |
| M-Q4 | Values of 32768 or more without a minus sign are also interpreted as 16-bit signed. However, the detune magnitude for N40000 is 192, not 64 (40000 becomes −25536, and 192 is the low 8 bits of its absolute value 25536), with the negative flag set | No example in the samples |
| D1-Q3 | A key-press wait checks the fast key only once, when the wait starts. It does not check it while waiting | Timing |
| D1-Q8 | The wait after 「魔法は使えません」 ("You cannot use magic") does not check the fast key (it exits only on confirm, cancel, or a direction key) | Timing |
| D2F-Q4 | If the BKG is shorter than 41008 bytes, treat it as no image (draw nothing and do not change the palette) | Malformed data only |
| D2F-Q11 | `--gold` `--item` `--warp` are applied on every New Game (not on Continue). Only `--battle` is "once only" | Test harness |
| D2F-Q12 | For a map file truncated before the version field, do not overwrite the version (it keeps the previous map's value) | Malformed data only |
| D2E-Q1 | When measuring the width of a choice window, every item that starts with "." is measured with the "." removed. In the fant1_8 map 4 example, x = 368 | **Visible on screen** |
| D2E-Q4 | When adding a new warp point, do not zero-fill the 16 bytes (bytes 9–12 keep their previous values). After deleting and compacting, do not clear the trailing slot either | Byte-exact saves |
| D2M-Q5 | Flush the key buffer when opening a two-column list (on the field side as well) | Input |
| D2M-Q7 | The final wait of the reorder screen neither flushes the key buffer nor performs the STOP check. It repeats "look at the buffer → if empty, wait 1 VSYNC" in that order | Minor |
| D2B-Q4 | Load the EGD using the EGD's own dimensions, and draw it using the dimensions of the combatant's size class (missing parts are transparent) | No example in the samples |
| D2M-Q2 | (Optional) ROLL UP / ROLL DOWN may be added to input. Recommended mapping: PageUp = +1, PageDown = −1 | Debug only |

## Conclusions for the 6 priority questions

| ID | Conclusion |
|---|---|
| D2F-Q2 | Chapter 22 is correct; chapter 21 §7.4 was wrong (fixed). The hero's display-facing lock is not cleared when an event ends. It is cleared only when movement command 2 is set |
| D2B-Q2 | The reading of the §17 table is correct. If the caster is blinded, the second random draw (% 10 < attack coefficient) is always made, even if the success-rate check has already failed. The current implementation is correct |
| M-Q5 | See the table above (rule (a)) |
| M-Q1 | Spec error. ≈262 Hz (φM / (32 × period)) is correct. FM and SSG sound in the same octave |
| D2E-Q1 | The "." is not counted (x = 368). See the table above |
| D1-Q7 | Prefixing the name is correct. Chapter 12 §13.3 and §13.4 had omitted it (fixed) |

---

# Answers on the sound driver (chapter 02) (owner: music)

Scope: spec-questions.md, section 「音源ドライバ（02 章）」 ("Sound driver (chapter 02)") M-Q1 to M-Q6, plus 「補足（修正済み）: チャンネル状態の起動時初期値」 ("Supplement (fixed): startup initial values of channel state").
Verification: checked the reference implementation's behavior, register-write logs from a standalone driver verification harness, SSG frequency measurements with ymfm on its own,
and regenerating the come reference audio with the reference implementation and confirming a byte-exact match (reference_audio/come.wav is exactly the output of the current reference implementation).

### M-Q1. SSG pitch (reference values in §3.10.4)

**Answer**: Spec error. The clean-room team's observation (≈262 Hz) is correct. Immediately after creating ymfm's YM2203 at the lowest fidelity, the reference implementation
performs only an address write of 0x2D (no data write) to select the 1/6 prescaler. It makes no other divider settings. With ymfm configured this way,
the SSG's internal counter advances at φM/16 = 249,600 Hz (3 ticks per 2 samples of the 166,400 Hz output), and the tone output toggles
each time it has counted the period value. Hence SSG frequency = φM / (32 × period). Measured with ymfm on its own: period 477 → 261.5 Hz, 283 → 441.0 Hz.
FM and SSG given the same octave sound at the same pitch. The v1.0 value "≈131 Hz" was computed with the datasheet formula φM/(64×period) and
is one octave off from ymfm's output. Which of the two real hardware produces is 【未確認】 (unconfirmed), but this specification treats the reference implementation's (ymfm's) output as authoritative.

**Spec change**: Corrected the parenthetical reference note in chapter 02 §3.10.4 (the SSG counting clock and frequency formula, 477 ≈ 261.6 Hz, O4A ≈ 441 Hz).
Also rewrote 「FM と SSG の音程関係」 ("pitch relationship between FM and SSG") under 「未確認事項・注意」 ("Unconfirmed items and notes"). Added acceptance-test item 19.

**Clean-room provisional implementation**: Confirmed (use ymfm's behavior as-is).

### M-Q2. Startup initial value of the vibrato direction (§3.2)

**Answer**: "Up" is correct. The startup direction is the same as the value set on key-on (up), i.e., adding to the F-Number for FM,
and subtracting from the period for SSG.

**Spec change**: Added "the direction's value at driver startup is up" to chapter 02 §3.2 and §3.12.

**Clean-room provisional implementation**: Confirmed.

### M-Q3. Invalid characters in an SSGENV line (§2.8)

**Answer**: Undefined in the reference implementation as well. At the invalid character the read position stops advancing and it keeps appending 0 indefinitely (effectively a hang).
The recommendation is the clean-room provisional implementation: "stop reading the line at that position. Do not append a value at the invalid character (only the values read up to that point remain)."
No sample contains such a line.

**Spec change**: Added to the note in chapter 02 §2.8, as a recommendation, that no value is appended when reading stops (with an example).

**Clean-room provisional implementation**: Confirmed.

### M-Q4. Sign of numeric values (§2.3)

**Answer**: Correct. Regardless of whether there is a minus sign, the value is finally reinterpreted as 16-bit signed. However, the clean-room team's example
"N40000 → magnitude 64, negative" is wrong. 40000 becomes −25536, and the detune magnitude is the low 8 bits of its absolute value 25536, so it is
**192** (negative flag 1). 64 is the low 8 bits of 40000 itself, i.e., the value truncated before sign inversion.
In verification, `1:N40000 A` gives F-Number = 1040 − floor(59×192/256) = 996 (A4=23, A0=E4). `N-40000` is +25536, giving
magnitude 192, positive (1040 + floor(62×192/256) = 1086). Commands with range checks (O, @, SOUND:@n, SSGENV n) also check against this signed value
(e.g., O40000 is negative and therefore an octave-out-of-range error). Commands that use only the low 8 bits are unaffected.

**Spec change**: Added to chapter 02 §2.3 step 5 that the value is interpreted as signed even without a minus sign, with examples (N40000, N-40000, N32768) and the effect on range checks.
Added acceptance-test item 21.

**Clean-room provisional implementation**: Change required. The detune magnitude must be "the low 8 bits of the absolute value of the signed value" (N40000 → 192, negative).
Interpreting the value as signed is itself fine as-is.

### M-Q5. SSG volume writes caused by fades while stopped or while a sound effect plays (§3.22)

**Answer**: (a) is correct. Precisely: "**After the player starts, and until playback has been started for the first time, setting the fade level only records
the fade level and writes no register at all, neither FM nor SSG**." Both (b) (no writes while stopped) and (c) (discarded during a sound effect even in Ver 2.00) are wrong.

- Reason: right after startup the channels are uninitialized until the first playback start, and all 7 channels are treated like "an FM channel
  with no voice loaded". The volume update therefore writes nothing on any channel. A channel is treated as an SSG channel only after it has been
  initialized by a playback start.
- Register-write logs (standalone driver harness, come's SOUND.DAT and BGM02, fade levels 0/5/10/15):
  - Never played, no sound effect → no writes (both Ver 2.00 and 2.21)
  - Never played, sound effect 15 playing → no writes (both versions)
  - Playing → writes FM carrier TL (0x4C, 0x48, 0x4D, 0x4E) and registers 8, 9, 10 every time
  - Stopped after having played once → writes, same as while playing
  - Stopped after having played once, sound effect 15 playing → also writes (no suppression in either Ver 2.00 or 2.21)
- In the come situation (frame 209, Ver 2.00, no song played yet), the 16 fade settings write no registers at all.
  This agrees with the clean-room team's experimental result that "writing nothing gives a 100% match".
- When the uninitialized state persists: merely loading a song does not initialize. The state also stays uninitialized when D1 did not play because the BGM file was missing,
  and when in D2 the file was missing after a stop. Once playback has been started even once, stopping afterwards does not return to the uninitialized state.

**Spec change**: Added to chapter 02 §3.22 step 2 that nothing is written while uninitialized, that writes occur while stopped and during sound effects after having played once, and the come example.
Added a paragraph on the "uninitialized state right after startup" to §3.2. Also annotated the side-effects column of the table in §5.1. Added acceptance-test item 18.

**Clean-room provisional implementation**: Change required. Add: "if playback has not been started even once since startup, only record the fade level and write nothing."
Everything else (after playback has started, always write, even while stopped or during a sound effect) can stay as currently implemented.

### Supplement: startup initial values of channel state

**Answer**: "Initial values at driver startup are all 0" is a rule about **state that is not reset at song start** (hardware envelope, soft-envelope period and
read position, tremolo/vibrato counters and directions, portamento, soft-LFO accumulators, etc.). The startup values of state that is reset at song start
(volume, current soft-envelope value, octave, default length, gate, Q, etc.) are not observable in the reference implementation.
The only path through which they could be observed before the first playback start is the fade, and per M-Q5 nothing is written there. Therefore, once the M-Q5 rule
(fades write nothing while uninitialized) is in place, it makes no difference whether these startup values are 0 or the song-start values.
The differences the clean-room team observed (different rms, spikes at frames 211–221) were caused not by the startup values but by writes from fades
in the uninitialized state.

**Spec change**: Added to chapter 02 §3.2 (the scope of "all 0", the uninitialized state, and that these values become meaningful from the first playback start).

**Clean-room provisional implementation**: The change that made startup values 0 can stay (it is harmless). It is not sufficient on its own, however; the M-Q5 change is required.

### M-Q6. Does the hardware envelope restart on restoration at the end of a sound effect in Ver 2.21? (§4.6)

**Answer**: The parenthetical in the spec is wrong. The clean-room implementation (following the steps; the volume update clears the hardware-envelope-enabled flag) is correct.
The restoration steps are "frequency write → volume update (writes the fixed volume and clears the hardware-envelope-enabled flag) → key-on if the key state is on",
and this key-on does not restart the hardware envelope (volume 0x10 and shape register 0x0D are not written). The only thing that restarts from the beginning is
the soft envelope, if it is enabled. From then on, the channel keeps sounding at a fixed volume until the next S/M command. In verification with Ver 2.21,
playing agasa's sound effect 11 during playback of `4:S8 M500 L1 C D` produced "07=BF 00=DD 01=01 08=0C 07=BE 02=DD 03=01 09=0C" on the ending VSYNC,
and neither 08=10 nor 0D was written.

**Spec change**: Changed the parenthetical in chapter 02 §4.6 to "if the soft envelope is enabled, it restarts from the beginning. The hardware envelope is not restarted",
and added a note explaining why. Added acceptance-test item 20.

**Clean-room provisional implementation**: Confirmed.

---

# Answers to D1 questions (chapters 10, 11, 12, 19) (owner: d1)

### D1-Q1. When the argument of event instruction 0x76 / 0x77 is not '0'–'7'
**Answer**: Undefined in the reference implementation as well. For '8' or above, the movement direction falls outside 0–3 and causes an out-of-range access. Below '0', a value other than 0–3 is stored as the facing; on screen it appears as the direction "k mod 4", but for the hero a subsequent "examine" or screen transition causes an out-of-range access. Recommendation: "do nothing (do not close the message window either) and just advance by the total length of 2". When 0x76 meets the conditions "slot number ≥ number of objects" or "graphic ≥ 0x40 with motion type 0", it does nothing anyway, so the result is the same.
**Spec change**: Added the k < 0 case and the recommended handling to the end of the paragraph 「上記以外の命令コード」 ("other instruction codes") in chapter 11 §8.2.
**Clean-room provisional implementation**: Confirmed.

### D1-Q2. Action bit 6 (steal MP) when the target is "none"
**Answer**: Undefined in the reference implementation as well (out-of-range access). The recommendation is the clean-room provisional implementation: draw the two random numbers of 乱数幅付け(10) ("random spread(10)"), reduce nobody's MP, use an empty string for the name, and output 「はＭＰを{量} p.奪われた！;」 ("…had {amount} MP stolen!"). In addition, a status update (redrawing the status window) is performed afterwards. It makes no visible difference.
**Spec change**: Added the recommended handling to the note in chapter 12 §10.3, §12.12 step 4, and 「未確認事項・注意」 ("Unconfirmed items and notes"). Also annotated the corresponding row in chapter 19 §7.2.
**Clean-room provisional implementation**: Confirmed (if the status window is not being redrawn, the redraw may be added, but it makes no difference on screen).

### D1-Q3. When the fast key is pressed during a key-press wait
**Answer**: The fast key is checked **only once, when the wait starts**. If it is held at the start, the wait advances 1 frame and ends. If not, the wait enters a loop waiting for confirm, cancel, or a direction key, and that loop does not look at the fast key. Pressing the fast key midway does not end the wait.
**Spec change**: Added to chapter 11 §1.3 「キー押し待ち」 ("key-press wait"). Added the same point to §7.4 (fast key).
**Clean-room provisional implementation**: Change required. Do not look at the fast key during the wait; check it only once, at the start.

### D1-Q4. Sound effect when confirm is pressed on an empty slot in hero selection
**Answer**: It plays. Sound effect 15 for the confirm key plays before checking whether the slot is empty. If the slot is empty, the press is simply ignored and, after a key-release wait, input waiting continues.
**Spec change**: Added to chapter 11 §3.6 step 2.4 "Confirm".
**Clean-room provisional implementation**: Confirmed.

### D1-Q5. In the system menu, is the debug key re-checked after saving or cancelling slot selection?
**Answer**: Yes. Both after saving and after cancelling slot selection, processing restarts from the beginning of the system menu (the debug-key check). If the debug key is held at that point, the "game progress monitor" appears. The spec's "go to 1.2" is wrong; it should be "go to 1.1".
**Spec change**: Corrected "go to 1.2" to "go to 1.1" in chapter 11 §9.9.1 steps 6.2 and 6.3.
**Clean-room provisional implementation**: Confirmed (an implementation that resumes from 1.1 is correct).

### D1-Q6. When the target of in-battle damage processing or damage display is an empty slot (character number 0xFF)
**Answer**: The reference implementation handles it mostly like the provisional implementation: the name is an empty string, the target is treated as not alive, and its status reads as 0. Note, however, the following differences from "a member with 0 HP":
- HP increases/decreases change nothing, and "fell" is returned regardless of the sign of d. For an attack spell this yields 「{空}は{量} p.のダメージ！#」 ("{empty} takes {amount} points of damage!") → screen shake → 「{空}は気絶してしまった‥‥」 ("{empty} has fainted..."), which looks the same as 0 HP.
- Setting and clearing status bits are discarded. The result values of spells 8–12 are as in the table; spell 11 gives 9 (「混乱してしまった」, "became confused"). Spell 7 fails.
- Stat changes change nothing, but are displayed as in the table.
- Revival (spell 6) fails (a member with 0 HP would be revived).
- Instant death draws one random number and, on success, shows the defeat display without changing anything.
- MP does not increase or decrease.
- If the target of normal-attack damage calculation is an empty slot, reading defense and luck causes an out-of-range access in the reference implementation as well. However, a target that is not alive is re-selected before the normal attack, so this does not happen on normal paths.

The most likely case is when slot 0 is empty and "magic #0, target 0" is left over. In that case, the target check prints 「は気絶している‥‥」 ("...is unconscious...", with no name), the action is invalidated, and only the MP is consumed.
**Spec change**: Added "when an empty slot becomes the current target" to chapter 12 §1.2.
**Clean-room provisional implementation**: Confirmed. However, verify that two points hold: revival does not bring the target back, and status and MP are not overwritten. If not, change it as described above.

### D1-Q7. Is the target's name prefixed to 「は隠れている‥‥;」 ("...is hiding...")?
**Answer**: Yes. In the reference implementation (based on analysis of the original), the "hiding" check and its output are a single shared routine, which always outputs the target's name before 「は隠れている‥‥;」. The wording in chapter 12 §13.3 (overall self-destruct processing) and §13.4 (magic resistance check) was an abbreviation and is a spec error.
**Spec change**: Corrected chapter 12 §13.3 and §13.4 to "target's name" + 「は隠れている‥‥;」. Also added "always prefixed with the name" to the general description in §12.8. Annotated the corresponding row in chapter 19 §7.3.
**Clean-room provisional implementation**: Confirmed.

### D1-Q8. Does the key wait after 「魔法は使えません」 ("You cannot use magic") look at the fast key?
**Answer**: No. It is not the §1.3 「キー押し待ち」 (key-press wait) but a dedicated wait: after a key-release wait, it waits until confirm, cancel, or a direction key is pressed. Holding the fast key does not exit it. The same applies to both the field (chapter 11 §9.5.1) and battle (chapter 12 §6.4).
**Spec change**: Added to chapter 11 §1.3: "waits that enumerate specific keys and are not described as 『キー押し待ち』 do not look at the fast key." Also made this explicit in chapter 11 §9.5.1 step 2 and chapter 12 §6.4 step 2. Corrected 「キー押しで戻る」 ("returns on a key press") in chapter 19 §5.8.
**Clean-room provisional implementation**: Change required. Make these two places waits that do not look at the fast key (exit only on confirm, cancel, or a direction key).

### A note on the observation tool
This is outside the reference implementation's behavior, so it is a guess. In the original DOS, ";" is not a command separator, so writing `MUSIC -R;RPG` on one line may not run as two commands as intended. Running MUSIC -R and RPG in sequence on separate lines (in a batch file) is a better way to check. Also, D1's RPG.EXE uses the driver's interrupt, so check that the emulator has the FM sound board (PC-9801-26K equivalent) enabled.

---

# Answers: D2 data and field (D2F-Q1 to Q12) / D2 events (D2E-Q1 to Q8)

Owner: d2fe. Basis: the reference implementation's behavior (source inspection, measured with the reference implementation where necessary).

---

## D2 data and field (chapters 20, 21)

### D2F-Q1. Which chip is the "chip underfoot" in "event value on the same layer as the hero"?
**Answer**: The chip at the queried position is correct, as in the clean-room provisional implementation. It looks at the chip at the map coordinates obtained by taking the given view coordinates modulo N, and if that chip's layer attribute is 0x60 or the hero's layer is nonzero, it looks up the lower-layer map. It does not look at the chip under the hero's feet (when querying the tile in front, it looks at the chip in front).
**Spec change**: Made explicit in chapter 21 §5.6 「主人公と同じ層のイベント値」 ("event value on the same layer as the hero") that 「足元チップ」 ("the chip underfoot") means the chip at the queried position (fixed in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2F-Q2. Is the hero's facing lock released when an event ends? (contradiction between 21 §7.4 and 22 §6.6)
**Answer**: Chapter 22 is correct; chapter 21 §7.4 step 3 is wrong. The 「全移動命令の解除」 ("clear all movement commands") step of end-of-event processing zeroes three things: the hero's movement command, every event's movement command, and every event's facing lock. **The hero's display-facing lock (state block 0x00F) is not cleared.**
- The hero's facing lock is cleared only when movement command 2 is set. It is not cleared by the clear-all of 0x10, 0x4A, or 0x59, and the "set command value 0" at the end of 0x4A does not change it either.
- If an event ends with the lock still set, the hero continues to be drawn with the locked facing during normal walking afterwards.
- The value is part of the state block, so it is preserved by save and load. In the title loop it returns to the initial value in PLAYER.DAT (0 in all 23 samples).
**Spec change**: Corrected chapter 21 §7.4 step 3 to "set the hero's movement command to 0, and all events' movement commands and facing locks to 0. The hero's display-facing lock is not cleared", and added how it is saved and initialized (fixed in v1.1). Added to 22 §6.6 that 0x10, 0x4A, and 0x59 do not clear it either (added in v1.1).
**Clean-room provisional implementation**: Confirmed (do not clear the hero's facing lock).

### D2F-Q3. Does re-applying the color tone with speed −1 (set only) also wait 1 VSYNC?
**Answer**: Yes. Step 4's "wait 1 VSYNC and set the wait counter to 0" is always performed regardless of speed. Therefore re-application on map load, on load, after battle, and after a picture each advances 1 frame.
**Spec change**: Added "always, regardless of s" to chapter 21 §2.4 step 4 and 22 §6.8 step 4 (added in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2F-Q4. When BKGnnn.EBD is at least 48 bytes but less than 41008 bytes
**Answer**: Treat it as "no image". Below 41008 bytes (including a missing file), nothing is drawn and the palette is not changed. That area keeps color 14, as painted by the battle-start effect. It is not drawn with the missing part treated as 0.
**Spec change**: Added to chapter 20 §8.5 (added in v1.1).
**Clean-room provisional implementation**: Change required. If shorter than 41008 bytes, do not draw the background and do not set the palette.

### D2F-Q5. When an EGD is at least 50 bytes but smaller than the specified size
**Answer**: The missing part is treated as 0. The palette and the size field (0x30) are used. Each of the five areas (the mask and the 4 planes) is initialized to 0, and then only what exists in the file is copied.
**Spec change**: Added to chapter 20 §8.3 (added in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2F-Q6. When the map size N is not one of the 9 steps
**Answer**: Any value from 1 to 160 works as-is with no special handling, even if it is not one of the steps. N is used for the row length and for the area-table position 0x2C+N², and positions at or beyond N become the outer-border chip.
- When N exceeds 160: the reference implementation writes outside its 160×160 array, so this is undefined.
- When N = 0: the modulo divisor becomes 0, so this is undefined.

Recommendation: "compute file offsets with the raw N, and cap the in-memory size (range checks, modulo, drawing) at 160". For N = 0, any handling that does not crash (such as treating it as 1) is acceptable. Real data contains no such case.
**Spec change**: Added a note below the header table in chapter 20 §7.3.1 (added in v1.1).
**Clean-room provisional implementation**: Confirmed (cap at 160). However, verify that the read offset of the area table and what follows is 0x2C+N×N using the raw N.

### D2F-Q7. Handling of animation type 4 and above
**Answer**: Same as type 3 (4 frames, fast). All types 2 and above are 4-frame types; of these, only type 2 uses the slow cycle (free-running counter ÷ 16), and the others use the fast cycle (÷ 8).
**Spec change**: Changed chapter 21 §5.5 step 2 to "type 2 or above" and added that 4 and above are the same as 3 (added in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2F-Q8. Matching an SJIS BGM name against host file names (converted to UTF-8)
**Answer**: The reference implementation uses the SJIS byte sequence as-is in the path. It compares against directory entry names on disk (UTF-8) after uppercasing ASCII letters only, so files that have been converted to UTF-8 are **not found and play silence**. Example measured with the reference implementation: in fant1_8, moving to MAP026 and fighting a battle, the battle BGM 「戦闘」 ("Battle") gives `missing DAT_BGM/戦闘.MML` (SJIS byte sequence), and nothing plays even though `DAT_BGM/戦闘.MML` (UTF-8 name) exists.
In the original (DOS) it plays, so the clean-room approach of "search with the SJIS name, and if not found, search with the name converted to UTF-8" is accepted as an improvement in line with the recommendation in 01 §6.1. However, the audio in that scene will then no longer match the reference implementation's reference output (silence).
**Spec change**: Added to chapter 21 §9.4 the reference implementation's behavior, the verification example, the allowance for the improvement, and the difference from the reference output. Also added a cross-reference from 0x46 in chapter 22 (both added in v1.1).
**Clean-room provisional implementation**: Confirmed (as an improvement). Take this difference into account when comparing against the reference output.

### D2F-Q9. Scope of "once only" for `--battle`
**Answer**: Once per program run. The battle occurs the first time the title menu is exited (whether New Game or Continue), and the option is disabled thereafter. Returning to the title and starting again does not trigger the battle again.
**Spec change**: Added to the application timing of the debug start options in chapter 01 §9.2 (added in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2F-Q10. Is the STOP check disabled in the key wait after the title image?
**Answer**: Yes, it may be disabled. The STOP check requires both the event-running flag and test-play mode, and neither holds here. In fact, the key wait for the title image is only performed in normal mode.
**Spec change**: Added a sentence to chapter 21 §3.3 (added in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2F-Q11. Are D2's `--gold`/`--item`/`--warp` applied on every New Game?
**Answer**: They are applied **every time** a New Game starts, not once per program run. Unlike `--battle`, these are not disabled; returning to the title and starting a New Game again sets the gold, adds the items, and performs the move once more. They are not applied on Continue.
Order of application: gold → items (in the order given) → move (set the map number and coordinates and redo the map-entry processing; the BGM is not restarted).
**Spec change**: Added to chapter 01 §9.2 (added in v1.1).
**Clean-room provisional implementation**: Change required. Do not use the "once only" marker (`debugApplied`) for these three; apply them on every New Game. Use the marker only for `--battle`.

### D2F-Q12. Map version of a map that is at least 0x2C bytes but truncated before the version field
**Answer**: There is no initial value. "Keeps the previous value" is correct; it is not 0.
- Reading is byte by byte. Of the 2 version bytes, only those present in the file are overwritten (if only 1 byte is present, only the low byte).
- Bytes not present in the file keep the version of the previously loaded map (0 if no map has been loaded since startup). This is the same rule as for the default map (shorter than 0x2C).
- The event area size is initialized to 0 before reading.

This affects the version comparison on load and the value written to saves, but real data contains no such case.
**Spec change**: Added to the 「途中で切れている場合」 ("if truncated partway") paragraph in chapter 20 §7.3 (added in v1.1).
**Clean-room provisional implementation**: Change required. Do not initialize the version before reading; overwrite only the bytes present in the file.

---

## D2 events (chapter 22)

### D2E-Q1. Is the "." of the cancel item counted in the choice window width?
**Answer**: No. Each item's length is measured after removing its leading ".". This applies to every item that has a ".", including those not chosen as the cancel item.
Measured on fant1_8 MAP004 event 0 (reference implementation, starting the auto event with `--warp 4,6,6` and selecting 「何か買う」 ("Buy something")). The left edge of the window for the nested choice 「道具／薬類／装備品／.前のメニューに戻る」 ("Tools / Medicines / Equipment / .Back to previous menu") was **x = 368** (w = 18, 0x200−144). The 「いいえ」 ("No") of 0x08 (6 bytes, x = 0x1D0) follows the same rule.
**Spec change**: Made explicit in chapter 22 §8 0x09 step 4 that the length is counted without the ".", with the measured example (fixed in v1.1).
**Clean-room provisional implementation**: Change required. Compute w from the length without the "." (in the fant1_8 example, 360 → 368).

### D2E-Q2. Meaning of "field redraw request"
**Answer**: Nothing is drawn on the spot. The change is reflected the next time a full redraw of the field occurs. Such occasions include the hero update in the tick, closing the message window, the redraw in 0x50/0x51, and the main loop's hero processing after an event ends. In the reference implementation this flag is only set and is never read anywhere. The same applies to 0x0A (erase event) and temporary erasure: the event disappears on the next draw.
**Spec change**: Added a definition of 「フィールド再描画要求」 ("field redraw request") to chapter 22 §6.1 (added in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2E-Q3. Conditional branches 0x0C/0x0D and invalid party-slot values (12 or more)
**Answer**: Undefined in the reference implementation as well; it accesses outside the hero records. The same happens in commands targeting 0x0D (0x1E–0x2C) and in the game-over check. Recommendation: skip that slot, so that it does not make "anyone" true and does not make "everyone" false. Normally a party slot never holds 12 or more (it can only happen if 0x1C adds a value of 12 or more).
**Spec change**: Added to chapter 22 §7.2 (added in v1.1).
**Clean-room provisional implementation**: Confirmed (skip).

### D2E-Q4. Remaining bytes of the BGM field of warp points (0x2D/0x2E)
**Answer**:
- Update: only the first 8 bytes are overwritten. Up to 8 bytes of the current BGM name are copied, and any shortfall below 8 bytes is filled with 0. Bytes 9–12 are kept. This matches the clean-room provisional implementation.
- New addition: the point's 16 bytes are **not** zeroed first. Only the map, X, Y, and the first 8 bytes of the BGM field are overwritten, and bytes 9–12 keep whatever that slot previously held. "Previously held" means the initial value from PLAYER.DAT, or a value left over after a deletion.
- Deletion: the following points are just copied forward one by one and the registered count is decreased by 1. The trailing slot after compaction is **not cleared**, so the contents of the original last point remain there.
**Spec change**: Added to 0x2D and 0x2E in chapter 22 §8 (added in v1.1).
**Clean-room provisional implementation**: Change required. Update can stay as-is. Stop zero-clearing the 16 bytes on a new addition (keep bytes 9–12). Also stop zero-clearing the last entry on deletion.

### D2E-Q5. Layer of an event moved by 0x50
**Answer**: Unchanged. Only X, Y, and the movement progress are set; the layer stays as it was. The event is registered at the new position in that layer's placement map (the upper-layer map if above ground, the lower-layer map if underground).
**Spec change**: Added to chapter 22 §8 0x50 step 2-3 (added in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2E-Q6. "If there is an event in the underground-side placement at the destination" in 0x50 step 2-2
**Answer**: "Temporarily erase only when the value is 0–249 and that event exists (is registered in the position table)" is fine. For 0xFD and 0xFF (and 250–254), do nothing. Note also that temporary erasure removes the registration at that event's own current position (and its origin); it does not directly clear the value of the destination cell.
**Spec change**: Added to chapter 22 §8 0x50 step 2-2 (added in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2E-Q7. Palette when the EBD file in 0x3A is short or missing
**Answer**: As written, the palette is not changed. As a result, applying T = 100 shows a solid screen in color 0 of the previous map's palette (with the color tone applied). On maps where color 0 is not black, this is a non-black solid color, and that is the reference implementation's behavior. For the second and subsequent images in a sequence, the result is the same, since step 8 of the previous 0x3A has already restored the map's palette.
**Spec change**: Made explicit in chapter 22 §8 0x3A step 3 (added in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2E-Q8. Type of key wait in the in-event game-over check
**Answer**: A key wait with a wait marker. It draws ▼ at (312,352), waits for "key release → press", erases the marker, and flushes the key buffer.
**Spec change**: Made chapter 22 §5.1 step 2-3 explicitly say 「待ち記号つきキー待ち」 ("key wait with wait marker") (fixed in v1.1).
**Clean-room provisional implementation**: Confirmed.

---

# Answers: D2 menus (chapter 23) and D2 battle (chapter 24) (owner: d2mb)

Scope: D2M-Q1 to Q7 and D2B-Q1 to Q9 in spec-questions.md. Chapters modified: 23_d2_menus.md, 24_d2_battle.md, 01_platform_common.md (D2M-Q2 only).

---

## D2 menus (chapter 23)

### D2M-Q1. When STOP occurs during the key wait after a switch-type item or spell
**Answer**: Undefined in the reference implementation as well. In the original, execution would jump to the event-abort return point even though no event is running; in the reference implementation there is nothing to catch the abort, and it terminates abnormally. The clean-room provisional handling (catch the abort inside the menu, set the flag to 0, and close the menu) is confirmed as the recommendation. At this point the switch is already ON, a consumable item has already been removed, and the spell's MP has already been spent. Since it is switch-type, there is no cave-escape or warp request, and the final state is the same as when exiting normally with a key (§3.5 step 6 → close without fade). Warp and cave escape return without waiting for a key after setting the flag, so there is no opportunity for STOP to occur.
**Spec change**: Added "STOP during the key wait after a switch-type effect" to chapter 23 §3.5 step 6.
**Clean-room provisional implementation**: Confirmed.

### D2M-Q2. ROLL UP / ROLL DOWN in the variable editor
**Answer**: They may be added (an optional extension). The reference implementation cannot input these two keys either, so values cannot be changed there. Add two booleans, ROLL UP and ROLL DOWN, to the input state; when filling the key buffer, append them after the six keys in the order ROLL UP → ROLL DOWN, and handle them by the same rules (add a keystroke at the moment of pressing, repeat, repeat release). The keystroke codes are 0x36 / 0x37. Recommended mapping: PageUp = ROLL UP (+1), PageDown = ROLL DOWN (−1). Implementations that do not add them are also conformant. As long as these two keys are not pressed, behavior is the same as the reference implementation.
**Spec change**: Added 「任意の拡張：ROLL UP / ROLL DOWN」 ("Optional extension: ROLL UP / ROLL DOWN") to chapter 01 §4.3. Added to chapter 23 §10.3 a reference to chapter 01 and the key-check order (cancel → ROLL UP → ROLL DOWN → direction). Also documented that the cursor does not move when the value does not change at the upper or lower limit, that the cell is redrawn for any key, and that the key wait neither flushes the buffer nor performs the STOP check.
**Clean-room provisional implementation**: Change required (optional). Add PageUp/PageDown to the input abstraction and put 0x36/0x37 into the key buffer per the rules above.

### D2M-Q3. "Empty" entries in the warp destination list
**Answer**: The provisional reading is correct. Starting from the top of the list, the warp points' map numbers are placed in as many entries as the registered count, and entries after that are empty regardless of the contents of the state block. An entry before the registered count is also treated as empty if its map number is 0xFFFF (not displayed, cannot be confirmed, and counted as empty in the "more entries" check). A registered count above 30 (data corruption) is an out-of-range access and undefined in the reference implementation, so treating it as 30 is recommended.
**Spec change**: Added the warp destinations to 「内容の並び」 ("content ordering") in chapter 23 §2.3.
**Clean-room provisional implementation**: Confirmed (clamping a registered count above 30 to 30 is advisable).

### D2M-Q4. "Previous position" in the debug menu
**Answer**: It is remembered only while the debug menu is open for a given visit. Each time it is entered from the system's "Debug", it starts at position 0. On returning from the viewer or the variable editor, and after toggling enemy encounters, it returns to the most recently selected position.
**Spec change**: Added the scope of what is remembered to chapter 23 §10.1.
**Clean-room provisional implementation**: Confirmed.

### D2M-Q5. Key buffer when a two-column list starts
**Answer**: It is flushed. On start, a two-column list flushes the key buffer, waits 1 VSYNC, sets the blink counter to 0, and then draws the window. The field and battle use the same routine, so the battle-side handling (flush) is correct.
**Spec change**: Added step 0 to 「表示」 ("display") in chapter 23 §2.3.
**Clean-room provisional implementation**: Change required. Flush the key buffer at the start of the field's two-column list as well (along with 1 VSYNC and blink counter 0).

### D2M-Q6. Cursor on cancel in a two-column list
**Answer**: The provisional reading is correct. Only the equipment candidate list erases the cursor on cancel. For inventory, spells, and warp destinations the cursor is not erased and stays in whatever blink state it was in at that moment.
**Spec change**: Made explicit in the key-operation table (cancel) of chapter 23 §2.3.
**Clean-room provisional implementation**: Confirmed.

### D2M-Q7. Final key wait of the reorder screen
**Answer**: It neither flushes the key buffer nor performs the STOP check. It first looks at the buffer, and if there is a keystroke, takes it immediately and finishes. If not, it waits 1 VSYNC and looks again (exactly 「打鍵があるまで待つ」, "wait until there is a keystroke", in chapter 01 §4.3). Reordering cannot be opened while an event is running, so performing the STOP check does not change the result. However, the order "wait for VSYNC, then look" introduces a 1-frame delay when a keystroke is already in the buffer.
**Spec change**: Added to chapter 23 §7 step 5.
**Clean-room provisional implementation**: Change required (minor). Not flushing can stay as-is. Change the wait order to "look → if empty, VSYNC". The STOP check may be removed; keeping it does not change the result.

---

## D2 battle (chapter 24)

### D2B-Q1. STOP during battle
**Answer**: An event is never aborted by STOP during battle. During battle the "event running" flag is always 0. The event's battle command sets the flag to 0 right before calling the battle and sets it back to 1 after returning (steps 3 and 5 of the battle command in chapter 22, and 「戦闘中はフラグが 0 なので中断されない」 ("not aborted during battle because the flag is 0") in chapter 22 §6.3). Therefore whether in-battle selections and key waits perform the STOP check has no effect on the result. If STOP is held down, the event is aborted by the check made when returning from battle and proceeding to the event's next command.
**Spec change**: Added "STOP during battle" to chapter 24 §3.1.
**Clean-room provisional implementation**: Confirmed.

### D2B-Q2. Conditions for drawing random numbers in the blindness check for attack spells (top priority)
**Answer**: The reading of the §17 table is correct; the body text was under-specified. If the caster is blinded, the second random number is always drawn (no short-circuit), even if the success-rate check has already determined failure. The order is "1 draw for the success-rate check (% 100) → if the caster is blinded, 1 draw (% 10 < attack coefficient means failure)". Even for a spell with attack coefficient 0, the second draw is made if blinded (its result cannot cause failure). Blindness is checked on the caster's status. For all-target attacks (type 3), these 1–2 draws are made per target.
**Spec change**: Added a supplement to chapter 24 §12.10.1 step 3 (fixed in v1.1). Clarified the attack-type row of the §17 table as "+1 if the caster is blinded, regardless of the success check result".
**Clean-room provisional implementation**: Confirmed (if blinded, always draw the second number).

### D2B-Q3. Step 15 after stopping early at §11.3 step 14
**Answer**: Even when stopping early, the step 15 checks (party wiped out → all enemies defeated) are always performed. Stopping early only means "do not proceed to the next round"; it does not skip the checks. If the last combatant in the turn order wipes out the enemies, victory is decided before poison damage.
**Spec change**: Added to chapter 24 §11.3 step 15.
**Clean-room provisional implementation**: Confirmed.

### D2B-Q4. Drawing when the transformed form's size class and the EGD size disagree
**Answer**: The reference implementation behaves as follows (the original is 【未確認】 (unconfirmed)).
- The EGD is always loaded with the EGD's own size (0x30). Each of the 5 planes is sliced as "EGD width/8 × EGD height" bytes.
- Normal display, white (flash), and step-forward drawing use the dimensions of the combatant's size class (small: 10 bytes × 96 rows; large: 20 bytes × 128 rows). The sliced byte sequence is read as rows, one row per class row-width in bytes. Positions beyond the plane's length are 0 (transparent).
- Erasing uses the rectangle of the combatant's class.
- The fill after stepping forward is determined by the EGD's values.
- If the EGD file is missing or shorter than 50 bytes, the EGD size is taken as "small" with all planes empty, and the same rules apply.

None of the 21 samples has an enemy (including transformed forms) whose class disagrees with the EGD's 0x30.
**Spec change**: Added to chapter 24 §12.3 step 2 how image data is interpreted when they disagree.
**Clean-room provisional implementation**: Change required (low priority). Change normal and white drawing from "the EGD's own dimensions" to "the combatant's class dimensions, reading the plane byte sequence at the class row width (missing parts are 0)". The erase area and the post-step-forward fill can stay as they are.

### D2B-Q5. Negative spell damage
**Answer**: As written, negative values are applied as-is (the reference implementation's behavior. The original also uses signed arithmetic, and no code that rejects negative values has been found; this is annotated as 【未確認】 (unconfirmed)). The variance function's result can be negative only when the variance value p is 25 or more (it is never negative for p ≤ 24). None of the 21 samples has a spell with p ≥ 25. The consequences are:
- HP damage: a negative number is displayed (e.g., 「-5 のダメージ！」, "-5 damage!"), with the usual sound effect and effects. HP increases, with no cap even if it exceeds max HP. The sleep wake-up check (1 random draw) is also performed.
- Drain: the caster's HP decreases (no lower bound; the caster does not faint even at 0 or below).
- MP damage: the target's MP increases. With drain, the caster's MP decreases.
- Healing: if negative, HP decreases (no lower bound; no fainting).
- Stat reduction: if negative, the current value increases, and no message is shown.
**Spec change**: Added "when the result is negative" to the end of chapter 24 §12.16 (listing the consequences for each process). Added references in §12.2 step 2 and §12.4 step 2.
**Clean-room provisional implementation**: Confirmed.

### D2B-Q6. List page/cursor memory (during battle)
**Answer**: Reset to 0 only once at the start of battle, and kept across turns during battle (the memory from chapter 23 §2.3 is used as-is). The inventory memory is shared by all characters; the spell memory is per character number, not per slot. The parenthetical "always from the top" in §4.1 meant "from the top for each battle" and was misleadingly worded.
**Spec change**: Added a supplement to chapter 24 §4.1 step 2 (fixed in v1.1).
**Clean-room provisional implementation**: Confirmed.

### D2B-Q7. ESC check for cancelling auto mode
**Answer**: Determined by the pressed state at that instant ("cancel" as captured at the most recent VSYNC). The key buffer is not looked at, consumed, or flushed. An ESC pressed and released within a single wait unit does not cancel auto mode. An ESC keystroke left in the buffer has no effect, because the next top-menu selection flushes the buffer when it starts.
**Spec change**: Added to chapter 24 §7 step 2.
**Clean-room provisional implementation**: Confirmed.

### D2B-Q8. Character widths of in-battle strings and symbols
**Answer**: Both are correct. 「Ｌ」 is full-width (SJIS 0x826B), and the following 2-digit level is half-width. In the HP/MP field `%4d/%4d%5d`, everything, including spaces, digits, and "/", is half-width. When digits overflow, the text is not compressed but extends to the right.
**Spec change**: Added the character types to chapter 24 §6.3.
**Clean-room provisional implementation**: Confirmed.

### D2B-Q9. When a party record number exceeds 11
**Answer**: Undefined in the reference implementation as well (it references a party record out of range; the original also reads out-of-range memory). The provisional "treat that slot as empty but count it in the member count" is confirmed as the recommendation. Handle the empty slot as follows:
- Do not initialize it as a combatant.
- Count it as fainted in the wipe-out check.
- Do not include it in the turn order or as a target.
- Do not draw its party row.
- Exclude it from write-back, experience, and level-ups.

Some field code reinterprets this as "if 12 or more, use hero 0", but imitating that in battle would duplicate write-backs, so it is not recommended.
**Spec change**: Added item 17 to chapter 24 §19.
**Clean-room provisional implementation**: Confirmed (if the handling of empty slots disagrees with the above, align it).

---
