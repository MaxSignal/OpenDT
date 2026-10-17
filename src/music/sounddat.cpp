#include "music/sounddat.h"

#include "music/mml.h"
#include "music/textscan.h"

namespace music {

static int16_t divTrunc(int a, int b) { return b == 0 ? 0 : int16_t(a / b); }

SoundDat parseSoundDat(const std::vector<uint8_t>& text) {
  SoundDat sd;
  int mul1 = 1, mul2 = 1;
  SfxRecord* cur = nullptr;
  for (const auto& line : splitLines(text)) {
    Cursor c(line);
    c.skipDelims();
    if (c.atEnd() || c.peek() == ';') continue;
    if (c.matchKeyword("SOUND:")) {
      sd.records.emplace_back();
      cur = &sd.records.back();
      c.skipDelims();
      if (c.peek() == '@') {
        ++c.p;
        int n = c.readNum();
        if (n >= 0 && n <= 64) sd.table[n] = int(sd.records.size()) - 1;
      } else {
        cur->chain = true;
      }
      uint16_t len = uint16_t(c.readNum());
      cur->length = len == 0 ? 60 : len;
      mul1 = c.readNum();
      if (mul1 == 0) mul1 = 1;
      mul2 = c.readNum();
      if (mul2 == 0) mul2 = 1;
      continue;
    }
    if (!cur) continue;
    int k = -1;
    enum { kTone, kVol, kNoise } kind;
    if (c.matchKeyword("TONE1:")) {
      kind = kTone, k = 0;
    } else if (c.matchKeyword("TONE2:")) {
      kind = kTone, k = 1;
    } else if (c.matchKeyword("VOL1:")) {
      kind = kVol, k = 0;
    } else if (c.matchKeyword("VOL2:")) {
      kind = kVol, k = 1;
    } else if (c.matchKeyword("NOISE:")) {
      kind = kNoise;
    } else {
      continue;
    }
    if (kind == kTone) {
      SfxParam& p = cur->tone[k];
      cur->mixer = uint8_t(cur->mixer & ~(1 << k));
      p.start = uint16_t(c.readNum());
      int change = c.readNum();
      int time = c.readNum();
      if (time == 0) time = 1;
      int16_t count = int16_t(time * mul1);
      p.inc = divTrunc(change, count);
      p.count = uint16_t(count);
      p.mode = c.readNum();
    } else if (kind == kVol) {
      SfxParam& p = cur->vol[k];
      p.start = uint16_t((uint8_t(c.readNum())) * 256);
      int change = int16_t(uint16_t(uint8_t(c.readNum()) * 256));
      int time = c.readNum();
      if (time == 0) time = 1;
      int16_t count = int16_t(time * mul2);
      p.inc = divTrunc(change, count);
      p.count = uint16_t(count);
      p.mode = c.readNum();
    } else {
      SfxParam& p = cur->noise;
      int use = c.readNum() & 3;
      if (use & 1) cur->mixer = uint8_t(cur->mixer & ~0x08);
      if (use & 2) cur->mixer = uint8_t(cur->mixer & ~0x10);
      p.start = uint16_t((uint8_t(c.readNum())) * 256);
      int change = int16_t(uint16_t(uint8_t(c.readNum()) * 256));
      int time = c.readNum();
      if (time == 0) time = 1;
      int16_t count = int16_t(time * mul1);
      p.inc = divTrunc(change, count);
      p.count = uint16_t(count);
      p.mode = c.readNum();
    }
  }
  return sd;
}

}  // namespace music
