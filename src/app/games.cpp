#include "app/games.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstring>

#include "core/screen.h"
#include "core/text.h"
#include "core/util.h"
#include "d2/d2.h"

namespace app {

using core::EngineKind;

static std::string upperAscii(std::string s) {
  for (auto& c : s)
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
  return s;
}

EngineKind engineForRun(const std::string& dir) {
  if (core::isRegularFile(core::resolvePath(dir, "DAT_RPG/PLAYER.DAT")) &&
      core::isDirectory(core::resolvePath(dir, "DAT_MAP")))
    return EngineKind::D2;
  return EngineKind::D1;
}

EngineKind detectGame(const std::string& dir) {
  if (!core::isDirectory(dir)) return EngineKind::None;
  bool datRpg = false, datMap = false, player = false, mapchip = false, palet = false;
  for (auto& n : core::listDir(dir)) {
    const std::string u = upperAscii(n);
    if (u == "DAT_RPG") datRpg = true;
    if (u == "DAT_MAP") datMap = true;
    if (u == "PLAYER.DAT") player = true;
    if (u == "MAPCHIP.DAT") mapchip = true;
    if (u == "PALET.DAT") palet = true;
  }
  if (datRpg && datMap && core::fileExists(dir, "DAT_RPG/PLAYER.DAT") &&
      core::isDirectory(core::resolvePath(dir, "DAT_MAP")))
    return EngineKind::D2;
  if (player && mapchip && palet) return EngineKind::D1;
  return EngineKind::None;
}

static std::string baseName(std::string p) {
  while (p.size() > 1 && p.back() == '/') p.pop_back();
  auto k = p.rfind('/');
  return k == std::string::npos ? p : p.substr(k + 1);
}

static std::string relPath(const std::string& start, const std::string& p) {
  std::string s = start;
  while (s.size() > 1 && s.back() == '/') s.pop_back();
  if (p.compare(0, s.size(), s) == 0 && p.size() > s.size() && p[s.size()] == '/') return p.substr(s.size() + 1);
  return std::string();
}

std::vector<GameEntry> scanGames(const std::string& start, int maxDepth) {
  std::vector<GameEntry> out;
  std::vector<std::pair<std::string, int>> stack{{start, 0}};
  while (!stack.empty()) {
    auto [dir, depth] = stack.back();
    stack.pop_back();
    EngineKind k = detectGame(dir);
    if (k != EngineKind::None) {
      GameEntry e;
      e.root = dir;
      e.kind = k;
      e.displayName = core::utf8ToSjis(baseName(dir));
      std::string rel = relPath(start, dir);
      if (rel.empty() || rel == ".") rel = dir;
      e.location = core::utf8ToSjis(rel);
      out.push_back(e);
      continue;
    }
    if (depth >= maxDepth) continue;
    auto names = core::listDir(dir);
    std::sort(names.begin(), names.end());
    for (auto& n : names) {
      if (n.empty() || n[0] == '.') continue;
      const std::string p = dir + (dir.back() == '/' ? "" : "/") + n;
      struct stat st;
      if (::lstat(p.c_str(), &st) != 0 || S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) continue;
      stack.push_back({p, depth + 1});
    }
  }
  std::sort(out.begin(), out.end(), [](const GameEntry& a, const GameEntry& b) {
    const std::string ua = upperAscii(a.displayName), ub = upperAscii(b.displayName);
    if (ua != ub) return ua < ub;
    return a.location < b.location;
  });
  return out;
}

// ---------------- canvas ----------------
void RgbCanvas::fill(int x, int y, int w, int h, uint32_t color) {
  for (int yy = std::max(0, y); yy < std::min(400, y + h); ++yy)
    for (int xx = std::max(0, x); xx < std::min(640, x + w); ++xx) {
      uint8_t* p = &rgb[(yy * 640 + xx) * 3];
      p[0] = uint8_t(color >> 16);
      p[1] = uint8_t(color >> 8);
      p[2] = uint8_t(color);
    }
}

int RgbCanvas::text(int x, int y, const std::string& s, uint32_t color, int right, bool truncate) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
  size_t i = 0;
  while (i < s.size()) {
    core::Glyph g;
    int used;
    if (core::isSjisLead(p[i]) && i + 1 < s.size()) {
      core::fullGlyphJis(core::sjisToJis(p[i], p[i + 1]), g);
      used = 2;
    } else {
      core::halfGlyph(p[i], g);
      used = 1;
    }
    if (truncate && x + g.width > right) {
      text(std::min(x, right - 16), y, "..", color, 640, false);
      return x;
    }
    for (int r = 0; r < 16; ++r)
      for (int k = 0; k < g.width; ++k)
        if (g.rows[r] & (0x8000 >> k)) fill(x + k, y + r, 1, 1, color);
    x += g.width;
    i += used;
  }
  return x;
}

// ---------------- browser ----------------
namespace {
constexpr uint32_t kBg = 0x101030, kPanel = 0x202058, kFrame = 0xD0D0E8, kSel = 0x4060B0, kWhite = 0xFFFFFF,
                   kGray = 0x9090B0, kTagD1 = 0x904040, kTagD2 = 0x308050, kErr = 0xFF8080;
void panel(RgbCanvas& c, int x, int y, int w, int h) {
  c.fill(x, y, w, h, kFrame);
  c.fill(x + 2, y + 2, w - 4, h - 4, kPanel);
}
}  // namespace

Browser::Browser(std::string startDir, std::vector<GameEntry> games) : start_(std::move(startDir)), games_(std::move(games)) {}

void Browser::select(int s) {
  if (games_.empty()) return;
  s = std::max(0, std::min(s, int(games_.size()) - 1));
  sel_ = s;
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + 16) top_ = sel_ - 15;
}

int Browser::handle(Event ev, int cx, int cy, int clicks, bool* quit) {
  const int n = int(games_.size());
  if (quit) *quit = false;
  switch (ev) {
    case Event::Cancel:
    case Event::Quit:
      if (quit) *quit = true;
      return -1;
    default:
      break;
  }
  if (n == 0) return -1;
  switch (ev) {
    case Event::Up:
    case Event::WheelUp:
      select(sel_ == 0 ? n - 1 : sel_ - 1);
      break;
    case Event::Down:
    case Event::WheelDown:
      select(sel_ == n - 1 ? 0 : sel_ + 1);
      break;
    case Event::Left:
    case Event::PageUp:
      select(sel_ - 16);
      break;
    case Event::Right:
    case Event::PageDown:
      select(sel_ + 16);
      break;
    case Event::Home:
      select(0);
      break;
    case Event::End:
      select(n - 1);
      break;
    case Event::Ok:
      return sel_;
    case Event::Click:
      if (cx >= 12 && cx <= 299 && cy >= 40 && cy <= 359) {
        const int row = top_ + (cy - 40) / 20;
        if (row < n) {
          if (clicks >= 2 && row == sel_) return sel_;
          select(row);
        }
      }
      break;
    default:
      break;
  }
  return -1;
}

Browser::Preview& Browser::preview(int idx) {
  auto it = previews_.find(idx);
  if (it != previews_.end()) return it->second;
  Preview& pv = previews_[idx];
  const GameEntry& g = games_[idx];
  try {
    std::vector<uint8_t> planes;
    uint8_t pal8[16][3] = {};
    bool have = false;
    if (g.kind == EngineKind::D1) {
      pv.info = "Dante98";
      if (core::tryReadFile(g.root, "OPNTITLE.DAT", planes)) {
        std::vector<uint8_t> p;
        if (!core::tryReadFile(g.root, "OPNTITLE.RGB", p)) core::tryReadFile(g.root, "PALET.DAT", p);
        if (p.size() >= 48)
          for (int i = 0; i < 16; ++i)
            for (int k = 0; k < 3; ++k) pal8[i][k] = uint8_t((p[i * 3 + k] & 15) * 17);
        have = true;
      }
    } else {
      std::vector<uint8_t> t = core::readFile(g.root, "DAT_RPG/TITLE.DAT");
      pv.info = std::string("Dante98 II (RPG.EXE ") + d2::buildLabel(g.root) + ")";
      if (t.size() >= 128048) {
        for (int i = 0; i < 16; ++i)
          for (int k = 0; k < 3; ++k) pal8[i][k] = uint8_t((t[i * 3 + k] >> 4) * 17);
        planes.assign(t.begin() + 48, t.end());
        have = true;
      }
    }
    if (have && planes.size() >= 128000) {
      std::vector<uint8_t> idxs(640 * 400);
      core::decodePlanes(planes.data(), 640, 400, 32000, idxs.data());
      pv.rgb.assign(320 * 200 * 3, 0);
      for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x)
          for (int k = 0; k < 3; ++k) {
            int s = pal8[idxs[(2 * y) * 640 + 2 * x]][k] + pal8[idxs[(2 * y) * 640 + 2 * x + 1]][k] +
                    pal8[idxs[(2 * y + 1) * 640 + 2 * x]][k] + pal8[idxs[(2 * y + 1) * 640 + 2 * x + 1]][k];
            pv.rgb[(y * 320 + x) * 3 + k] = uint8_t(s / 4);
          }
      pv.has = true;
    }
  } catch (...) {
    pv.has = false;
  }
  return pv;
}

void Browser::render(RgbCanvas& c) {
  c.fill(0, 0, 640, 400, kBg);
  c.text(16, 8, kAppName, kWhite);
  const int n = int(games_.size());
  std::string cnt = n == 1 ? "1 game" : std::to_string(n) + " games";
  c.text(624 - 8 * int(cnt.size()), 8, cnt, kGray);
  if (n == 0) {
    panel(c, 16, 40, 608, 120);
    c.text(32, 56, "No Dante98 or Dante98 II games were found in", kWhite);
    c.text(32, 80, core::utf8ToSjis(start_), kGray, 608);
    c.text(32, 112, std::string("Start ") + kAppName + " with a game folder, or with a folder", kWhite);
    c.text(32, 132, "that contains game folders.", kWhite);
    c.text(16, 376, "Esc: quit", kGray);
    return;
  }
  panel(c, 8, 32, 296, 336);
  for (int r = 0; r < 16; ++r) {
    const int i = top_ + r;
    if (i >= n) break;
    const int y = 40 + 20 * r;
    if (i == sel_) c.fill(12, y, 288, 20, kSel);
    const bool d2 = games_[i].kind == EngineKind::D2;
    c.fill(18, y + 2, 24, 16, d2 ? kTagD2 : kTagD1);
    c.text(22, y + 2, d2 ? "II" : "98", kWhite);
    c.text(50, y + 2, games_[i].displayName, kWhite, 296);
  }
  if (top_ > 0) c.text(280, 34, "\x81\xA3", kGray);
  if (top_ + 16 < n) c.text(280, 350, "\x81\xA5", kGray);
  Preview& pv = preview(sel_);
  c.fill(310, 32, 324, 204, kFrame);
  if (pv.has) {
    for (int y = 0; y < 200; ++y) std::memcpy(&c.rgb[((34 + y) * 640 + 312) * 3], &pv.rgb[y * 320 * 3], 320 * 3);
  } else {
    c.fill(312, 34, 320, 200, 0);
    c.text(400, 126, "(no title picture)", kGray);
  }
  panel(c, 310, 244, 324, 108);
  c.text(322, 254, games_[sel_].displayName, kWhite, 626);
  c.text(322, 278, pv.info, kGray, 626);
  c.text(322, 302, games_[sel_].location, kGray, 626);
  if (!message_.empty()) c.text(16, 360, message_, kErr, 632);
  c.text(16, 380, "Up/Down: select   Z/Enter: start   Esc: quit", kGray);
}

}  // namespace app
