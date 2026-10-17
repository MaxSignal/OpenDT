#include "d1/d1_data.h"

#include <cstdio>
#include <algorithm>
#include <cstring>

#include "core/util.h"

namespace d1 {

using core::rd16;

static std::string num2(int n) {
  char b[16];
  std::snprintf(b, sizeof b, "%02d", n);
  return b;
}

static std::vector<uint8_t> need(const std::string& root, const char* name, size_t minSize) {
  std::vector<uint8_t> v = core::readFile(root, name);
  if (v.size() < minSize) throw core::DataError(std::string(name) + " is too short");
  return v;
}

static core::Image planes(const uint8_t* p, int w, int h) {
  core::Image img(w, h);
  core::decodePlanes(p, w, h, (w / 8) * h, img.px.data());
  return img;
}

void Data::load(const std::string& r) {
  root = r;
  auto pl = need(root, "PLAYER.DAT", 470);
  for (int n = 0; n < 8; ++n) {
    const uint8_t* p = &pl[n * 56];
    HeroDef& h = heroes[n];
    std::memcpy(h.name, p, 17);
    h.level = p[0x11];
    h.gStr = p[0x12];
    h.gAgi = p[0x13];
    h.gVit = p[0x14];
    h.gInt = p[0x15];
    h.gLuck = p[0x16];
    h.graphic = p[0x17];
    std::memcpy(h.magicLv, p + 0x18, 32);
  }
  {
    const uint8_t* s = &pl[0x1C0];
    start.screen = rd16(s);
    start.pos = s[2];
    start.gold = rd16(s + 3);
    start.bgm = s[5];
    std::memcpy(start.party, s + 6, 4);
    start.shipScreen = rd16(s + 0x0A);
    start.shipPos = s[0x0C];
    start.shipGfx = s[0x0D];
    start.airScreen = rd16(s + 0x0E);
    start.airPos = s[0x10];
    start.airGfx = s[0x11];
    start.battleBgm = s[0x12];
    start.ring = s[0x13];
    start.shipBgm = s[0x14];
    start.airBgm = s[0x15];
  }
  auto it = need(root, "ITEM.DAT", 3328);
  for (int i = 0; i < 128; ++i) {
    const uint8_t* p = &it[i * 26];
    std::memcpy(items[i].name, p, 17);
    items[i].price = rd16(p + 0x11);
    items[i].kind = p[0x13];
    items[i].value = rd16(p + 0x14);
    items[i].equipMask = p[0x16];
  }
  auto mg = need(root, "MAGIC.DAT", 640);
  for (int i = 0; i < 32; ++i) {
    const uint8_t* p = &mg[i * 20];
    std::memcpy(magics[i].name, p, 17);
    magics[i].mp = p[0x11];
    magics[i].power = rd16(p + 0x12);
  }
  auto en = need(root, "ENEMY.DAT", 6400);
  for (int i = 0; i < 160; ++i) {
    const uint8_t* p = &en[i * 40];
    EnemyDef& e = enemies[i];
    std::memcpy(e.name, p, 17);
    e.image = p[0x11];
    e.hp = rd16(p + 0x12);
    e.atk = rd16(p + 0x14);
    e.def = rd16(p + 0x16);
    e.agi = rd16(p + 0x18);
    e.exp = rd16(p + 0x1A);
    e.gold = rd16(p + 0x1C);
    std::memcpy(e.magicMask, p + 0x1E, 4);
    std::memcpy(e.act, p + 0x22, 5);
    e.flags = p[0x27];
  }
  {
    std::memset(enemyMap, 0, sizeof enemyMap);
    auto em = core::readFile(root, "ENEMYMAP.DAT");
    std::memcpy(enemyMap, em.data(), std::min<size_t>(1024, em.size()));
  }
  auto mc = need(root, "MAPCHIP.DAT", 65536);
  chips.clear();
  for (int c = 0; c < 512; ++c) chips.push_back(planes(&mc[c * 128], 16, 16));
  auto mp = need(root, "MAPPATN.DAT", 2112);
  for (int p = 0; p < 256; ++p)
    for (int k = 0; k < 4; ++k) patChips[p][k] = rd16(&mp[p * 8 + k * 2]) & 0x1FF;
  std::memcpy(landBits, &mp[0x800], 32);
  std::memcpy(blockBits, &mp[0x820], 32);
  for (int b = 0; b < 16; ++b) {
    maps[b].clear();
    std::vector<uint8_t> v;
    if (core::tryReadFile(root, "MAP" + num2(b) + ".DAT", v) && v.size() >= 15360) maps[b] = v;
    ivents[b].clear();
    if (!core::tryReadFile(root, "IVENT" + num2(b) + ".DAT", ivents[b]) || ivents[b].size() < 2)
      ivents[b] = {0xFF, 0xFF, 0xFF, 0xFF};
  }
  for (int g = 0; g < 64; ++g) {
    std::vector<uint8_t> v;
    chr[g].clear();
    chrOk[g] = core::tryReadFile(root, "CHR" + num2(g) + ".DAT", v) && v.size() >= 4096;
    if (chrOk[g])
      for (int f = 0; f < 8; ++f) chr[g].push_back(planes(&v[f * 512], 32, 32));
  }
  std::vector<uint8_t> ca, cb;
  const bool haveA = core::tryReadFile(root, "CHR-A.DAT", ca) && ca.size() >= 32768;
  const bool haveB = core::tryReadFile(root, "CHR-B.DAT", cb) && cb.size() >= 32768;
  for (int g = 0; g < 64; ++g) {
    if (haveA)
      chrA[g] = planes(&ca[g * 512], 32, 32);
    else
      chrA[g] = chrOk[g] ? chr[g][4] : core::Image(32, 32);
    if (haveB)
      chrB[g] = planes(&cb[g * 512], 32, 32);
    else
      chrB[g] = chrOk[g] ? chr[g][5] : core::Image(32, 32);
  }
  for (int i = 0; i < 48; ++i) {
    std::vector<uint8_t> v;
    if (core::tryReadFile(root, "ENEMY" + num2(i) + ".DAT", v) && v.size() >= 3840)
      enemyImg[i] = planes(v.data(), 80, 96);
    else
      enemyImg[i] = core::Image(80, 96);
  }
  core::loadPalette48(need(root, "PALET.DAT", 48), palet);
  core::loadPalette48(need(root, "PALET2.DAT", 48), palet2);
  std::vector<uint8_t> t;
  hasTitle = core::tryReadFile(root, "OPNTITLE.DAT", t) && t.size() >= 128000;
  if (hasTitle) {
    titleIdx.assign(640 * 400, 0);
    core::decodePlanes(t.data(), 640, 400, 32000, titleIdx.data());
    std::vector<uint8_t> pr;
    if (core::tryReadFile(root, "OPNTITLE.RGB", pr) && pr.size() >= 48)
      core::loadPalette48(pr, titlePal);
    else
      std::memcpy(titlePal, palet, sizeof palet);
  }
}

uint8_t Data::mapCell(int screen, int x, int y) const {
  const int X = screen & 31, Y = (screen >> 5) & 31;
  const int block = (Y / 8) * 4 + X / 8;
  const auto& m = maps[block];
  if (m.empty()) return 0;
  const int off = ((Y % 8) * 8 + (X % 8)) * 240 + y * 20 + x;
  return m[off];
}

int objPattern(int gfx) {
  static const int tab[36] = {1,   2,   3,   18,  21,  31,  70,  76,  37,  38,  39,  40,
                              58,  54,  113, 120, 121, 122, 133, 140, 184, 41,  42,  85,
                              81,  100, 101, 128, 129, 130, 186, 231, 124, 211, 71,  93};
  if (gfx < 0x40 || gfx > 0x63) return 0;
  return tab[gfx - 0x40];
}

int findFFFF(const std::vector<uint8_t>& b, int from) {
  for (int i = from; i + 1 < int(b.size()); ++i)
    if (b[i] == 0xFF && b[i + 1] == 0xFF) return i;
  return int(b.size());
}

static uint8_t at(const std::vector<uint8_t>& b, int i) { return (i >= 0 && i < int(b.size())) ? b[i] : 0xFF; }

void eventPages(const std::vector<uint8_t>& b, int e, PageHdr pg[4]) {
  int p = e;
  for (int k = 0; k < 4; ++k) {
    if (k > 0) p = findFFFF(b, pg[k - 1].start + 10);
    pg[k].start = p;
    if (k == 0) {
      pg[k].item = 0x80;
      pg[k].party = 0;
      pg[k].sw = 200;
      pg[k].gold = 10000;
    } else {
      pg[k].item = at(b, p + 2);
      pg[k].party = at(b, p + 3);
      pg[k].sw = at(b, p + 4);
      pg[k].gold = at(b, p + 5) | (at(b, p + 6) << 8);
    }
    pg[k].gfx = at(b, p + 7);
    pg[k].move = at(b, p + 8);
    pg[k].trig = at(b, p + 9);
  }
}

int nextEvent(const std::vector<uint8_t>& b, int e) {
  PageHdr pg[4];
  eventPages(b, e, pg);
  return findFFFF(b, pg[3].start + 10);
}

bool eventEnd(const std::vector<uint8_t>& b, int e) {
  if (e + 3 >= int(b.size())) return true;
  return b[e + 2] == 0xFF && b[e + 3] == 0xFF;
}

int eventScreen(const std::vector<uint8_t>& b, int e) { return at(b, e + 2) | (at(b, e + 3) << 8); }

int cmdLen(const std::vector<uint8_t>& b, int pc) {
  const int op = at(b, pc);
  switch (op) {
    case 0x3A: case 0x3F: case 0x64: case 0x70: case 0x71: return 1;
    case 0x62: case 0x65: case 0x76: case 0x77: case 0x79: return 2;
    case 0x6A: case 0x74: return 4;
    case 0x73: return 5;
    case 0x2A: return 2 + at(b, pc + 1);
    default: return 3;
  }
}

int signedArg(const uint8_t* p, bool* neg) {
  const int v = p[0] | (p[1] << 8);
  if (neg) *neg = (v & 0x8000) != 0;
  return v & 0x7FFF;
}

int expNeeded(int level) {
  if (level >= 20) return (level - 20) * 10000 + 22168;
  int v = 10;
  for (int i = 1; i < level; ++i) v = v * 3 / 2;
  return v;
}

}  // namespace d1
