// Shift-JIS handling and glyph rendering (spec 01 §2).
#pragma once
#include <cstdint>
#include <string>

#include "core/screen.h"

namespace core {

extern const uint8_t kFontHalf[256 * 16];
extern const uint8_t kFontFull[94 * 94 * 32];

inline bool isSjisLead(uint8_t b) { return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC); }

// Shift-JIS (h,l) -> JIS row/col (spec 01 §2.2). Returns (row<<8)|col. May be out of range.
uint16_t sjisToJis(uint8_t h, uint8_t l);

// 16 rows of 16-bit glyph data (MSB = leftmost dot). Half-width glyphs use the upper 8 bits.
struct Glyph {
  uint16_t rows[16];
  int width;  // 8 or 16
};

// Glyph lookups.
void halfGlyph(uint8_t c, Glyph& g);
void fullGlyphJis(uint16_t jis, Glyph& g);  // blank if out of range

// D1 rules: 1 or 2 bytes from s (spec 01 §2.4/2.5). Returns bytes consumed.
int fetchGlyphD1(const uint8_t* s, size_t len, Glyph& g);

// D1 opaque bold drawing. code is either single byte (<0x100) or SJIS 2-byte (h<<8|l).
// Returns advance width (8 or 16).
int drawCharD1(Screen& scr, int x, int y, uint16_t code, int fg = 15, int bg = 0);

// Fullwidth conversion table for D1 (spec 01 §2.7). Returns 0 if not convertible.
uint16_t d1HalfToFull(uint8_t c);

// D2 gaiji table: 256 glyphs, 32 bytes each (16 rows x 2 bytes). Set by D2 engine; may be null.
struct GaijiTable {
  const uint8_t* data = nullptr;  // 256*32
};

// D2 glyph fetch (spec 01 §2.6). Returns bytes consumed (0 at end/NUL). full set true when 16 wide.
int fetchGlyphD2(const uint8_t* s, size_t len, const GaijiTable& gaiji, Glyph& g);

// D2 graphic drawing: x rounded down to multiple of 8, bold, transparent. Returns advance.
int drawGlyphD2(Screen& scr, int x, int y, const Glyph& g, int color);
// Draw a NUL terminated / length-bounded SJIS string, returns final x.
int drawStringD2(Screen& scr, int x, int y, const std::string& s, int color, const GaijiTable& gaiji);
// Text layer write (no bold). Returns next column.
int textWriteD2(Screen& scr, int col, int row, const std::string& s, uint8_t attr, const GaijiTable& gaiji);

// Name trimming (spec 01 §6.4).
std::string trimName(const uint8_t* p, size_t n);
std::string rawName(const uint8_t* p, size_t n);  // up to NUL, no trimming

// UTF-8 -> Shift-JIS for display (spec 01 §2.10).
std::string utf8ToSjis(const std::string& s);
// Shift-JIS -> UTF-8 (for console/debug output only; not part of the spec).
std::string sjisToUtf8(const std::string& s);

}  // namespace core
