// MML compiler (spec 02 §2).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace music {

// Intermediate code byte values (spec 02 §2.11; the reference values are used as-is).
enum : uint8_t {
  kOpRestBase = 0xC0,
  kOpDefLen = 0xD0,
  kOpOctave = 0xE0,
  kOpVolume = 0xF0,
  kOpOctUp = 0xF1,
  kOpOctDown = 0xF2,
  kOpTie = 0xF3,
  kOpLoopEnd = 0xF4,
  kOpLoopStart = 0xF5,
  kOpVoice = 0xF6,
  kOpCall = 0xF7,
  kOpHwShape = 0xF8,
  kOpHwPeriod = 0xF9,
  kOpGate = 0xFA,
  kOpExt = 0xFB,  // FB 00-0F wait, F0 reg, F1 detune, F2 porta, F3 tremolo, F4 vibrato
  kOpNop = 0xFC,
  kOpLineEnd = 0xFD,
  kOpReturn = 0xFE,
  kOpPartEnd = 0xFF,
};

// FM voice. Operator slot index s = 0..3 corresponds to register offset s*4,
// i.e. slot 0 = OP1, 1 = OP3, 2 = OP2, 3 = OP4.
struct Voice {
  uint8_t wf = 0, speed = 0, depth = 0, algfb = 0;
  uint8_t dtml[4] = {}, tl[4] = {}, ksar[4] = {}, dr[4] = {}, sr[4] = {}, slrr[4] = {}, dt2[4] = {};
  int16_t lfo[13] = {};  // per note-name LFO amount (index 12 = rest = 0)
};

struct Song {
  std::vector<uint8_t> code;
  std::vector<uint8_t> env;  // SSG soft envelope table
  int envStart[21];          // position of the period value, -1 if undefined
  Voice voices[21];
  int partStart[7];          // per channel (1,2,3,D,4,5,6); -1 if no part lines
  uint16_t tempo = 120;
  int errorCount = 0;
  std::vector<std::string> errors;  // up to 32 messages
  Song() {
    for (int& e : envStart) e = -1;
    for (int& p : partStart) p = -1;
  }
  bool partValid(int ch) const {
    return partStart[ch] >= 0 && partStart[ch] < int(code.size()) && code[partStart[ch]] != kOpPartEnd;
  }
};

// Ticks for a length code (0..11); 0 for codes 12..15 (meaning "default length").
int lengthTicks(int code);

// Splits MML/SOUND.DAT text into lines (spec 02 §2.1). Each returned line ends with '\r'.
std::vector<std::string> splitLines(const std::vector<uint8_t>& text);

Song compileMml(const std::vector<uint8_t>& text);

// Timer-B count q for a tempo (spec 02 §1.3), 1..256.
int tempoToQ(uint16_t tempo);

}  // namespace music
