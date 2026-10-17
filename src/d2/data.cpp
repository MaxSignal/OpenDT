#include "d2/data.h"

#include <cstdio>
#include <cstring>

#include "core/screen.h"
#include "core/util.h"
#include "d2/d2.h"

namespace d2 {

using core::DataError;

std::string numberedName(const char* prefix, int n, const char* ext) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s%03d%s", prefix, n, ext);
  return buf;
}

// ---------------------------------------------------------------------------------------------
Build detectBuild(const std::string& root) {
  std::vector<uint8_t> d;
  if (!core::tryReadFile(root, "RPG.EXE", d) || d.size() <= 0x40) return Build::B9710;
  const size_t hdr = size_t(core::rd16(&d[0x08])) * 16;
  const size_t cs = core::rd16(&d[0x16]), ip = core::rd16(&d[0x14]);
  const size_t e = hdr + cs * 16 + ip;
  if (e + 3 > d.size() || d[e] != 0xBA) return Build::B9710;
  switch (core::rd16(&d[e + 1])) {
    case 0x17E1: return Build::B9606;
    case 0x17E2: return Build::B9608;
    case 0x17EC: return Build::B9703;
    default: return Build::B9710;
  }
}

const char* buildName(Build b) {
  switch (b) {
    case Build::B9606: return "1996-06";
    case Build::B9608: return "1996-08";
    case Build::B9703: return "1997-03";
    default: return "1997-10";
  }
}

std::string buildLabel(const std::string& root) { return buildName(detectBuild(root)); }

// ---------------------------------------------------------------------------------------------
std::string Database::wordAt(int off) const {
  std::string s;
  for (int i = off; i < int(sizeof(word)) && word[i]; ++i) s.push_back(char(word[i]));
  return s;
}

static std::vector<uint8_t> required(const std::string& root, const std::string& rel) {
  std::vector<uint8_t> d;
  if (!core::tryReadFile(root, rel, d)) throw DataError(rel + " cannot be read.");
  return d;
}

void loadDatabase(const std::string& root, Database& db) {
  // GAIJI
  {
    auto d = required(root, "DAT_RPG/GAIJI.DAT");
    std::memset(db.gaiji, 0, sizeof(db.gaiji));
    for (int c = 0; c < 256; ++c)
      for (int k = 0; k < 32; ++k) {
        size_t o = 32 + size_t(c) * 32 + k;
        if (o < d.size()) db.gaiji[c * 32 + k] = d[o];
      }
  }
  // MAPNAME (optional)
  {
    std::memset(db.mapNames, 0, sizeof(db.mapNames));
    std::vector<uint8_t> d;
    if (core::tryReadFile(root, "DAT_RPG/MAPNAME.DAT", d))
      for (int i = 0; i < 1000; ++i)
        if (size_t(i + 1) * 16 <= d.size()) std::memcpy(db.mapNames[i], &d[size_t(i) * 16], 16);
  }
  // ITEM
  {
    auto d = required(root, "DAT_RPG/ITEM.DAT");
    std::memset(db.items, 0, sizeof(db.items));
    for (int i = 0; i < 256; ++i) std::memset(db.items[i].name, '*', 16);
    std::memset(&db.items[255], 0, sizeof(Item));
    db.items[255].f[0x22 - 0x17] = 95;
    for (int i = 0; i < 256; ++i)
      if (size_t(i + 1) * 40 <= d.size()) std::memcpy(&db.items[i], &d[size_t(i) * 40], 40);
  }
  // MAGIC
  {
    auto d = required(root, "DAT_RPG/MAGIC.DAT");
    std::memset(db.magics, 0, sizeof(db.magics));
    for (int i = 0; i < 255; ++i) std::memset(db.magics[i].name, '*', 16);
    for (int i = 0; i < 256; ++i)
      if (size_t(i + 1) * 80 <= d.size()) std::memcpy(&db.magics[i], &d[size_t(i) * 80], 80);
  }
  // WORD
  {
    auto d = required(root, "DAT_RPG/WORD.DAT");
    std::memset(db.word, 0, sizeof(db.word));
    std::memcpy(db.word, d.data(), std::min(d.size(), sizeof(db.word)));
  }
  // ENEMY
  {
    auto d = required(root, "DAT_RPG/ENEMY.DAT");
    d.resize(26112, 0);
    std::memcpy(db.enemies, d.data(), 0x5000);
    std::memcpy(db.groups, d.data() + 0x5000, 0x1600);
  }
  // PLAYER
  {
    auto d = required(root, "DAT_RPG/PLAYER.DAT");
    std::memset(db.members0, 0, sizeof(db.members0));
    for (int i = 0; i < 12; ++i)
      if (size_t(i + 1) * 200 <= d.size()) std::memcpy(&db.members0[i], &d[size_t(i) * 200], 200);
    std::memset(&db.state0, 0, sizeof(db.state0));
    if (d.size() >= 0x960 + 0x2A8) std::memcpy(&db.state0, &d[0x960], 0x2A8);
    db.state0.loadedMap = 0xFFFF;
    db.state0.dir = 2;
    db.playerFile = d;
  }
  // TITLE (optional)
  loadPicture(root, "DAT_RPG/TITLE.DAT", db.title);
}

// ---------------------------------------------------------------------------------------------
const uint8_t* MapData::script(int ev, int page, uint16_t* len) const {
  if (ev < 0 || ev >= kMaxEvents || !events[ev]) return nullptr;
  const uint16_t off = events[ev]->pages[page].script;
  if (off < 2 || size_t(off) >= eventArea.size()) return nullptr;
  uint16_t l = core::rd16(&eventArea[off - 2]);
  if (size_t(off) + l > eventArea.size()) l = uint16_t(eventArea.size() - off);
  if (len) *len = l;
  return &eventArea[off];
}

void loadMap(const std::string& root, int no, MapData& m, const StateBlock& st, uint16_t prevVersion) {
  std::vector<uint8_t> d;
  bool have = core::tryReadFile(root, numberedName("DAT_MAP/MAP", no, ".DAT"), d);
  std::memset(&m.hdr, 0, sizeof(m.hdr));
  std::memset(m.grid, 0, sizeof(m.grid));
  std::memset(m.zones, 0xFF, sizeof(m.zones));
  for (auto& p : m.eventPos) p = 0xFFFF;
  std::fill(m.eventArea.begin(), m.eventArea.end(), 0);
  for (auto& e : m.events) e = nullptr;
  m.eventAreaSize = 0;
  if (!have || d.size() < 0x2C) {
    m.fromFile = false;
    m.hdr.size = 32;
    m.hdr.outerChip = 0;
    m.hdr.chipset = 0;
    for (int i = 0; i < 4; ++i) m.hdr.chr[i] = st.defChr[i];
    m.hdr.encRate = 20;
    std::memcpy(m.hdr.battleBgm, st.defBattleBgm, 9);
    m.version = prevVersion;
    // default grid: all 0; outer area filled with outer chip 0
    return;
  }
  m.fromFile = true;
  std::memcpy(&m.hdr, d.data(), sizeof(MapHeader));
  int n = m.hdr.size;
  if (n > kMapDim) n = kMapDim;  // safety
  size_t p = 0x2C;
  // grid
  std::vector<uint8_t> grid(size_t(m.hdr.size) * m.hdr.size, 0);
  for (size_t i = 0; i < grid.size() && p + i < d.size(); ++i) grid[i] = d[p + i];
  p += grid.size();
  for (int y = 0; y < kMapDim; ++y)
    for (int x = 0; x < kMapDim; ++x)
      m.grid[y][x] = (x < n && y < n) ? grid[size_t(y) * m.hdr.size + x] : m.hdr.outerChip;
  // zones
  if (p + 0x960 <= d.size()) std::memcpy(m.zones, &d[p], 0x960);
  p += 0x960;
  uint16_t version = 0;
  if (p + 2 <= d.size()) version = core::rd16(&d[p]);
  m.version = version;
  p += 2;
  if (p + 2 <= d.size()) m.eventAreaSize = core::rd16(&d[p]);
  p += 2;
  for (int i = 0; i < kMaxEvents; ++i)
    if (p + 2 * i + 2 <= d.size()) m.eventPos[i] = core::rd16(&d[p + 2 * i]);
  p += 500;
  size_t s = m.eventAreaSize;
  if (s > size_t(kEventAreaSize)) s = kEventAreaSize;
  for (size_t i = 0; i < s && p + i < d.size(); ++i) m.eventArea[i] = d[p + i];
  for (int i = 0; i < kMaxEvents; ++i) {
    const uint16_t off = m.eventPos[i];
    if (off == 0xFFFF || size_t(off) + sizeof(EventRec) > size_t(kEventAreaSize)) continue;
    m.events[i] = reinterpret_cast<EventRec*>(&m.eventArea[off]);
  }
}

void loadPatterns(const std::string& root, const std::string& rel, int count, PatternSet& out) {
  out.count = count;
  out.data.assign(size_t(count) * kPatternBytes, 0);
  out.hasPal = false;
  std::vector<uint8_t> d;
  if (!core::tryReadFile(root, rel, d)) return;
  if (d.size() >= 48) {
    out.hasPal = true;
    for (int i = 0; i < 16; ++i)
      for (int k = 0; k < 3; ++k) out.pal[i][k] = d[i * 3 + k] & 0x0F;
  }
  for (size_t i = 0; i < out.data.size() && 48 + i < d.size(); ++i) out.data[i] = d[48 + i];
}

void loadChipAttr(const std::string& root, int no, ChipAttr& out) {
  std::vector<uint8_t> d;
  bool ok = core::tryReadFile(root, numberedName("DAT_MCP/MCP", no, ".DAT"), d) && d.size() >= 1024;
  if (!ok) {
    for (int i = 0; i < 256; ++i) {
      out.attr[i] = 0x20;
      out.enc[i] = 1;
      out.bg[i] = 0;
    }
  } else {
    for (int i = 0; i < 256; ++i) {
      out.attr[i] = d[i];
      out.enc[i] = d[0x100 + i];
      out.bg[i] = core::rd16(&d[0x200 + 2 * i]);
    }
  }
  for (int i = 0; i < 256; ++i) out.attr[i] &= 0x7F;
  for (int i = 0; i < 4; ++i) out.attr[i] |= 0x80;
  for (int i = 0xC0; i < 0x100; ++i) {
    out.attr[i] = out.attr[0];
    out.enc[i] = out.enc[0];
    out.bg[i] = out.bg[0];
  }
}

bool loadPicture(const std::string& root, const std::string& rel, Picture& out) {
  out.valid = false;
  std::vector<uint8_t> d;
  if (!core::tryReadFile(root, rel, d) || d.size() < 128048) return false;
  for (int i = 0; i < 16; ++i)
    for (int k = 0; k < 3; ++k) out.pal8[i][k] = d[i * 3 + k];
  out.px.assign(640 * 400, 0);
  core::decodePlanes(d.data() + 48, 640, 400, 32000, out.px.data());
  out.valid = true;
  return true;
}

bool loadEnemyImage(const std::string& root, const std::string& rel, EnemyImage& out) {
  out.valid = false;
  std::vector<uint8_t> d;
  if (!core::tryReadFile(root, rel, d) || d.size() < 50) return false;
  for (int i = 0; i < 16; ++i)
    for (int k = 0; k < 3; ++k) out.pal[i][k] = d[i * 3 + k] & 0x0F;
  out.sizeKind = d[0x30] ? 1 : 0;
  out.w = out.sizeKind ? 160 : 80;
  out.h = out.sizeKind ? 128 : 96;
  const size_t plane = size_t(out.w / 8) * out.h;
  std::vector<uint8_t> body(plane * 5, 0);
  for (size_t i = 0; i < body.size() && 0x32 + i < d.size(); ++i) body[i] = d[0x32 + i];
  out.mask.assign(body.begin(), body.begin() + plane);
  out.planes.assign(body.begin() + plane, body.end());
  out.valid = true;
  return true;
}

bool loadBattleBg(const std::string& root, const std::string& rel, BattleBg& out) {
  out.valid = false;
  std::vector<uint8_t> d;
  if (!core::tryReadFile(root, rel, d) || d.size() < 48) return false;
  for (int i = 0; i < 16; ++i)
    for (int k = 0; k < 3; ++k) out.pal[i][k] = d[i * 3 + k] & 0x0F;
  std::vector<uint8_t> body(4 * 0x2800, 0);
  for (size_t i = 0; i < body.size() && 48 + i < d.size(); ++i) body[i] = d[48 + i];
  out.px.assign(640 * 128, 0);
  core::decodePlanes(body.data(), 640, 128, 0x2800, out.px.data());
  out.valid = true;
  return true;
}

}  // namespace d2
