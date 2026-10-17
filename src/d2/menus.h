// D2 menus, item/magic field use, shops, inns, debug menu (spec 23).
// Shared UI parts and formulas exported for the battle (24) and event (22) modules.
#pragma once
#include <string>
#include <vector>

#include "d2/game.h"

namespace d2 {

// ---- 2-column list windows (23 §2.3) ----
// Inventory list. Uses the shared memory g.itemListPage/g.itemListPos (field menu, shop sell,
// battle). Returns the selected item number, or -1 on cancel.
int itemListWindow(Game& g, int x, int y);
// Magic list of character c (learned slots packed). Uses g.magicListPage[c]/g.magicListPos[c].
// Returns the selected magic number, or -1.
int magicListWindow(Game& g, int c, int x, int y);
// Warp destination list (registered warp points). Memory g.warpListPage/Pos (never reset).
// Returns the warp point index (page*5+pos), or -1.
int warpListWindow(Game& g, int x, int y);
// Equipment candidates for character c and slot s (no memory). On decide performs the
// equipment change (23 §6.3) and returns 0; -1 on cancel. Shows the stat-change preview.
int equipListWindow(Game& g, int c, int slot, int x, int y);

// ---- other UI parts ----
void drawPartyWindow(Game& g, int x, int y);          // 23 §3.4
void drawStatusWindow(Game& g, int c, int x, int y);  // 23 §6.2 (does 23 §1.4 first)
void menuMsgOpen(Game& g);                            // 23 §2.4: window (80,256) 480x96
void menuMsgLine(Game& g, int n, const std::string& sjis);
// Event-style choice window (22 §8 0x09 display rules). cancelIndex < 0: cancel key disabled.
// Returns the chosen index (cancel key -> cancelIndex). Does NOT redraw outside the window.
int choiceWindow(Game& g, const std::vector<std::string>& sjisItems, int cancelIndex);
int yesNoWindow(Game& g);  // 0 yes, 1 no (22 §8 0x08 display)

// ---- formulas (23 §9.3, shared with 24 §12.16) ----
int variance(Game& g, int v, int p);  // draws 0..2 random numbers
int magicPower(Game& g, const Member& caster, const Magic& m);
void magicFlash(Game& g, const Magic& m);  // 23 §9.5 (SFX + flash or 3 wait units)

// ---- field menu internals exposed for tests ----
void closeFieldMenu(Game& g, int fadeSpeed);  // 23 §3.6
void debugMenu(Game& g);                      // 23 §10

}  // namespace d2
