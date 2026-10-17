#include "core/text.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace core {

extern const uint16_t kSjisTable[][2];
extern const int kSjisTableSize;

uint16_t sjisToJis(uint8_t h, uint8_t l) {
  int hh = h;
  if (hh >= 0xE0) hh -= 0x40;
  int row = (hh - 0x81) * 2 + 0x21;
  int col;
  if (l >= 0x9F) {
    row += 1;
    col = l - 0x9F + 0x21;
  } else {
    col = l - 0x40 + 0x21;
    if (l >= 0x80) col -= 1;
  }
  return uint16_t(((row & 0xFF) << 8) | (col & 0xFF));
}

void halfGlyph(uint8_t c, Glyph& g) {
  for (int y = 0; y < 16; ++y) g.rows[y] = uint16_t(kFontHalf[c * 16 + y] << 8);
  g.width = 8;
}

void fullGlyphJis(uint16_t jis, Glyph& g) {
  const int row = jis >> 8, col = jis & 0xFF;
  g.width = 16;
  if (row < 0x21 || row > 0x7E || col < 0x21 || col > 0x7E) {
    std::memset(g.rows, 0, sizeof(g.rows));
    return;
  }
  const uint8_t* p = kFontFull + ((row - 0x21) * 94 + (col - 0x21)) * 32;
  for (int y = 0; y < 16; ++y) g.rows[y] = uint16_t((p[y * 2] << 8) | p[y * 2 + 1]);
}

int fetchGlyphD1(const uint8_t* s, size_t len, Glyph& g) {
  if (len == 0) {
    halfGlyph(0x20, g);
    return 0;
  }
  if (isSjisLead(s[0]) && len >= 2) {
    fullGlyphJis(sjisToJis(s[0], s[1]), g);
    return 2;
  }
  halfGlyph(s[0], g);
  return 1;
}

int drawCharD1(Screen& scr, int x, int y, uint16_t code, int fg, int bg) {
  Glyph g;
  if (code >= 0x100) {
    fullGlyphJis(sjisToJis(uint8_t(code >> 8), uint8_t(code & 0xFF)), g);
  } else {
    halfGlyph(uint8_t(code), g);
  }
  const bool bold = code != 0x20;
  for (int r = 0; r < 16; ++r) {
    uint16_t bits = g.rows[r];
    if (bold) bits = uint16_t(bits | (bits << 1));
    for (int k = 0; k < g.width; ++k) scr.pset(x + k, y + r, (bits & (0x8000 >> k)) ? fg : bg);
  }
  return g.width;
}

uint16_t d1HalfToFull(uint8_t c) {
  if (c >= '0' && c <= '9') return uint16_t(0x824F + (c - '0'));
  if (c >= 'A' && c <= 'Z') return uint16_t(0x8260 + (c - 'A'));
  if (c >= 'a' && c <= 'z') return uint16_t(0x8281 + (c - 'a'));
  static const char sym[] = " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  static const uint16_t tab[] = {0x8140, 0x8149, 0x8168, 0x8194, 0x8190, 0x8193, 0x8195, 0x8166, 0x8169,
                                 0x816A, 0x8196, 0x817B, 0x8143, 0x817C, 0x8144, 0x815E, 0x8146, 0x8147,
                                 0x8183, 0x8181, 0x8184, 0x8148, 0x8197, 0x816D, 0x815F, 0x816E, 0x814F,
                                 0x8151, 0x814D, 0x816F, 0x8162, 0x8170, 0x8160};
  for (size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); ++i)
    if (uint8_t(sym[i]) == c) return tab[i];
  return 0;
}

int fetchGlyphD2(const uint8_t* s, size_t len, const GaijiTable& gaiji, Glyph& g) {
  if (len == 0 || s[0] == 0) return 0;
  uint16_t jis;
  int used;
  if (isSjisLead(s[0]) && len >= 2) {
    used = 2;
    jis = sjisToJis(s[0], s[1]);
    if (!(jis >= 0x2921 && jis <= 0x2B7E)) {
      const int row = jis >> 8, col = jis & 0xFF;
      if (row == 0x76 || row == 0x77) {
        const int idx = (row == 0x77 ? 0x80 : 0) | (col & 0x7F);
        g.width = 16;
        if (gaiji.data) {
          const uint8_t* p = gaiji.data + idx * 32;
          for (int y = 0; y < 16; ++y) g.rows[y] = uint16_t((p[y * 2] << 8) | p[y * 2 + 1]);
        } else {
          std::memset(g.rows, 0, sizeof(g.rows));
        }
      } else {
        fullGlyphJis(jis, g);
      }
      return used;
    }
  } else {
    used = 1;
    const uint8_t b = s[0];
    if (b >= 0xA1 && b <= 0xDF)
      jis = uint16_t(0x2980 + b);
    else if (b <= 0x20 || b == 0x7F || b >= 0x80)
      jis = 0x2B21;
    else
      jis = uint16_t(0x2900 + b);
  }
  if (jis >= 0x2921 && jis <= 0x297F)
    halfGlyph(uint8_t(jis - 0x2900), g);
  else if (jis >= 0x2A21 && jis <= 0x2A5F)
    halfGlyph(uint8_t(jis - 0x2980), g);
  else
    std::memset(g.rows, 0, sizeof(g.rows));
  g.width = 8;
  return used;
}

int drawGlyphD2(Screen& scr, int x, int y, const Glyph& g, int color) {
  x &= ~7;
  for (int r = 0; r < 16; ++r) {
    uint16_t bits = g.rows[r];
    bits = uint16_t(bits | (bits << 1));
    if (!bits) continue;
    for (int k = 0; k < 16; ++k)
      if (bits & (0x8000 >> k)) scr.pset(x + k, y + r, color);
  }
  return g.width;
}

int drawStringD2(Screen& scr, int x, int y, const std::string& s, int color, const GaijiTable& gaiji) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
  size_t len = s.size();
  size_t i = 0;
  x &= ~7;
  while (i < len) {
    Glyph g;
    int used = fetchGlyphD2(p + i, len - i, gaiji, g);
    if (used == 0) break;
    x += drawGlyphD2(scr, x, y, g, color);
    i += used;
  }
  return x;
}

int textWriteD2(Screen& scr, int col, int row, const std::string& s, uint8_t attr, const GaijiTable& gaiji) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
  size_t len = s.size();
  size_t i = 0;
  while (i < len && col < 80) {
    Glyph g;
    int used = fetchGlyphD2(p + i, len - i, gaiji, g);
    if (used == 0) break;
    i += used;
    uint8_t cell[16];
    for (int y = 0; y < 16; ++y) cell[y] = uint8_t(g.rows[y] >> 8);
    scr.setTextCell(col, row, attr, cell);
    ++col;
    if (g.width == 16) {
      if (col >= 80) break;
      for (int y = 0; y < 16; ++y) cell[y] = uint8_t(g.rows[y] & 0xFF);
      scr.setTextCell(col, row, attr, cell);
      ++col;
    }
  }
  return col;
}

std::string rawName(const uint8_t* p, size_t n) {
  size_t k = 0;
  while (k < n && p[k] != 0) ++k;
  return std::string(reinterpret_cast<const char*>(p), k);
}

std::string trimName(const uint8_t* p, size_t n) {
  std::string s = rawName(p, n);
  for (;;) {
    if (!s.empty() && (s.back() == ' ' || s.back() == '\0')) {
      s.pop_back();
      continue;
    }
    if (s.size() >= 2 && uint8_t(s[s.size() - 2]) == 0x81 && uint8_t(s[s.size() - 1]) == 0x40) {
      s.resize(s.size() - 2);
      continue;
    }
    break;
  }
  return s;
}

namespace {
struct SjisMaps {
  std::unordered_map<uint32_t, uint16_t> uniToSjis;
  std::unordered_map<uint16_t, uint32_t> sjisToUni;
  SjisMaps() {
    for (int i = 0; i < kSjisTableSize; ++i) {
      const uint16_t sj = kSjisTable[i][0];
      const uint32_t u = kSjisTable[i][1];
      if (!uniToSjis.count(u)) uniToSjis[u] = sj;
      sjisToUni[sj] = u;
    }
    for (uint32_t u = 0xFF61; u <= 0xFF9F; ++u)
      if (!uniToSjis.count(u)) uniToSjis[u] = uint16_t(0xA1 + (u - 0xFF61));
    const uint32_t extra[][2] = {{0x301C, 0x8160}, {0x2016, 0x8161}, {0x2212, 0x817C},
                                 {0x00A2, 0x8191}, {0x00A3, 0x8192}, {0x00AC, 0x81CA}};
    for (auto& e : extra)
      if (!uniToSjis.count(e[0])) uniToSjis[e[0]] = uint16_t(e[1]);
  }
};
const SjisMaps& maps() {
  static SjisMaps m;
  return m;
}
}  // namespace

std::string utf8ToSjis(const std::string& in) {
  const uint8_t* s = reinterpret_cast<const uint8_t*>(in.data());
  const size_t n = in.size();
  std::vector<uint32_t> cps;
  size_t i = 0;
  while (i < n) {
    const uint8_t b = s[i];
    int extra;
    uint32_t cp;
    if (b < 0x80) {
      extra = 0;
      cp = b;
    } else if (b >= 0xC0 && b <= 0xDF) {
      extra = 1;
      cp = b & 0x1F;
    } else if (b >= 0xE0 && b <= 0xEF) {
      extra = 2;
      cp = b & 0x0F;
    } else if (b >= 0xF0 && b <= 0xF7) {
      extra = 3;
      cp = b & 0x07;
    } else {
      return in;
    }
    for (int k = 1; k <= extra; ++k) {
      if (i + k >= n) return in;
      const uint8_t c = s[i + k];
      if (c < 0x80 || c > 0xBF) return in;
      cp = (cp << 6) | (c & 0x3F);
    }
    cps.push_back(cp);
    i += extra + 1;
  }
  std::string out;
  const auto& m = maps();
  for (uint32_t u : cps) {
    uint32_t code;
    if (u < 0x80)
      code = u;
    else if (u == 0xA5)
      code = 0x5C;
    else if (u == 0x203E)
      code = 0x7E;
    else {
      auto it = m.uniToSjis.find(u);
      code = it != m.uniToSjis.end() ? it->second : 0x8148;
    }
    if (code >= 0x100) out.push_back(char(code >> 8));
    out.push_back(char(code & 0xFF));
  }
  return out;
}

std::string sjisToUtf8(const std::string& in) {
  const auto& m = maps();
  std::string out;
  auto put = [&](uint32_t u) {
    if (u < 0x80)
      out.push_back(char(u));
    else if (u < 0x800) {
      out.push_back(char(0xC0 | (u >> 6)));
      out.push_back(char(0x80 | (u & 0x3F)));
    } else {
      out.push_back(char(0xE0 | (u >> 12)));
      out.push_back(char(0x80 | ((u >> 6) & 0x3F)));
      out.push_back(char(0x80 | (u & 0x3F)));
    }
  };
  for (size_t i = 0; i < in.size(); ++i) {
    const uint8_t b = uint8_t(in[i]);
    if (isSjisLead(b) && i + 1 < in.size()) {
      const uint16_t code = uint16_t((b << 8) | uint8_t(in[i + 1]));
      auto it = m.sjisToUni.find(code);
      put(it != m.sjisToUni.end() ? it->second : 0xFFFD);
      ++i;
    } else if (b >= 0xA1 && b <= 0xDF) {
      put(0xFF61 + (b - 0xA1));
    } else {
      put(b);
    }
  }
  return out;
}

}  // namespace core
