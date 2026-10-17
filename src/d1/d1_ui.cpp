// D1 low level helpers, UI parts, formulas, inventory, save/load (spec 11 §1, §3, §7, §10, §11).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/text.h"
#include "d1/d1_game.h"

namespace d1 {

std::string S(const char* u8) { return core::utf8ToSjis(u8); }

// ---------------------------------------------------------------- low level
void Game::frame() {
  host_.vsync(scr_);
  ++frameCounter_;
  ++totalFrames_;
  if ((frameCounter_ & 15) == 0) anim_ ^= 1;
}

void Game::waitMs(double ms) { frames(ms_.add(ms)); }

int Game::dirKey(const core::InputState& s) const {
  if (s.up) return 0;
  if (s.right) return 1;
  if (s.down) return 2;
  if (s.left) return 3;
  return -1;
}

bool Game::anyKey(const core::InputState& s) const { return s.ok || s.cancel || s.up || s.down || s.left || s.right; }

void Game::waitRelease() {
  while (anyKey(in())) frame();
}

void Game::waitPress() {
  for (;;) {
    core::InputState s = in();
    if (s.fast) {
      frame();
      return;
    }
    if (anyKey(s)) return;
    frame();
  }
}

void Game::se(int n) {
  core::SoundRequest r;
  r.kind = core::SoundRequest::Sfx;
  r.value = n;
  host_.sound(r);
}

void Game::bgmStop() {
  for (int v = 0; v < 16; ++v) {
    core::SoundRequest r;
    r.kind = core::SoundRequest::Fade;
    r.value = v;
    host_.sound(r);
    waitMs(20);
  }
  core::SoundRequest r;
  r.kind = core::SoundRequest::Stop;
  host_.sound(r);
}

void Game::bgmChange(int n) {
  bgmStop();
  core::SoundRequest r;
  r.kind = core::SoundRequest::PlayNumber;
  r.value = n;
  host_.sound(r);
}

void Game::applyPal(int mode, int offset) {
  for (int i = 0; i < 16; ++i) {
    int r = db.palet[i][0], g = db.palet[i][1], b = db.palet[i][2];
    switch (mode) {
      case 0: {
        int v = 15 - (r + g + b) / 3;
        r = g = b = v;
        break;
      }
      case 1: r += 8; g -= 8; b -= 8; break;
      case 2: r -= 6; g -= 6; b += 6; break;
      case 3: {
        int v = (r + g + b) / 3 + 1;
        r = g = b = v;
        break;
      }
      case 4: r += 4; g += 4; b += 4; break;
      case 5: r /= 3; g /= 3; b /= 3; break;
      default: break;
    }
    scr_.setPal(i, r - offset, g - offset, b - offset);
  }
}

void Game::applyPalRaw(const uint8_t p[16][3], int add) {
  for (int i = 0; i < 16; ++i)
    scr_.setPal(i, std::min(15, p[i][0] + add), std::min(15, p[i][1] + add), std::min(15, p[i][2] + add));
}

void Game::fade(int a, int b) {
  const int step = a < b ? 1 : -1;
  for (int v = a;; v += step) {
    frame();
    frame();
    applyPal(palMode, v);
    if (v == b) break;
  }
}

// ---------------------------------------------------------------- UI parts
void Game::window(int x1, int y1, int x2, int y2) {
  x1 &= ~1;
  x2 &= ~1;
  const int X1 = x1 * 8, X2 = x2 * 8, Y1 = y1 * 24, Y2 = y2 * 24;
  scr_.fillRect(X1, Y1, X2 - X1 + 16, Y2 - Y1 + 24, 0);
  static const uint8_t tl[8] = {0x00, 0x1F, 0x3F, 0x7F, 0x7F, 0x7C, 0x78, 0x78};
  static const uint8_t tr[8] = {0x00, 0xF8, 0xFC, 0xFE, 0xFE, 0x3E, 0x1E, 0x1E};
  static const uint8_t bl[8] = {0x78, 0x78, 0x7C, 0x7F, 0x7F, 0x3F, 0x1F, 0x00};
  static const uint8_t br[8] = {0x1E, 0x1E, 0x3E, 0xFE, 0xFE, 0xFC, 0xF8, 0x00};
  scr_.bitmap1(tl, 8, X1, Y1, 15);
  scr_.bitmap1(tr, 8, X2 + 8, Y1, 15);
  scr_.bitmap1(bl, 8, X1, Y2 + 16, 15);
  scr_.bitmap1(br, 8, X2 + 8, Y2 + 16, 15);
  const int hx0 = X1 + 8, hw = (x2 - x1) * 8;
  scr_.fillRect(hx0, Y1 + 1, hw, 4, 15);
  const int vy0 = Y1 + 8, vh = (y2 - y1) * 24 + 8;
  scr_.fillRect(X1 + 1, vy0, 4, vh, 15);
  scr_.fillRect(X2 + 11, vy0, 4, vh, 15);
  scr_.fillRect(hx0, Y2 + 19, hw, 4, 15);
}

int Game::drawStr(int col, int row, const std::string& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
  size_t i = 0;
  while (i < s.size()) {
    if (p[i] == 0) break;
    uint16_t code;
    if (core::isSjisLead(p[i]) && i + 1 < s.size()) {
      code = uint16_t((p[i] << 8) | p[i + 1]);
      i += 2;
      core::drawCharD1(scr_, col * 8, row * 24, code);
      col += 2;
    } else {
      code = p[i];
      i += 1;
      core::drawCharD1(scr_, col * 8, row * 24, code);
      col += 1;
    }
  }
  return col;
}

int Game::drawStrFull(int col, int row, const std::string& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
  size_t i = 0;
  while (i < s.size()) {
    if (p[i] == 0) break;
    if (core::isSjisLead(p[i]) && i + 1 < s.size()) {
      core::drawCharD1(scr_, col * 8, row * 24, uint16_t((p[i] << 8) | p[i + 1]));
      i += 2;
    } else {
      uint16_t f = core::d1HalfToFull(p[i]);
      core::drawCharD1(scr_, col * 8, row * 24, f ? f : p[i]);
      i += 1;
    }
    col += 2;
  }
  return col;
}

void Game::cursor(int col, int row, bool show) {
  static const uint16_t arrow[16] = {0x0C00, 0x1E00, 0x1F00, 0x0F80, 0x0FC0, 0x07E0, 0x7FF0, 0xFFF8,
                                     0xFFF8, 0x7FF0, 0x07E0, 0x0FC0, 0x0F80, 0x1F00, 0x1E00, 0x0C00};
  const int x = col * 8, y = row * 24;
  for (int r = 0; r < 16; ++r)
    for (int k = 0; k < 16; ++k) scr_.pset(x + k, y + r, (show && (arrow[r] & (0x8000 >> k))) ? 15 : 0);
}

static std::string padTo(const std::string& s, int w) {
  std::string r = s.substr(0, std::min<size_t>(s.size(), size_t(w)));
  while (int(r.size()) < w) r.push_back(' ');
  return r;
}

MenuRes Game::menu(const std::vector<std::string>& items, int w, int C, int R, int x, int y, int init, bool paging) {
  for (int c = 0; c < C; ++c)
    for (int r = 0; r < R; ++r) {
      const int k = c * R + r;
      drawStr(x + 2 + (w + 6) * c, y + r, padTo(k < int(items.size()) ? items[k] : std::string(), w));
    }
  if (init < 0) init = 0;
  int row = init % R, col = init / R;
  cursor(x + (w + 6) * col, y + row, true);
  waitRelease();
  resetAnim();
  for (;;) {
    cursor(x + (w + 6) * col, y + row, anim_ != 0);
    core::InputState s = in();
    if (s.ok) {
      se(15);
      cursor(x + (w + 6) * col, y + row, true);
      return {MenuRes::Selected, col * R + row};
    }
    if (s.cancel) {
      se(12);
      return {MenuRes::Cancel, col * R + row};
    }
    const int d = dirKey(s);
    if (d >= 0) {
      se(11);
      cursor(x + (w + 6) * col, y + row, false);
      if (d == 0) row = (row + R - 1) % R;
      if (d == 2) row = (row + 1) % R;
      if (d == 1) {
        if (paging && col == C - 1) return {MenuRes::Next, col * R + row};
        col = (col + 1) % C;
      }
      if (d == 3) {
        if (paging && col == 0) return {MenuRes::Prev, row};
        col = (col + C - 1) % C;
      }
      resetAnim();
      cursor(x + (w + 6) * col, y + row, true);
      waitRelease();
      continue;
    }
    frame();
  }
}

int Game::heroSelect(int init, int x, int y, bool showAbility) {
  int slot = init;
  cursor(x, y + slot, true);
  for (;;) {
    cursor(x, y + slot, anim_ != 0);
    if (showAbility) {
      scr_.fillRect(472, 192, 129, 121, 0);
      scr_.fillRect(344, 192, 65, 161, 0);
      if (validSlot(slot)) abilityBlock(party[slot], 0x23, 8);
    }
    waitRelease();
    for (;;) {
      cursor(x, y + slot, anim_ != 0);
      core::InputState s = in();
      if (s.cancel && !s.ok) {
        se(12);
        cursor(x, y + slot, false);
        return -1;
      }
      if (s.ok) {
        se(15);
        if (!validSlot(slot)) {
          waitRelease();
          continue;
        }
        cursor(x, y + slot, true);
        return slot;
      }
      const int d = dirKey(s);
      if (d >= 0) {
        se(11);
        cursor(x, y + slot, false);
        if (d == 0) slot = (slot + 3) % 4;
        if (d == 2) slot = (slot + 1) % 4;
        resetAnim();
        break;
      }
      frame();
    }
  }
}

void Game::prompt(int x1, int y1, int x2, int y2, const std::string& body, const std::string& sub) {
  window(x1, y1, x2, y2);
  drawStr(x1 + 2, y1 + 1, S("※") + body);
  if (!sub.empty()) drawStr(x1 + 4, y1 + 2, sub);
}

static std::string fmt(const char* f, long v) {
  char b[64];
  std::snprintf(b, sizeof b, f, v);
  return b;
}

void Game::goldWindow() {
  window(0x34, 0xD, 0x4B, 0xE);
  drawStrFull(0x36, 0xE, fmt("G/%8ld", gold));
}

std::string Game::statusStr(int c) const {
  const Hero& h = heroes[c];
  std::string s;
  if (h.status & 0x01) s = S("　毒");
  if (h.status & 0x04) s = S("　封");
  if (h.status & 0x08) s = S("　乱");
  if (h.status & 0x10) s = S("　眠");
  if (h.status & 0x02) s = S("　ﾏﾋ");
  if (h.hp == 0) s = S("　") + "\xEB\xC0";
  return s;
}

bool Game::yesNo(int x, int y) {
  for (;;) {
    window(x, y, x + 0xC, y + 3);
    MenuRes r = menu({S("はい　"), S("いいえ")}, 6, 1, 2, x + 2, y + 1, 0, false);
    if (r.kind == MenuRes::Selected) return r.pos == 0;
  }
}

// ---------------------------------------------------------------- message area
void Game::msgClear() {
  scr_.fillRect(mX0_ * 8, mY0_ * 24, mW_ * 8 + 16, (mH_ - 1) * 24 + 17, 0);
  mC_ = -1;
  mR_ = 0;
}

static bool kinsoku(uint16_t c) {
  static const uint16_t t[] = {0x8141, 0x8142, 0x8143, 0x8144, 0x8145, 0x8148, 0x8149, 0x814C,
                               0x815B, 0x8160, 0x8164, 0x8166, 0x8168, 0x816A, 0x816C, 0x816E,
                               0x8170, 0x8172, 0x8174, 0x8176, 0x8178, 0x817A, 0x818D};
  for (uint16_t k : t)
    if (k == c) return true;
  return false;
}

void Game::msgPut(const std::string& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
  size_t i = 0;
  while (i < s.size()) {
    const uint8_t b = p[i];
    if (b == 0) break;
    const bool two = core::isSjisLead(b) && i + 1 < s.size();
    if (!two) {
      if (b == '#') {
        mC_ = -1;
        ++mR_;
        ++i;
        continue;
      }
      if (b == ';') {
        waitMs(200);
        ++i;
        continue;
      }
      if (b == '@') {
        waitPress();
        msgClear();
        ++i;
        continue;
      }
      if (b == '_') {
        waitRelease();
        waitPress();
        msgClear();
        ++i;
        continue;
      }
    }
    if ((mR_ == mH_ - 1 && mC_ == 39) || mR_ == mH_) {
      waitRelease();
      waitPress();
      msgClear();
    }
    ++mC_;
    if (two) {
      const uint16_t code = uint16_t((b << 8) | p[i + 1]);
      if ((mC_ == mW_ && !kinsoku(code)) || mC_ > mW_) {
        ++mR_;
        mC_ = 0;
      }
      core::drawCharD1(scr_, (mX0_ + mC_) * 8, (mY0_ + mR_) * 24, code);
      ++mC_;
      i += 2;
    } else {
      if (mC_ >= mW_) {
        ++mR_;
        mC_ = 0;
      }
      core::drawCharD1(scr_, (mX0_ + mC_) * 8, (mY0_ + mR_) * 24, b);
      i += 1;
    }
  }
}

// ---------------------------------------------------------------- formulas
int Game::calcAgi(int c) const {
  int v = heroes[c].level * db.heroes[c].gAgi;
  if (heroes[c].equip[4] < 0x80) v += db.items[heroes[c].equip[4]].value;
  return v;
}

int Game::calcAtk(int c) const {
  int v = heroes[c].level * db.heroes[c].gStr + 10;
  if (heroes[c].equip[0] < 0x80) v += db.items[heroes[c].equip[0]].value;
  return v;
}

int Game::calcDef(int c) const {
  int v = heroes[c].level * db.heroes[c].gVit + 10;
  for (int k = 1; k <= 3; ++k)
    if (heroes[c].equip[k] < 0x80) v += db.items[heroes[c].equip[k]].value;
  return v;
}

int Game::variance(int v) {
  if (v <= 3) return v;
  const int r = rng.next() % (v / 4);
  return rng.next() < 16000 ? v + r : v - r;
}

int Game::addExp(int slot, int v) {
  if (!validSlot(slot)) return 0;
  Hero& h = heroes[party[slot]];
  if (h.hp == 0) return 0;
  h.exp += v;
  int result = 0;
  while (h.exp >= expNeeded(h.level)) {
    const int need = expNeeded(h.level);
    if (h.level < 99) ++h.level;
    h.exp -= need;
    result = h.level;
  }
  return result;
}

bool Game::isGameOver() const {
  int hpSum = 0, notPar = 0;
  for (int s = 0; s < 4; ++s) {
    if (!validSlot(s)) continue;
    const Hero& h = heroes[party[s]];
    hpSum += h.hp;
    if (!(h.status & 0x02)) ++notPar;
  }
  return hpSum == 0 || notPar == 0;
}

// ---------------------------------------------------------------- inventory
bool Game::addItem(int a) {
  for (int i = 0; i < 50; ++i) {
    if (invId[i] == a && invCnt[i] < 99) {
      ++invCnt[i];
      return true;
    }
    if (invId[i] >= 0x80) {
      invId[i] = uint8_t(a);
      invCnt[i] = 1;
      return true;
    }
  }
  return false;
}

void Game::removeItem(int a, bool all) {
  for (int i = 0; i < 50; ++i) {
    if (invId[i] != a) continue;
    if (invCnt[i] == 1 || all) {
      for (int k = i; k < 49; ++k) {
        invId[k] = invId[k + 1];
        invCnt[k] = invCnt[k + 1];
      }
      invId[49] = kNone;
    } else {
      --invCnt[i];
    }
    return;
  }
}

bool Game::hasItem(int a) const {
  for (int i = 0; i < 50; ++i)
    if (invId[i] == a) return true;
  return false;
}

// ---------------------------------------------------------------- new game / save / load
void Game::newGame() {
  for (int n = 0; n < 8; ++n) {
    Hero& h = heroes[n];
    h.level = db.heroes[n].level;
    h.status = 0;
    h.exp = 0;
    for (auto& e : h.equip) e = kNone;
    h.hp = h.recMaxHp = calcMaxHp(n);
    h.mp = h.recMaxMp = calcMaxMp(n);
  }
  const StartInfo& s = db.start;
  curScreen = s.screen & 1023;
  heroX = s.pos % 20;
  heroY = s.pos / 20;
  gold = s.gold;
  fieldBgm = s.bgm;
  std::memcpy(party, s.party, 4);
  shipScreen = s.shipScreen;
  shipPos = s.shipPos;
  shipGfx = s.shipGfx;
  shipBgm = s.shipBgm;
  airScreen = s.airScreen;
  airPos = s.airPos;
  airGfx = s.airGfx;
  airBgm = s.airBgm;
  battleBgm = s.battleBgm;
  ring = s.ring;
  heroDir_ = 2;
  std::memset(switches, 0, sizeof switches);
  for (int i = 0; i < 50; ++i) {
    invId[i] = kNone;
    invCnt[i] = 0;
  }
  vehicle = 0;
  palMode = 6;
  warpScreen = 0;
  warpPos = 0;
  warpPal = 6;
  warpBgm = 0;
  curBlock = ((curScreen >> 5) / 8) * 4 + (curScreen & 31) / 8;
}

std::vector<uint8_t> Game::buildSave() const {
  std::vector<uint8_t> d(270, 0);
  for (int n = 0; n < 8; ++n) {
    uint8_t* p = &d[n * 15];
    const Hero& h = heroes[n];
    p[0] = uint8_t(h.level);
    p[1] = uint8_t(h.status);
    core::wr16(p + 2, uint16_t(int16_t(h.hp)));
    core::wr16(p + 4, uint16_t(int16_t(h.mp)));
    core::wr32(p + 6, uint32_t(h.exp));
    std::memcpy(p + 0xA, h.equip, 5);
  }
  core::wr16(&d[0x78], uint16_t(curScreen));
  d[0x7A] = uint8_t(heroY * 20 + heroX);
  core::wr32(&d[0x7B], uint32_t(gold));
  d[0x7F] = uint8_t(fieldBgm);
  std::memcpy(&d[0x80], party, 4);
  std::memcpy(&d[0x84], switches, 25);
  std::memcpy(&d[0x9D], invId, 50);
  std::memcpy(&d[0xCF], invCnt, 50);
  core::wr16(&d[0x101], uint16_t(shipScreen));
  d[0x103] = uint8_t(shipPos);
  core::wr16(&d[0x104], uint16_t(airScreen));
  d[0x106] = uint8_t(airPos);
  core::wr16(&d[0x107], uint16_t(warpScreen));
  d[0x109] = uint8_t(warpPos);
  d[0x10A] = uint8_t(vehicle);
  d[0x10B] = uint8_t(palMode);
  d[0x10C] = uint8_t(warpPal);
  d[0x10D] = uint8_t(warpBgm);
  return d;
}

bool Game::applySave(const std::vector<uint8_t>& src) {
  if (src.size() < 266) return false;
  std::vector<uint8_t> d = src;
  d.resize(270, 0);
  for (int n = 0; n < 8; ++n) {
    const uint8_t* p = &d[n * 15];
    Hero& h = heroes[n];
    h.level = p[0];
    h.status = p[1];
    h.hp = core::rds16(p + 2);
    h.mp = core::rds16(p + 4);
    h.exp = int32_t(core::rd32(p + 6));
    std::memcpy(h.equip, p + 0xA, 5);
    h.recMaxHp = calcMaxHp(n);
    h.recMaxMp = calcMaxMp(n);
  }
  curScreen = core::rd16(&d[0x78]) & 1023;
  heroX = d[0x7A] % 20;
  heroY = d[0x7A] / 20;
  gold = long(core::rd32(&d[0x7B]));
  fieldBgm = d[0x7F];
  std::memcpy(party, &d[0x80], 4);
  std::memcpy(switches, &d[0x84], 25);
  std::memcpy(invId, &d[0x9D], 50);
  std::memcpy(invCnt, &d[0xCF], 50);
  shipScreen = core::rd16(&d[0x101]);
  shipPos = d[0x103];
  airScreen = core::rd16(&d[0x104]);
  airPos = d[0x106];
  warpScreen = core::rd16(&d[0x107]);
  warpPos = d[0x109];
  vehicle = d[0x10A];
  palMode = d[0x10B];
  warpPal = d[0x10C];
  warpBgm = d[0x10D];
  const StartInfo& s = db.start;
  shipGfx = s.shipGfx;
  shipBgm = s.shipBgm;
  airGfx = s.airGfx;
  airBgm = s.airBgm;
  battleBgm = s.battleBgm;
  ring = s.ring;
  curBlock = ((curScreen >> 5) / 8) * 4 + (curScreen & 31) / 8;
  heroDir_ = 2;
  return true;
}

static std::string slotName(int slot) { return "SAVEDATA." + std::to_string(slot + 1); }

bool Game::loadSlot(int slot) {
  std::vector<uint8_t> d;
  if (!core::tryReadFile(db.root, slotName(slot), d)) return false;
  return applySave(d);
}

bool Game::saveSlot(int slot) {
  std::string path = core::resolvePath(db.root, slotName(slot));
  return core::writeFileAbs(path, buildSave());
}

}  // namespace d1
