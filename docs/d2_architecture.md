# D2 engine architecture (for subsequent implementers)

Scope: `src/d2/`. Specification chapters 20–24. This document describes the foundation that is already
implemented and the extension points (hooks) to be filled in by the owners of chapters 22 (events), 23 (menus)
and 24 (battle).

## Files

| File | Contents | Owner |
|---|---|---|
| `data.h/.cpp` | All data formats of chapter 20 (packed structs that match the files byte for byte), build detection, loaders | Done |
| `game.h` | `d2::Game` (declarations of all state and all helpers) | Done (keep additions minimal) |
| `game.cpp` | VSYNC, waits, input (key buffer/repeat/direction reading/key wait), palette (tone, fades, color tone, flash, shake), window frames, text, selection cursor, message window, single text line, gold window, common game rules (23 §1) | Done |
| `field.cpp` | Chapter 21: map loading/entry, drawing, hero movement, NPCs, move commands, tick, vehicles, encounters, battle invocation (21 §10.2), event trigger checks and start/end processing, main loop | Done |
| `flow.cpp` | Startup, title, new game, save/load, debug start options, `createEngine` | Done |
| `hooks.h` | Hook declarations (see below) | Done |
| `events.cpp` | **Provisional**. To be replaced by the chapter 22 owner | Ch. 22 owner |
| `menus.cpp` | **Provisional**. To be replaced by the chapter 23 owner | Ch. 23 owner |
| `battle.cpp` | **Provisional**. To be replaced by the chapter 24 owner | Ch. 24 owner |

Owners may add files (e.g. `events_*.cpp`; CMake picks up `src/d2/*.cpp` automatically). Keep owner-specific
state in a struct in your own file or as a minimal addition to `Game` (to avoid concurrent edits, put additions
to `game.h` in the per-owner section at the end).

## Hooks (`hooks.h`)

```cpp
int  runEvent(Game& g, int eventNo, int trigger);                 // 22: called after acceptance. §4.1→§5→§4.2
void fieldMenu(Game& g);                                          // 23 §3
void shopBuySell(Game& g, const std::vector<uint8_t>& goods);     // 23 §11.1 (0x3C)
void shopBuy(Game& g, const std::vector<uint8_t>& goods);         // 23 §11.2 (0x3D)
void shopSell(Game& g);                                           // 23 §11.3 (0x3E)
void inn(Game& g, int price);                                     // 23 §12 (0x3F)
int  battle(Game& g, int group, int gameOverMode);                // 24 (includes transition effect and §4.5 cleanup)
```

- Before `runEvent` is called, `Game::tryStartEvent` has already performed the acceptance check of
  21 §7.3 / 22 §3.2. Inside `runEvent`, call `g.beginEvent(i, t)` (steps 1 and 3–7 of 22 §4.1, including
  turning to face) → command loop → `g.endEvent()` (22 §4.2). The label table, branch result table and work
  area belong to the events side.
  Commands that move the event itself (0x4B/0x50/0x51/0x56) set `g.run_.movedSelf = true`.
  When a location change moves to a different map, replace `g.run_.savedBattleBgm` with the new map's battle
  BGM name (22 0x10). `g.run_.startMap` is the map at the start.
- STOP (Ctrl+cancel in test play) makes `g.checkStop()` throw `d2::EventAbort` (only while an event is
  running). `keyWait`, `select` and `tick` call `checkStop()` internally. `runEvent` catches it and performs
  the procedure of 22 §5.4 (`st.eventRunning=0` → `waitHeroStill()` → `drawField()` → `endEvent()`, return 0).
- To start a battle, use `g.callBattle(group, gameOverMode, eraseEvent)`, which includes the caller-side
  processing (21 §10.2: battle BGM, graphics reload, erase, page update, mask, color tone reapplication,
  redraw, T=100, BGM restore). `battle()` itself only does what chapter 24 covers. Game over is
  `throw d2::ReturnToTitle()`.
- The caller sets the battle background number in `g.st.battleBg`.

## Control flow (exceptions)

| Exception | Meaning | Caught in |
|---|---|---|
| `d2::ReturnToTitle` | Return to title (party wiped out, event 0x61) | `titleLoop` (state is restored from PLAYER.DAT) |
| `d2::QuitProgram` | Quit the program (system menu 「終了」 ("Quit"), event 0x62). The caller does any blackout etc. first | `run` (returns to title with `--no-quit`) |
| `d2::EventAbort` | Event aborted by STOP | `runEvent` |
| `core::QuitGame` | Quit request from the host | Host |

## Main state (`Game` members)

- `members[12]` (`Member`, the 200-byte PLAYER.DAT record as is), `st` (`StateBlock`, 0x2A8 bytes),
  `inventory[260][2]`, `switches[1001]`, `vars[100]`. Saving: `buildSave()` / `saveSlot(k)` / `loadSlot(k)`.
- `db` (`Database`): items, magic, enemies, enemy groups, WORD.DAT (`db.wordAt(off)`), map names, gaiji
  (custom glyphs), title image.
- `map` (`MapData`): header, 160×160 grid, area table, version, events (`map.events[i]` is an `EventRec*`
  into the event area, or nullptr if absent). Scripts: `map.script(i, page, &len)` (len includes the
  terminator).
- `chips` (attributes, encounter type, battle background), `chipset` / `chrSlot[4]` / `cache[33]` (patterns).
- `upperMap` / `lowerMap` (event position maps), `viewX/viewY` (view origin).
- Palette: `curPal` (8-bit), `basePal` (chipset, 4-bit), `tone`. Apply to the display with `applyPalette()`.
- `frameCounter` (free-running counter), `waitCounter` (wait counter), `embed` (embedded string for `\@`).
- `build` (`Build::B9606/B9608/B9703/B9710`, `isOldBuild(build)`), `testPlay` (`--no-title`).

## Main helpers

| Purpose | Functions |
|---|---|
| Time | `vsync()`, `waitFrames(n)`, `waitMs(ms)` (converted at 56.42 Hz, with carry-over), `waitUnits(n)` (21 §1.2) |
| Sound | `sfx(n)` (n+10 is sent to the driver), `playBgm(name9)`, `stopBgm()`, `bgmEqual` (9 bytes)/`bgmEqual8` |
| Input | `popKey()` (`kKeyEsc` etc., `kKeyNone` if empty), `clearKeys()`, `recordLatch()`, `readDirection()`, `keyWait()` (key release → press, consumes RNG), `keyWaitMark()` (with ▼ marker), `cur`/`prev` (current/previous input) |
| Palette | `setTone(t)`, `vsyncTone(t)`, `blackOut/blackIn/whiteOut/whiteIn(s)`, `toneChange(k, s)`, `reapplyTone()`, `flash()`, `shake()`, `setPal4(i,r,g,b)` |
| Drawing | `drawText(x,y,sjis,color)` (D2 graphic text, bold, transparent), `textWidth()`, `drawWindow(x,y,w,h)` (23 §2.1), `drawCursor()`, `select(x,y,n,init,mode)` (22 §6.4), `fieldMask()/menuMask()/windowMask()/clearTextLayer()`, `goldWindow(x,y)`, `scr` (`core::Screen`) |
| Messages | `openMessage()`, `closeMessage()`, `messageLine(line, s)` (one line with the control codes of 22 §6.2; returns true when it sees `\^`) |
| Field | `drawField()`, `enterMap()`, `updatePages()`, `activePage(i)`, `eraseEvent(i, permanent)`, `tick(n)`, `waitHeroStill()`, `waitEventStill(i)`, `clearMoveCommands()`, `setHeroMoveCommand(m)`, `setEventMoveCommand(i,m)`, `reloadHeroSprite()`, `reloadVehicleSprites()`, `reloadAllSprites()`, `boardShip(withBgm)`, `callBattle(...)`, `chipAt/attrAt(x,y)` |
| Rules | `recalc(m)`, `recalcAll()`, `levelUp(m)`, `levelDown(m)`, `addHp(m,n)`, `fullHeal(m)`, `gameOver()`, `joinParty/leaveParty/inParty/leader()`, `learnMagic/forgetMagic`, `addItem/removeItem/packInventory/moveItemToEnd/discardItem/hasItem/itemCount`, `addGold/subGold`, `updatePoison()`, `expForLevel/statForLevel` |
| Flow | `slotSelect(x,y)`, `saveSlot(k)`, `loadSlot(k)` (includes the blackout of 20 §9.3; returns false if the file does not exist) |

String constants (strings built into RPG.EXE, such as 「ニューゲーム」 ("New Game")) are written in UTF-8 and
converted to SJIS with `core::utf8ToSjis()`.

## Status of the provisional implementations

- `events.cpp`: **fully implemented** (all chapter 22 commands, labels, branch result table, skipping, STOP,
  game-over check). For unit tests it exposes `events.h` (`runScript`, `evalCondition`, `skipTo`,
  `moveWaitTicks`). Questions are in `docs/questions_d2_events.md`.
- `menus.cpp`: opens the menu and closes it immediately. Shops and inns do nothing.
- `battle.cpp`: performs only the 24 §4.5 cleanup and returns victory.

## Tests

- `tests/test_d2.cpp` (acceptance points of chapters 20 and 21).
- Comparison with reference screens: run all 23 games with
  `-k "60 shot:01_title 30 z 30 z 150 shot:02_start z 70 z 70 shot:03_after_ok x 60 shot:04_menu x 60 r*40 l*40 shot:05_walk" -n 1500`
  and compare pixels against `reference_shots/<game>/`. Currently 01_title matches for all games, and dino,
  keep and knuckle match on all 5 shots. The remaining differences are due to unimplemented event commands
  (choices, pictures, color tone, etc.).
