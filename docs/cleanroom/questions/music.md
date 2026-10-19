# Questions for the Spec (Chapter 02: Sound Driver)

## Q1. SSG pitch (reference value in §3.10.4) (resolved by v1.1)
**Resolved by v1.1**: Error in the spec. The ymfm output (period 477 ≈ 261.6 Hz, O4A ≈ 441 Hz) is correct (§3.10.4 corrected, acceptance criterion 19). No implementation change.
§3.10.4 states "with ymfm's YM2203 model and a 1/6 prescaler … SSG period 477 is calculated as ≈ 131 Hz", but
with the bundled ymfm (OPN_FIDELITY_MIN, prescaler 1/6 selected by a write to address 0x2D) the SSG engine runs at φM/16 = 249,600 Hz,
so period 477 is ≈ 262 Hz and O4 A (period 283) is 441 Hz (the same octave as FM O4 A = 440 Hz).
The spectral peaks match the reference audio exactly (SOUND.DAT @15 in reference_audio/come.wav and agasa.wav, @12 in kokoro.wav),
so we concluded that the reference implementation's actual output is also this (≈262 Hz) and use ymfm's behavior as is.
We would like to confirm whether the "≈131 Hz" in §3.10.4 is a calculation error in the text, or whether the reference implementation uses a different setting.

## Q2. Power-on initial value of the vibrato direction (§3.2) (resolved by v1.1)
**Resolved by v1.1**: The power-on direction is "up" (added to §3.2 and §3.12). The provisional implementation was confirmed and the PROVISIONAL marker removed.
The spec says "the initial values at driver startup are all 0", but does not state whether a vibrato "direction" of 0 means up or down.
This only matters when `I` is set before key-on and a frequency write occurs while the wait is 0. Provisionally we use "up"
(PROVISIONAL: driver.h `vibUp = true`).

## Q3. Invalid characters in an SSGENV line (§2.8) (resolved by v1.1)
**Resolved by v1.1**: Stopping at the invalid character without adding a value is adopted as the recommended correct behavior (added to §2.8). No implementation change.
The implementation stops reading the line when it encounters a character that is neither a digit, a separator, nor end of line (no 0 is appended).
No sample contains such a line, so compatibility is not affected.

## Q4. Sign of numeric values (§2.3) (resolved by v1.1)
**Resolved by v1.1**: Values are interpreted as 16-bit signed regardless of whether a minus sign is present. The magnitude of N40000 is 192, not 64, and negative (the example in the question was wrong). The implementation already used the low 8 bits of the absolute value of the signed value, so no change (a test for acceptance criterion 21 was added).
Values of 32768 or more without a minus sign (e.g. `N40000`) are also interpreted as 16-bit signed (N40000 → −25536 → magnitude 64, negative).
We would like to confirm whether "if there is a minus sign, negate, and interpret as 16-bit signed" means the signed interpretation also applies when there is no minus sign.
No sample uses such a value.

## Q5. SSG volume writes by a fade while stopped (no song ever played) or during a sound effect (§3.22) (resolved by v1.1)
**Resolved by v1.1**: A rule close to (a). While no playback has been started since the player launched, a fade only records the step and does not write registers. Once playback has been started, writes always occur, even while stopped or during a sound effect (§3.2 and §3.22 revised, acceptance criterion 18). Implemented (`everPlayed_` in the driver). 100% sample match with the come, agasa and kokoro reference audio.
- Reproduction: `opendt-headless samples/games/come/come -k "100 z 100 z 100 z 100 z 400" -n 900 -w come.wav` (same for agasa).
  At frame 209, immediately after the D1 engine requests sound effect 15, it starts a BGM switch (fade 0→15 every 20 ms → stop → BGM00).
  At this point no song has been played yet (Ver 2.00).
- Spec §3.22 says "SSG is always written, including disabled parts and stopped channels". Implemented this way, at the start of each frame
  registers 8 and 9 are written with values derived from the channel state (0 after the fix below), and the sound effect volume changes for the few chip samples
  until the sound effect rewrites them at the next audio-side VSYNC (1–4 chip samples later). The only differences from the reference audio are
  spikes in the first 1–2 output samples of frames 211, 213 and 215 (maximum difference 2502) and a ±1 tail from the DC cut (up to frame 221); 99.5% of the whole matches.
- Experiment: making the fade **write no registers at all** gave a 100% sample match with the reference audio for both come and agasa.
  So the reference implementation apparently does not write the SSG volume registers in this situation (or the writes do not show up in the output).
  However, this data cannot distinguish which of the following rules applies:
  (a) Channels for which no song has ever been played (or that have never been initialized) are not written (like the FM rule "do not write if no voice has been loaded")
  (b) Nothing is written while stopped (playing flag off)
  (c) While a sound effect is playing, writes to SSG A/B are discarded even in Ver 2.00
- Currently the implementation follows the spec (always write). We would like to confirm which rule is correct.

## Note (fixed): Power-on initial values of the channel state (resolved by v1.1)
**Resolved by v1.1**: "All 0" specifies the state that is not reset at song start. The power-on values of the state that is reset at song start are not observable under the Q5 rule (leaving them at 0 is harmless).
We applied §3.2's "the initial values at driver startup are all 0" also to the state that is reset at song start (volume, current soft-envelope value, octave,
default length, gate, Q, etc.). Previously the song-start values (volume 12, etc.) were held from startup, so in the situation above the fade
wrote 12 − step to registers 8 and 9, changing the sound effect amplitude (rms 5953 at frame 209 vs. 4591 in the reference), and after the sound effect ended
a tail caused by a DC step remained (up to frame 224). With power-on values of 0, the only difference left was the spikes in frames 211–221 described above.
We would like to confirm, just in case, whether the spec wording also covers the state that is reset at song start.

## Q6. Does the hardware envelope restart on restore at the end of a sound effect in Ver 2.21? (§4.6) (resolved by v1.1)
**Resolved by v1.1**: The parenthetical was wrong. The hardware envelope does not restart (§4.6 revised, a test for acceptance criterion 20 was added). No implementation change.
§4.6 gives the restore procedure as "frequency write → volume update (§3.9.2) → key-on if the key state is on (the soft/hard envelope starts over from the beginning)", but
the volume update in §3.9.2 clears the hardware-envelope-enabled flag, so the subsequent key-on does not write the hardware envelope (08=10, 0D=shape)
and the channel resumes at a fixed volume (only the soft envelope starts over). The implementation currently follows the procedure as written (the volume update clears the flag).
We would like to confirm whether the parenthetical statement that the "hard" envelope also restarts is wrong, or whether the order is different.
