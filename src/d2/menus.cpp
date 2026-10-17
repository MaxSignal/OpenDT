// PLACEHOLDER menus/shops/inn (spec 23). Replace with the full implementation.
#include "d2/game.h"

namespace d2 {

void fieldMenu(Game& g) {
  // Menu not implemented yet: behave like "open then close without fade" (23 §3.6).
  g.sfx(2);
  g.updatePoison();
  g.updatePages();
  g.vsync();
  g.setTone(0);
  g.drawField();
  g.vsync();
  g.setTone(100);
  g.clearKeys();
}

void shopBuySell(Game&, const std::vector<uint8_t>&) {}
void shopBuy(Game&, const std::vector<uint8_t>&) {}
void shopSell(Game&) {}
void inn(Game&, int) {}

}  // namespace d2
