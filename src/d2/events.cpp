// D2 event interpreter (spec 22): execution loop, branching, labels and every event command.
#include "d2/events.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "d2/game.h"

namespace d2 {

namespace {

std::string sj(const char* utf8) { return core::utf8ToSjis(utf8); }

constexpr size_t kPad = 64;     // zero bytes appended to the work area (spec 22 §2.4)
constexpr size_t kSlack = 600;  // extra physical zeros so argument reads never leave the buffer

// Apply fn to each target member of commands 0x1E..0x2C (spec 22 §7.2).
template <class F>
void forTargets(Game& g, int t, F fn) {
  if (t >= 0 && t < 12) {
    fn(g.members[t]);
  } else if (t == 0x0D) {
    for (int k = 0; k < g.st.partyCount && k < 5; ++k) {
      const int c = g.st.party[k];
      if (c <= 11) fn(g.members[c]);
    }
  }
}

// Member specification in conditions: 0..11 that hero, 0x0C anyone in party, 0x0D whole party.
template <class F>
bool memberCond(Game& g, int spec, F pred) {
  if (spec >= 0 && spec < 12) return pred(g.members[spec]);
  if (spec == 0x0C || spec == 0x0D) {
    const bool any = spec == 0x0C;
    for (int k = 0; k < g.st.partyCount && k < 5; ++k) {
      const int c = g.st.party[k];
      if (c > 11) continue;  // PROVISIONAL(Q3): invalid party slots are skipped
      const bool r = pred(g.members[c]);
      if (any && r) return true;
      if (!any && !r) return false;
    }
    return !any;  // empty party: "anyone" false, "all" true
  }
  return false;
}

bool comparePos(int how, int x, int y, int v) {
  switch (how) {
    case 0: return x == v;
    case 1: return x >= v;
    case 2: return x <= v;
    case 3: return y == v;
    case 4: return y >= v;
    case 5: return y <= v;
    default: return false;
  }
}

struct Interp {
  Game& g;
  int self;
  std::vector<uint8_t> w;
  size_t size = 0;  // logical work area size (L + 64)
  long p = 0;
  long labels[5];
  int8_t results[256];

  Interp(Game& game, int s, const uint8_t* script, size_t len) : g(game), self(s) {
    w.assign(script, script + len);
    size = len + kPad;
    w.resize(size + kSlack, 0);
    for (auto& l : labels) l = -1;
    std::memset(results, 0, sizeof(results));
  }

  long remaining() const { return long(size) - p; }
  int target(int e) const { return e == 0xFF ? self : e; }
  bool sameMapAsStart() const { return g.st.curMap == g.run_.startMap; }
  std::string str(long at, int n) const { return std::string(reinterpret_cast<const char*>(&w[size_t(at)]), size_t(n)); }

  int run();
  bool step(int& result);  // false when the script ends
  void gameOverCheck();
  void text(int n);
  int choice(const std::vector<std::string>& items, int cancelIdx);
  void doBattle(int c, int n, uint8_t depth, int& result, bool& end);
  void picture(int no, bool next);
  void keyInputWait();
  void moveHeroWait(int c);
  void moveEventWait(int e, int c);
  void placeEvent(int e, int x, int y);
  void swapEvents(int a, int b);
  void finishPlacement();
};

void Interp::gameOverCheck() {
  if (!g.gameOver()) return;
  g.openMessage();
  g.messageLine(0, g.db.wordAt(0x653));
  g.keyWaitMark();
  throw ReturnToTitle();
}

// 0x01 + up to three 0x65 lines (spec 22 §8 0x01).
void Interp::text(int n) {
  g.waitHeroStill();
  g.embed.clear();
  g.openMessage();
  bool noWait = g.messageLine(0, str(p, n));
  long q = p + n;
  for (int line = 1; line <= 3; ++line) {
    if (long(size) - q < 3 || w[size_t(q)] != 0x65) break;
    const int m = w[size_t(q) + 1];
    noWait |= g.messageLine(line, str(q + 3, m));
    q += 3 + m;
  }
  if (!noWait) g.keyWaitMark();
  p = q;
}

// Choice window (0x08 / 0x09). Returns 0..3 or -1.
int Interp::choice(const std::vector<std::string>& items, int cancelIdx) {
  int wbytes = 0;
  for (auto& s : items) {
    // PROVISIONAL(Q1): width counts the stored bytes including a leading '.' of the cancel item.
    const int len = int(std::min<size_t>(s.size(), 32));
    wbytes = std::max(wbytes, len);
  }
  const int n = int(items.size());
  const int x = 0x200 - 8 * wbytes;
  g.drawWindow(x, 0x50, 0x230 - x, n * 24 + 24);
  for (int k = 0; k < n; ++k) {
    std::string s = items[k];
    if (!s.empty() && s[0] == '.') s.erase(0, 1);
    g.drawText(x + 32, 0x60 + 24 * k, s, 15);
  }
  int r = g.select(x, 0x50, n, 0, cancelIdx >= 0 ? 1 : 0);
  if (r < 0 && n > 0) r = cancelIdx;
  g.drawField();  // redraw outside the message window (removes the choice window)
  return r;
}

void Interp::doBattle(int c, int n, uint8_t depth, int& result, bool& end) {
  const uint8_t* a = &w[size_t(p)];
  const int group = a[0];
  const uint16_t bg = core::rd16(a + 1);
  g.waitHeroStill();
  g.updatePages();
  g.closeMessage();
  g.st.battleBg = bg == 0xFFFF ? g.chips.bg[g.chipAt(g.st.x, g.st.y)] : bg;
  int eraseEv = -1;
  if (c == 0x42) eraseEv = self;
  if (c == 0x43) {
    const size_t q = size_t(p + n);
    if (q + 3 < w.size() && w[q] == 0x0A && w[q + 3] == 0xFF) eraseEv = self;
  }
  g.st.eventRunning = 0;
  const int r = g.callBattle(group, c == 0x44 ? 0 : 1, eraseEv);
  if (r == 2) {
    result = 2;
    end = true;
    return;
  }
  g.st.eventRunning = 1;
  if (c == 0x44) {
    if (r == 1) {
      results[depth] = 0;
    } else if (r == 3) {
      results[depth] = 1;
      Member& m = g.members[g.leader()];
      m.status[5] = 0;
      if (m.hp == 0) {
        m.hp = 1;
        m.alive = 1;
      }
    }
  }
}

// 0x3A picture (spec 22 §8 0x3A).
void Interp::picture(int no, bool next) {
  g.waitHeroStill();
  g.vsyncTone(0);
  g.updatePages();
  g.closeMessage();
  char rel[32];
  std::snprintf(rel, sizeof(rel), "EBD_PCT/PCT%03d.EBD", no);
  Picture pic;
  loadPicture(g.root, rel, pic);
  g.scr.clear(0);
  if (pic.valid) {
    std::memcpy(g.curPal, pic.pal8, sizeof(g.curPal));
    std::memcpy(g.scr.pix, pic.px.data(), pic.px.size());
  }
  g.clearTextLayer();
  g.vsyncTone(100);
  g.keyWait();
  g.vsyncTone(0);
  g.scr.clear(0);
  g.fieldMask();
  g.reapplyTone();
  g.drawField();
  if (!next) g.vsyncTone(100);
}

// 0x0E key wait with the field still updating the hero (spec 22 §8 0x0E).
void Interp::keyInputWait() {
  g.waitHeroStill();
  g.updatePages();
  g.closeMessage();
  bool released = false;
  for (;;) {
    g.recordLatch();
    g.waitCounter = 0;
    while (g.waitCounter < 3) g.vsync();
    g.heroProcess();
    g.clearKeys();
    const int d = g.readDirection();
    if (d == 0)
      released = true;
    else if (released)
      break;
  }
  g.clearKeys();
}

void Interp::moveHeroWait(int c) {
  g.waitHeroStill();
  g.clearMoveCommands();
  g.setHeroMoveCommand(c);
  g.updatePages();
  g.closeMessage();
  g.tick(moveWaitTicks(c));
  g.setHeroMoveCommand(0);
}

void Interp::moveEventWait(int e, int c) {
  if (!sameMapAsStart()) return;
  if (e == self) g.run_.movedSelf = true;
  g.waitEventStill(e);
  const bool fix1 = g.eventExists(e) && g.map.events[e]->dirFix == 1;
  g.clearMoveCommands();
  if (fix1) g.map.events[e]->dirFix = 1;
  g.setEventMoveCommand(e, c);
  g.updatePages();
  g.closeMessage();
  g.tick(moveWaitTicks(c));
  g.setEventMoveCommand(e, 0);
}

void Interp::finishPlacement() {
  g.updatePages();
  if (g.st.msgOpen)
    g.closeMessage();
  else
    g.drawField();
  while (g.waitCounter < 4) g.vsync();
}

static void resetRuntimeForPlacement(EventRec* e) {
  std::memset(e->dirProg, 0, sizeof(e->dirProg));
  e->prog = 0;
  e->page1 = 0;
}

void Interp::placeEvent(int e, int x, int y) {
  if (!sameMapAsStart()) return;
  g.waitHeroStill();
  g.waitCounter = 0;
  if (EventRec* r = g.ev(e)) {
    g.eraseEvent(e, false);
    const int other = g.lowerMap[y % kMapDim][x % kMapDim];
    if (other < kMaxEvents && g.eventExists(other)) g.eraseEvent(other, false);
    r->x = uint8_t(x);
    r->y = uint8_t(y);
    resetRuntimeForPlacement(r);
    g.updatePages();
  }
  if (e == self) g.run_.movedSelf = true;
  finishPlacement();
}

void Interp::swapEvents(int a, int b) {
  if (!sameMapAsStart()) return;
  g.waitHeroStill();
  g.waitCounter = 0;
  EventRec* ra = g.ev(a);
  EventRec* rb = g.ev(b);
  if (ra && rb) {
    g.eraseEvent(a, false);
    g.eraseEvent(b, false);
    std::swap(ra->x, rb->x);
    std::swap(ra->y, rb->y);
    resetRuntimeForPlacement(ra);
    resetRuntimeForPlacement(rb);
    g.updatePages();
  }
  if (a == self || b == self) g.run_.movedSelf = true;
  finishPlacement();
}

int Interp::run() {
  int result = 1;
  while (step(result)) {
  }
  return result;
}

// One iteration of spec 22 §5.1. Returns false when execution ends.
bool Interp::step(int& result) {
  g.checkStop();
  gameOverCheck();
  if (remaining() < 3) return false;
  int c = w[size_t(p)];
  const int n = w[size_t(p) + 1];
  const uint8_t depth = w[size_t(p) + 2];
  const long head = p;
  p += 3;
  if (c > 0x72) c = 0x0F;
  const uint8_t* a = &w[size_t(p)];
  Game& G = g;
  switch (c) {
    case 0x00:
    case 0x0B:
      return false;

    case 0x01:
      text(n);
      return true;

    case 0x02:
    case 0x03: {
      const int sw = core::rd16(a);
      if (sw <= 1000) G.switches[sw] = c == 0x02 ? 1 : 0;
    } break;

    case 0x04: G.vars[a[0] % 100] = a[1]; break;
    case 0x05: G.vars[a[0] % 100] = int16_t(std::min(99, G.vars[a[0] % 100] + a[1])); break;
    case 0x06: G.vars[a[0] % 100] = int16_t(std::max(0, G.vars[a[0] % 100] - a[1])); break;
    case 0x07: G.vars[a[0] % 100] = int16_t(G.rng.next() % (a[1] + 1)); break;

    case 0x08: {
      G.waitHeroStill();
      const int r = choice({sj("はい"), sj("いいえ")}, 1);
      results[depth] = int8_t(r);
    } break;

    case 0x09: {
      G.waitHeroStill();
      std::vector<std::string> items;
      long q = p + n;
      while (items.size() < 4 && long(size) - q >= 3 && w[size_t(q)] == 0x69) {
        const int m = w[size_t(q) + 1];
        std::string s = str(q + 3, m);
        const size_t nul = s.find('\0');
        if (nul != std::string::npos) s.resize(nul);
        items.push_back(s);
        q += 3 + m;
      }
      int cancelIdx = -1;
      for (int k = 0; k < int(items.size()); ++k)
        if (!items[k].empty() && items[k][0] == '.') cancelIdx = k;
      const int r = choice(items, cancelIdx);
      results[depth] = int8_t(r);
      p = q;
      return true;
    }

    case 0x0A: G.eraseEvent(target(a[0]), true); break;

    case 0x0C: labels[a[0] % 5] = p; break;
    case 0x0D: {
      const long l = labels[a[0] % 5];
      if (l >= 0) {
        p = l + n;
      } else {
        p = 0;
      }
      return true;
    }

    case 0x0E: keyInputWait(); break;

    case 0x10: {
      G.clearMoveCommands();
      G.waitHeroStill();
      G.closeMessage();
      G.st.curMap = core::rd16(a);
      G.st.x = a[2];
      G.st.y = a[3];
      G.enterMap();
      G.drawField();
      if (G.st.curMap != G.run_.startMap) std::memcpy(G.run_.savedBattleBgm, G.map.hdr.battleBgm, 9);
    } break;

    case 0x11:
      if (G.st.vehicle == 1) {
        G.clearMoveCommands();
        G.waitHeroStill();
        G.closeMessage();
        G.st.curMap = core::rd16(a);
        G.st.x = a[2];
        G.st.y = a[3];
        G.enterMap();
        G.drawField();
        if (G.st.curMap != G.run_.startMap) std::memcpy(G.run_.savedBattleBgm, G.map.hdr.battleBgm, 9);
      } else {
        G.st.shipMap = core::rd16(a);
        G.st.shipX = a[2];
        G.st.shipY = a[3];
        G.lowerMap[a[3] % kMapDim][a[2] % kMapDim] = 0xFF;
      }
      break;

    case 0x12:
      G.st.airMap = core::rd16(a);
      G.st.airX = a[2];
      G.st.airY = a[3];
      G.lowerMap[a[3] % kMapDim][a[2] % kMapDim] = 0xFF;
      break;

    case 0x13:
      for (int k = 0; k < a[0]; ++k) G.waitMs(100);
      break;

    case 0x14: G.addItem(a[0]); break;
    case 0x15: G.removeItem(a[0]); break;
    case 0x16: G.addGold(long(core::rd32(a))); break;
    case 0x17: G.subGold(long(core::rd32(a))); break;

    case 0x18: {
      const int ch = a[0], item = a[1];
      const int slot = int(G.db.items[item].kind) - 1;
      if (ch < 12 && slot >= 0 && slot <= 4) {
        G.members[ch].equip[slot] = uint8_t(item);
        G.recalc(G.members[ch]);
      }
    } break;

    case 0x19: {
      const int ch = a[0], s = a[1];
      if (ch < 12 && s < 5) {
        Member& m = G.members[ch];
        if (m.equip[s] != 0xFF) G.addItem(m.equip[s]);
        m.equip[s] = 0xFF;
        G.recalc(m);
      }
    } break;

    case 0x1A:
      if (a[0] < 12) G.learnMagic(G.members[a[0]], a[1]);
      break;
    case 0x1B:
      if (a[0] < 12) G.forgetMagic(G.members[a[0]], a[1]);
      break;

    case 0x1C:
      if (a[0] < 12) G.joinParty(a[0]);
      G.reloadHeroSprite();
      break;
    case 0x1D:
      if (a[0] < 12) G.leaveParty(a[0]);
      G.reloadHeroSprite();
      break;

    case 0x1E: {
      const uint32_t v = core::rd32(a + 1);
      forTargets(G, a[0], [&](Member& m) {
        m.exp = uint32_t(std::min<uint64_t>(9999999, uint64_t(m.exp) + v));
        G.levelUp(m);
      });
    } break;
    case 0x1F: {
      const uint32_t v = core::rd32(a + 1);
      forTargets(G, a[0], [&](Member& m) {
        m.exp = m.exp > v ? m.exp - v : 0;
        G.levelDown(m);
      });
    } break;

    case 0x20: {
      const int k = a[1];
      forTargets(G, a[0], [&](Member& m) {
        const int L = std::min(int(m.level) + k, int(m.maxLevel));
        m.exp = uint32_t(Game::expForLevel(m, L));
        G.levelUp(m);
      });
    } break;
    case 0x21: {
      const int k = a[1];
      forTargets(G, a[0], [&](Member& m) {
        const int L = std::max(int(m.level) - k, 1);
        m.exp = uint32_t(Game::expForLevel(m, L));
        G.levelDown(m);
      });
    } break;

    case 0x22: {
      const int v = core::rd16(a + 1);
      forTargets(G, a[0], [&](Member& m) { G.addHp(m, v); });
    } break;
    case 0x23: {
      const int v = core::rd16(a + 1);
      forTargets(G, a[0], [&](Member& m) {
        if (v < m.hp)
          m.hp = int16_t(m.hp - v);
        else if (m.hp > 0)
          m.hp = 1;
      });
    } break;
    case 0x24: {
      const int v = core::rd16(a + 1);
      forTargets(G, a[0], [&](Member& m) { m.mp = int16_t(std::min(int(m.mp) + v, int(m.maxMp))); });
    } break;
    case 0x25: {
      const int v = core::rd16(a + 1);
      forTargets(G, a[0], [&](Member& m) {
        int16_t r = int16_t(int(m.mp) - v);
        if (r < 1) r = 0;
        m.mp = r;
      });
    } break;

    case 0x26:
    case 0x27:
      forTargets(G, a[0], [&](Member& m) { m.status[0] = c == 0x26 ? 1 : 0; });
      G.updatePoison();
      break;
    case 0x28:
    case 0x29:
      forTargets(G, a[0], [&](Member& m) { m.status[5] = c == 0x28 ? 1 : 0; });
      break;
    case 0x2A:
      forTargets(G, a[0], [&](Member& m) {
        m.hp = 0;
        m.alive = 0;
        std::memset(m.status, 0, sizeof(m.status));
      });
      G.updatePoison();
      break;
    case 0x2B:
      forTargets(G, a[0], [&](Member& m) {
        if (m.hp == 0) {
          m.hp = 1;
          m.alive = 1;
        }
      });
      break;
    case 0x2C:
      forTargets(G, a[0], [&](Member& m) { G.fullHeal(m); });
      G.updatePoison();
      break;

    case 0x2D: {
      const uint16_t mapNo = core::rd16(a);
      bool done = false;
      for (int k = 0; k < G.st.warpCount && k < 30; ++k) {
        WarpPoint& wp = G.st.warps[k];
        if (wp.map == mapNo) {
          wp.x = a[2];
          wp.y = a[3];
          // PROVISIONAL(Q4): only the 8 name bytes are replaced.
          std::memcpy(wp.bgm, G.st.curBgm, 8);
          done = true;
          break;
        }
      }
      if (!done && G.st.warpCount < 30) {
        WarpPoint& wp = G.st.warps[G.st.warpCount++];
        std::memset(&wp, 0, sizeof(wp));
        wp.map = mapNo;
        wp.x = a[2];
        wp.y = a[3];
        std::memcpy(wp.bgm, G.st.curBgm, 8);
      }
    } break;

    case 0x2E: {
      const uint16_t mapNo = core::rd16(a);
      const int cnt = std::min<int>(G.st.warpCount, 30);
      for (int k = 0; k < cnt; ++k)
        if (G.st.warps[k].map == mapNo) {
          for (int j = k; j + 1 < cnt; ++j) G.st.warps[j] = G.st.warps[j + 1];
          // PROVISIONAL(Q4): the freed last entry is cleared.
          std::memset(&G.st.warps[cnt - 1], 0, sizeof(WarpPoint));
          --G.st.warpCount;
          break;
        }
    } break;

    case 0x2F: G.st.noWarp = a[0] == 0 ? 1 : 0; break;

    case 0x30:
      G.st.escMap = core::rd16(a);
      G.st.escX = a[2];
      G.st.escY = a[3];
      std::memcpy(G.st.escBgm, G.st.curBgm, 8);
      G.st.escBgm[8] = 0;
      break;

    case 0x31: G.st.noEscape = a[0] == 0 ? 1 : 0; break;

    case 0x32: G.flash(); break;
    case 0x33: G.shake(); break;

    case 0x34:
      if (a[0] == 0)
        G.setTone(0);
      else
        G.blackOut(a[0]);
      G.frameCounter = 0;
      break;
    case 0x35:
      G.blackIn(a[0]);
      G.frameCounter = 0;
      break;
    case 0x36: G.toneChange(a[0], a[1]); break;
    case 0x37: G.toneChange(-1, a[0]); break;
    case 0x38:
      if (a[0] == 0)
        G.setTone(200);
      else
        G.whiteOut(a[0]);
      G.frameCounter = 0;
      break;
    case 0x39:
      G.whiteIn(a[0]);
      G.frameCounter = 0;
      break;

    case 0x3A: {
      const size_t q = size_t(p + n);
      const bool next = long(size) - long(q) >= 3 && w[q] == 0x3A;
      picture(core::rd16(a), next);
    } break;

    case 0x3B: G.st.noReorder = a[0] == 0 ? 1 : 0; break;

    case 0x3C:
    case 0x3D: {
      G.waitHeroStill();
      std::vector<uint8_t> goods;
      long q = p + n;
      while (goods.size() < 6 && long(size) - q >= 3 && w[size_t(q)] == 0x70) {
        goods.push_back(w[size_t(q) + 3]);
        q += 4;
      }
      if (c == 0x3C)
        shopBuySell(G, goods);
      else
        shopBuy(G, goods);
      p = q;
      return true;
    }

    case 0x3E:
      G.waitHeroStill();
      shopSell(G);
      break;
    case 0x3F:
      G.waitHeroStill();
      inn(G, core::rds16(a));
      break;

    case 0x42:
    case 0x43:
    case 0x44: {
      bool end = false;
      doBattle(c, n, depth, result, end);
      if (end) return false;
    } break;

    case 0x46: {
      char name[9] = {};
      std::memcpy(name, a, 8);
      std::memcpy(G.map.hdr.battleBgm, name, 9);
      if (!Game::bgmEqual(name, G.st.curBgm)) {
        if (G.st.vehicle == 0) std::memcpy(G.st.prevBgm, G.st.curBgm, 9);
        std::memcpy(G.st.curBgm, name, 9);
        G.playBgm(G.st.curBgm);
      }
    } break;
    case 0x47:
      std::memcpy(G.map.hdr.battleBgm, G.st.prevBgm, 9);
      if (!Game::bgmEqual(G.st.curBgm, G.st.prevBgm)) {
        std::memcpy(G.st.curBgm, G.st.prevBgm, 9);
        G.playBgm(G.st.curBgm);
      }
      break;
    case 0x48: G.sfx(a[0]); break;

    case 0x4A: moveHeroWait(a[0]); break;
    case 0x4B: moveEventWait(target(a[0]), a[1]); break;

    case 0x4C:
      if (a[0] < 12) {
        G.members[a[0]].chrFile = core::rd16(a + 1);
        G.members[a[0]].chrPat = a[3];
      }
      G.reloadHeroSprite();
      break;
    case 0x4D:
      G.st.shipChr = core::rd16(a);
      G.st.shipPat = a[2];
      G.reloadVehicleSprites();
      break;
    case 0x4E:
      G.st.airChr = core::rd16(a);
      G.st.airPat = a[2];
      G.reloadVehicleSprites();
      break;

    case 0x4F:
      G.updatePages();
      G.closeMessage();
      G.tick(2);
      break;

    case 0x50: placeEvent(target(a[0]), a[2], a[3]); break;
    case 0x51: swapEvents(a[0], a[1]); break;

    case 0x52: G.st.hidden = 1; break;
    case 0x53: G.st.hidden = 0; break;

    case 0x55: G.setHeroMoveCommand(a[0]); break;
    case 0x56:
      if (sameMapAsStart()) {
        const int e = target(a[0]);
        G.setEventMoveCommand(e, a[2]);
        if (e == self) G.run_.movedSelf = true;
      }
      break;

    case 0x57:
      G.updatePages();
      G.closeMessage();
      G.tick(a[0] * 4);
      break;
    case 0x58:
      G.updatePages();
      G.closeMessage();
      G.tick(1);
      break;
    case 0x59: G.clearMoveCommands(); break;

    case 0x5A:
      if (!evalCondition(G, a)) {
        p = long(skipTo(w, size, size_t(head + 8), 0x6F, 0x6F, depth));
        return true;
      }
      break;

    case 0x61: throw ReturnToTitle();
    case 0x62:
      G.stopBgm();
      throw QuitProgram();

    // branch headings (spec 22 §5.3)
    case 0x66:
      if (results[depth] != 0) {
        p = long(skipTo(w, size, size_t(p + n), 0x67, 0x68, depth));
        return true;
      }
      break;
    case 0x67:
      if (results[depth] != 1) {
        p = long(skipTo(w, size, size_t(p + n), 0x68, 0x68, depth));
        return true;
      }
      break;
    case 0x6A:
    case 0x6B:
    case 0x6C:
    case 0x6D:
      if (results[depth] != c - 0x6A) {
        p = long(skipTo(w, size, size_t(p + n), c + 1, 0x6E, depth));
        return true;
      }
      break;
    case 0x71:
      if (results[depth] != 0) {
        p = long(skipTo(w, size, size_t(p + n), 0x72, 0x73, depth));
        return true;
      }
      break;
    case 0x72:
      if (results[depth] != 1) {
        p = long(skipTo(w, size, size_t(p + n), 0x73, 0x73, depth));
        return true;
      }
      break;

    default:
      // no-op commands: 0x0F, 0x40, 0x41, 0x45, 0x49, 0x54, 0x5B..0x60, 0x63, 0x64, 0x65, 0x68,
      // 0x69, 0x6E, 0x6F, 0x70, 0x73 (and 0x74..0xFF mapped to 0x0F).
      break;
  }
  p += n;
  return true;
}

}  // namespace

int moveWaitTicks(int c) {
  static const int t[20] = {0, 0, 0, 4, 4, 2, 4, 4, 2, 4, 4, 2, 4, 4, 2, 12, 12, 8, 235, 183};
  return (c >= 0 && c < 20) ? t[c] : 0;
}

size_t skipTo(const std::vector<uint8_t>& w, size_t size, size_t q, int A, int B, uint8_t D) {
  long pos = long(q);
  while (long(size) - pos >= 3) {
    const int c = w[size_t(pos)];
    if ((c == A || c == B) && w[size_t(pos) + 2] == D) return size_t(pos);
    pos += 3 + w[size_t(pos) + 1];
  }
  return size - 3;
}

bool evalCondition(Game& g, const uint8_t* a) {
  const int a1 = a[1], a2 = a[2], a3 = a[3];
  const int w12 = a1 + a2 * 256;
  const int w23 = a2 + a3 * 256;
  const uint32_t d1234 = uint32_t(a1) | (uint32_t(a2) << 8) | (uint32_t(a3) << 16) | (uint32_t(a[4]) << 24);
  auto evRef = [&](int e) -> EventRec* { return g.ev(e == 0xFF ? int(g.st.pendingEvent) : e); };
  switch (a[0]) {
    case 0: return w12 <= 1000 && g.switches[w12] != 0;
    case 1: return w12 <= 1000 && g.switches[w12] == 0;
    case 2: return g.vars[a1 % 100] == a2;
    case 3: return g.vars[a1 % 100] >= a2;
    case 4: return g.vars[a1 % 100] <= a2;
    case 5: return g.st.dir == a1 * 2 + 2;
    case 6: {
      EventRec* e = evRef(a1);
      return e && e->dir == a2 * 2 + 2;
    }
    case 7: return comparePos(a1, g.st.x, g.st.y, a2);
    case 8: {
      EventRec* e = evRef(a1);
      return e && comparePos(a2, e->x, e->y, a3);
    }
    case 9: return g.hasItem(a1);
    case 10: return g.st.gold >= d1234;
    case 11:
      return memberCond(g, a1, [&](const Member& m) {
        for (int s = 0; s < 5; ++s)
          if (m.equip[s] == a2) return true;
        return false;
      });
    case 12:
      return memberCond(g, a1, [&](const Member& m) {
        for (int s = 0; s < 30; ++s)
          if (m.learned[s] == a2) return true;
        return false;
      });
    case 13: return g.st.partyCount == a1;
    case 14: return g.inParty(a1);
    case 15: return memberCond(g, a1, [&](const Member& m) { return int(m.level) >= w23; });
    case 16: return memberCond(g, a1, [&](const Member& m) { return int(m.hp) >= w23; });
    case 17: return memberCond(g, a1, [&](const Member& m) { return m.hp == m.maxHp; });
    case 18: return memberCond(g, a1, [&](const Member& m) { return int(m.mp) >= w23; });
    case 19: return memberCond(g, a1, [&](const Member& m) { return m.mp == m.maxMp; });
    case 20: return memberCond(g, a1, [&](const Member& m) { return m.status[0] != 0; });
    case 21: return memberCond(g, a1, [&](const Member& m) { return m.status[5] != 0; });
    case 22: return memberCond(g, a1, [&](const Member& m) { return m.alive == 0; });
    case 23: return g.st.shopDone != 0;
    default: return false;
  }
}

int runScript(Game& g, int self, const uint8_t* script, size_t len) {
  Interp it(g, self, script, len);
  return it.run();
}

int runEvent(Game& g, int eventNo, int trigger) {
  const int page = g.activePage(eventNo);
  uint16_t len = 0;
  const uint8_t* src = page >= 0 ? g.map.script(eventNo, page, &len) : nullptr;
  // Copy the script before the start processing: commands may reload the map.
  std::vector<uint8_t> script;
  if (src) script.assign(src, src + len);
  g.beginEvent(eventNo, trigger);
  int result;
  try {
    result = runScript(g, eventNo, script.data(), script.size());
  } catch (EventAbort&) {
    g.st.eventRunning = 0;
    g.waitHeroStill();
    g.drawField();
    g.endEvent();
    return 0;
  }
  g.endEvent();
  return result;
}

}  // namespace d2
