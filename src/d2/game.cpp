// D2 engine core helpers: timing, input, palette, drawing helpers, common game rules.
#include "d2/game.h"

#include <algorithm>
#include <cstring>

namespace d2 {

Game::Game(core::Host& h, const std::string& r, const core::StartOptions& o)
    : host(h), root(r), opt(o), build(Build::B9710), testPlay(o.noTitle) {
  loadDatabase(root, db);
  build = detectBuild(root);
  gaiji.data = db.gaiji;
  std::memcpy(members, db.members0, sizeof(members));
  st = db.state0;
  std::memset(inventory, 0, sizeof(inventory));
  std::memset(switches, 0, sizeof(switches));
  std::memset(vars, 0, sizeof(vars));
  std::memset(cache, 0, sizeof(cache));
  std::memset(upperMap, 0xFF, sizeof(upperMap));
  std::memset(lowerMap, 0xFF, sizeof(lowerMap));
  std::memset(curPal, 0, sizeof(curPal));
  std::memset(basePal, 0, sizeof(basePal));
  std::memset(npcDone, 0, sizeof(npcDone));
  std::memset(map.grid, 0, sizeof(map.grid));
  std::memset(map.zones, 0xFF, sizeof(map.zones));
  for (auto& e : map.events) e = nullptr;
  for (auto& p : map.eventPos) p = 0xFFFF;
  std::memset(&map.hdr, 0, sizeof(map.hdr));
  map.hdr.size = 32;
  chipset.data.assign(160 * kPatternBytes, 0);
  chipset.count = 160;
  for (auto& s : chrSlot) {
    s.data.assign(64 * kPatternBytes, 0);
    s.count = 64;
  }
  std::memset(chips.attr, 0x20, sizeof(chips.attr));
  std::memset(chips.enc, 1, sizeof(chips.enc));
  std::memset(chips.bg, 0, sizeof(chips.bg));
}

// =============================================================================================
// time
void Game::vsync() {
  ++frameCounter;
  ++waitCounter;
  if (st.vehicle == 2 && shadowOn) shadowUpdate();
  host.vsync(scr);
  prev = cur;
  cur = host.input();
  // key buffer and typematic (spec 01 §4.3): Esc, Return, Up, Down, Left, Right
  const bool now[6] = {cur.cancel, cur.ok, cur.up, cur.down, cur.left, cur.right};
  const bool was[6] = {prev.cancel, prev.ok, prev.up, prev.down, prev.left, prev.right};
  static const uint8_t codes[6] = {kKeyEsc, kKeyReturn, kKeyUp, kKeyDown, kKeyLeft, kKeyRight};
  bool any = false;
  for (int k = 0; k < 6; ++k) {
    if (now[k]) any = true;
    if (now[k] && !was[k]) {
      if (keyCount < 16) keyBuf[keyCount++] = codes[k];
      repeatKey = k;
      repeatCount = 0;
    }
  }
  if (!any) repeatKey = -1;
  if (repeatKey >= 0 && !now[repeatKey]) repeatKey = -1;
  if (repeatKey >= 0) {
    ++repeatCount;
    if (repeatCount >= 28 && (repeatCount - 28) % 3 == 0 && keyCount < 16) keyBuf[keyCount++] = codes[repeatKey];
  }
}

void Game::waitFrames(int n) {
  for (int i = 0; i < n; ++i) vsync();
}

void Game::waitMs(double ms) {
  const int n = msBal.add(ms);
  for (int i = 0; i < n; ++i) vsync();
}

void Game::waitUnits(int n) {
  waitCounter = 0;
  const uint32_t limit = uint32_t(n * (cur.fast ? 3 : 7));
  while (waitCounter < limit) vsync();
}

void Game::sfx(int n) {
  core::SoundRequest r;
  r.kind = core::SoundRequest::Sfx;
  r.value = n + 10;
  host.sound(r);
}

bool Game::bgmEqual(const char* a, const char* b) { return std::memcmp(a, b, 9) == 0; }
bool Game::bgmEqual8(const char* a, const char* b) { return std::strncmp(a, b, 8) == 0; }

void Game::playBgm(const char* name9) {
  std::string name;
  for (int i = 0; i < 8 && name9[i]; ++i) name.push_back(name9[i]);
  std::string rel = "DAT_BGM/" + name + ".MML";
  bool ascii = true;
  for (char c : name)
    if (uint8_t(c) >= 0x80) ascii = false;
  // Data refers to SJIS names; host file names may be UTF-8 (spec 01 §6.1 recommendation).
  if (!ascii && !core::fileExists(root, rel)) {
    std::string alt = "DAT_BGM/" + core::sjisToUtf8(name) + ".MML";
    if (core::fileExists(root, alt)) rel = alt;
  }
  core::SoundRequest r;
  r.kind = core::SoundRequest::PlayFile;
  r.path = rel;
  host.sound(r);
}

void Game::stopBgm() {
  core::SoundRequest r;
  r.kind = core::SoundRequest::Stop;
  host.sound(r);
}

// =============================================================================================
// input
int Game::popKey() {
  if (keyCount == 0) return kKeyNone;
  const uint8_t k = keyBuf[0];
  std::memmove(keyBuf, keyBuf + 1, size_t(keyCount - 1));
  --keyCount;
  return k;
}

void Game::clearKeys() { keyCount = 0; }

void Game::recordLatch() {
  latch[0] = cur.down;
  latch[1] = cur.left;
  latch[2] = cur.right;
  latch[3] = cur.up;
  latch[4] = cur.ok || cur.cancel;
}

bool Game::anyHeld() const { return cur.down || cur.left || cur.right || cur.up || cur.ok || cur.cancel; }

int Game::readDirection() {
  const bool d = latch[0] || cur.down, l = latch[1] || cur.left, r = latch[2] || cur.right, u = latch[3] || cur.up;
  const bool b = latch[4] || cur.ok || cur.cancel;
  const int v = (d ? 1 : 0) - (u ? 1 : 0);
  const int h = (r ? 1 : 0) - (l ? 1 : 0);
  if (h && v) return lastAxisVertical ? (h > 0 ? 6 : 4) : (v > 0 ? 2 : 8);
  if (h) {
    lastAxisVertical = false;
    return h > 0 ? 6 : 4;
  }
  if (v) {
    lastAxisVertical = true;
    return v > 0 ? 2 : 8;
  }
  if (d || l || r || u || b) return 5;
  return 0;
}

void Game::keyWait() {
  bool released = false;
  for (;;) {
    recordLatch();
    clearKeys();
    waitMs(2);
    const int d = readDirection();
    checkStop();
    if (d == 0)
      released = true;
    else if (released)
      return;
    rng.next();
  }
}

void Game::checkStop() {
  if (st.eventRunning && stopPressed()) throw EventAbort();
}

// =============================================================================================
// palette
void Game::applyPalette() {
  int t = tone < 0 ? 0 : tone;
  for (int i = 0; i < 16; ++i)
    for (int k = 0; k < 3; ++k) {
      const int v = curPal[i][k] / 16;
      int o;
      if (t <= 100)
        o = v * t / 100;
      else if (t < 200)
        o = 15 - (15 - v) * (200 - t) / 100;
      else
        o = 15;
      scr.pal[i][k] = uint8_t(o);
    }
}

void Game::setPal4(int i, int r, int g, int b) {
  curPal[i][0] = uint8_t(r * 17);
  curPal[i][1] = uint8_t(g * 17);
  curPal[i][2] = uint8_t(b * 17);
}

static void fadeGeneric(Game& g, int init, int delta, int end, int s) {
  g.tone = init;
  g.vsync();
  for (;;) {
    g.applyPalette();
    g.waitFrames(s);
    g.tone += delta;
    if (delta < 0 ? !(g.tone > end) : !(g.tone < end)) break;
  }
  g.tone = end;
  g.applyPalette();
}

void Game::blackOut(int s) { fadeGeneric(*this, 100, -6, 0, s); }
void Game::blackIn(int s) { fadeGeneric(*this, 0, 6, 100, s); }
void Game::whiteOut(int s) { fadeGeneric(*this, 100, 6, 200, s); }
void Game::whiteIn(int s) { fadeGeneric(*this, 200, -6, 100, s); }

void Game::toneChange(int kind, int s) {
  int start[16][3], target[16][3];
  for (int i = 0; i < 16; ++i)
    for (int k = 0; k < 3; ++k) {
      start[i][k] = curPal[i][k] / 17;
      target[i][k] = basePal[i][k];
    }
  auto cl = [](int v) { return v < 0 ? 0 : (v > 15 ? 15 : v); };
  for (int i = 0; i < 16; ++i) {
    int R = target[i][0], G = target[i][1], B = target[i][2];
    const int R0 = R, G0 = G, B0 = B;
    auto gray = [&]() { return std::min(15, (R + G + B) / 3 + 3); };
    switch (kind) {
      case 0: R = cl(R + 4); G = cl(G + 4); B = cl(B + 4); break;
      case 1: R = cl(R - 4); G = cl(G - 4); B = cl(B - 4); break;
      case 2: R = gray(); G = cl(G - 8); B = cl(B - 7); break;
      case 3: R = gray(); G = gray(); B = cl(B - 7); break;
      case 4: R = cl(R - 8); G = gray(); B = cl(B - 7); break;
      case 5: R = cl(R - 8); G = gray(); B = gray(); B = cl(B + 2); break;
      case 6: R = cl(R - 7); G = cl(G - 7); B = gray(); B = cl(B + 2); break;
      case 7: R = gray(); G = cl(G - 8); B = gray(); B = cl(B + 2); break;
      case 8: { int g = gray(); R = G = B = g; } break;
      case 9: {
        int g = gray();
        R = G = B = g;
        R = cl(R + 2); G = cl(G + 2); G = cl(G - 1); B = cl(B - 2);
        R = (R * 7 + R0) / 8; G = (G * 7 + G0) / 8; B = (B * 7 + B0) / 8;
      } break;
      case 10: {
        int g = gray();
        R = G = B = g;
        R = cl(R + 4); G = cl(G + 2); G = cl(G - 3); B = cl(B + 2); B = cl(B - 2);
        R = (R * 7 + R0) / 8; G = (G * 7 + G0) / 8; B = (B * 7 + B0) / 8;
      } break;
      case 11: R = cl(R - 4); G = cl(G - 4); B = cl(B - 2); break;
      default: break;
    }
    target[i][0] = R;
    target[i][1] = G;
    target[i][2] = B;
  }
  st.tone = uint8_t(kind + 1);
  vsync();
  waitCounter = 0;
  if (s > 0) {
    for (int j = 0; j <= 6; ++j) {
      for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 3; ++k) curPal[i][k] = uint8_t((start[i][k] + (target[i][k] - start[i][k]) * j / 8) * 17);
      while (waitCounter < uint32_t(j * s)) vsync();
      applyPalette();
    }
    while (waitCounter < uint32_t(7 * s)) vsync();
  }
  for (int i = 0; i < 16; ++i)
    for (int k = 0; k < 3; ++k) curPal[i][k] = uint8_t(target[i][k] * 17);
  if (s >= 0) applyPalette();
}

void Game::flash() {
  for (int i = 0; i <= 7; ++i) {
    vsync();
    setTone(120 + 6 * i);
  }
  for (int i = 3; i >= 0; --i) {
    vsync();
    setTone(120 + 12 * i);
  }
  vsync();
  setTone(100);
}

void Game::shake() {
  for (int i = 0; i < 3; ++i) {
    waitFrames(2);
    scr.hOffset = 1;
    waitFrames(2);
    scr.hOffset = 0;
  }
}

// =============================================================================================
// drawing helpers
void Game::drawText(int x, int y, const std::string& s, int color) { core::drawStringD2(scr, x, y, s, color, gaiji); }

int Game::textWidth(const std::string& s) const {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
  size_t i = 0;
  int w = 0;
  while (i < s.size()) {
    core::Glyph g;
    int used = core::fetchGlyphD2(p + i, s.size() - i, gaiji, g);
    if (!used) break;
    w += g.width;
    i += used;
  }
  return w;
}

static void outline(core::Screen& s, int x0, int y0, int x1, int y1, int c) {
  for (int x = x0; x <= x1; ++x) {
    s.pset(x, y0, c);
    s.pset(x, y1, c);
  }
  for (int y = y0; y <= y1; ++y) {
    s.pset(x0, y, c);
    s.pset(x1, y, c);
  }
}

void Game::drawWindow(int x, int y, int w, int h) {
  scr.fillRect(x & ~7, y, (w / 8) * 8, h, 0);
  outline(scr, x, y, x + w - 1, y + h - 1, 14);
  outline(scr, x + 1, y + 1, x + w - 2, y + h - 2, 14);
  outline(scr, x + 2, y + 1, x + w - 3, y + h - 2, 15);
  outline(scr, x + 1, y + 2, x + w - 2, y + h - 3, 15);
  outline(scr, x + 3, y + 3, x + w - 4, y + h - 4, 15);
}

void Game::fieldMask() {
  scr.maskTextAll();
  scr.unmaskTextRect(2, 0, 77, 23);
}
void Game::menuMask() {
  scr.maskTextAll();
  scr.unmaskTextRect(0, 0, 79, 23);
}
void Game::windowMask() {
  scr.maskTextAll();
  scr.unmaskTextRect(1, 0, 78, 24);
}
void Game::clearTextLayer() { scr.clearText(); }

void Game::drawCursor(int x, int y, int color) { drawText(x, y, "\xEB\x9F", color); }

int Game::select(int x, int y, int n, int init, int mode) {
  if (n <= 0) return -1;
  clearKeys();
  int pos = init;
  int t = 0;
  for (;;) {
    const int cx = x + 16, cy = y + 16 + 24 * pos;
    if (t < 4)
      drawCursor(cx, cy, 15);
    else if (t >= 21 && t <= 23)
      drawCursor(cx, cy, 0);
    vsync();
    ++t;
    if (t > 30) t = 0;
    checkStop();
    const int k = popKey();
    if (k == kKeyNone) continue;
    if (k == kKeyEsc) {
      if (mode != 0) {
        sfx(3);
        drawCursor(cx, cy, 0);
        return -1;
      }
    } else if (isOkKey(k)) {
      sfx(2);
      drawCursor(cx, cy, mode == 2 ? 0 : 15);
      return pos;
    } else if (isUpKey(k) || isDownKey(k)) {
      sfx(1);
      drawCursor(cx, cy, 0);
      if (isUpKey(k))
        pos = pos - 1 < 0 ? n - 1 : pos - 1;
      else
        pos = pos + 1 > n - 1 ? 0 : pos + 1;
      t = 0;
    } else if (isLeftKey(k) || isRightKey(k)) {
      sfx(1);
    }
  }
}

void Game::openMessage() {
  vsync();
  if (!st.msgOpen)
    drawWindow(80, 240, 480, 128);
  else
    scr.fillRect(96, 256, 448, 96, 0);
  st.msgOpen = 1;
}

void Game::closeMessage() {
  if (!st.msgOpen) return;
  st.msgOpen = 0;
  drawField();
}

bool Game::messageLine(int line, const std::string& s) {
  bool speed = true, noWait = false;
  int x = 96;
  const int y = 256 + 24 * line;
  auto putChars = [&](const std::string& str, bool controls) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(str.data());
    size_t i = 0;
    while (i < str.size()) {
      if (controls && p[i] == 0x5C && i + 1 < str.size()) {
        const char c = char(p[i + 1]);
        bool handled = true;
        switch (c) {
          case '!': keyWaitMark(); break;
          case '$': goldWindow(0x198, 0x130); break;
          case '.': waitMs(400); break;
          case '>': speed = false; break;
          case '^': noWait = true; break;
          case '@': {
            const std::string e = embed;
            const uint8_t* q = reinterpret_cast<const uint8_t*>(e.data());
            size_t j = 0;
            while (j < e.size()) {
              core::Glyph g;
              int used = core::fetchGlyphD2(q + j, e.size() - j, gaiji, g);
              if (!used) break;
              x += core::drawGlyphD2(scr, x, y, g, 15);
              if (speed && !cur.fast) waitMs(g.width == 16 ? 16 : 8);
              j += used;
            }
          } break;
          default: handled = false; break;
        }
        if (handled) {
          i += 2;
          continue;
        }
      }
      core::Glyph g;
      int used = core::fetchGlyphD2(p + i, str.size() - i, gaiji, g);
      if (!used) {
        // NUL byte inside: blank half-width
        used = 1;
        g.width = 8;
      } else {
        core::drawGlyphD2(scr, x, y, g, 15);
      }
      x += g.width;
      if (speed && !cur.fast) waitMs(g.width == 16 ? 16 : 8);
      i += used;
    }
  };
  putChars(s, true);
  return noWait;
}

void Game::keyWaitMark() {
  drawText(312, 352, "\xEB\xB7", 15);
  keyWait();
  drawText(312, 352, "\xEB\xB7", 0);
  clearKeys();
}

void Game::goldWindow(int x, int y) {
  if (st.gold > 999999) st.gold = 999999;
  const std::string unit = db.wordAt(0x6F);
  const int u = int(unit.size());
  const int o = 0x30 - 8 * u;
  drawWindow(x + o, y, 0x88 - o, 0x30);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%6u", unsigned(st.gold));
  drawText(x + o + 0x18, y + 16, std::string(buf) + unit);
}

// =============================================================================================
// rules
int64_t Game::expForLevel(const Member& m, int L) {
  if (L < 2) return 0;
  if (L > 99) L = 99;
  const int64_t a = L + 2;
  const int64_t t = ((a * a + 23) * (a * (a - 4))) / 111 + 1;
  const int64_t f = (int64_t(m.expB) - 10) * a + (int64_t(m.expA) + 5) * 100;
  return t * f / 100 + m.expC;
}

int16_t Game::statForLevel(const Member& m, int j, int L) {
  const int b = m.lv1[j], g = m.growth[j], K = m.growthK;
  int v;
  if (K < L)
    v = b + int(int16_t(g * (K - 1))) / 10 + g * (L - K) / 20;
  else
    v = b + g * (L - 1) / 10;
  const int cap = j == 0 ? 9999 : 999;
  if (v > cap) v = cap;
  return int16_t(v);
}

void Game::recalcLevel(Member& m) {
  if (m.level >= m.maxLevel) m.level = m.maxLevel;
  m.maxHp = statForLevel(m, 0, m.level);
  m.maxMp = statForLevel(m, 1, m.level);
  for (int i = 0; i < 4; ++i) m.base[i] = statForLevel(m, 2 + i, m.level);
  m.nextExp = m.level == m.maxLevel ? 0 : uint32_t(expForLevel(m, m.level + 1));
}

void Game::recalc(Member& m) {
  recalcLevel(m);
  for (int i = 0; i < 4; ++i) {
    // 23 §1.5 / 24 §16.3: plain sum then clamp (no 16-bit truncation; see 24 §19.2).
    int v = m.base[i];
    for (int s = 0; s < 5; ++s)
      if (m.equip[s] != 0xFF) v += db.items[m.equip[s]].mod(i);
    m.stat[i] = int16_t(v < 0 ? 0 : (v > 999 ? 999 : v));
  }
  for (int i = 0; i < 30; ++i)
    if (m.learned[i] != m.magicList[i]) m.learned[i] = 0xFF;
}

void Game::recalcAll() {
  for (auto& m : members) recalc(m);
}

bool Game::levelUp(Member& m) {
  bool up = false;
  while (m.level != m.maxLevel && uint64_t(m.exp) >= uint64_t(expForLevel(m, m.level + 1))) {
    ++m.level;
    up = true;
    for (int i = 0; i < 30; ++i)
      if (m.learnLevel[i] == m.level) {
        if (isOldBuild(build))
          m.learned[i] = m.magicList[i];
        else
          learnMagic(m, m.magicList[i]);
      }
  }
  recalc(m);
  return up;
}

void Game::levelDown(Member& m) {
  while (m.level > 0 && uint64_t(m.exp) < uint64_t(expForLevel(m, m.level))) --m.level;
  recalc(m);
  if (m.hp > m.maxHp) m.hp = m.maxHp;
  if (m.mp > m.maxMp) m.mp = m.maxMp;
}

void Game::addHp(Member& m, int n) {
  if (build == Build::B9710 && m.hp <= 0) return;
  m.hp = int16_t(std::min(int(m.hp) + n, int(m.maxHp)));
}

void Game::fullHeal(Member& m) {
  if (m.hp == 0) {
    m.hp = 1;
    m.alive = 1;
  }
  m.status[5] = 0;
  m.status[0] = 0;
  addHp(m, isOldBuild(build) ? 999 : 9999);
  m.mp = int16_t(std::min(int(m.mp) + 999, int(m.maxMp)));
}

bool Game::gameOver() const {
  if (st.partyCount == 0) return false;
  for (int k = 0; k < st.partyCount && k < 5; ++k) {
    const int c = st.party[k];
    if (c > 11) continue;
    if (members[c].alive && !members[c].status[5]) return false;
  }
  return true;
}

bool Game::inParty(int c) const {
  for (int k = 0; k < st.partyCount && k < 5; ++k)
    if (st.party[k] == c) return true;
  return false;
}

void Game::joinParty(int c) {
  if (st.partyCount >= 5 || inParty(c)) return;
  st.party[st.partyCount++] = uint8_t(c);
}

void Game::leaveParty(int c) {
  for (int k = 0; k < st.partyCount && k < 5; ++k)
    if (st.party[k] == c) {
      for (int j = k; j + 1 < st.partyCount && j + 1 < 5; ++j) st.party[j] = st.party[j + 1];
      --st.partyCount;
      return;
    }
}

int Game::leader() const { return st.party[0] > 11 ? 0 : st.party[0]; }

void Game::learnMagic(Member& m, int magic) {
  for (int i = 0; i < 30; ++i)
    if (m.magicList[i] == magic) {
      m.learned[i] = uint8_t(magic);
      return;
    }
}

void Game::forgetMagic(Member& m, int magic) {
  for (int i = 0; i < 30; ++i)
    if (m.learned[i] == magic) m.learned[i] = 0xFF;
}

bool Game::addItem(int a) {
  if (a == 0xFF) return false;
  for (int i = 0; i < 256; ++i) {
    if (inventory[i][1] == 0) {
      inventory[i][0] = uint8_t(a);
      inventory[i][1] = 1;
      return true;
    }
    if (inventory[i][0] == a) {
      if (inventory[i][1] >= 99) return false;
      ++inventory[i][1];
      return true;
    }
  }
  return false;
}

void Game::packInventory() {
  uint8_t tmp[kInventorySlots][2];
  int n = 0;
  for (int i = 0; i < kInventorySlots; ++i)
    if (inventory[i][1] != 0) {
      tmp[n][0] = inventory[i][0];
      tmp[n][1] = inventory[i][1];
      ++n;
    }
  for (int i = n; i < kInventorySlots; ++i) {
    tmp[i][0] = 0xFF;
    tmp[i][1] = 0;
  }
  std::memcpy(inventory, tmp, sizeof(tmp));
}

bool Game::removeItem(int a) {
  for (int i = 0; i < 256; ++i)
    if (inventory[i][1] != 0 && inventory[i][0] == a) {
      if (--inventory[i][1] == 0) packInventory();
      return true;
    }
  if (a == 0xFF) return false;
  for (int k = 0; k < st.partyCount && k < 5; ++k) {
    const int c = st.party[k];
    if (c > 11) continue;
    for (int s = 0; s < 5; ++s)
      if (members[c].equip[s] == a) {
        members[c].equip[s] = 0xFF;
        recalc(members[c]);
        return true;
      }
  }
  return false;
}

void Game::moveItemToEnd(int a) {
  for (int i = 0; i < kInventorySlots; ++i)
    if (inventory[i][1] != 0 && inventory[i][0] == a) {
      const uint8_t cnt = inventory[i][1];
      inventory[i][1] = 0;
      packInventory();
      for (int j = 0; j < kInventorySlots; ++j)
        if (inventory[j][1] == 0) {
          inventory[j][0] = uint8_t(a);
          inventory[j][1] = cnt;
          break;
        }
      return;
    }
}

void Game::discardItem(int a) {
  for (int i = 0; i < kInventorySlots; ++i)
    if (inventory[i][1] != 0 && inventory[i][0] == a) {
      inventory[i][1] = 0;
      packInventory();
      return;
    }
}

bool Game::hasItem(int a) const {
  if (a == 0xFF) return false;
  for (int i = 0; i < 256; ++i)
    if (inventory[i][1] != 0 && inventory[i][0] == a) return true;
  if (build == Build::B9606) return false;
  for (int k = 0; k < st.partyCount && k < 5; ++k) {
    const int c = st.party[k];
    if (c > 11) continue;
    for (int s = 0; s < 5; ++s)
      if (members[c].equip[s] == a) return true;
  }
  return false;
}

int Game::itemCount(int a) const {
  for (int i = 0; i < 256; ++i)
    if (inventory[i][1] != 0 && inventory[i][0] == a) return inventory[i][1];
  return 0;
}

void Game::addGold(long n) {
  long v = long(st.gold) + n;
  if (v > 999999) v = 999999;
  st.gold = uint32_t(v);
}

void Game::subGold(long n) {
  if (long(st.gold) < n)
    st.gold = 0;
  else
    st.gold = uint32_t(long(st.gold) - n);
}

void Game::updatePoison() {
  st.poison = 0;
  for (int k = 0; k < st.partyCount && k < 5; ++k) {
    const int c = st.party[k];
    if (c <= 11 && members[c].status[0]) st.poison = 1;
  }
}

}  // namespace d2
