#include "core/screen.h"

#include <algorithm>
#include <cstring>

namespace core {

void decodePlanes(const uint8_t* data, int w, int h, int planeStride, uint8_t* out) {
  const int bpr = w / 8;
  for (int y = 0; y < h; ++y) {
    for (int bx = 0; bx < bpr; ++bx) {
      const size_t o = size_t(y) * bpr + bx;
      const uint8_t b = data[o];
      const uint8_t r = data[o + planeStride];
      const uint8_t g = data[o + 2 * size_t(planeStride)];
      const uint8_t i = data[o + 3 * size_t(planeStride)];
      uint8_t* dst = out + size_t(y) * w + bx * 8;
      for (int k = 0; k < 8; ++k) {
        const int sh = 7 - k;
        dst[k] = uint8_t(((b >> sh) & 1) | (((r >> sh) & 1) << 1) | (((g >> sh) & 1) << 2) | (((i >> sh) & 1) << 3));
      }
    }
  }
}

std::vector<uint8_t> decodePlanes(const std::vector<uint8_t>& data, size_t offset, int w, int h,
                                  int planeStride) {
  std::vector<uint8_t> out(size_t(w) * h, 0);
  const size_t need = offset + 3 * size_t(planeStride) + size_t(w / 8) * h;
  if (data.size() >= need) {
    decodePlanes(data.data() + offset, w, h, planeStride, out.data());
  } else {
    // Short data: pad with zeros.
    std::vector<uint8_t> tmp(need - offset, 0);
    if (data.size() > offset) std::memcpy(tmp.data(), data.data() + offset, data.size() - offset);
    decodePlanes(tmp.data(), w, h, planeStride, out.data());
  }
  return out;
}

Screen::Screen() { reset(); }

void Screen::reset() {
  std::memset(pix, 0, sizeof(pix));
  std::memset(pal, 0, sizeof(pal));
  textEnabled = false;
  clearText();
  frameBorder = false;
  graphicsOn = true;
  scrollLine = 0;
  hOffset = 0;
}

static inline int sat15(int v) { return v < 0 ? 0 : (v > 15 ? 15 : v); }

void Screen::setPal(int i, int r, int g, int b) {
  if (i < 0 || i > 15) return;
  pal[i][0] = uint8_t(sat15(r));
  pal[i][1] = uint8_t(sat15(g));
  pal[i][2] = uint8_t(sat15(b));
}

void Screen::setPalette(const uint8_t p[16][3]) {
  for (int i = 0; i < 16; ++i) setPal(i, p[i][0], p[i][1], p[i][2]);
}

void Screen::clear(int c) { std::memset(pix, c & 0xFF, sizeof(pix)); }

void Screen::fillRect(int x, int y, int w, int h, int c) {
  int x0 = std::max(x, 0), y0 = std::max(y, 0);
  int x1 = std::min(x + w, kScreenW), y1 = std::min(y + h, kScreenH);
  for (int yy = y0; yy < y1; ++yy)
    for (int xx = x0; xx < x1; ++xx) pix[yy * kScreenW + xx] = uint8_t(c);
}

void Screen::xorRect(int x, int y, int w, int h, int v) {
  int x0 = std::max(x, 0), y0 = std::max(y, 0);
  int x1 = std::min(x + w, kScreenW), y1 = std::min(y + h, kScreenH);
  for (int yy = y0; yy < y1; ++yy)
    for (int xx = x0; xx < x1; ++xx) pix[yy * kScreenW + xx] ^= uint8_t(v);
}

void Screen::orSilhouette(const Image& img, int x, int y, int w, int h, int v) {
  w = std::min(w, img.w);
  h = std::min(h, img.h);
  for (int yy = 0; yy < h; ++yy)
    for (int xx = 0; xx < w; ++xx) {
      if (img.at(xx, yy) == 0) continue;
      int dx = x + xx, dy = y + yy;
      if (unsigned(dx) < unsigned(kScreenW) && unsigned(dy) < unsigned(kScreenH)) pix[dy * kScreenW + dx] |= uint8_t(v);
    }
}

void Screen::blitPart(const Image& img, int sx, int sy, int w, int h, int x, int y, bool transparent0,
                      const ClipRect& clip) {
  const int cx0 = std::max(clip.x0, 0), cy0 = std::max(clip.y0, 0);
  const int cx1 = std::min(clip.x1, kScreenW), cy1 = std::min(clip.y1, kScreenH);
  for (int yy = 0; yy < h; ++yy) {
    const int dy = y + yy;
    if (dy < cy0 || dy >= cy1) continue;
    const int iy = sy + yy;
    if (iy < 0 || iy >= img.h) continue;
    for (int xx = 0; xx < w; ++xx) {
      const int dx = x + xx;
      if (dx < cx0 || dx >= cx1) continue;
      const int ix = sx + xx;
      if (ix < 0 || ix >= img.w) continue;
      const uint8_t c = img.at(ix, iy);
      if (transparent0 && c == 0) continue;
      pix[dy * kScreenW + dx] = c;
    }
  }
}

void Screen::blit(const Image& img, int x, int y, bool transparent0, const ClipRect& clip) {
  blitPart(img, 0, 0, img.w, img.h, x, y, transparent0, clip);
}

void Screen::bitmap1(const uint8_t* rows, int n, int x, int y, int c) {
  for (int r = 0; r < n; ++r)
    for (int k = 0; k < 8; ++k)
      if (rows[r] & (0x80 >> k)) pset(x + k, y + r, c);
}

void Screen::maskedPlanes(const uint8_t* mask, const uint8_t* planes, int planeStride, int w, int h, int x, int y) {
  const int bpr = w / 8;
  for (int yy = 0; yy < h; ++yy) {
    const int dy = y + yy;
    if (unsigned(dy) >= unsigned(kScreenH)) continue;
    for (int bx = 0; bx < bpr; ++bx) {
      const size_t o = size_t(yy) * bpr + bx;
      const uint8_t m = mask[o];
      const uint8_t pb = planes[o], pr = planes[o + planeStride], pg = planes[o + 2 * size_t(planeStride)],
                    pi = planes[o + 3 * size_t(planeStride)];
      for (int k = 0; k < 8; ++k) {
        const int dx = x + bx * 8 + k;
        if (unsigned(dx) >= unsigned(kScreenW)) continue;
        const int sh = 7 - k;
        uint8_t& d = pix[dy * kScreenW + dx];
        uint8_t v = d;
        if ((m >> sh) & 1) v = 0;
        v |= uint8_t(((pb >> sh) & 1) | (((pr >> sh) & 1) << 1) | (((pg >> sh) & 1) << 2) | (((pi >> sh) & 1) << 3));
        d = v;
      }
    }
  }
}

void Screen::clearText() {
  for (int r = 0; r < 25; ++r)
    for (int c = 0; c < 80; ++c) {
      textAttr[r][c] = kTextWhite;
      std::memset(textGlyph[r][c], 0, 16);
    }
}

void Screen::setTextCell(int col, int row, uint8_t attr, const uint8_t* glyph16) {
  if (col < 0 || col >= 80 || row < 0 || row >= 25) return;
  textAttr[row][col] = attr;
  if (glyph16)
    std::memcpy(textGlyph[row][col], glyph16, 16);
  else
    std::memset(textGlyph[row][col], 0, 16);
}

void Screen::maskTextAll() {
  for (int r = 0; r < 25; ++r)
    for (int c = 0; c < 80; ++c) {
      textAttr[r][c] = kTextMask;
      std::memset(textGlyph[r][c], 0, 16);
    }
}

void Screen::unmaskTextRect(int c0, int r0, int c1, int r1) {
  for (int r = std::max(r0, 0); r <= std::min(r1, 24); ++r)
    for (int c = std::max(c0, 0); c <= std::min(c1, 79); ++c) {
      textAttr[r][c] = kTextWhite;
      std::memset(textGlyph[r][c], 0, 16);
    }
}

void Screen::compose(uint8_t* rgb) const {
  uint8_t table[16][3];
  for (int i = 0; i < 16; ++i)
    for (int k = 0; k < 3; ++k) table[i][k] = uint8_t(pal[i][k] * 17);
  const int L = (scrollLine >= 1 && scrollLine <= 383) ? scrollLine : 0;
  const long total = long(kScreenW) * kScreenH;
  for (int y = 0; y < kScreenH; ++y) {
    int sy = y;
    if (y < 384 && L) sy = (y < 384 - L) ? y + L : y - (384 - L);
    uint8_t* out = rgb + size_t(y) * kScreenW * 3;
    for (int x = 0; x < kScreenW; ++x, out += 3) {
      if (!graphicsOn || (frameBorder && (y < 16 || y >= 368 || x < 16 || x >= 624))) {
        out[0] = out[1] = out[2] = 0;
        continue;
      }
      long p = long(sy) * kScreenW + x + long(hOffset) * 16;
      p %= total;
      if (p < 0) p += total;
      const uint8_t c = pix[p] & 15;
      out[0] = table[c][0];
      out[1] = table[c][1];
      out[2] = table[c][2];
    }
  }
  if (!textEnabled) return;
  for (int r = 0; r < 25; ++r)
    for (int c = 0; c < 80; ++c) {
      const uint8_t a = textAttr[r][c];
      if (!(a & 1)) continue;
      const bool rev = (a & 4) != 0;
      const uint8_t cr = (a & 0x40) ? 255 : 0, cg = (a & 0x80) ? 255 : 0, cb = (a & 0x20) ? 255 : 0;
      for (int yy = 0; yy < 16; ++yy) {
        const uint8_t g = textGlyph[r][c][yy];
        uint8_t* out = rgb + (size_t(r * 16 + yy) * kScreenW + c * 8) * 3;
        for (int k = 0; k < 8; ++k, out += 3) {
          const bool bit = (g >> (7 - k)) & 1;
          if (bit != rev) {
            out[0] = cr;
            out[1] = cg;
            out[2] = cb;
          }
        }
      }
    }
}

}  // namespace core
