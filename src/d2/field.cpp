// D2 field: map loading, drawing, hero/NPC movement, vehicles, encounters, event start/end,
// main loop (spec 21).
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "d2/game.h"

namespace d2 {

namespace {
// direction helpers: 2 down, 4 left, 6 right, 8 up
inline int dirIndex(int d) { return d == 2 ? 0 : d == 4 ? 1 : d == 6 ? 2 : d == 8 ? 3 : -1; }
inline int ddx(int d) { return d == 4 ? -1 : d == 6 ? 1 : 0; }
inline int ddy(int d) { return d == 2 ? 1 : d == 8 ? -1 : 0; }
// edge bit of the current cell in direction d / of the target cell on the opposite side
inline int edgeFrom(int d) { return d == 2 ? 0x01 : d == 4 ? 0x02 : d == 6 ? 0x04 : d == 8 ? 0x08 : 0; }
inline int edgeTo(int d) { return d == 2 ? 0x08 : d == 4 ? 0x04 : d == 6 ? 0x02 : d == 8 ? 0x01 : 0; }
inline bool isDir(int d) { return d == 2 || d == 4 || d == 6 || d == 8; }
inline bool validEv(int v) { return v < 0xFA; }

const uint8_t kAutotile[64][4] = {
    {0x00, 0x01, 0x07, 0x07}, {0x0F, 0x01, 0x07, 0x07}, {0x00, 0x0E, 0x07, 0x07}, {0x0F, 0x0E, 0x07, 0x07},
    {0x04, 0x01, 0x04, 0x03}, {0x04, 0x0E, 0x04, 0x03}, {0x04, 0x01, 0x04, 0x0C}, {0x04, 0x0E, 0x04, 0x0C},
    {0x00, 0x05, 0x02, 0x05}, {0x0F, 0x05, 0x02, 0x05}, {0x00, 0x05, 0x0D, 0x05}, {0x0F, 0x05, 0x0D, 0x05},
    {0x06, 0x06, 0x02, 0x03}, {0x06, 0x06, 0x0D, 0x03}, {0x06, 0x06, 0x02, 0x0C}, {0x06, 0x06, 0x0D, 0x0C},
    {0x04, 0x05, 0x04, 0x05}, {0x06, 0x06, 0x07, 0x07}, {0x04, 0x01, 0x0A, 0x07}, {0x04, 0x0E, 0x0A, 0x07},
    {0x08, 0x06, 0x04, 0x03}, {0x08, 0x06, 0x04, 0x0C}, {0x06, 0x09, 0x02, 0x05}, {0x06, 0x09, 0x0D, 0x05},
    {0x00, 0x05, 0x07, 0x0B}, {0x0F, 0x05, 0x07, 0x0B}, {0x08, 0x09, 0x04, 0x05}, {0x06, 0x09, 0x07, 0x0B},
    {0x08, 0x06, 0x0A, 0x07}, {0x04, 0x05, 0x0A, 0x0B}, {0x00, 0x01, 0x0D, 0x03}, {0x00, 0x01, 0x02, 0x0C},
    {0x0F, 0x01, 0x02, 0x03}, {0x00, 0x0E, 0x02, 0x03}, {0x00, 0x01, 0x0D, 0x0C}, {0x0F, 0x01, 0x0D, 0x03},
    {0x00, 0x0E, 0x0D, 0x03}, {0x0F, 0x01, 0x02, 0x0C}, {0x00, 0x0E, 0x02, 0x0C}, {0x0F, 0x0E, 0x02, 0x03},
    {0x0F, 0x0E, 0x02, 0x0C}, {0x0F, 0x0E, 0x0D, 0x03}, {0x00, 0x0E, 0x0D, 0x0C}, {0x0F, 0x01, 0x0D, 0x0C},
    {0x0F, 0x0E, 0x0D, 0x0C}, {0x40, 0x00, 0x46, 0x45}, {0x50, 0x43, 0x54, 0x52}, {0x4C, 0x20, 0x56, 0x31},
    {0x2E, 0x35, 0x42, 0x20}, {0x30, 0x34, 0x2F, 0x30}, {0x38, 0x2F, 0x31, 0x39}, {0x39, 0x32, 0x00, 0x00},
    {0x01, 0x01, 0x01, 0x02}, {0x02, 0x01, 0x01, 0x01}, {0x01, 0x01, 0x01, 0x02}, {0x02, 0x01, 0x01, 0x01},
    {0x01, 0x01, 0x01, 0x01}, {0x02, 0x01, 0x01, 0x01}, {0x01, 0xD1, 0x02, 0x80}, {0x80, 0x01, 0x01, 0x01},
    {0x01, 0x01, 0x01, 0x80}, {0xC5, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00}, {0x01, 0x00, 0xD6, 0x0B},
};

const uint8_t kZeroPattern[kPatternBytes] = {};

// Pixel color from a pattern (planes only).
inline uint8_t patColor(const uint8_t* pat, int x, int y) {
  const int o = y * 4 + (x >> 3);
  const int sh = 7 - (x & 7);
  return uint8_t(((pat[128 + o] >> sh) & 1) | (((pat[256 + o] >> sh) & 1) << 1) | (((pat[384 + o] >> sh) & 1) << 2) |
                 (((pat[512 + o] >> sh) & 1) << 3));
}
inline bool patMask(const uint8_t* pat, int x, int y) { return (pat[y * 4 + (x >> 3)] >> (7 - (x & 7))) & 1; }

struct Cell {
  uint8_t px[32][32];
  void chip(const uint8_t* pat) {
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) px[y][x] = patColor(pat, x, y);
  }
  void quarter(const uint8_t* pat, int srcQ, int dstQ) {
    const int sx = (srcQ & 1) * 16, sy = (srcQ >> 1) * 16, dx = (dstQ & 1) * 16, dy = (dstQ >> 1) * 16;
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) px[dy + y][dx + x] = patColor(pat, sx + x, sy + y);
  }
  void sprite(const uint8_t* pat, int ox, int oy) {
    for (int y = 0; y < 32; ++y) {
      const int ty = y + oy;
      if (ty < 0 || ty >= 32) continue;
      for (int x = 0; x < 32; ++x) {
        const int tx = x + ox;
        if (tx < 0 || tx >= 32) continue;
        uint8_t v = patMask(pat, x, y) ? 0 : px[ty][tx];
        px[ty][tx] = uint8_t(v | patColor(pat, x, y));
      }
    }
  }
};
}  // namespace

// =============================================================================================
// patterns and sprites (spec 21 §4.3/4.4)
const uint8_t* Game::pattern(int no) const {
  if (no < 0) return kZeroPattern;
  if (no < 0xA0) return chipset.pattern(no);
  if (no <= 0xC0) return cache[no - 0xA0];
  if (no < 0x100) return kZeroPattern;
  const int k = no - 0x100;
  return chrSlot[(k / 64) % 4].pattern(k % 64);
}

void Game::loadSpriteToCache(int base, int chrFile, int pat) {
  const int b = base - 0xA0;
  for (int i = 0; i < 8; ++i) std::memset(cache[b + i], 0, kPatternBytes);
  const PatternSet* src = nullptr;
  PatternSet tmp;
  for (int s = 0; s < 4; ++s)
    if (map.hdr.chr[s] == chrFile && chrLoaded[s] == chrFile) {
      src = &chrSlot[s];
      break;
    }
  if (!src) {
    loadPatterns(root, numberedName("ECD_CHR/CHR", chrFile, ".ECD"), 64, tmp);
    src = &tmp;
  }
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 2; ++c) {
      const int p = pat + 8 * r + c;
      if (p <= 63) std::memcpy(cache[b + 2 * r + c], src->pattern(p), kPatternBytes);
    }
}

void Game::reloadHeroSprite() {
  const Member& m = members[leader()];
  loadSpriteToCache(0xA0, m.chrFile, m.chrPat);
}

void Game::reloadVehicleSprites() {
  loadSpriteToCache(0xA8, st.shipChr, st.shipPat);
  loadSpriteToCache(0xB0, st.airChr, st.airPat);
}

void Game::reloadAllSprites() {
  reloadVehicleSprites();
  reloadHeroSprite();
}

// =============================================================================================
// map loading (spec 21 §4)
int Game::chipAt(int x, int y) const { return map.grid[wrap(y) % kMapDim][wrap(x) % kMapDim]; }
int Game::attrAt(int x, int y) const { return chips.attr[chipAt(x, y)]; }

void Game::loadMapGraphics() {
  for (int s = 0; s < 4; ++s) {
    const int f = map.hdr.chr[s];
    if (chrLoaded[s] != f) {
      loadPatterns(root, numberedName("ECD_CHR/CHR", f, ".ECD"), 64, chrSlot[s]);
      chrLoaded[s] = f;
    }
  }
  if (chipsetLoaded != map.hdr.chipset) {
    loadPatterns(root, numberedName("ECD_MCP/MCP", map.hdr.chipset, ".ECD"), 160, chipset);
    if (chipset.hasPal) {
      std::memcpy(basePal, chipset.pal, sizeof(basePal));
      for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 3; ++k) curPal[i][k] = uint8_t(basePal[i][k] * 17);
    }
    loadChipAttr(root, map.hdr.chipset, chips);
    chipsetLoaded = map.hdr.chipset;
  }
  for (int i = 0; i < 4; ++i) std::memcpy(cache[0xB8 - 0xA0 + i], chipset.pattern(i), kPatternBytes);
}

void Game::loadCurrentMapData() {
  loadMap(root, st.curMap, map, st, map.version);
  loadMapGraphics();
  std::memset(upperMap, 0xFF, sizeof(upperMap));
  std::memset(lowerMap, 0xFF, sizeof(lowerMap));
  for (int i = 0; i < kMaxEvents; ++i) {
    EventRec* e = map.events[i];
    if (!e) continue;
    const int l = chips.attr[map.grid[e->y % kMapDim][e->x % kMapDim]] & 0x60;
    e->layer = (l == 0x20 || l == 0x60) ? 0x20 : 0x00;
    e->timer = uint8_t(rng.next() % 40 + 10);
  }
}

void Game::loadCurrentMap() {
  loadCurrentMapData();
  reapplyTone();
}

void Game::enterMap() {
  if (st.curMap != st.loadedMap) {
    st.loadedMap = st.curMap;
    loadCurrentMap();
  }
  updatePages();
  st.scroll = 0;
  st.steps = 0;
  const int n = map.n();
  if (map.hdr.loop) {
    viewX = n ? (st.x + n - 9) % n : 0;
    viewY = n ? (st.y + n - 6) % n : 0;
  } else {
    viewX = st.x - 9;
    viewY = st.y - 6;
  }
  updateHeroLayer();
}

void Game::updateHeroLayer() {
  const int l = attrAt(st.x, st.y) & 0x60;
  if (l == 0x00)
    st.layer = 0x00;
  else if (l == 0x20)
    st.layer = 0x20;
  else if (l == 0x60)
    st.layer = 0x60;
}

// =============================================================================================
// pages (spec 21 §7)
bool Game::pageCondition(const EventPage& p) const {
  switch (p.cond) {
    case 1: return p.condSwitch <= 1000 && switches[p.condSwitch];
    case 2: return hasItem(p.condItem);
    case 3: return inParty(p.condMember & 0xFF);
    case 4: return st.gold >= p.condGold;
    default: return false;
  }
}

int Game::activePage(int i) const {
  if (!eventExists(i)) return -1;
  const EventRec* e = map.events[i];
  for (int p = 3; p >= 0; --p)
    if (pageCondition(e->pages[p])) return p;
  return e->pages[0].cond == 0 ? 0 : -1;
}

void Game::updatePages() {
  static const uint8_t dirFromRow[4] = {8, 6, 2, 4};
  for (int i = 0; i < kMaxEvents; ++i) {
    EventRec* e = map.events[i];
    if (!e || e->page1 == 9) continue;
    const int p = activePage(i);
    if (e->page1 == p + 1) continue;
    if (e->page1 != 0) e->dir = 0;
    e->page1 = uint8_t(p + 1);
    e->redrawReq = 1;
    if (p == -1) {
      eraseEvent(i, false);
      e->page1 = 0;
      e->active = 0;
      continue;
    }
    (e->layer == 0 ? upperMap : lowerMap)[e->y % kMapDim][e->x % kMapDim] = uint8_t(i);
    if (e->prog == 32 || e->dir == 0) {
      e->prog = 0;
      std::memset(e->dirProg, 0, 4);
    }
    if (e->prog == 0 && e->dir == 0) e->dir = dirFromRow[(e->pages[p].gfx >> 3) & 3];
    e->active = 1;
  }
}

void Game::eraseEvent(int i, bool permanent) {
  EventRec* e = ev(i);
  if (!e) return;
  const int x = e->x % kMapDim, y = e->y % kMapDim;
  if (lowerMap[y][x] == i) {
    lowerMap[y][x] = 0xFD;
    if (!isOldBuild(build) && upperMap[y][x] == i) upperMap[y][x] = 0xFD;
  } else if (upperMap[y][x] == i) {
    upperMap[y][x] = 0xFD;
  }
  if (e->prog) {
    int sx = e->x, sy = e->y;
    if (e->dirProg[0]) sy -= 1;
    else if (e->dirProg[1]) sx += 1;
    else if (e->dirProg[2]) sx -= 1;
    else if (e->dirProg[3]) sy += 1;
    sx = wrap(sx) % kMapDim;
    sy = wrap(sy) % kMapDim;
    if (lowerMap[sy][sx] == i) lowerMap[sy][sx] = 0xFD;
    if (upperMap[sy][sx] == i) upperMap[sy][sx] = 0xFD;
  }
  if (permanent) {
    e->page1 = 9;
    e->active = 0;
  } else {
    e->page1 = 0;
  }
}

int Game::eventAll(int x, int y) const {
  x = wrap(x) % kMapDim;
  y = wrap(y) % kMapDim;
  const int u = upperMap[y][x];
  if (validEv(u) && eventExists(u)) return u;
  const int l = lowerMap[y][x];
  if (validEv(l) && eventExists(l)) return l;
  return -1;
}

int Game::sameLayerValue(int vx, int vy) const {
  const int x = wrap(viewX + vx), y = wrap(viewY + vy);
  if (st.shipMap == st.curMap && st.shipX == x && st.shipY == y) return kShipValue;
  // "足元チップ" (the chip underfoot) is the chip at the queried position (spec 21 §5.6, v1.1; D2F-Q1).
  if ((attrAt(x, y) & 0x60) == 0x60 || st.layer != 0) return lowerMap[y % kMapDim][x % kMapDim];
  return upperMap[y % kMapDim][x % kMapDim];
}

int Game::otherLayerValue(int vx, int vy) const {
  const int x = wrap(viewX + vx), y = wrap(viewY + vy);
  if (st.shipMap == st.curMap && st.shipX == x && st.shipY == y) return kShipValue;
  if (st.layer == 0) return lowerMap[y % kMapDim][x % kMapDim];
  return upperMap[y % kMapDim][x % kMapDim];
}

bool Game::blockingEventAt(int mx, int my) const {
  mx = wrap(mx);
  my = wrap(my);
  const int l = attrAt(mx, my) & 0x60;
  const int i = (l == 0 || (l == 0x40 && st.layer == 0)) ? upperMap[my % kMapDim][mx % kMapDim]
                                                           : lowerMap[my % kMapDim][mx % kMapDim];
  if (i >= kMaxEvents || !eventExists(i)) return false;
  const int p = activePage(i);
  return p != -1 && map.events[i]->pages[p].priority == 0;
}

// =============================================================================================
// drawing (spec 21 §5)
int Game::eventPatternNo(int i, const EventPage& p) const {
  const EventRec* e = map.events[i];
  int g = p.gfx;
  if (g == 0) return 0;
  const uint32_t A = frameCounter;
  if (p.anim <= 1) {
    const bool dirSep = p.dirMode != 2 || e->dirFix == 1;
    if (dirSep) {
      if ((g & 0x1F) > 15) g -= 16;
      if ((g & 0x1F) > 7) g -= 8;
    }
    if (p.anim == 0) g = (g & ~1) + int((A / 16) & 1);
    if (dirSep) {
      static const int off[5] = {84, 16, 24, 8, 0};
      const int use = e->dirFix >= 2 ? e->dirFix : e->dir;
      g += off[(use / 2) % 5];
    }
  } else {
    if ((g & 0x0F) > 7) g -= 8;
    static const int add[4] = {0, 1, 8, 9};
    const int t = p.anim == 2 ? int((A / 16) & 3) : int((A / 8) & 3);
    g += add[t];
  }
  return g;
}

void Game::drawField(bool hideHero) {
  int dx = 0, dy = 0;
  if (st.scroll) {
    if (st.dirProg[1]) dx = -st.dirProg[1];
    if (st.dirProg[2]) dx = st.dirProg[2];
    if (st.dirProg[0]) dy = st.dirProg[0];
    if (st.dirProg[3]) dy = -st.dirProg[3];
  }
  const int n = map.n();
  const bool loop = map.hdr.loop != 0;
  const bool air = st.vehicle == 2;
  const uint32_t A = frameCounter;
  const int f = air ? 1 : 0;
  Cell cell;

  auto drawEv = [&](int X, int ox, int oy, int lvl, bool upper) {
    if (X < 0) return;
    const EventRec* e = map.events[X];
    const int p = activePage(X);
    if (p < 0) return;
    if (lvl == 0x40 && e->layer == 0x20) return;
    const EventPage& pg = e->pages[p];
    if (upper ? pg.priority != 2 : pg.priority == 2) return;
    const int g = eventPatternNo(X, pg);
    if (g == 0) return;
    cell.sprite(pattern(g), ox, oy);
  };
  auto layerPass = [&](int E, int U, int R, int L, int D, int lvl, bool upper) {
    const EventRec* e = E >= 0 ? map.events[E] : nullptr;
    // normal pass: E still and not under a bridge; upper pass: E still (spec 21 §5.7 steps 6/11)
    if (e && e->prog == 0 && (upper || !(lvl == 0x40 && e->layer == 0x20))) {
      drawEv(E, 0, 0, lvl, upper);
      return;
    }
    auto dp = [&](int X, int k) -> int { return X >= 0 ? map.events[X]->dirProg[k] : 0; };
    int p;
    if ((p = dp(E, 0))) drawEv(E, 0, p - 32, lvl, upper);
    if ((p = dp(U, 3))) drawEv(U, 0, -p, lvl, upper);
    if ((p = dp(E, 1))) drawEv(E, 32 - p, 0, lvl, upper);
    if ((p = dp(R, 2))) drawEv(R, p, 0, lvl, upper);
    if ((p = dp(E, 2))) drawEv(E, p - 32, 0, lvl, upper);
    if ((p = dp(L, 1))) drawEv(L, -p, 0, lvl, upper);
    if ((p = dp(E, 3))) drawEv(E, 0, 32 - p, lvl, upper);
    if ((p = dp(D, 0))) drawEv(D, 0, p, lvl, upper);
  };

  int heroRow;
  {
    static const int rowOff[5] = {21, 4, 6, 2, 0};
    const int use = st.dirFix ? st.dirFix : st.dir;
    heroRow = rowOff[(use / 2) % 5];
  }
  int heroFrame;
  if (st.vehicle == 0) {
    heroFrame = int((A / 16) & 1);
  } else {
    const int kind = st.vehicle == 1 ? st.shipAnim : st.airAnim;
    heroFrame = kind == 0 ? int((A / 16) & 1) : kind == 1 ? int((A / 4) & 1) : 0;
  }
  const int heroPat = 0xA0 + st.heroSprite + heroRow + heroFrame;
  int heroDir = 0, heroP = 0;
  if (st.scroll) {
    for (int k = 0; k < 4; ++k)
      if (st.dirProg[k]) {
        heroDir = k;
        heroP = st.dirProg[k];
      }
  }

  for (int vy = -1; vy <= 13; ++vy)
    for (int vx = -1; vx <= 19; ++vx) {
      const int sx = 16 + 32 * vx - dx, sy = -16 + 32 * vy - dy;
      if (sx >= 624 || sx + 32 <= 16 || sy >= 384 || sy + 32 <= 0) continue;
      if (st.msgOpen && st.scroll == 0 && vx >= 2 && vx <= 16 && vy >= 8 && vy <= 11) continue;
      int mx = viewX + vx, my = viewY + vy;
      bool outside = false;
      if (loop) {
        mx = wrap(mx);
        my = wrap(my);
      } else if (mx < 0 || my < 0 || mx >= n || my >= n) {
        outside = true;
      }
      auto drawChip = [&](int c) {
        if (c < 0xC0) {
          cell.chip(pattern(c));
        } else {
          const uint8_t* row = kAutotile[c & 0x3F];
          for (int q = 0; q < 4; ++q) {
            const int d = row[q];
            cell.quarter(cache[0xB8 - 0xA0 + ((d >> 2) & 3)], d & 3, q);
          }
        }
      };
      if (outside) {
        drawChip(map.hdr.outerChip);
      } else {
        const int c = map.grid[my % kMapDim][mx % kMapDim];
        const int a = chips.attr[c];
        drawChip(c);
        if (!((a & 0x10) && !air)) {
          const int lvl = a & 0x60;
          const int E = eventAll(mx, my);
          const int U = eventAll(mx, my - 1), R = eventAll(mx + 1, my), L = eventAll(mx - 1, my),
                    D = eventAll(mx, my + 1);
          if (!(a & 0x10)) layerPass(E, U, R, L, D, lvl, false);
          if (lvl != 0x40 && st.shipMap == st.curMap && st.shipX == mx && st.shipY == my)
            cell.sprite(cache[0xAA - 0xA0 + ((A / 16) & 1)], 0, 0);
          if (st.airMap == st.curMap && st.airX == mx && st.airY == my) cell.sprite(cache[0xB2 - 0xA0], 0, 0);
          if (air) layerPass(E, U, R, L, D, lvl, true);
          if (!st.hidden && !hideHero && !(lvl == 0x40 && st.layer == 0x20 && !air)) {
            const uint8_t* hp = pattern(heroPat);
            const int bx = 9, by = 6 - f;
            if (st.scroll == 0) {
              if (vx == bx && vy == by) cell.sprite(hp, 0, 0);
            } else {
              // heroDir: 0 down, 1 left, 2 right, 3 up
              if (vx == bx && vy == by) {
                const int ox = heroDir == 2 ? heroP : heroDir == 1 ? -heroP : 0;
                const int oy = heroDir == 0 ? heroP : heroDir == 3 ? -heroP : 0;
                cell.sprite(hp, ox, oy);
              }
              int tx = bx, ty = by, ox = 0, oy = 0;
              switch (heroDir) {
                case 3: ty = 5 - f; oy = 32 - heroP; break;
                case 0: ty = 7 - f; oy = heroP - 32; break;
                case 2: tx = 10; ox = heroP - 32; break;
                case 1: tx = 8; ox = 32 - heroP; break;
              }
              if (vx == tx && vy == ty) cell.sprite(hp, ox, oy);
            }
          }
          if (!air) layerPass(E, U, R, L, D, lvl, true);
        }
      }
      // copy to screen, clipped to X 16..639, Y 0..383
      for (int y = 0; y < 32; ++y) {
        const int py = sy + y;
        if (py < 0 || py >= 384) continue;
        for (int x = 0; x < 32; ++x) {
          const int px = sx + x;
          if (px < 16 || px >= 640) continue;
          scr.pix[py * core::kScreenW + px] = cell.px[y][x];
        }
      }
    }
}

// =============================================================================================
// hero movement (spec 21 §6)
void Game::heroStep(int d, bool fast) {
  if (st.scroll != 0) return;
  if (!heroPassable(d)) return;
  st.scroll = uint8_t(fast ? 16 : 8);
  st.x = uint8_t(wrap(st.x + ddx(d)));
  st.y = uint8_t(wrap(st.y + ddy(d)));
  st.fast = fast ? 1 : 0;
  std::memset(st.dirProg, 0, 4);
  st.dirProg[dirIndex(d)] = st.scroll;
  updateHeroLayer();
  st.heroMoved = 1;
}

bool Game::heroPassable(int d) {
  st.dir = uint8_t(d);
  if (!isDir(d)) return false;
  const int n = map.n();
  const int tx = st.x + ddx(d), ty = st.y + ddy(d);
  const bool loop = map.hdr.loop != 0;
  if (!loop && (tx < 0 || ty < 0 || tx >= n || ty >= n)) return false;
  if (!((st.hidden == 0 || !st.eventRunning) && !st.forced)) return true;
  const int A = attrAt(st.x, st.y);
  const int B = attrAt(tx, ty);
  const int tvx = 9 + ddx(d), tvy = 6 + ddy(d);
  if (st.vehicle == 0) {
    const bool exception = st.eventRunning && sameLayerValue(tvx, tvy) == kShipValue && canDisembarkWalk(d);
    if (!exception) {
      if ((A & 0x60) != 0x60 && ((st.layer == 0 && (B & 0x60) == 0x20) || (st.layer == 0x20 && (B & 0x60) == 0)))
        return false;
      if (blockingEventAt(tx, ty) || (A & edgeFrom(d)) || (B & edgeTo(d))) {
        tryStartEvent(sameLayerValue(tvx, tvy), 1);
        return false;
      }
    }
  } else if (st.vehicle == 1) {
    if (!st.eventRunning || !canDisembarkShip(d)) {
      if (blockingEventAt(tx, ty)) {
        tryStartEvent(sameLayerValue(tvx, tvy), 1);
        return false;
      }
      if (!(B & 0x80)) return false;
    } else {
      // disembark on the spot (BGM unchanged)
      st.vehicle = 0;
      st.heroSprite = 0;
      st.shipMap = st.curMap;
      st.shipX = st.x;
      st.shipY = st.y;
      st.steps = 0;
    }
  }
  return true;
}

int Game::heroProcess() {
  if (st.eventRunning && st.scroll == 0) {
    if (st.moveCmd >= 3) st.heroMoved = 0;
    doMoveCommandHero();
  }
  if (st.scroll == 0) {
    drawField();
    return 0;
  }
  bool arrive = false;
  if (!st.eventRunning) {
    if (st.vehicle == 0 && st.poison) {
      if (st.scroll == 24) {
        setTone(160);
        for (int k = 0; k < st.partyCount && k < 5; ++k) {
          const int c = st.party[k];
          if (c <= 11 && members[c].status[0] && members[c].hp > 1) --members[c].hp;
        }
      } else if (st.scroll == 32) {
        setTone(100);
      }
    }
    if (st.scroll == 32) arrive = true;
  }
  drawField();
  if (st.scroll == 32) {
    const bool loop = map.hdr.loop != 0;
    if (st.dirProg[0]) viewY += 1;
    if (st.dirProg[1]) viewX -= 1;
    if (st.dirProg[2]) viewX += 1;
    if (st.dirProg[3]) viewY -= 1;
    if (loop) {
      viewX = wrap(viewX);
      viewY = wrap(viewY);
    }
    st.scroll = 0;
    std::memset(st.dirProg, 0, 4);
  } else {
    const int add = st.fast ? 16 : 8;
    st.scroll = uint8_t(st.scroll + add);
    for (auto& p : st.dirProg)
      if (p) p = uint8_t(p + add);
  }
  if (!arrive) return 0;
  if (st.vehicle == 0) ++st.steps;
  int r = tryStartEvent(sameLayerValue(9, 6), 3);
  if (r) return r;
  r = tryStartEvent(otherLayerValue(9, 6), 3);
  if (r) return r;
  if (st.vehicle) return 0;
  return encounterCheck();
}

void Game::forcedStep() {
  st.forced = 1;
  heroStep(st.dir, false);
  st.forced = 0;
  recordLatch();
}

// =============================================================================================
// NPC (spec 21 §8)
void Game::npcCycle() {
  std::memset(npcDone, 0, sizeof(npcDone));
  for (int i = 0; i < kMaxEvents; ++i) npcProcess(i);
}

void Game::npcProcess(int i) {
  if (i < 0 || i >= kMaxEvents || npcDone[i]) return;
  npcDone[i] = true;
  EventRec* e = map.events[i];
  if (!e || !e->active) return;
  const int p = activePage(i);
  if (p < 0) return;
  const EventPage& pg = e->pages[p];
  if (e->prog == 0) {
    if (!st.eventRunning) {
      ++e->timer;
      if (e->timer > 35) {
        const int type = pg.moveType;
        if (type == 1 || type == 2) {
          const uint8_t orig = e->dir;
          const int r1 = rng.next();
          int d;
          if (r1 & 3)
            d = (rng.next() & 3) * 2 + 2;
          else
            d = e->dir;
          if (!npcTryStep(i, d, false)) {
            if (!isOldBuild(build)) e->dir = orig;
          } else {
            e->timer = uint8_t(type == 1 ? rng.next() % 10 : rng.next() % 4 + 20);
          }
        } else if (type == 3 || type == 4) {
          int first, second;
          if (type == 3) {
            first = e->dir == 8 ? 8 : 2;
            second = first == 8 ? 2 : 8;
          } else {
            first = e->dir == 4 ? 4 : 6;
            second = first == 4 ? 6 : 4;
          }
          bool ok = npcTryStep(i, first, false);
          if (!ok) ok = npcTryStep(i, second, false);
          e->timer = ok ? 28 : 0;
        } else if (type == 5 || type == 6) {
          const int dx = int(st.x) - int(e->x), dy = int(st.y) - int(e->y);
          int d = 0;
          if (std::abs(dx) < 9 && std::abs(dy) < 7 && st.vehicle == 0) {
            const int m = rng.next() % 5;
            if (m <= 1)
              d = dx > 0 ? 6 : dx < 0 ? 4 : 0;
            else if (m <= 3)
              d = dy > 0 ? 2 : dy < 0 ? 8 : 0;
            else
              d = (rng.next() & 3) * 2 + 2;
          } else {
            d = (rng.next() & 3) * 2 + 2;
          }
          // direction "none" -> do nothing (spec 21 §8.3)
          if (d && npcTryStep(i, d, false))
            e->timer = uint8_t(type == 5 ? rng.next() % 8 + 20 : rng.next() % 4 + 28);
        }
      }
    }
  } else if (e->prog == 32) {
    e->prog = 0;
    std::memset(e->dirProg, 0, 4);
    if (!st.eventRunning && pg.moveType != 0 && pg.trigger == 3 && e->x == st.x && e->y == st.y) {
      st.pendingKind = 3;
      st.pendingEvent = uint8_t(i);
    }
  } else {
    const int add = e->fast ? 16 : 8;
    e->prog = uint8_t(e->prog + add);
    for (auto& d : e->dirProg)
      if (d) d = uint8_t(d + add);
  }
  if (st.eventRunning && e->prog == 0) doMoveCommandEvent(i);
}

bool Game::npcTryStep(int i, int d, bool fast) {
  EventRec* e = map.events[i];
  if (!e || !isDir(d)) return false;
  const int tx = wrap(e->x + ddx(d)), ty = wrap(e->y + ddy(d));
  const int ca = chipAt(e->x, e->y), cb = chipAt(tx, ty);
  const int a = chips.attr[ca], b = chips.attr[cb];
  e->dir = uint8_t(d);
  const bool sea = isOldBuild(build) || (chips.attr[0] & 0x0F) == 0x0F;
  if (sea && (ca < 4 || ca >= 0xA0)) {
    if (cb >= 4 && cb < 0xA0) return false;
  } else if ((a & edgeFrom(d)) || (b & edgeTo(d))) {
    return false;
  }
  const int p = activePage(i);
  const int priority = p >= 0 ? e->pages[p].priority : 0;
  const int moveType = p >= 0 ? e->pages[p].moveType : 0;
  int res = 0;
  if (!map.hdr.loop && (std::abs(tx - int(e->x)) >= 2 || std::abs(ty - int(e->y)) >= 2)) {
    res = 1;
  } else {
    if ((a & 0x60) == 0x60) {
      if ((b & 0x60) == 0)
        e->layer = 0;
      else if ((b & 0x60) == 0x20)
        e->layer = 0x20;
    } else if ((e->layer == 0 && (b & 0x60) == 0x20) || (e->layer == 0x20 && (b & 0x60) == 0)) {
      res = 1;
    }
    if (!res) {
      uint8_t(*m)[kMapDim] = (e->layer != 0 || (b & 0x60) == 0x60) ? lowerMap : upperMap;
      int o = m[ty % kMapDim][tx % kMapDim];
      if (!(o >= 0xFA || o == i)) {
        if (st.eventRunning) {
          if (!npcDone[o]) npcProcess(o);
          o = m[ty % kMapDim][tx % kMapDim];
          if (!(o >= 0xFA || o == i)) res = 1;
        } else {
          res = 1;
        }
      }
    }
    // Step 4.4 decides the result on its own ("otherwise 0"): a pass-through event may step
    // onto the hero even when a parked vehicle shares the hero's cell (step 4.5 is skipped).
    bool decided = false;
    if (!res && tx == st.x && ty == st.y && (st.hidden == 0 || !st.eventRunning) &&
        (st.layer == 0x60 || e->layer == st.layer)) {
      res = priority == 0 ? 2 : 0;
      decided = true;
    }
    if (!res && !decided && st.shipMap == st.curMap && st.shipX == tx && st.shipY == ty) res = 1;
    if (!res && !decided && st.airMap == st.curMap && st.airX == tx && st.airY == ty) res = 1;
  }
  if (!st.eventRunning && moveType >= 5 && res == 2) {
    e->timer = uint8_t(rng.next() % 4 + 20);
    e->dir = uint8_t(d);
    st.pendingKind = 1;
    st.pendingEvent = uint8_t(i);
  }
  if (res != 0) return false;
  e->fast = fast ? 1 : 0;
  e->prog = uint8_t(fast ? 16 : 8);
  std::memset(e->dirProg, 0, 4);
  e->dirProg[dirIndex(d)] = e->prog;
  npcMoveTo(i, tx, ty);
  return true;
}

void Game::npcMoveTo(int i, int tx, int ty) {
  EventRec* e = map.events[i];
  const int ox = e->x % kMapDim, oy = e->y % kMapDim;
  e->x = uint8_t(tx);
  e->y = uint8_t(ty);
  if ((attrAt(tx, ty) & 0x60) == 0x60) {
    e->layer = 0x20;
    upperMap[ty % kMapDim][tx % kMapDim] = uint8_t(i);
  }
  (e->layer == 0 ? upperMap : lowerMap)[ty % kMapDim][tx % kMapDim] = uint8_t(i);
  if (upperMap[oy][ox] == i) upperMap[oy][ox] = 0xFD;
  if (lowerMap[oy][ox] == i) lowerMap[oy][ox] = 0xFD;
}

static int rotate(int d, bool cw) {
  if (cw) return d == 2 ? 4 : d == 4 ? 8 : d == 8 ? 6 : d == 6 ? 2 : d;
  return d == 2 ? 6 : d == 6 ? 8 : d == 8 ? 4 : d == 4 ? 2 : d;
}

void Game::doMoveCommandHero() {
  const int m = st.moveCmd;
  if (m >= 3 && m <= 14) {
    const int d = (m / 3) * 2, kind = m % 3;
    if (kind == 0)
      st.dir = uint8_t(d);
    else
      heroStep(d, kind == 2);
  } else if (m >= 15 && m <= 17) {
    heroRot = (heroRot + 1) % (m == 17 ? 2 : 3);
    if (heroRot == 1 && isDir(st.dir)) st.dir = uint8_t(rotate(st.dir, m != 16));
  }
}

void Game::doMoveCommandEvent(int i) {
  EventRec* e = ev(i);
  if (!e) return;
  const int m = e->moveCmd;
  if (m >= 3 && m <= 14) {
    const int d = (m / 3) * 2, kind = m % 3;
    if (kind == 0)
      e->dir = uint8_t(d);
    else if (activePage(i) >= 0)
      npcTryStep(i, d, kind == 2);
  } else if (m >= 15 && m <= 17) {
    eventRot = (eventRot + 1) % (m == 17 ? 2 : 3);
    if (eventRot == 1 && isDir(e->dir)) e->dir = uint8_t(rotate(e->dir, m != 16));
  }
}

void Game::clearMoveCommands() {
  st.moveCmd = 0;
  for (int i = 0; i < kMaxEvents; ++i)
    if (map.events[i]) {
      map.events[i]->moveCmd = 0;
      map.events[i]->dirFix = 0;
    }
}

void Game::setHeroMoveCommand(int m) {
  st.moveCmd = uint8_t(m);
  if (m == 1)
    st.dirFix = st.dir;
  else if (m == 2)
    st.dirFix = 0;
  else if (m >= 3 && m <= 14 && st.scroll == 0)
    st.dir = uint8_t((m / 3) * 2);
}

void Game::setEventMoveCommand(int i, int m) {
  EventRec* e = ev(i);
  if (!e) return;
  e->moveCmd = uint8_t(m);
  e->redrawKind = 0;
  if (m == 1)
    e->dirFix = e->dir;
  else if (m == 2)
    e->dirFix = 1;
  else if (m >= 3 && m <= 14 && e->prog == 0)
    e->dir = uint8_t((m / 3) * 2);
}

// =============================================================================================
// time advance during events (spec 22 §6.5)
void Game::shipBoardCheck() {
  if (st.scroll == 0 && st.vehicle == 0 && st.shipMap == st.curMap && st.shipX == st.x && st.shipY == st.y)
    boardShip(false);
}

void Game::tick(int n) {
  waitCounter = 0;
  for (int k = 0; k < n; ++k) {
    checkStop();
    while (waitCounter < uint32_t(3 * k)) vsync();
    npcCycle();
    heroProcess();
    shipBoardCheck();
  }
}

void Game::waitHeroStill() {
  while (st.scroll != 0) {
    waitFrames(3);
    npcCycle();
    heroProcess();
    shipBoardCheck();
  }
}

void Game::waitEventStill(int i) {
  for (;;) {
    EventRec* e = ev(i);
    if (!e || e->prog == 0) return;
    if (e->prog == 32 && build != Build::B9606) return;
    waitFrames(3);
    npcCycle();
    heroProcess();
    shipBoardCheck();
  }
}

// =============================================================================================
// vehicles (spec 21 §9)
bool Game::canDisembarkWalk(int d) {
  if (!isDir(d)) return true;
  if (attrAt(st.x, st.y) & edgeFrom(d)) return false;
  if (blockingEventAt(st.x + ddx(d), st.y + ddy(d))) return false;
  return true;
}

bool Game::canDisembarkShip(int d) {
  if (!isDir(d)) return true;
  const int fx = st.x + ddx(d), fy = st.y + ddy(d);
  const int B = attrAt(fx, fy);
  if (B & edgeTo(d)) return false;
  if (blockingEventAt(fx, fy)) return false;
  st.layer = (B & 0x60) ? 0x20 : 0x00;
  return true;
}

void Game::boardShip(bool withBgm) {
  st.vehicle = 1;
  st.heroSprite = 8;
  st.shipMap = 0xFFFF;
  std::memcpy(st.prevBgm, st.curBgm, 9);
  if (withBgm && !bgmEqual(st.curBgm, st.shipBgm)) {
    std::memcpy(st.curBgm, st.shipBgm, 9);
    playBgm(st.curBgm);
  }
}

void Game::leaveShip() {
  st.vehicle = 0;
  st.heroSprite = 0;
  st.shipMap = st.curMap;
  st.shipX = st.x;
  st.shipY = st.y;
  st.steps = 0;
  forcedStep();
  if (!bgmEqual(st.prevBgm, st.curBgm)) {
    std::memcpy(st.curBgm, st.prevBgm, 9);
    std::memcpy(st.prevBgm, st.shipBgm, 9);
    playBgm(st.curBgm);
  }
}

void Game::airshipAnim(int startY, int dy) {
  waitCounter = 0;
  const uint8_t* pat = cache[0xB2 - 0xA0];
  for (int i = 0; i <= 8; ++i) {
    while (waitCounter < uint32_t(3 * i)) vsync();
    drawField(true);
    const int y0 = startY + dy * i;
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x)
        if (patMask(pat, x, y)) scr.pset(304 + x, y0 + y, patColor(pat, x, y));
  }
}

void Game::shadowUpdate() {
  shadowCounter = (shadowCounter + 1) % 3;
  if (shadowCounter != 0) {
    core::textWriteD2(scr, 39, 12, "\xEB\xB3", core::kTextBlack, gaiji);
  } else {
    scr.setTextCell(39, 12, core::kTextWhite, nullptr);
    scr.setTextCell(40, 12, core::kTextWhite, nullptr);
  }
}

void Game::takeOff() {
  st.vehicle = 2;
  st.heroSprite = 0x10;
  st.airMap = 0xFFFF;
  st.dir = 6;
  st.hidden = 0;
  airshipAnim(176, -4);
  shadowOn = true;
  recordLatch();
  drawField();
  std::memcpy(st.prevBgm, st.curBgm, 9);
  if (!bgmEqual(st.curBgm, st.airBgm)) {
    std::memcpy(st.curBgm, st.airBgm, 9);
    playBgm(st.curBgm);
  }
}

void Game::land() {
  const int A = attrAt(st.x, st.y);
  const bool no = (A & 0x0F) == 0x0F || (A & 0x10) || eventAll(st.x, st.y) >= 0 ||
                  (st.shipMap == st.curMap && st.shipX == st.x && st.shipY == st.y);
  shadowOn = false;
  scr.setTextCell(39, 12, core::kTextWhite, nullptr);
  scr.setTextCell(40, 12, core::kTextWhite, nullptr);
  airshipAnim(144, 4);
  st.steps = 0;
  if (no) {
    airshipAnim(176, -4);
    shadowOn = true;
    drawField();
    return;
  }
  st.vehicle = 0;
  st.heroSprite = 0;
  st.airMap = st.curMap;
  st.airX = st.x;
  st.airY = st.y;
  st.dir = 2;
  const int l = A & 0x60;
  st.layer = (l == 0 || l == 0x40) ? 0x00 : 0x20;
  if (!bgmEqual(st.prevBgm, st.curBgm)) {
    std::memcpy(st.curBgm, st.prevBgm, 9);
    playBgm(st.curBgm);
  }
  drawField();
}

// =============================================================================================
// encounters and battle (spec 21 §10)
int Game::encounterCheck() {
  if (st.noEncounter) return 0;
  const int zx = st.x / 16, zy = st.y / 16;
  const uint8_t* rec = map.zones + zy * 0xF0 + zx * 0x18;
  std::vector<int> cand;
  for (int j = 0; j < 8; ++j) {
    const uint16_t cond = core::rd16(rec + 2 * j);
    const int grp = rec[16 + j];
    if (grp == 0xFF) continue;
    if (cond == 0xFFFF || (cond <= 1000 && switches[cond])) cand.push_back(grp);
  }
  if (cand.empty()) return 0;
  int R = map.hdr.encRate;
  const int chip = chipAt(st.x, st.y);
  const int e = chips.enc[chip];
  if (e == 0) return 0;
  if (e == 2) R /= 2;
  if (e == 3) R /= 3;
  const int S = st.steps;
  if (R < S) {
    if (R / 2 >= 2 && rng.next() % (R / 2) != 0) return 0;
  } else if (R / 4 < S) {
    if (R >= 2 && rng.next() % R != 0) return 0;
  } else {
    if (S < 5) return 0;
    if (R * 2 >= 2 && rng.next() % (R * 2) != 0) return 0;
  }
  if (st.shipMap == st.curMap && st.shipX == st.x && st.shipY == st.y) return 0;
  st.steps = 0;
  const int r = rng.next();
  st.battleBg = chips.bg[chip];
  return callBattle(cand[r % int(cand.size())], 1, -1);
}

int Game::callBattle(int group, int gameOverMode, int eraseEv) {
  if (!bgmEqual8(map.hdr.battleBgm, st.curBgm)) playBgm(map.hdr.battleBgm);
  const int r = battle(*this, group, gameOverMode);
  invalidateGraphics();
  loadMapGraphics();
  reloadAllSprites();
  if (r == 1 && eraseEv >= 0) eraseEvent(eraseEv, true);
  updatePages();
  fieldMask();
  reapplyTone();
  drawField();
  vsync();
  setTone(100);
  if (!bgmEqual8(map.hdr.battleBgm, st.curBgm)) playBgm(st.curBgm);
  return r;
}

// =============================================================================================
// events start/end (spec 21 §7.3/7.4, 22 §3.2/4)
int Game::tryStartEvent(int i, int t) {
  if (st.eventRunning || st.vehicle == 2) return 0;
  if (!eventExists(i)) return 0;
  EventRec* e = map.events[i];
  if (e->prog != 0 || !e->active) return 0;
  if (stopPressed()) return 0;
  if (t == 1 && e->timer < 0x18) ++e->timer;
  const int p = activePage(i);
  if (p < 0) return 0;
  const EventPage& pg = e->pages[p];
  if (pg.trigger == 1) {
    if (t != 0 && t != 1) return 0;
  } else if (pg.trigger != t) {
    return 0;
  }
  if (st.vehicle == 0 && t <= 1 && (attrAt(e->x, e->y) & 0x60) != 0x60) {
    if ((e->layer == 0 && st.layer == 0x20) || (e->layer == 0x20 && st.layer == 0)) return 0;
  }
  uint16_t len = 0;
  const uint8_t* s = map.script(i, p, &len);
  if (!s || len == 0 || s[0] == 0) return 0;
  return runEvent(*this, i, t);
}

void Game::beginEvent(int i, int t) {
  EventRec* e = map.events[i];
  st.pendingEvent = uint8_t(i);
  run_ = EventRun();
  run_.eventNo = i;
  run_.trigger = t;
  run_.startMap = st.curMap;
  e->redrawKind = 0;
  st.eventRunning = 1;
  std::memcpy(run_.savedBattleBgm, map.hdr.battleBgm, 9);
  const int p = activePage(i);
  if (p >= 0) {
    const EventPage& pg = e->pages[p];
    if (eventPatternNo(i, pg) != 0 && t < 2 && pg.dirMode == 0 && (pg.moveType < 5 || st.pendingKind == 0)) {
      run_.turned = true;
      run_.savedDir = e->dir;
      e->dir = uint8_t(10 - st.dir);
    }
  }
  drawField();
}

void Game::endEvent() {
  std::memcpy(map.hdr.battleBgm, run_.savedBattleBgm, 9);
  if (run_.turned && !run_.movedSelf && st.curMap == run_.startMap && eventExists(run_.eventNo))
    map.events[run_.eventNo]->dir = run_.savedDir;
  clearMoveCommands();
  updatePages();
  waitHeroStill();
  closeMessage();
  st.eventRunning = 0;
  clearKeys();
}

// =============================================================================================
// main loop (spec 21 §11)
void Game::fieldLoop() {
  setTone(0);
  fieldMask();
  drawField();
  blackIn(1);
  clearKeys();
  lastCycle = 0;
  for (;;) {
    recordLatch();
    while (frameCounter == lastCycle) vsync();
    const uint32_t c = frameCounter;
    lastCycle = c;
    if (c % 3 == 0) {
      if (c % 81 == 0) rng.next();
      if (heroProcess()) continue;
      npcCycle();
    }
    bool started = false;
    if (st.scroll == 0) {
      const int e1 = sameLayerValue(9, 6), e2 = otherLayerValue(9, 6);
      if (st.vehicle == 0 && e1 == kShipValue) boardShip(true);
      if (st.pendingKind == 1)
        started = tryStartEvent(st.pendingEvent, 1) != 0;
      else if (st.pendingKind == 3)
        started = tryStartEvent(st.pendingEvent, 3) != 0;
      st.pendingKind = 0;
      for (;;) {
        bool any = false;
        for (int i = 0; i < kMaxEvents; ++i)
          if (tryStartEvent(i, 4)) any = true;
        if (!any) break;
        started = true;
      }
      const int key = popKey();
      clearKeys();
      if (key == kKeyEsc && st.vehicle != 2) {
        fieldMenu(*this);
        started = true;
      }
      if (!started && isOkKey(key)) {
        if (tryStartEvent(e1, 2)) continue;
        if (tryStartEvent(e2, 2)) continue;
        const int fvx = 9 + ddx(st.dir), fvy = 6 + ddy(st.dir);
        const int f1 = sameLayerValue(fvx, fvy), f2 = otherLayerValue(fvx, fvy);
        if (st.airMap == st.curMap && st.airX == st.x && st.airY == st.y) {
          if (st.vehicle == 0 && !((attrAt(st.x, st.y) & 0x60) == 0x40 && st.layer != 0)) takeOff();
        } else if (st.vehicle == 1 && canDisembarkShip(st.dir)) {
          leaveShip();
        } else if (st.vehicle == 2) {
          land();
        } else if (f1 == kShipValue && canDisembarkWalk(st.dir)) {
          forcedStep();
        } else {
          if (tryStartEvent(f1, 0)) continue;
          if (tryStartEvent(f2, 0)) continue;
        }
      }
    }
    if (started) continue;
    const int d = readDirection();
    if (isDir(d)) heroStep(d, st.vehicle == 2);
  }
}

}  // namespace d2
