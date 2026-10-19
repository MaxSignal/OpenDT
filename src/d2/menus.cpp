// D2 field menu, item/magic field use, save/load menu, debug menu, shops and inns (spec 23).
#include "d2/menus.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace d2 {

namespace {

std::string sj(const char* utf8) { return core::utf8ToSjis(utf8); }

// printf into a string. The format may contain Shift-JIS text (SJIS bytes never equal '%').
// (va_start needs a non-reference last parameter, so the variadic part takes a C string.)
std::string vfmt(const char* f, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, f);
  std::vsnprintf(buf, sizeof(buf), f, ap);
  va_end(ap);
  return buf;
}
template <class... Args>
std::string fmt(const std::string& f, Args... args) {
  return vfmt(f.c_str(), args...);
}

std::string memberName(const Member& m) { return core::rawName(reinterpret_cast<const uint8_t*>(m.name), 20); }
std::string itemName(const Game& g, int a) {
  return core::rawName(reinterpret_cast<const uint8_t*>(g.db.items[a & 0xFF].name), 16);
}
std::string magicName(const Game& g, int m) {
  return core::rawName(reinterpret_cast<const uint8_t*>(g.db.magics[m & 0xFF].name), 17);
}

const char* kLeftArrow = "\xEB\xB4";
const char* kRightArrow = "\xEB\xB5";

int partyChar(const Game& g, int k) { return g.st.party[k]; }
Member& partyMember(Game& g, int k) {
  int c = g.st.party[k];
  return g.members[c > 11 ? 0 : c];
}

// ---------------------------------------------------------------------------------------------
// 2-column list window (23 §2.3)
enum class LK { Items, Equip, Magic, Warp };

struct Entry {
  bool empty = true;
  int id = 0;           // item / magic number / warp index
  std::string name;     // SJIS
  std::string extra;    // count / MP text
};

struct ListCtx {
  LK kind;
  std::vector<Entry> entries;
  int c = 0, slot = 0;  // equip: character and slot
};

int colGap(LK k) { return k == LK::Warp ? 0xA0 : 0xC0; }
int winWidth(LK k) { return k == LK::Warp ? 0x160 : 0x1A0; }
int eraseWidth(LK k) { return k == LK::Warp ? 0x12 * 8 : 0x16 * 8; }
int rightArrowX(LK k, int x) { return k == LK::Warp ? x + 0x150 : x + 400; }

const Entry& entryAt(const ListCtx& L, int i) {
  static const Entry kEmpty;
  if (i < 0 || i >= int(L.entries.size())) return kEmpty;
  return L.entries[size_t(i)];
}

bool hasMore(const ListCtx& L, int P) {
  if (L.kind == LK::Equip) return !entryAt(L, (P + 2) * 5 - 1).empty;
  return !entryAt(L, (P + 2) * 5).empty;
}

void drawListPage(Game& g, const ListCtx& L, int x, int y, int P) {
  g.vsync();
  for (int col = 0; col < 2; ++col)
    for (int r = 0; r < 5; ++r) {
      const Entry& e = entryAt(L, (P + col) * 5 + r);
      const int nx = x + col * colGap(L.kind) + 0x28, ny = y + 24 * r + 16;
      g.scr.fillRect((nx / 8 - 2) * 8, ny, eraseWidth(L.kind), 16, 0);
      if (e.empty) continue;
      g.drawText(nx, ny, e.name);
      if (!e.extra.empty()) g.drawText(nx + 0x80, ny, e.extra);
    }
  g.drawText(x + 8, y + 0x78, kLeftArrow, P > 0 ? 15 : 0);
  g.drawText(rightArrowX(L.kind, x), y + 0x78, kRightArrow, hasMore(L, P) ? 15 : 0);
}

void equipChange(Game& g, int c, int s, int n);
int waitKey(Game& g);

void equipPreview(Game& g, const ListCtx& L, int P, int pos) {
  const Member& m = g.members[L.c];
  const Entry& e = entryAt(L, P * 5 + pos);
  uint8_t eq[5];
  std::memcpy(eq, m.equip, 5);
  eq[L.slot] = e.empty ? 0xFF : uint8_t(e.id);
  g.scr.fillRect(0x128, 0x60, 40, 0x58, 0);
  for (int i = 0; i < 4; ++i) {
    int v = m.base[i];
    for (int s = 0; s < 5; ++s)
      if (eq[s] != 0xFF) v += g.db.items[eq[s]].mod(i);
    v = std::max(0, std::min(999, v));
    g.drawText(0x128, 0x60 + 24 * i, fmt("%+4d", v - m.stat[i]));
  }
}

// Returns the selected entry id (or 0 for equip), -1 on cancel.
int runList(Game& g, ListCtx& L, int x, int y, int& P, int& pos) {
  // 23 §2.3 step 0 (v1.1): clear the key buffer, wait 1 VSYNC, blink counter 0, then draw.
  g.clearKeys();
  g.vsync();
  g.drawWindow(x, y, winWidth(L.kind), 0x90);
  drawListPage(g, L, x, y, P);
  if (L.kind == LK::Equip) equipPreview(g, L, P, pos);
  int t = 0;
  auto cursorAt = [&](int p, int color) {
    g.drawCursor(x + (p / 5) * colGap(L.kind) + 0x18, y + (p % 5) * 24 + 16, color);
  };
  for (;;) {
    if (t < 4)
      cursorAt(pos, 15);
    else if (t >= 21 && t <= 23)
      cursorAt(pos, 0);
    g.vsync();
    ++t;
    if (t > 30) t = 0;
    g.checkStop();
    const int k = g.popKey();
    if (k == kKeyNone) continue;
    const int oldP = P, oldPos = pos;
    if (k == kKeyEsc) {
      g.sfx(3);
      if (L.kind == LK::Equip) cursorAt(pos, 0);
      return -1;
    }
    if (isOkKey(k)) {
      g.sfx(2);
      const Entry& e = entryAt(L, P * 5 + pos);
      if (L.kind == LK::Equip) {
        cursorAt(pos, 0);
        equipChange(g, L.c, L.slot, e.empty ? 0xFF : e.id);
        return 0;
      }
      if (e.empty) continue;
      cursorAt(pos, 15);
      return e.id;
    }
    if (isUpKey(k)) {
      g.sfx(1);
      pos = pos == 0 ? 4 : (pos == 5 ? 9 : pos - 1);
    } else if (isDownKey(k)) {
      g.sfx(1);
      pos = pos == 4 ? 0 : (pos == 9 ? 5 : pos + 1);
    } else if (isRightKey(k)) {
      g.sfx(1);
      if (pos < 5)
        pos += 5;
      else if (hasMore(L, P))
        ++P;
    } else if (isLeftKey(k)) {
      g.sfx(1);
      if (pos >= 5)
        pos -= 5;
      else if (P > 0)
        --P;
    } else {
      continue;
    }
    if (P != oldP) {
      drawListPage(g, L, x, y, P);
    } else if (pos != oldPos) {
      cursorAt(oldPos, 0);
    } else {
      continue;
    }
    t = 0;
    if (L.kind == LK::Equip) equipPreview(g, L, P, pos);
  }
}

Entry itemEntry(const Game& g, int a, int count) {
  Entry e;
  e.empty = false;
  e.id = a;
  e.name = itemName(g, a);
  e.extra = fmt(sj("：%2d"), count);
  return e;
}

// ---------------------------------------------------------------------------------------------
// item/magic use (23 §9)
struct UseCtx {
  Game& g;
  bool effect = false;
  explicit UseCtx(Game& gg) : g(gg) {}

  void gap() {
    if (effect) g.waitUnits(4);
    effect = true;
  }
  void hpRecover(Member& p, int n) {
    n = std::min(n, 9999);
    gap();
    const std::string hp = g.db.wordAt(0x00);
    if (n == 9999)
      menuMsgLine(g, 1, memberName(p) + sj("の") + hp + sj("が全回復した！"));
    else
      menuMsgLine(g, 1, memberName(p) + sj("の") + hp + sj("が") + std::to_string(n) + sj("回復した！"));
    p.hp = int16_t(std::min(int(p.hp) + n, int(p.maxHp)));
  }
  void mpRecover(Member& p, int n) {
    n = std::min(n, 999);
    gap();
    const std::string mp = g.db.wordAt(0x0B);
    if (n == 999)
      menuMsgLine(g, 1, memberName(p) + sj("の") + mp + sj("が全回復した！"));
    else
      menuMsgLine(g, 1, memberName(p) + sj("の") + mp + sj("が") + std::to_string(n) + sj("回復した！"));
    p.mp = int16_t(std::min(int(p.mp) + n, int(p.maxMp)));
  }
  void stateRecover(Member& p, int s) {
    if (!p.status[s]) return;
    gap();
    p.status[s] = 0;
    menuMsgLine(g, 1, memberName(p) + g.db.wordAt(0x167 + s * 0x84));
  }
  void revive(Member& p, int n) {
    if (p.alive) return;
    gap();
    p.alive = 1;
    p.hp = int16_t(std::min(n == 0 ? 1 : n, int(p.maxHp)));
    menuMsgLine(g, 1, memberName(p) + g.db.wordAt(0x47F));
  }
  void magicEffect(const Member& c, Member& p, const Magic& m) {
    if (p.alive) {
      if (m.hasEffect(0)) hpRecover(p, magicPower(g, c, m));
      for (int s = 0; s < 6; ++s)
        if (m.hasEffect(s + 2)) {
          (void)magicPower(g, c, m);
          stateRecover(p, s);
        }
    }
    if (m.hasEffect(8)) revive(p, magicPower(g, c, m));
  }
  void itemEffect(Member& p, const Item& it) {
    const uint8_t cure = it.cureBits();
    if (p.alive) {
      if (!(cure & 0x40) && it.healHp() != 0) hpRecover(p, it.healHp());
      if (it.healMp() != 0) mpRecover(p, it.healMp());
      for (int s = 0; s < 6; ++s)
        if (cure & (1 << s)) stateRecover(p, s);
    }
    if (cure & 0x40) revive(p, it.healHp());
  }
};

// Target selection (23 §9.4). Returns a character number or -1.
int targetSelect(Game& g) {
  drawPartyWindow(g, 0xC0, 0x50);
  const int r = g.select(0xC0, 0x50, g.st.partyCount, 0, 1);
  return r < 0 ? -1 : partyChar(g, r);
}

// Warp destination selection (23 §9.1 step 2). Returns false on cancel.
bool warpSelect(Game& g) {
  if (g.st.warpCount == 0) {
    g.st.warpSel = 0xFF;
    return true;
  }
  const int r = warpListWindow(g, 0xC0, 0x38);
  if (r < 0) return false;
  g.st.warpSel = uint8_t(r);
  return true;
}

bool warpAllowed(const Game& g) { return g.st.warpCount >= 1 && !g.st.noWarp && g.st.vehicle == 0; }
bool escapeAllowed(const Game& g) { return !g.st.noEscape && g.st.vehicle == 0; }

void useItem(Game& g, int a) {
  const Item& it = g.db.items[a & 0xFF];
  const int k = it.kind;
  const int user = g.leader();
  const int m = it.magicNo();
  const Magic& mg = g.db.magics[m];
  int target = -1;
  // 1. target
  if (k == 6 || k == 9 || (k == 8 && mg.target == 0)) {
    target = targetSelect(g);
    if (target < 0) return;
  }
  // 2. warp destination
  if (k == 8 && mg.target == 4 && !warpSelect(g)) return;
  // 3. magic book
  if (k == 9) {
    Member& p = g.members[target];
    menuMsgOpen(g);
    int found = 0;  // 1 learned, 2 learnable
    for (int i = 0; i < 30; ++i) {
      if (p.learned[i] == m) {
        found = 1;
        break;
      }
      if (p.magicList[i] == m) {
        found = 2;
        break;
      }
    }
    if (found == 1) {
      menuMsgLine(g, 0, sj("すでに覚えている。"));
    } else if (found == 2) {
      g.sfx(8);
      menuMsgLine(g, 0, memberName(p) + sj("は") + magicName(g, m) + sj("を覚えた！"));
      g.learnMagic(p, m);
      if (it.consumable()) g.removeItem(a);
    } else {
      menuMsgLine(g, 0, memberName(p) + sj("は") + itemName(g, a) + sj("を使えない。"));
    }
    g.keyWait();
    return;
  }
  // 4.
  UseCtx u(g);
  menuMsgOpen(g);
  menuMsgLine(g, 0, itemName(g, a) + sj("を使った！"));
  // 5.
  if (k == 8 && mg.target != 2 && mg.target != 3)
    magicFlash(g, mg);
  else
    g.waitUnits(2);
  // 6.
  const Member& userM = g.members[user];
  switch (k) {
    case 8:
      switch (mg.target) {
        case 0: u.magicEffect(userM, g.members[target], mg); break;
        case 1:
          for (int i = 0; i < g.st.partyCount && i < 5; ++i) u.magicEffect(userM, partyMember(g, i), mg);
          break;
        case 4:
          if (warpAllowed(g)) {
            g.blackOut(2);
            g.st.warpReq = 1;
            g.st.eventRunning = 1;
            if (it.consumable()) g.removeItem(a);
            return;
          }
          break;
        case 5:
          if (escapeAllowed(g)) {
            g.blackOut(2);
            g.st.escapeReq = 1;
            g.st.eventRunning = 1;
            if (it.consumable()) g.removeItem(a);
            return;
          }
          break;
        case 6:
          u.effect = true;
          if (mg.switchNo <= 1000) g.switches[mg.switchNo] = 1;
          g.st.eventRunning = 1;
          break;
        default: break;
      }
      break;
    case 6: u.itemEffect(g.members[target], it); break;
    case 7:
      for (int i = 0; i < g.st.partyCount && i < 5; ++i) u.itemEffect(partyMember(g, i), it);
      break;
    case 10:
      u.effect = true;
      if (it.switchNo() <= 1000) g.switches[it.switchNo()] = 1;
      g.st.eventRunning = 1;
      break;
    default: break;
  }
  // 7.
  if (u.effect) {
    if (it.consumable()) g.removeItem(a);
  } else {
    menuMsgLine(g, 1, sj("しかし何も起こらなかった！"));
  }
  // 8.
  g.keyWait();
}

void useMagic(Game& g, int c, int m) {
  const Magic& mg = g.db.magics[m & 0xFF];
  Member& caster = g.members[c];
  int target = -1;
  if (mg.target == 0) {
    target = targetSelect(g);
    if (target < 0) return;
  } else if (mg.target == 4) {
    if (!warpSelect(g)) return;
  }
  UseCtx u(g);
  menuMsgOpen(g);
  menuMsgLine(g, 0, memberName(caster) + core::rawName(reinterpret_cast<const uint8_t*>(mg.message), 39));
  magicFlash(g, mg);
  if (caster.mp < mg.mpCost) {
    menuMsgLine(g, 1, g.db.wordAt(0x0B) + sj("が足りない！"));
    g.keyWait();
    return;
  }
  caster.mp = int16_t(caster.mp - mg.mpCost);
  switch (mg.target) {
    case 0: u.magicEffect(caster, g.members[target], mg); break;
    case 1:
      for (int i = 0; i < g.st.partyCount && i < 5; ++i) u.magicEffect(caster, partyMember(g, i), mg);
      break;
    case 4:
      if (warpAllowed(g)) {
        g.blackOut(2);
        g.st.eventRunning = 1;
        g.st.warpReq = 1;
        return;
      }
      break;
    case 5:
      if (escapeAllowed(g)) {
        g.blackOut(2);
        g.st.eventRunning = 1;
        g.st.escapeReq = 1;
        return;
      }
      break;
    case 6:
      u.effect = true;
      if (mg.switchNo <= 1000) g.switches[mg.switchNo] = 1;
      g.st.eventRunning = 1;
      break;
    default: u.effect = true; break;
  }
  // 6. failure display
  if (!u.effect && mg.target < 4) {
    bool any = false;
    for (int b = 0; b < 20; ++b)
      if (mg.hasEffect(b)) any = true;
    if (!any) u.effect = true;
  }
  if (!u.effect && mg.failKind <= 1) menuMsgLine(g, 1, g.db.wordAt(0x4A0 + (mg.failKind + 9) * 0x1D));
  g.keyWait();
}

// ---------------------------------------------------------------------------------------------
// field menu screen (23 §3)
struct MenuCtx {
  int cmdPos = 0;
  int memberPos = 0;
};

void drawCommandWindow(Game& g, int x, int y) {
  const bool noReorder = g.st.noReorder != 0;
  g.drawWindow(x, y, 120, noReorder ? 120 : 144);
  std::vector<std::string> items = {g.db.wordAt(0xAF), g.db.wordAt(0xA4), sj("装備")};
  if (!noReorder) items.push_back(sj("並び替え"));
  items.push_back(sj("システム"));
  for (size_t k = 0; k < items.size(); ++k) g.drawText(x + 32, y + 16 + 24 * int(k), items[k]);
}

void drawMenuBody(Game& g) {
  g.scr.clear(14);
  drawCommandWindow(g, 0x30, 0x30);
  drawPartyWindow(g, 0xC0, 0x30);
  if (g.testPlay) {
    g.drawWindow(0x30, 0x120, 352, 48);
    const std::string name = core::rawName(reinterpret_cast<const uint8_t*>(g.map.hdr.name), 16);
    g.drawText(72, 304, fmt("MAP=%03d:%-18s(X=%03d,Y=%03d)", int(g.st.curMap), name.c_str(), int(g.st.x), int(g.st.y)));
  }
  g.goldWindow(0x1C8, 0x120);
}

void drawMenuScreen(Game& g) {
  g.vsync();
  g.setTone(0);
  drawMenuBody(g);
  g.vsync();
  g.setTone(100);
}

int memberSelect(Game& g, MenuCtx& mc, int mode = 1) {
  const int r = g.select(0xC0, 0x30, g.st.partyCount, mc.memberPos, mode);
  if (r >= 0) mc.memberPos = r;
  return r;
}

// §4
void itemCommand(Game& g) {
  const int a = itemListWindow(g, 0xB8, 0x28);
  if (a < 0) return;
  const bool canDiscard = g.db.items[a].noDiscard == 0;
  g.drawWindow(0xF8, 0x50, 0x68, canDiscard ? 0x60 : 0x48);
  g.drawText(0x118, 0x60, sj("使う"));
  g.drawText(0x118, 0x78, sj("整理"));
  if (canDiscard) g.drawText(0x118, 0x90, sj("捨てる"));
  const int r = g.select(0xF8, 0x50, canDiscard ? 3 : 2, 0, 1);
  if (r == 0) {
    useItem(g, a);
  } else if (r == 1) {
    g.moveItemToEnd(a);
  } else if (r == 2) {
    g.drawWindow(0x100, 0x70, 0x68, 0x48);
    g.drawText(0x120, 0x80, sj("捨てる"));
    g.drawText(0x120, 0x98, sj("やめる"));
    if (g.select(0x100, 0x70, 2, 1, 1) == 0) g.discardItem(a);
  }
}

// §5
void magicCommand(Game& g, MenuCtx& mc) {
  const int r = memberSelect(g, mc);
  if (r < 0) return;
  const int c = partyChar(g, r);
  if (c > 11) return;
  const Member& m = g.members[c];
  if (!m.alive || m.status[5]) return;
  const int mg = magicListWindow(g, c, 0xB8, 0x28);
  if (mg < 0) return;
  useMagic(g, c, mg);
}

// §6
void equipCommand(Game& g, MenuCtx& mc) {
  const int r = memberSelect(g, mc);
  if (r < 0) return;
  const int c = partyChar(g, r);
  if (c > 11) return;
  int s = 0;
  for (;;) {
    drawStatusWindow(g, c, 0x88, 0x18);
    const int sel = g.select(0x150, 0x38, 5, s, 1);
    if (sel < 0) return;
    s = sel;
    equipListWindow(g, c, s, 0xB0, 200);
  }
}

// §7
void reorderCommand(Game& g, MenuCtx& mc) {
  const int n = g.st.partyCount;
  if (n <= 1) return;
  int k = 0;
  bool used[5] = {false, false, false, false, false};
  int chosen[5] = {0, 0, 0, 0, 0};
  auto drawNumbers = [&](int count) {
    g.scr.fillRect(0xC8, 0x40, 8, n * 24, 0);
    for (int j = 0; j < count; ++j) g.drawText(0xC8, 0x40 + 24 * chosen[j], std::to_string(j + 1));
  };
  while (k < n - 1) {
    drawNumbers(k);
    const int r = memberSelect(g, mc, 2);
    if (r < 0) {
      if (k == 0) return;
      --k;
      used[chosen[k]] = false;
      continue;
    }
    if (!used[r]) {
      chosen[k] = r;
      used[r] = true;
      ++k;
    }
  }
  for (int i = 0; i < n; ++i)
    if (!used[i]) chosen[n - 1] = i;
  drawNumbers(n);
  waitKey(g);  // 23 §7 step 5 (v1.1): look first, VSYNC only when empty; no clear, no STOP check
  uint8_t np[5];
  for (int j = 0; j < n; ++j) np[j] = g.st.party[chosen[j]];
  std::memcpy(g.st.party, np, size_t(n));
  g.reloadHeroSprite();
}

// §8. Returns true when a load succeeded (menu already closed).
bool systemCommand(Game& g) {
  const bool tp = g.testPlay;
  g.drawWindow(0x40, 0x88, tp ? 120 : 104, tp ? 120 : 96);
  std::vector<std::string> items = {sj("セーブ"), sj("ロード")};
  if (tp) items.push_back(sj("デバッグ"));
  items.push_back(sj("終了"));
  for (size_t k = 0; k < items.size(); ++k) g.drawText(0x60, 0x98 + 24 * int(k), items[k]);
  int r = g.select(0x40, 0x88, int(items.size()), 0, 1);
  if (r < 0) return false;
  if (!tp && r == 2) r = 3;
  switch (r) {
    case 0: {
      const int k = g.slotSelect(0x48, 0xA8);
      if (k >= 0) g.saveSlot(k);
      return false;
    }
    case 1: {
      const int k = g.slotSelect(0x48, 0xC0);
      if (k >= 0 && g.loadSlot(k)) {
        closeFieldMenu(g, 1);
        return true;
      }
      return false;
    }
    case 2:
      debugMenu(g);
      return false;
    default:
      g.blackOut(1);
      g.stopBgm();
      throw QuitProgram();
  }
}

void moveToPoint(Game& g, uint16_t map, uint8_t x, uint8_t y, const char* bgm, size_t bgmLen) {
  g.st.curMap = map;
  g.st.x = x;
  g.st.y = y;
  g.st.dir = 2;
  g.enterMap();
  std::memcpy(g.st.prevBgm, g.st.curBgm, 9);
  std::memset(g.st.curBgm, 0, 9);
  std::memcpy(g.st.curBgm, bgm, std::min<size_t>(bgmLen, 9));
  g.playBgm(g.st.curBgm);
}

// ---------------------------------------------------------------------------------------------
// debug menu (§10)
void drawDebugWindow(Game& g) {
  g.drawWindow(0x40, 0x88, 208, 192);
  const char* tilde = "\x81\x60";
  for (int q = 0; q < 5; ++q)
    g.drawText(96, 152 + 24 * q, sj("スイッチ ") + fmt("%03d", q * 200) + tilde + fmt("%03d", q * 200 + 199));
  g.drawText(96, 152 + 24 * 5, sj("変数 00") + tilde + "99");
  g.drawText(96, 152 + 24 * 6, sj(g.st.noEncounter ? "敵出現：オフ" : "敵出現：オン"));
}

void textAt(Game& g, int col, int row, const std::string& s, uint8_t attr) { core::textWriteD2(g.scr, col, row, s, attr, g.gaiji); }

// "Wait until a keystroke is available" (01 §4.3): no buffer clear, no STOP check; the buffer is
// looked at first and a VSYNC is waited only when it is empty.
int waitKey(Game& g) {
  for (;;) {
    const int k = g.popKey();
    if (k != kKeyNone) return k;
    g.vsync();
  }
}

void switchViewer(Game& g, int q) {
  g.drawWindow(144, 0, 352, 383);
  textAt(g, 20, 1, sj("スイッチ ") + fmt("%03d", 200 * q) + "\x81\x60" + fmt("%03d", 200 * q + 199), 0xE1);
  auto cell = [&](int i, bool cursor) {
    const int sw = 200 * q + i;
    textAt(g, (i % 10) * 4 + 21, i / 10 + 3, g.switches[sw] ? fmt("%03d", sw) : std::string("---"), cursor ? 0xE5 : 0xE1);
  };
  int cur = 0;
  for (int i = 0; i < 200; ++i) cell(i, i == cur);
  for (;;) {
    const int k = waitKey(g);
    const int old = cur;
    int col = cur % 10, row = cur / 10;
    if (k == kKeyEsc) break;
    if (isOkKey(k)) {
      uint8_t& s = g.switches[200 * q + cur];
      s = s ? 0 : 1;
    } else if (isRightKey(k)) {
      col = (col + 1) % 10;
    } else if (isLeftKey(k)) {
      col = (col + 9) % 10;
    } else if (isDownKey(k)) {
      row = (row + 1) % 20;
    } else if (isUpKey(k)) {
      row = (row + 19) % 20;
    }
    cur = row * 10 + col;
    cell(old, false);
    cell(cur, true);
  }
  g.fieldMask();
}

void varEditor(Game& g) {
  g.drawWindow(144, 0, 352, 383);
  textAt(g, 20, 1, sj("変数 00") + "\x81\x60" + "99  ", 0xE1);
  auto cell = [&](int i, bool cursor) {
    textAt(g, (i % 5) * 8 + 21, i / 5 + 3, fmt("%02d", i) + "\x81\x63", 0xE1);
    textAt(g, (i % 5) * 8 + 25, i / 5 + 3, fmt("%02d", int(g.vars[i])), cursor ? 0xE5 : 0xE1);
  };
  int cur = 0;
  for (int i = 0; i < 100; ++i) cell(i, i == cur);
  for (;;) {
    const int k = waitKey(g);
    const int old = cur;
    int col = cur % 5, row = cur / 5;
    if (k == kKeyEsc) break;
    if (k == kKeyRollUp) {
      if (g.vars[cur] < 99) ++g.vars[cur];
    } else if (k == kKeyRollDown) {
      if (g.vars[cur] > 0) --g.vars[cur];
    } else if (isRightKey(k)) {
      col = (col + 1) % 5;
    } else if (isLeftKey(k)) {
      col = (col + 4) % 5;
    } else if (isDownKey(k)) {
      row = (row + 1) % 20;
    } else if (isUpKey(k)) {
      row = (row + 19) % 20;
    }
    cur = row * 5 + col;
    cell(old, false);
    cell(cur, true);
  }
  g.fieldMask();
}

// ---------------------------------------------------------------------------------------------
// shops (§11)
void shopMsg(Game& g, int line, int k) { g.messageLine(line, "\\>" + g.db.wordAt(0x670 + k * 0x1D)); }
void shopMsgClear(Game& g) { g.scr.fillRect(96, 256, 320, 40, 0); }

}  // namespace

// =============================================================================================
// exported UI parts

int itemListWindow(Game& g, int x, int y) {
  ListCtx L;
  L.kind = LK::Items;
  L.entries.resize(kInventorySlots);
  for (int i = 0; i < kInventorySlots; ++i)
    if (g.inventory[i][1] != 0) L.entries[size_t(i)] = itemEntry(g, g.inventory[i][0], g.inventory[i][1]);
  return runList(g, L, x, y, g.itemListPage, g.itemListPos);
}

int magicListWindow(Game& g, int c, int x, int y) {
  ListCtx L;
  L.kind = LK::Magic;
  const Member& m = g.members[c];
  for (int i = 0; i < 30; ++i) {
    if (m.learned[i] == 0xFF) continue;
    Entry e;
    e.empty = false;
    e.id = m.learned[i];
    e.name = magicName(g, e.id);
    e.extra = fmt("[%2d]", int(g.db.magics[e.id].mpCost));
    L.entries.push_back(e);
  }
  L.entries.resize(30);
  return runList(g, L, x, y, g.magicListPage[c], g.magicListPos[c]);
}

int warpListWindow(Game& g, int x, int y) {
  ListCtx L;
  L.kind = LK::Warp;
  L.entries.resize(30);
  const int count = std::min<int>(g.st.warpCount, 30);  // corrupt counts above 30 -> 30 (23 §2.3 v1.1)
  for (int i = 0; i < count; ++i) {
    const WarpPoint& w = g.st.warps[i];
    if (w.map == 0xFFFF) continue;
    Entry& e = L.entries[size_t(i)];
    e.empty = false;
    e.id = i;
    e.name = core::rawName(reinterpret_cast<const uint8_t*>(g.db.mapNames[w.map % 1000]), 16);
  }
  const int r = runList(g, L, x, y, g.warpListPage, g.warpListPos);
  return r < 0 ? -1 : g.warpListPage * 5 + g.warpListPos;
}

int equipListWindow(Game& g, int c, int slot, int x, int y) {
  ListCtx L;
  L.kind = LK::Equip;
  L.c = c;
  L.slot = slot;
  for (int i = 0; i < kInventorySlots; ++i) {
    if (g.inventory[i][1] == 0) continue;
    const int a = g.inventory[i][0];
    const Item& it = g.db.items[a];
    if (it.kind == slot + 1 && ((it.equipMask() >> c) & 1)) L.entries.push_back(itemEntry(g, a, g.inventory[i][1]));
  }
  L.entries.resize(kInventorySlots);
  int P = 0, pos = 0;
  return runList(g, L, x, y, P, pos);
}

namespace {
void equipChange(Game& g, int c, int s, int n) {
  Member& m = g.members[c];
  const int o = m.equip[s];
  m.equip[s] = uint8_t(n);
  if (o != 0xFF) g.addItem(o);
  if (n != 0xFF) g.removeItem(n);
  g.recalc(m);
}
}  // namespace

void drawPartyWindow(Game& g, int x, int y) {
  const int n = g.st.partyCount;
  if (n == 0) return;
  g.drawWindow(x, y, 400, n * 24 + 24);
  for (int k = 0; k < n && k < 5; ++k) {
    const Member& m = partyMember(g, k);
    const int yy = y + 16 + 24 * k;
    g.drawText(x + 32, yy, memberName(m));
    std::string st;
    if (!m.alive) {
      st = g.db.wordAt(0xC5 + 9 * 6);
    } else {
      int found = -1;
      for (int s = 5; s >= 0 && found < 0; --s)
        if (m.status[s]) found = s;
      st = found >= 0 ? g.db.wordAt(0xC5 + 9 * found) : fmt(sj("Ｌ%2d"), int(m.level));
    }
    g.drawText(x + 32 + 0x88, yy, st);
    g.drawText(x + 32 + 200, yy, fmt("%4d/%4d%5d/%3d", int(m.hp), int(m.maxHp), int(m.mp), int(m.maxMp)));
  }
}

void drawStatusWindow(Game& g, int c, int x, int y) {
  Member& m = g.members[c];
  g.recalcLevel(m);
  g.drawWindow(x, y, 0x1D0, 0xE8);
  g.drawText(x + 0x18, y + 0x10, memberName(m));
  g.drawText(x + 0x58, y + 0x28, fmt(sj("レベル%3d"), int(m.level)));
  static const int kWord[4] = {0x16, 0x21, 0x2C, 0x37};
  for (int i = 0; i < 4; ++i) {
    g.drawText(x + 0x18, y + 0x48 + 0x18 * i, g.db.wordAt(kWord[i]));
    g.drawText(x + 0x68, y + 0x48 + 0x18 * i, fmt(sj("：%4d (%3d)"), int(m.stat[i]), int(m.base[i])));
  }
  g.drawText(x + 0x18, y + 0xB0, g.db.wordAt(0x00));
  g.drawText(x + 0x68, y + 0xB0, fmt(sj("：%4d/%4d"), int(m.hp), int(m.maxHp)));
  g.drawText(x + 0x18, y + 0xC8, g.db.wordAt(0x0B));
  g.drawText(x + 0x68, y + 0xC8, fmt(sj("：%4d/%4d"), int(m.mp), int(m.maxMp)));
  for (int k = 0; k < 5; ++k) {
    g.drawText(x + 0xE8, y + 0x30 + 24 * k, g.db.wordAt(0x42 + 9 * k));
    g.drawText(x + 0x128, y + 0x30 + 24 * k, sj("：") + itemName(g, m.equip[k]));
  }
  g.drawText(x + 0xE8, y + 0xB0, fmt(sj("経験値%20lld"), (long long)m.exp));
  const long long rest = m.nextExp == 0 ? 0 : (long long)m.nextExp - (long long)m.exp;
  g.drawText(x + 0xE8, y + 0xC8, fmt(sj("次のレベルまで%12lld"), rest));
}

void menuMsgOpen(Game& g) { g.drawWindow(80, 256, 480, 96); }

void menuMsgLine(Game& g, int n, const std::string& s) {
  g.scr.fillRect(104, 272 + 24 * n, 432, 16, 0);
  g.drawText(104, 272 + 24 * n, s);
}

int choiceWindow(Game& g, const std::vector<std::string>& items, int cancelIndex) {
  const int n = int(items.size());
  int w = 0;
  for (auto& s : items) {
    int len = int(std::strlen(s.c_str()));
    w = std::max(w, std::min(len, 32));
  }
  const int x = 0x200 - 8 * w;
  g.drawWindow(x, 0x50, 0x230 - x, n * 24 + 24);
  for (int k = 0; k < n; ++k) g.drawText(x + 32, 0x60 + 24 * k, items[size_t(k)]);
  if (n == 0) return -1;
  const int r = g.select(x, 0x50, n, 0, cancelIndex >= 0 ? 1 : 0);
  return r < 0 ? cancelIndex : r;
}

int yesNoWindow(Game& g) { return choiceWindow(g, {sj("はい"), sj("いいえ")}, 1); }

int variance(Game& g, int v, int p) {
  if (p <= 0) return v;
  v += g.rng.next() % 3;
  const int r = v * p / 12;
  if (r > 1) v += r / 2 - (g.rng.next() % r);
  return v;
}

int magicPower(Game& g, const Member& c, const Magic& m) {
  int v = m.power;
  if (m.atkCoef) v += int(int16_t(c.stat[0] * m.atkCoef)) / 10;
  if (m.spiCoef) v += int(int16_t(c.stat[2] * m.spiCoef)) / 10;
  if (v <= 1)
    v = 1;
  else
    v = variance(g, v, m.variance);
  if (v > 30000) v = 30000;
  return v;
}

void magicFlash(Game& g, const Magic& m) {
  g.sfx(m.sfx);
  if (!m.flash) {
    g.waitUnits(3);
    return;
  }
  uint8_t saved[16][3];
  std::memcpy(saved, g.curPal, sizeof(saved));
  for (int n = 0; n < 10; ++n) {
    for (int i = 0; i < 16; ++i)
      for (int k = 0; k < 3; ++k) g.curPal[i][k] = uint8_t((m.flashRgb[k] & 0x0F) * 0x11);
    g.waitFrames(2);
    g.applyPalette();
    std::memcpy(g.curPal, saved, sizeof(saved));
    g.vsync();
    g.applyPalette();
  }
}

void closeFieldMenu(Game& g, int f) {
  g.updatePoison();
  g.updatePages();
  g.vsync();
  g.setTone(0);
  g.drawField();
  g.vsync();
  if (f)
    g.blackIn(f);
  else
    g.setTone(100);
  g.clearKeys();
}

void debugMenu(Game& g) {
  int pos = 0;
  drawDebugWindow(g);
  for (;;) {
    const int r = g.select(0x40, 0x88, 7, pos, 1);
    if (r < 0) return;
    pos = r;
    if (r == 6) {
      g.st.noEncounter ^= 1;
      g.setTone(0);
      drawDebugWindow(g);
      g.setTone(100);
      continue;
    }
    if (r < 5)
      switchViewer(g, r);
    else
      varEditor(g);
    g.setTone(0);
    drawMenuBody(g);
    drawDebugWindow(g);
    g.setTone(100);
  }
}

// =============================================================================================
// hooks

void fieldMenu(Game& g) {
  MenuCtx mc;
  g.itemListPage = g.itemListPos = 0;
  for (int i = 0; i < 12; ++i) g.magicListPage[i] = g.magicListPos[i] = 0;
  drawMenuScreen(g);
  g.sfx(2);
  try {
    for (;;) {
      const bool noReorder = g.st.noReorder != 0;
      int sel = g.select(0x30, 0x30, noReorder ? 4 : 5, mc.cmdPos, 1);
      if (sel < 0) {
        closeFieldMenu(g, 0);
        return;
      }
      mc.cmdPos = sel;
      if (noReorder && sel == 3) sel = 4;
      switch (sel) {
        case 0: itemCommand(g); break;
        case 1: magicCommand(g, mc); break;
        case 2: equipCommand(g, mc); break;
        case 3: reorderCommand(g, mc); break;
        default:
          if (systemCommand(g)) return;
          break;
      }
      if (g.st.eventRunning) {
        g.st.eventRunning = 0;
        if (g.st.escapeReq) {
          g.st.escapeReq = 0;
          moveToPoint(g, g.st.escMap, g.st.escX, g.st.escY, g.st.escBgm, 9);
        }
        if (g.st.warpReq) {
          g.st.warpReq = 0;
          const int i = g.st.warpSel;
          if (i < 30) {
            const WarpPoint w = g.st.warps[i];
            moveToPoint(g, w.map, w.x, w.y, w.bgm, 8);
          }
        }
        closeFieldMenu(g, 0);
        return;
      }
      drawMenuScreen(g);
    }
  } catch (EventAbort&) {
    // STOP during the key wait after a switch-type item/magic (23 §3.5 step 6, v1.1): treated as
    // the end of that key wait -> flag cleared, menu closed without fade.
    g.st.eventRunning = 0;
    closeFieldMenu(g, 0);
  }
}

void shopBuy(Game& g, const std::vector<uint8_t>& goods) {
  if (goods.empty()) return;
  g.openMessage();
  g.goldWindow(0x198, 0x130);
  shopMsg(g, 0, 1);
  const int n = int(goods.size());
  g.drawWindow(0x50, 0x30, 0xE0, n * 24 + 24);
  for (int i = 0; i < n; ++i) {
    g.drawText(0x70, 0x40 + 24 * i, itemName(g, goods[size_t(i)]));
    g.drawText(0xF0, 0x40 + 24 * i, fmt("%6d", int(g.db.items[goods[size_t(i)]].price)));
  }
  int pos = 0, t = 0;
  for (;;) {
    const int cx = 0x60, cy = 0x40 + 24 * pos;
    if (t < 4)
      g.drawCursor(cx, cy, 15);
    else if (t >= 21 && t <= 23)
      g.drawCursor(cx, cy, 0);
    g.vsync();
    ++t;
    if (t > 30) t = 0;
    g.checkStop();
    const int k = g.popKey();
    if (k == kKeyNone) continue;
    if (k == kKeyEsc) {
      g.sfx(3);
      g.drawField();
      return;
    }
    if (isOkKey(k)) {
      g.sfx(2);
      g.drawCursor(cx, cy, 15);
      shopMsgClear(g);
      const int a = goods[size_t(pos)];
      const uint32_t price = g.db.items[a].price;
      g.embed = itemName(g, a);
      if (g.st.gold >= price) {
        if (g.addItem(a)) {
          g.subGold(long(price));
          g.goldWindow(0x198, 0x130);
          shopMsg(g, 0, 5);
          g.st.shopDone = 1;
        } else {
          shopMsg(g, 0, 8);
        }
      } else {
        shopMsg(g, 0, 7);
      }
      g.keyWaitMark();
      shopMsgClear(g);
      shopMsg(g, 0, 1);
      t = 0;
    } else if (isUpKey(k) || isDownKey(k)) {
      g.sfx(1);
      g.drawCursor(cx, cy, 0);
      if (isUpKey(k))
        pos = pos == 0 ? n - 1 : pos - 1;
      else
        pos = pos == n - 1 ? 0 : pos + 1;
      t = 0;
    } else if (isLeftKey(k) || isRightKey(k)) {
      g.sfx(1);
    }
  }
}

void shopSell(Game& g) {
  g.openMessage();
  g.goldWindow(0x198, 0x130);
  for (;;) {
    shopMsg(g, 0, 2);
    const int a = itemListWindow(g, 0x90, 0x30);
    if (a < 0) break;
    shopMsgClear(g);
    const uint32_t price = g.db.items[a].price;
    if (price == 0) {
      shopMsg(g, 0, 9);
      g.keyWaitMark();
      shopMsgClear(g);
      continue;
    }
    uint32_t sell = price / 2;
    if (sell == 0) sell = 1;
    g.embed = std::to_string(sell);
    shopMsg(g, 0, 3);
    shopMsg(g, 1, 4);
    const int r = yesNoWindow(g);
    shopMsgClear(g);
    if (r == 0) {
      g.embed = itemName(g, a);
      g.removeItem(a);
      g.addGold(long(sell));
      g.goldWindow(0x198, 0x130);
      shopMsg(g, 0, 6);
      g.keyWaitMark();
      shopMsgClear(g);
      g.st.shopDone = 1;
    }
    g.drawField();
  }
  g.drawField();
}

void shopBuySell(Game& g, const std::vector<uint8_t>& goods) {
  g.st.shopDone = 0;
  g.openMessage();
  g.goldWindow(0x198, 0x130);
  for (;;) {
    shopMsgClear(g);
    shopMsg(g, 0, 0);
    const int r = choiceWindow(g, {sj("買う"), sj("売る"), sj("やめる")}, 2);
    g.drawField();
    if (r == 0)
      shopBuy(g, goods);
    else if (r == 1)
      shopSell(g);
    else
      break;
  }
  g.drawField();
}

void inn(Game& g, int price) {
  const int P = int16_t(price);
  g.st.shopDone = 0;
  bool yes = true;
  if (P != 0) {
    g.openMessage();
    g.goldWindow(0x198, 0x130);
    g.messageLine(0, "\\>" + sj("宿泊料金は") + std::to_string(P) + g.db.wordAt(0x6F) + sj("です。"));
    g.messageLine(1, "\\>" + sj("泊まりますか？"));
    yes = yesNoWindow(g) == 0;
  }
  if (!yes) {
    g.drawField();
    g.openMessage();
    return;
  }
  if (int64_t(g.st.gold) >= int64_t(P)) {
    g.st.shopDone = 1;
    g.subGold(long(uint32_t(int32_t(P))));
    static const char kInn[9] = {'I', 'N', 'N', 0, 0, 0, 0, 0, 0};
    g.playBgm(kInn);
    g.blackOut(2);
    g.drawField();
    g.closeMessage();
    for (int k = 0; k < g.st.partyCount && k < 5; ++k) g.fullHeal(partyMember(g, k));
    g.waitMs(2800);
    g.updatePoison();
    g.blackIn(2);
    g.playBgm(g.st.curBgm);
  } else {
    g.drawField();
    shopMsgClear(g);
    shopMsg(g, 0, 7);
    g.keyWaitMark();
  }
}

}  // namespace d2
