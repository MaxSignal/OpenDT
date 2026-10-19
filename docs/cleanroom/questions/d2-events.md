# Questions for the Spec (Chapter 22: D2 Events)

## Q1. Choice window width and the "." of the cancel item (§8 0x09 step 4)

**Resolved (v1.1, D2E-Q1)**: Every item that has a "." is counted without the ".". Implementation changed; confirmed that the left edge of the nested choice in fant1_8 MAP004 event 0 is at x=368. Test `d2ev_choice_width_ignores_dot`.
The display width w is defined as "the byte count of each item's string", but it is not stated whether the leading "." of the cancel item is counted.
In the nested choice of event 0 in fant1_8 MAP004 (real data), the cancel item 「.前のメニューに戻る」 (".Back to the previous menu", 19 bytes) is the longest;
counting the "." gives x = 0x200 − 152 = 360, not counting it gives 368.
Provisionally the width is computed from the stored byte count including the "." (`PROVISIONAL(Q1)`, events.cpp `Interp::choice`).

## Q2. Meaning of "field redraw request" (フィールド再描画要求) (0x11, 0x12, 0x1C, 0x1D, 0x4C, 0x52, 0x53)

**Resolved (v1.1, D2E-Q2)**: Nothing is drawn on the spot (it is reflected at the next full redraw). Same as the current implementation.
It is not stated whether a full redraw happens immediately, or whether it waits for the next drawing opportunity (tick, hero update, closing a window, etc.).
Provisionally "nothing is drawn on the spot" (drawing happens in the main loop after the hero update or the end-of-event processing).
0x0A (erase event) does not mention a redraw either, so it is likewise treated as disappearing at the next draw.

## Q3. Hero specifiers 0x0C/0x0D in conditional branches and invalid party slot values

**Resolved (v1.1, D2E-Q3)**: Slots with a value of 12 or more are skipped. Same as the current implementation (PROVISIONAL marker removed).
The handling of a party slot value of 12 or more (does not normally happen) is not described. Provisionally such slots are skipped
(`PROVISIONAL(Q3)`).

## Q4. Remaining bytes of the BGM field of warp points (0x2D/0x2E)

**Resolved (v1.1, D2E-Q4)**: On add, do not clear the 16 bytes; write only the map, X, Y and the first 8 bytes of the BGM field (zero-padded after the name). Bytes 9–12 are left as they are. Deletion only compacts the list and does not clear the last entry. Implementation changed. Test `d2ev_warp_bytes_v11`.
The BGM field of each point is 12 bytes (chapter 20), but 0x2D only says "the current BGM name (8 characters)".
Whether bytes 9–12 are kept or cleared when updating an existing point, and whether the last point is cleared or kept after 0x2E compacts the list,
affect byte-exact save files. Provisionally: on update only the first 8 bytes are replaced; on a new addition the 16 bytes are zeroed before writing;
on deletion the last entry after compaction is cleared with 0 (`PROVISIONAL(Q4)`).

## Q5. Layer of an event moved by 0x50

**Resolved (v1.1, D2E-Q5)**: The layer is not changed. Same as the current implementation.
0x50 only sets the event's X and Y. It is not stated whether the event's layer (surface/underground) is changed to match the layer attribute
of the destination chip. Provisionally it is not changed (the event is placed on its original layer).

## Q6. 0x50 step 2-2 "if there is an event in the underground placement at the destination"

**Resolved (v1.1, D2E-Q6)**: Temporarily erase it only when the value is 0–249 and the event exists. Same as the current implementation.
Is it correct to do nothing when the underground placement value is 0xFD (the erased marker) or 0xFF? Provisionally the event is temporarily erased "only when the value is 0–249
and the event exists".

## Q7. Palette when the EBD file for picture 0x3A is short or missing

**Resolved (v1.1, D2E-Q7)**: The palette is not changed, resulting in a solid screen of color 0. Same as the current implementation.
The spec says "if missing, the screen stays black (the palette is not changed)", but applying T = 100 in step 5 displays black (color 0)
using the previous map's palette. On a map where color 0 is not black, the result is a solid non-black screen; is that acceptable?
Provisionally as written (the palette is not changed).

## Q8. Type of key wait in the game-over check during an event (§5.1 step 2-3)

**Resolved (v1.1, D2E-Q8)**: Key wait with the wait marker. Same as the current implementation (PROVISIONAL marker removed).
§5.1 only says "key wait (§6.3)", so it is unclear whether it means §6.3's "key wait with wait marker" (待ち記号つきキー待ち; draws ▼ at (312,352)) or the
"key release → press" wait (キー離し→押し; no marker) (other places, e.g. 0x01 step 6 and 0x3A step 6, state it explicitly).
This determines whether ▼ appears on screen. Provisionally the wait with the marker is used (`PROVISIONAL(Q8)`, events.cpp `Interp::gameOverCheck`).
