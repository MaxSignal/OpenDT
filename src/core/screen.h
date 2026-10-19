// Pseudo PC-9801 screen model (spec 01 §1).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

constexpr int kScreenW = 640;
constexpr int kScreenH = 400;

// Text attribute values used by D2 (spec 01 §1.6).
constexpr uint8_t kTextWhite = 0xE1;
constexpr uint8_t kTextBlack = 0x01;
constexpr uint8_t kTextMask = 0x05;

// Decode a plane-sequential (B,R,G,I) image into palette indices (spec 01 §1.4).
// data must contain at least 4*planeStride bytes. W must be a multiple of 8.
void decodePlanes(const uint8_t* data, int w, int h, int planeStride, uint8_t* out);
std::vector<uint8_t> decodePlanes(const std::vector<uint8_t>& data, size_t offset, int w, int h,
                                  int planeStride);

// A paletted image (one byte per pixel, color index 0..15).
struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> px;
  Image() = default;
  Image(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * h_, 0) {}
  uint8_t at(int x, int y) const { return px[size_t(y) * w + x]; }
  uint8_t& at(int x, int y) { return px[size_t(y) * w + x]; }
};

struct ClipRect {
  int x0 = 0, y0 = 0, x1 = kScreenW, y1 = kScreenH;  // [x0,x1) x [y0,y1)
};

class Screen {
 public:
  Screen();

  // ---- state ----
  uint8_t pix[kScreenW * kScreenH];  // color index per pixel
  uint8_t pal[16][3];                // R,G,B 4-bit (0..15)
  bool textEnabled = false;
  uint8_t textAttr[25][80];
  uint8_t textGlyph[25][80][16];
  bool frameBorder = false;  // D1 black border (spec 01 §1.7)
  bool graphicsOn = true;
  int scrollLine = 0;  // 0..383 split scroll (spec 01 §1.9)
  int hOffset = 0;     // horizontal offset in 16-dot words

  void reset();

  // ---- palette ----
  void setPal(int i, int r, int g, int b);  // saturates each to 0..15
  void setPalette(const uint8_t p[16][3]);

  // ---- primitives (all clip to screen) ----
  void clear(int c);
  void fillRect(int x, int y, int w, int h, int c);
  void xorRect(int x, int y, int w, int h, int v);
  void orSilhouette(const Image& img, int x, int y, int w, int h, int v);
  void blit(const Image& img, int x, int y, bool transparent0, const ClipRect& clip = ClipRect());
  void blitPart(const Image& img, int sx, int sy, int w, int h, int x, int y, bool transparent0,
                const ClipRect& clip = ClipRect());
  void bitmap1(const uint8_t* rows, int n, int x, int y, int c);
  void pset(int x, int y, int c) {
    if (unsigned(x) < unsigned(kScreenW) && unsigned(y) < unsigned(kScreenH)) pix[y * kScreenW + x] = uint8_t(c);
  }
  uint8_t pget(int x, int y) const {
    if (unsigned(x) < unsigned(kScreenW) && unsigned(y) < unsigned(kScreenH)) return pix[y * kScreenW + x];
    return 0;
  }
  // Masked plane composite (spec 01 §1.4). mask/planes are 1bpp row-major (w/8 bytes per row),
  // planes ordered B,R,G,I each of planeStride bytes. x must be a multiple of 8 for byte
  // semantics; arbitrary x is handled per pixel with identical results.
  void maskedPlanes(const uint8_t* mask, const uint8_t* planes, int planeStride, int w, int h, int x, int y);

  // ---- text layer ----
  void clearText();                        // all cells attr 0xE1, glyph 0
  void setTextCell(int col, int row, uint8_t attr, const uint8_t* glyph16);
  void maskTextAll();                      // all cells black mask (0x05, glyph 0)
  void unmaskTextRect(int c0, int r0, int c1, int r1);  // inclusive range -> 0xE1 blank

  // ---- compose (spec 01 §1.10) ----
  void compose(uint8_t* rgb) const;  // 640*400*3
};

}  // namespace core
