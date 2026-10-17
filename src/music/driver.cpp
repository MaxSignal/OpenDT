#include "music/driver.h"

#include <algorithm>

namespace music {

namespace {

const int kFnum[4][13] = {
    {617, 655, 694, 735, 779, 825, 874, 926, 981, 1040, 1102, 1167, 617},
    {431, 458, 485, 514, 545, 577, 611, 648, 686, 728, 771, 816, 431},
    {493, 524, 555, 588, 623, 660, 699, 740, 784, 832, 881, 933, 493},
    {530, 563, 596, 632, 669, 709, 751, 796, 843, 894, 947, 1003, 530},
};
const int kTone[13] = {3816, 3602, 3400, 3209, 3029, 2859, 2698, 2547, 2404, 2269, 2142, 2022, 33659};
const int kDeltaFm[13] = {34, 38, 39, 41, 44, 46, 49, 52, 55, 59, 62, 65, 67};
const int kDeltaSsg[13] = {228, 214, 202, 191, 180, 170, 161, 151, 143, 135, 127, 120, 114};
const int kCarriers[8] = {1, 1, 1, 1, 2, 3, 3, 4};
const int kCarrierSlots[4] = {3, 2, 1, 0};  // OP4, OP2, OP3, OP1

inline bool isFm(int ch) { return ch <= 2; }
inline bool isSsg(int ch) { return ch >= 4; }
inline int floorDiv256(int v) { return v >= 0 ? v / 256 : -((-v + 255) / 256); }

}  // namespace

Driver::Driver(WriteFn w) : w_(std::move(w)) {
  for (int i = 0; i < 7; ++i) ch_[i].start = ch_[i].pos = -1;
}

void Driver::write(uint8_t reg, uint8_t val) {
  shadow_[reg] = val;
  if (w_) w_(reg, val);
}

void Driver::writeM(uint8_t reg, uint8_t val) {
  if (suppress_) return;
  write(reg, val);
}

void Driver::load(Song song) { song_ = std::move(song); }

// ---------------------------------------------------------------------------
// channel setup

void Driver::initChannel(int i) {
  Chan& c = ch_[i];
  c.start = song_.partValid(i) ? song_.partStart[i] : -1;
  c.pos = c.start;
  c.defLen = 31;
  c.remain = 0;
  c.octave = 4;
  c.note = 0;
  c.tie = false;
  c.vol = 12;
  c.envVal = 15;
  c.envOn = false;
  c.callStack.clear();
  c.loopStack.clear();
  c.gate = 31;
  c.q = 8;
  c.detMag = 0;
  c.detNeg = false;
  c.porta = 0;
  c.portaRemain = 0;
  c.prevOct = 0;
  c.prevNote = 0;
  c.tremDepth = 0;
  c.vibDepth = 0;
  c.keyState = false;
}

void Driver::loadVoice(int ch, int n) {
  if (!isFm(ch)) return;
  n = std::max(0, std::min(n, 20));
  Chan& c = ch_[ch];
  const Voice& v = song_.voices[n];
  c.voice = n;
  c.voiceLoaded = true;
  c.lfoCur = 0;
  c.lfoDepth = v.depth;
  c.lfoSpeed = v.speed;
  c.lfoWave = v.wf;
  c.lfoShift = v.wf != 0 ? 1 : 0;
  write(uint8_t(0xB0 + ch), v.algfb);
  c.alg = v.algfb & 7;
  const uint8_t* groups[6] = {v.dtml, v.tl, v.ksar, v.dr, v.sr, v.slrr};
  for (int g = 0; g < 6; ++g)
    for (int s = 0; s < 4; ++s) write(uint8_t(0x30 + g * 0x10 + s * 4 + ch), groups[g][s]);
  volumeUpdate(ch);
}

// ---------------------------------------------------------------------------
// volume / frequency

void Driver::volumeUpdate(int ch) {
  Chan& c = ch_[ch];
  if (isFm(ch)) {
    int a = int(c.vol) - fade_;
    if (a < 0) a = 0;
    a = (a * 4) & 0xFF;
    if (c.tremDepth != 0) {
      a = (a + c.tremCur) & 0xFF;
      if (a >= 128)
        a = 0;
      else if (a > 60)
        a = 60;
    }
    const int base = (64 - a) & 0xFF;
    const Voice& v = song_.voices[c.voice];
    for (int k = 0; k < kCarriers[c.alg]; ++k) {
      const int s = kCarrierSlots[k];
      write(uint8_t(0x40 + s * 4 + ch), uint8_t(std::min(base + v.tl[s], 127)));
    }
  } else if (isSsg(ch)) {
    int v = int(c.vol) + c.envVal - 15;
    if (v < 0) v = 0;
    v -= fade_;
    if (v < 0) v = 0;
    if (c.tremDepth != 0) {
      v = int(int8_t(uint8_t(v + c.tremCur)));
      v = std::max(0, std::min(v, 15));
    }
    writeM(uint8_t(8 + ch - 4), uint8_t(v));
    c.hwOn = false;
  }
}

int Driver::fmDetune(const Chan& c, int note) const {
  if (c.detMag == 0) return 0;
  const int i = c.detNeg ? note : note + 1;
  const int v = kDeltaFm[std::min(i, 12)] * c.detMag / 256;
  return c.detNeg ? -v : v;
}

int Driver::ssgDetune(const Chan& c, int note) const {
  if (c.detMag == 0) return 0;
  const int i = c.detNeg ? note : note + 1;
  const int v = kDeltaSsg[std::min(i, 12)] * c.detMag / 256;
  return c.detNeg ? v : -v;
}

int Driver::fmOffset(const Chan& c, int note) const {
  int off = fmDetune(c, note);
  if (c.vibDepth != 0 && c.vibWait == 0) {
    const int i = c.vibUp ? note + 1 : note;
    const int v = kDeltaFm[std::min(i, 12)] * c.vibDepth / 256;
    off += c.vibUp ? v : -v;
  }
  return off;
}

int Driver::ssgOffset(const Chan& c, int note) const {
  int off = ssgDetune(c, note);
  if (c.vibDepth != 0 && c.vibWait == 0) {
    const int i = c.vibUp ? note + 1 : note;
    const int v = kDeltaSsg[std::min(i, 12)] * c.vibDepth / 256;
    off += c.vibUp ? -v : v;
  }
  return off;
}

void Driver::freqWrite(int ch) {
  Chan& c = ch_[ch];
  if (c.note >= 12 || ch == 3) return;
  const bool porta = c.porta != 0 && c.portaRemain != 0;
  if (ch <= 1) {
    int F, B;
    if (porta) {
      F = c.portaCur;
      B = c.portaBlock;
    } else {
      F = kFnum[0][c.note] + floorDiv256(c.lfoCur) + fmOffset(c, c.note);
      B = c.octave;
    }
    F &= 0xFFFF;
    write(uint8_t(0xA4 + ch), uint8_t((B * 8) | (F >> 8)));
    write(uint8_t(0xA0 + ch), uint8_t(F & 0xFF));
  } else if (ch == 2) {
    static const struct {
      uint8_t hi, lo;
      int slot;
    } ops[4] = {{0xA6, 0xA2, 3}, {0xAE, 0xAA, 2}, {0xAC, 0xA8, 1}, {0xAD, 0xA9, 0}};
    const Voice& v = song_.voices[c.voice];
    for (auto& op : ops) {
      const int d = v.dt2[op.slot];
      int F, B;
      if (porta) {
        F = c.portaCur;
        B = c.portaBlock;
      } else {
        F = kFnum[d][c.note] + floorDiv256(c.lfoCur) + fmOffset(c, c.note);
        B = c.octave;
      }
      if (d != 0) ++B;
      F &= 0xFFFF;
      write(op.hi, uint8_t((B * 8) | (F >> 8)));
      write(op.lo, uint8_t(F & 0xFF));
    }
  } else {
    int P, s;
    if (porta) {
      P = c.portaCur;
      s = c.portaBlock;
    } else {
      P = kTone[c.note] + ssgOffset(c, c.note);
      s = c.octave;
    }
    const int val = ((P * 2) & 0xFFFF) >> (s & 15);
    const int k = ch - 4;
    writeM(uint8_t(2 * k), uint8_t(val & 0xFF));
    writeM(uint8_t(2 * k + 1), uint8_t((val >> 8) & 0xFF));
  }
}

// ---------------------------------------------------------------------------
// key on/off

void Driver::keyOn(int ch) {
  Chan& c = ch_[ch];
  c.keyState = true;
  if (isFm(ch)) {
    write(0x28, uint8_t(0xF0 + ch));
  } else if (isSsg(ch)) {
    const int k = ch - 4;
    writeM(7, uint8_t(shadow_[7] & ~(1 << k)));
    if (c.hwOn) {
      writeM(uint8_t(8 + k), 0x10);
      writeM(0x0D, c.hwShape);
    } else if (c.envOn) {
      const int p = c.envVolStart;
      if (p >= 0 && p < int(song_.env.size())) {
        const uint8_t v = song_.env[p];
        c.envVal = v;  // 0xFF -> 255 (no volume values)
      } else {
        c.envVal = 15;
      }
      c.envPos = p + 1;
      c.envCounter = c.envRate;
      volumeUpdate(ch);
    }
  }
}

void Driver::keyOff(int ch) {
  Chan& c = ch_[ch];
  c.keyState = false;
  if (isFm(ch)) {
    write(0x28, uint8_t(ch));
  } else if (isSsg(ch)) {
    writeM(7, uint8_t(shadow_[7] | (1 << (ch - 4))));
  }
}

// ---------------------------------------------------------------------------
// play / stop / fade

void Driver::play() {
  playing_ = false;
  for (int i = 0; i < 7; ++i) {
    initChannel(i);
    if (isFm(i)) loadVoice(i, i + 1);
    freqWrite(i);
  }
  for (bool& f : lineEnd_) f = false;
  write(8, 0);
  write(9, 0);
  write(10, 0);
  write(0x27, 0x40);
  fade_ = 0;
  playing_ = true;
  phase_ = false;
  write(0x26, uint8_t((256 - timerQ()) & 0xFF));
  write(0x27, 0x7A);
}

void Driver::stop() {
  playing_ = false;
  write(0x28, 2);
  write(0x28, 1);
  write(0x28, 0);
  write(7, 0xBF);
  write(10, 0);
  write(9, 0);
  write(8, 0);
  if (ver221_)
    for (int r = 0x90; r <= 0x9E; ++r) write(uint8_t(r), 0);
}

void Driver::setFade(int level) {
  fade_ = level & 0xFF;
  for (int i = 0; i < 7; ++i) {
    if (isFm(i) && !ch_[i].voiceLoaded) continue;
    volumeUpdate(i);
  }
}

// ---------------------------------------------------------------------------
// sequencer

void Driver::timerB() {
  if (!playing_) return;
  phase_ = !phase_;
  if (phase_) tick();
}

void Driver::tick() {
  bool any = false;
  for (int i = 0; i < 7; ++i)
    if (ch_[i].start >= 0) any = true;
  if (!any) return;
  for (int round = 0; round < 4; ++round) {
    for (int i = 0; i < 7; ++i) {
      suppress_ = ver221_ && sfxRemain_ != 0 && (i == 4 || i == 5);
      channelTick(i);
      suppress_ = false;
    }
    bool all = true;
    for (bool f : lineEnd_) all = all && f;
    if (!all) break;
    for (int i = 0; i < 7; ++i) {
      lineEnd_[i] = false;
      if (ch_[i].start >= 0) ch_[i].pos += 1;
    }
  }
}

void Driver::channelTick(int ch) {
  Chan& c = ch_[ch];
  if (c.start < 0) {
    lineEnd_[ch] = true;
    return;
  }
  // vibrato
  if (c.vibDepth != 0) {
    if (c.vibWait == 0) {
      c.vibCount -= 1;
      if (c.vibCount == 0) {
        c.vibUp = !c.vibUp;
        c.vibCount = c.vibPeriod;
        freqWrite(ch);
      }
    } else {
      c.vibWait -= 1;
      if (c.vibWait == 0) freqWrite(ch);
    }
  }
  // tremolo
  if (c.tremDepth != 0) {
    if (c.tremWait == 0) {
      c.tremCount -= 1;
      if (c.tremCount == 0) {
        c.tremCur = -c.tremCur;
        c.tremCount = c.tremPeriod;
        volumeUpdate(ch);
      }
    } else {
      c.tremWait -= 1;
      if (c.tremWait == 0) {
        c.tremCur = c.tremDepth;
        volumeUpdate(ch);
      }
    }
  }
  // portamento
  if (c.porta != 0 && c.portaRemain != 0) {
    c.portaRemain = uint8_t(c.portaRemain - 1);
    if (c.portaRemain != 0) c.portaCur += c.portaInc;
    freqWrite(ch);
  }
  // gate
  if (c.gate == 0) {
    keyOff(ch);
    c.gate = 65535;
  } else {
    c.gate = uint16_t(c.gate - 1);
  }
  // soft envelope
  if (c.envOn) {
    if (c.envCounter == 0) {
      const uint8_t v = (c.envPos >= 0 && c.envPos < int(song_.env.size())) ? song_.env[c.envPos] : 0xFF;
      if (v != 0xFF) {
        c.envVal = v;
        volumeUpdate(ch);
        c.envPos += 1;
        c.envCounter = c.envRate;
      }
    } else {
      c.envCounter -= 1;
    }
  }
  if (c.remain > 0) {
    c.remain -= 1;
    if (c.lfoDepth != 0 && c.note != 12) lfoAdvance(ch);
    return;
  }
  execute(ch);
}

void Driver::gatePrep(Chan& c, int nextPos) {
  if (codeAt(nextPos) == kOpTie || c.q >= 8)
    c.gate = 65535;
  else
    c.gate = uint16_t((c.remain * c.q / 8) & 0xFFFF);
}

void Driver::portaPrep(int ch) {
  Chan& c = ch_[ch];
  if (c.porta == 0 || c.note == 12 || c.prevNote == 12 || ch == 3) {
    c.portaRemain = 0;
    return;
  }
  const int chO = c.octave, cl = c.prevOct;
  int block = chO;
  c.portaRemain = uint8_t(c.porta + 1);
  int target, start;
  if (isFm(ch)) {
    target = kFnum[0][c.note] + fmDetune(c, c.note);
    if (chO < cl) {
      block = cl;
      target = (target & 0xFFFF) >> (cl - chO);
    }
    start = kFnum[0][c.prevNote] + fmDetune(c, c.prevNote);
    if (chO > cl) start = (start & 0xFFFF) >> (chO - cl);
  } else {
    target = kTone[c.note] + ssgDetune(c, c.note);
    if (chO > cl) {
      block = cl;
      target = (target & 0xFFFF) >> (chO - cl);
    }
    start = kTone[c.prevNote] + ssgDetune(c, c.prevNote);
    if (chO < cl) start = (start & 0xFFFF) >> (cl - chO);
  }
  c.portaBlock = block;
  c.portaCur = start;
  const int diff = int(int16_t(uint16_t(target - start)));
  c.portaInc = c.portaRemain ? diff / int(c.portaRemain) : 0;
}

void Driver::lfoAdvance(int ch) {
  Chan& c = ch_[ch];
  c.lfoAcc += c.lfoSpeed;
  if (c.lfoAcc < 256) return;
  c.lfoAcc = (c.lfoAcc - 256) & 0xFF;
  c.lfoStep = (c.lfoStep + 1) & 0xFF;
  const int k = c.lfoStep;
  const int t = song_.voices[c.voice].lfo[std::min(c.note, 12)];
  const int R = -(t * (1 << c.lfoShift));
  int nv = c.lfoCur;
  switch (c.lfoWave) {
    case 0: nv = (k & 1) ? -t : t; break;
    case 1:
      if (k == 5) {
        nv = R;
        c.lfoStep = 0;
      } else {
        nv = c.lfoCur + t;
      }
      break;
    case 2:
      if (k < 5)
        nv = c.lfoCur + t;
      else if (k <= 7)
        nv = t - c.lfoCur;
      else {
        nv = R;
        c.lfoStep = 0;
      }
      break;
    case 3:
      if (k < 3)
        nv = c.lfoCur + t;
      else {
        nv = 0;
        c.lfoStep = 6;
      }
      break;
    default: return;
  }
  c.lfoCur = int16_t(uint16_t(nv));
  freqWrite(ch);
}

void Driver::doNote(int ch, int semi, int code) {
  Chan& c = ch_[ch];
  const int N = (code < 12) ? lengthTicks(code) : c.defLen + 1;
  c.remain = N - 1;
  c.note = semi;
  portaPrep(ch);
  c.prevOct = c.octave;
  c.prevNote = c.note;
  gatePrep(c, c.pos + 1);
  if (ch == 3) {
    static const int map[12] = {2, -1, 3, -1, 4, 5, -1, 6, -1, 0, -1, 1};
    if (semi < 12 && map[semi] >= 0) requestSfx(0, map[semi]);
    c.pos += 1;
    return;
  }
  if (!c.tie) keyOff(ch);
  {
    const int t = isFm(ch) ? song_.voices[c.voice].lfo[std::min(semi, 12)] : 0;
    c.lfoCur = int16_t(uint16_t(-(t * (1 << c.lfoShift))));
    c.lfoAcc = 0;
    c.lfoStep = 0;
  }
  if (c.tie || semi == 12) {
    c.tie = false;
  } else {
    keyOn(ch);
    if (c.vibDepth != 0) {
      c.vibWait = c.vibDelay;
      c.vibCount = c.vibPeriod;
      c.vibUp = true;
    }
    if (c.tremDepth != 0) {
      if (c.tremDelay == 0) c.tremCur = c.tremDepth;
      c.tremWait = c.tremDelay;
      c.tremCount = c.tremPeriod;
    }
  }
  freqWrite(ch);
  c.pos += 1;
}

void Driver::execute(int ch) {
  Chan& c = ch_[ch];
  for (int count = 0; count < 4096; ++count) {
    const uint8_t op = codeAt(c.pos);
    if (op < 0xC0) {
      doNote(ch, op >> 4, op & 15);
      return;
    }
    if (op < 0xD0) {
      doNote(ch, 12, op & 15);
      return;
    }
    if (op < 0xE0) {
      const int code = op & 15;
      if (code < 12) c.defLen = lengthTicks(code) - 1;
      c.pos += 1;
      continue;
    }
    if (op < 0xF0) {
      c.octave = op & 15;
      c.pos += 1;
      continue;
    }
    switch (op) {
      case kOpVolume:
        c.vol = codeAt(c.pos + 1);
        c.pos += 2;
        volumeUpdate(ch);
        break;
      case kOpOctUp:
        if (c.octave < 7) c.octave += 1;
        c.pos += 1;
        break;
      case kOpOctDown:
        if (c.octave > 0) c.octave -= 1;
        c.pos += 1;
        break;
      case kOpTie:
        c.tie = true;
        c.pos += 1;
        break;
      case kOpLoopStart:
        if (c.loopStack.size() < 16) c.loopStack.push_back({c.pos + 2, codeAt(c.pos + 1)});
        c.pos += 2;
        break;
      case kOpLoopEnd:
        if (c.loopStack.empty()) {
          c.pos += 1;
        } else {
          auto& top = c.loopStack.back();
          if (top.second == 0) {
            c.pos = top.first;
          } else {
            top.second -= 1;
            if (top.second != 0) {
              c.pos = top.first;
            } else {
              c.loopStack.pop_back();
              c.pos += 1;
            }
          }
        }
        break;
      case kOpVoice: {
        const int n = codeAt(c.pos + 1);
        c.pos += 2;
        if (isFm(ch)) {
          loadVoice(ch, n);
        } else if (isSsg(ch)) {
          if (n == 0) {
            c.envOn = false;
            c.envVal = 15;
          } else if (n >= 1 && n <= 20 && song_.envStart[n] >= 0) {
            const int st = song_.envStart[n];
            const int p = st < int(song_.env.size()) ? song_.env[st] : 0;
            c.envRate = p == 0 ? 0 : p - 1;
            c.envVolStart = st + 1;
            c.envPos = 0;
            c.envOn = true;
            c.hwOn = false;
          }
        }
        break;
      }
      case kOpCall: {
        const int target = codeAt(c.pos + 1) | (codeAt(c.pos + 2) << 8);
        if (c.callStack.size() < 16) c.callStack.push_back(c.pos + 3);
        c.pos = target;
        break;
      }
      case kOpReturn:
        if (c.callStack.empty()) {
          c.pos += 1;
        } else {
          c.pos = c.callStack.back();
          c.callStack.pop_back();
        }
        break;
      case kOpHwShape:
        c.hwShape = codeAt(c.pos + 1);
        c.hwOn = true;
        c.envOn = false;
        c.pos += 2;
        break;
      case kOpHwPeriod:
        write(0x0B, codeAt(c.pos + 1));
        write(0x0C, codeAt(c.pos + 2));
        c.envOn = false;
        c.hwOn = true;
        c.pos += 3;
        break;
      case kOpGate:
        c.q = codeAt(c.pos + 1);
        c.pos += 2;
        break;
      case kOpExt: {
        const uint8_t sub = codeAt(c.pos + 1);
        if (sub <= 0x0F) {
          // wait (W)
          const int N = (sub < 12) ? lengthTicks(sub) : c.defLen + 1;
          c.remain = N - 1;
          c.note = 12;
          gatePrep(c, c.pos + 2);
          c.pos += 2;
          return;
        }
        switch (sub) {
          case 0xF0: {
            const uint8_t r = codeAt(c.pos + 2);
            uint8_t d = codeAt(c.pos + 3);
            if (r == 7) d = uint8_t((shadow_[7] & 0xC0) + (d & 0x3F));
            write(r, d);
            c.pos += 4;
            break;
          }
          case 0xF1:
            c.detMag = codeAt(c.pos + 2);
            c.detNeg = codeAt(c.pos + 3) != 0;
            c.pos += 4;
            break;
          case 0xF2:
            c.porta = codeAt(c.pos + 2);
            c.vibDepth = 0;
            c.pos += 3;
            break;
          case 0xF3:
            c.tremDepth = codeAt(c.pos + 2);
            c.tremPeriod = codeAt(c.pos + 3);
            if (c.tremPeriod == 0) c.tremPeriod = 1;
            c.tremDelay = codeAt(c.pos + 4);
            c.pos += 5;
            break;
          case 0xF4:
            c.vibDepth = codeAt(c.pos + 2);
            c.vibPeriod = codeAt(c.pos + 3);
            if (c.vibPeriod == 0) c.vibPeriod = 1;
            c.vibDelay = codeAt(c.pos + 4);
            c.porta = 0;
            c.pos += 5;
            break;
          default:
            c.pos += 2;
            break;
        }
        break;
      }
      case kOpNop:
        c.pos += 1;
        break;
      case kOpLineEnd:
        lineEnd_[ch] = true;
        return;
      case kOpPartEnd:
      default:
        c.pos = c.start;
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// effect sounds

void Driver::requestSfx(int prio, int n) {
  if (sfxRemain_ != 0 && prio < sfxPrio_) return;
  sfxPrio_ = prio;
  sfxPending_ = n;
}

void Driver::sfxStart(int idx) {
  const SfxRecord& r = sfx_.records[idx];
  sfxRec_ = idx;
  auto copy = [](SfxState& s, const SfxParam& p) {
    s.cur = p.start;
    s.start = p.start;
    s.inc = p.inc;
    s.mode = p.mode;
    s.counter = s.reload = p.count;
  };
  for (int k = 0; k < 2; ++k) {
    copy(sTone_[k], r.tone[k]);
    copy(sVol_[k], r.vol[k]);
  }
  copy(sNoise_, r.noise);
  sfxRemain_ = r.length;
  write(7, uint8_t((shadow_[7] | 0x1B) & r.mixer));
}

int Driver::sfxUpdate(SfxState& s, int maxv) {
  int out;
  if (s.counter == 0) {
    if (s.mode == 0) {
      s.cur = s.start;
      s.counter = s.reload;
    }
    out = s.cur;
  } else {
    s.counter -= 1;
    out = std::max(0, std::min(s.cur + s.inc, maxv));
    s.cur = out;
  }
  return out;
}

void Driver::vsync() {
  if (sfxPending_ >= 0) {
    const int n = sfxPending_;
    sfxPending_ = -1;
    if (n <= 64 && sfx_.table[n] >= 0) sfxStart(sfx_.table[n]);
  }
  if (sfxRemain_ == 0) return;
  for (int k = 0; k < 2; ++k) {
    const int t = sfxUpdate(sTone_[k], 0xFFF);
    write(uint8_t(2 * k), uint8_t(t & 0xFF));
    write(uint8_t(2 * k + 1), uint8_t((t >> 8) & 0xFF));
    int v = sfxUpdate(sVol_[k], 0xF00) / 256;
    if (sfxPrio_ == 0) v = std::max(0, v - fade_);
    write(uint8_t(8 + k), uint8_t(v));
  }
  const int nz = sfxUpdate(sNoise_, 0x3F00);
  write(6, uint8_t((nz >> 8) & 0xFF));
  sfxRemain_ -= 1;
  if (sfxRemain_ == 0) sfxEnd();
}

void Driver::sfxEnd() {
  write(7, uint8_t(shadow_[7] | 0x1B));
  const int next = sfxRec_ + 1;
  if (next < int(sfx_.records.size()) && sfx_.records[next].chain) {
    sfxStart(next);
    return;
  }
  if (!ver221_ || !playing_) {
    write(8, 0);
    write(9, 0);
  } else {
    for (int ch = 4; ch <= 5; ++ch) {
      freqWrite(ch);
      volumeUpdate(ch);
      if (ch_[ch].keyState) keyOn(ch);
    }
  }
}

}  // namespace music
