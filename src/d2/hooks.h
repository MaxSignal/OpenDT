// Extension points implemented by the event (ch.22), menu (ch.23) and battle (ch.24) modules.
// Placeholder implementations live in events.cpp / menus.cpp / battle.cpp; replace them there.
#pragma once
#include <cstdint>
#include <vector>

namespace d2 {

class Game;

// ---- events (spec 22) ----
// Called by Game::tryStartEvent after the acceptance checks of spec 21 §7.3 / 22 §3.2 passed.
// Must perform 22 §4.1 (use Game::beginEvent), the command loop (22 §5), and 22 §4.2 (use
// Game::endEvent). Handles EventAbort (STOP) per 22 §5.4. Returns 1 executed, 2 ended by battle
// escape, 3 (reserved), 0 aborted.
int runEvent(Game& g, int eventNo, int trigger);

// ---- menus (spec 23) ----
void fieldMenu(Game& g);                                       // 23 §3 (ESC on the field)
void shopBuySell(Game& g, const std::vector<uint8_t>& goods);  // 23 §11.1 (event 0x3C)
void shopBuy(Game& g, const std::vector<uint8_t>& goods);      // 23 §11.2 (event 0x3D)
void shopSell(Game& g);                                        // 23 §11.3 (event 0x3E)
void inn(Game& g, int price);                                  // 23 §12 (event 0x3F)

// ---- battle (spec 24) ----
// The battle itself (24 §4..§15, including the transition and the cleanup of 24 §4.5).
// gameOverMode != 0: on defeat show the game-over sequence and throw d2::ReturnToTitle.
// Returns 1 victory, 2 escaped, 3 defeat (only when gameOverMode == 0).
int battle(Game& g, int group, int gameOverMode);

}  // namespace d2
