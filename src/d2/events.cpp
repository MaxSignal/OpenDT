// PLACEHOLDER event interpreter (spec 22). Replace with the full implementation.
// Supports only: 0x00/0x0B end, 0x01(+0x65) text, 0x02/0x03 switches. Other commands are skipped.
#include <cstring>

#include "d2/game.h"

namespace d2 {

int runEvent(Game& g, int eventNo, int trigger) {
  const int page = g.activePage(eventNo);
  uint16_t len = 0;
  const uint8_t* src = g.map.script(eventNo, page, &len);
  std::vector<uint8_t> work(src, src + len);
  work.resize(work.size() + 64, 0);
  g.beginEvent(eventNo, trigger);
  try {
    size_t p = 0;
    for (;;) {
      g.checkStop();
      if (work.size() - p < 3) break;
      const int c = work[p], n = work[p + 1];
      p += 3;
      if (c == 0x00 || c == 0x0B) break;
      if (c == 0x01) {
        g.waitHeroStill();
        g.embed.clear();
        g.openMessage();
        bool noWait = g.messageLine(0, std::string(reinterpret_cast<const char*>(&work[p]), n));
        p += n;
        for (int line = 1; line <= 3 && p + 3 <= work.size() && work[p] == 0x65; ++line) {
          const int m = work[p + 1];
          noWait |= g.messageLine(line, std::string(reinterpret_cast<const char*>(&work[p + 3]), m));
          p += 3 + m;
        }
        if (!noWait) g.keyWaitMark();
        continue;
      }
      const uint8_t* a = &work[p];
      switch (c) {
        case 0x02:
        case 0x03: {
          const int sw = core::rd16(a);
          if (sw <= 1000) g.switches[sw] = c == 0x02 ? 1 : 0;
        } break;
        case 0x0A: g.eraseEvent(a[0] == 0xFF ? eventNo : a[0], true); break;
        case 0x10:
          g.clearMoveCommands();
          g.waitHeroStill();
          g.closeMessage();
          g.st.curMap = core::rd16(a);
          g.st.x = a[2];
          g.st.y = a[3];
          g.enterMap();
          g.drawField();
          break;
        case 0x13:
          for (int k = 0; k < a[0]; ++k) g.waitMs(100);
          break;
        case 0x1C: g.joinParty(a[0]); g.reloadHeroSprite(); break;
        case 0x1D: g.leaveParty(a[0]); g.reloadHeroSprite(); break;
        case 0x34:
          if (a[0] == 0) g.setTone(0); else g.blackOut(a[0]);
          g.frameCounter = 0;
          break;
        case 0x35: g.blackIn(a[0]); g.frameCounter = 0; break;
        case 0x46: {
          char name[9] = {};
          std::memcpy(name, a, 8);
          std::memcpy(g.map.hdr.battleBgm, name, 9);
          if (!Game::bgmEqual(name, g.st.curBgm)) {
            if (g.st.vehicle == 0) std::memcpy(g.st.prevBgm, g.st.curBgm, 9);
            std::memcpy(g.st.curBgm, name, 9);
            g.playBgm(g.st.curBgm);
          }
        } break;
        case 0x47:
          std::memcpy(g.map.hdr.battleBgm, g.st.prevBgm, 9);
          if (!Game::bgmEqual(g.st.curBgm, g.st.prevBgm)) {
            std::memcpy(g.st.curBgm, g.st.prevBgm, 9);
            g.playBgm(g.st.curBgm);
          }
          break;
        case 0x48: g.sfx(a[0]); break;
        case 0x52: g.st.hidden = 1; break;
        case 0x53: g.st.hidden = 0; break;
        default: break;
      }
      p += n;
    }
    g.vsync();  // placeholder guard so repeated auto events still advance time
  } catch (EventAbort&) {
    g.st.eventRunning = 0;
    g.waitHeroStill();
    g.drawField();
    g.endEvent();
    return 0;
  }
  g.endEvent();
  return 1;
}

}  // namespace d2
