// SOUND.DAT effect definitions (spec 02 §4.1–4.3).
#pragma once
#include <cstdint>
#include <vector>

namespace music {

struct SfxParam {
  uint16_t start = 0;
  int16_t inc = 0;
  uint16_t count = 0;
  int mode = 0;
};

struct SfxRecord {
  uint8_t mixer = 0xFF;
  bool chain = false;
  uint16_t length = 60;
  SfxParam tone[2];  // SSG A, B
  SfxParam vol[2];
  SfxParam noise;
  SfxRecord() {
    for (auto& t : tone) t = {100, 10, 10, 0};
    for (auto& v : vol) v = {0, -375, 10, 1};
    noise = {0x0F00, -375, 10, 0};
  }
};

struct SoundDat {
  std::vector<SfxRecord> records;  // in file order
  int table[65];                   // number -> record index, -1 if none
  SoundDat() {
    for (int& t : table) t = -1;
  }
};

SoundDat parseSoundDat(const std::vector<uint8_t>& text);

}  // namespace music
