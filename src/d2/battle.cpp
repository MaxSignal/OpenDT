// PLACEHOLDER battle (spec 24). Replace with the full implementation.
// Ends immediately as a victory after the cleanup of 24 §4.5.
#include "d2/game.h"

namespace d2 {

int battle(Game& g, int group, int gameOverMode) {
  (void)group;
  (void)gameOverMode;
  g.st.steps = 0;
  g.vsync();
  g.setTone(0);
  g.scr.clear(0);
  g.fieldMask();
  g.clearKeys();
  return 1;
}

}  // namespace d2
