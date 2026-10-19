# Questions for the Spec (Chapter 24: D2 Battle)

Implementation: `src/d2/battle.cpp` (flow, screen, actions), `src/d2/battle_calc.cpp` (formulas, initialization), `src/d2/battle.h`.
Provisional implementations are marked `PROVISIONAL(Qn)` in the code.

**Status (spec v1.1; answers in `spec-answers-v1.1.md`, "D2 Battle"): Q1–Q9 are all resolved. No PROVISIONAL markers remain.**

| Question | Answer | Action |
|---|---|---|
| Q1 STOP | During battle the event-running flag is 0, so there is no abort (24 §3.1) | Confirmed (STOP is not checked inside battle) |
| Q2 Second draw for blindness | Always drawn, regardless of the success check result (§12.10.1, §17) | Confirmed. PROVISIONAL removed |
| Q3 Check after cut-off | Always checked (§11.3 step 15) | Confirmed |
| Q4 Size class vs. EGD mismatch | The EGD is read using its own dimensions; normal, white and step-forward drawing use the size-class dimensions, reading the plane byte sequence with the size class's row width (missing part is 0). Erasing uses the size class; the fill after stepping forward uses the EGD | Changed (`bt::classView`). Tests added |
| Q5 Negative damage | Applied as is with no lower-bound clamping (end of §12.16) | Confirmed. Tests added |
| Q6 List memory | Reset to 0 only once at battle start and kept during battle (§4.1) | Confirmed. Lists changed to use the shared menu components (`itemListWindow` / `magicListWindow`), including clearing the key buffer on open (23 §2.3 step 0) |
| Q7 ESC for cancelling auto | Pressed state at that instant (§7 step 2) | Confirmed |
| Q8 Character widths | 「Ｌ」 is full-width; digits and `/` are half-width (§6.3) | Confirmed |
| Q9 Member numbers of 12 or more | Treated as an empty slot but counted in the party size (§19 item 17) | Confirmed. Fixed so that such party rows are not drawn and are excluded from all-member processing. Tests added |

Related: D2F-Q4 (no image if the BKG is smaller than 41008 bytes) is now decided solely in `loadBattleBg` (the duplicated size check on the battle side was removed).

## Q1. STOP during battle (Ctrl+Cancel in test play)
In a battle started by an event's battle command, the "event running" (イベント実行中) flag in the state block is set. The generic selection and
key waits of chapter 23 perform a STOP check, but chapter 24 says nothing about STOP during battle. Does STOP abort the event in the middle of a battle
(exiting without the battle cleanup), or is STOP not checked during battle?
Current: cursor selection, lists and key waits within battle do not check STOP (battle-specific implementations).

## Q2. Condition for drawing a random number in the blindness check of attack magic (§12.10.1 step 3 vs. the table in §17)
The body text says "if random % 100 ≥ success rate, it fails. If the caster is blind, draw one more random number…", while the table in §17 says "success 1, +1 if blind".
If the success-rate check has already failed, is the blindness random number still drawn (the table's reading) or not (short-circuit reading)?
Current: if blind, the second number is always drawn (PROVISIONAL(Q2)).

## Q3. Step 15 (checks) after cutting off in §11.3 step 14
When step 14 cuts off the remaining repetitions (sleep, paralysis or fainting after the action; action interrupted), are the step 15 checks for party wipe / all enemies defeated
performed before exiting, or does it move on to the next slot without checking? This makes a difference for the last combatant in the action order (without the check,
even with all enemies defeated, victory comes only after the end-of-round poison damage is applied).
Current: even when cutting off, the step 15 checks are performed first.

## Q4. Drawing when the transformation target's size class and the EGD size disagree (【未確認】 (unverified) in §12.3)
When the combatant's size class (record 0x46) differs from the size of the loaded EGD (0x30), which dimensions are used to interpret and draw
the image data?
Current: normal/white display is drawn with the EGD's own dimensions; the erase area uses the combatant's size class; the fill after stepping forward is determined by the EGD values.

## Q5. Negative magic damage
With a large variance value (around p > 24), the result of the variance function can be negative. §12.2 only handles
"when the damage is 0"; negative damage satisfies "damage < target's HP", so HP increases. Does the original do this too? MP damage (§12.4) is
the same (only "if value = 0, do nothing").
Current: as written (negative values are applied as is).

## Q6. List page/cursor memory (during battle)
§4.1 step 2 resets the memory of the item list and every character's magic list to 0 at battle start. Is the memory kept across turns
during battle (using the memory of 23 §2.3 as is), or does the list start from the top every time (the parenthetical in §4.1, "lists during battle always
start from the top")?
Current: reset to 0 only at battle start, and remembered across turns during battle.

## Q7. ESC check for cancelling auto battle
In §7 step 2, "wait 1 unit, and if ESC is pressed at that point", does it look at the pressed state at that instant (held down),
or at a keystroke in the key buffer?
Current: looks at the held-down state (the key buffer is not used).

## Q8. Strings of external characters and symbols during battle
The 「Ｌ」 in `Ｌ%2d` was taken to be SJIS 0x826B (full-width L). The HP/MP fields are drawn half-width as `%4d/%4d%5d`.
Both match the example in §6.3 (` 348/ 348   32`), but we would like to confirm half-width vs. full-width in the original.
(We have already confirmed, by capturing the original battle screen in DOSBox-X, that the layout and column positions match.)

## Q9. Party record numbers above 11
If the party order in the state block contains a value of 12 or more (data corruption), that slot is treated as empty (but counted in the party size).
