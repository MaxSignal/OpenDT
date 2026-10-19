# Questions for the Spec (Chapter 23: Menus, Shops and Inns)

## Q1. STOP after the "event running" (イベント実行中) flag is set inside a menu (switch-type in §9.1 / §9.2)

**Resolved (v1.1, D2M-Q1)**: The provisional behavior (catch it inside the menu, set the flag to 0 and close without a fade) was confirmed as the recommendation. Added to 23 §3.5 step 6. PROVISIONAL marker removed.
Switch-type items and magic perform a "key release → press" (キー離し→押し) wait after setting the event-running flag to 1. The STOP check of this wait
(22 §6.3) looks at the event-running flag, so pressing Ctrl+Cancel during a test play can trigger an abort, but
the handling of an abort inside a menu is not described.
Provisionally: `fieldMenu` catches the abort, sets the flag to 0 and closes the menu (menus.cpp `PROVISIONAL(Q1)`).

## Q2. ROLL UP / ROLL DOWN in the variable editor (§10.3)

**Resolved (v1.1, D2M-Q2)**: Optional extension in 01 §4.3 (0x36/0x37). The variable editor is implemented with the 23 §10.3 v1.1 key order (Cancel → ROLL UP → ROLL DOWN → direction), neither the value nor the cursor changes at the limits, the cell is redrawn on any key, and the key wait does not clear the buffer and has no STOP check (input wiring is the D2 core owner's responsibility). Test d2menu_v11_variable_editor_roll_keys.
Chapter 01's input abstraction (8 booleans) and key buffer (6 kinds of keystrokes) have no ROLL UP / ROLL DOWN,
so value changes cannot be reached in practice (only the handling of keystroke codes 0x36/0x37 is implemented).
May we add PageUp/PageDown to the input abstraction (this would be a change to chapter 01)?

## Q3. "Empty" in the warp destination list (§2.3)

**Resolved (v1.1, D2M-Q3)**: The provisional reading is correct. Added clamping of the registered count to 30 when it exceeds 30. Test d2menu_v11_warp_count_clamped.
The spec says "empty check = map number 0xFFFF", but does not say whether the slots after the registered count (state block 0x081) are always 0xFFFF
(the initial values in PLAYER.DAT, and the value of the last slot after 0x2E deletes and compacts).
Provisionally: only the slots before the registered count are listed, and among them a map number of 0xFFFF is treated as empty.

## Q4. "Previous position" in the debug menu (§10.1)

**Resolved (v1.1, D2M-Q4)**: Same as the provisional behavior (remembered only while the menu is open once; reset to 0 on every entry).
It is unclear whether the scope of "the position is remembered within the debug menu" is a single display of the debug menu or the whole
program run. Provisionally: starts from 0 each time it is entered from the system menu, and returns to the previous position when coming back from a viewer.

## Q5. Key buffer when a two-column list opens (§2.3)

**Resolved (v1.1, D2M-Q5)**: Changed to clear the key buffer on open, wait 1 VSYNC, set the blink counter to 0, then draw (all two-column lists in the field). Test d2menu_v11_list_open_clears_key_buffer.
The generic selection (22 §6.4) clears the key buffer when it starts, but the two-column list has no such description. Provisionally: not cleared.
(Review note: the in-battle list (battle.cpp `listSelect`, which uses 23 §2.3 via 24 §8.2) clears it on open,
so the field and battle behave differently. We would like to confirm which is correct.)

## Q6. Cursor on cancel in a two-column list (§2.3)

**Resolved (v1.1, D2M-Q6)**: The provisional reading is correct (only the equipment candidate list erases the cursor on cancel).
The spec says the cursor is erased on cancel only for the equipment candidates. For items, magic and warp destinations the implementation does not
erase the cursor on cancel (the menu screen is redrawn right afterwards, so there is almost no visible effect).

## Q7. Final key wait in reordering (§7 step 5)

**Resolved (v1.1, D2M-Q7)**: Changed to: do not clear, no STOP check, and the order "peek → VSYNC if nothing" (the key waits of the switch viewer and variable editor use the same function). Test d2menu_v11_reorder.
It is not stated whether the key buffer is cleared, or a STOP check is performed, before "wait until one key arrives from the key buffer".
Provisionally: not cleared; a STOP check is done every VSYNC, then one keystroke is taken.
