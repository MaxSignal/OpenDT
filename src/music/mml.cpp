#include "music/mml.h"

#include <algorithm>
#include <cstring>

#include "music/textscan.h"

namespace music {

int lengthTicks(int code) {
  static const int t[12] = {3, 6, 12, 24, 48, 1, 2, 4, 8, 16, 32, 64};
  return (code >= 0 && code < 12) ? t[code] : 0;
}

int tempoToQ(uint16_t tempo) {
  unsigned t = std::max<unsigned>(tempo, 30);
  int q = int((5880u / t) & 0xFF);
  return q == 0 ? 256 : q;
}

std::vector<std::string> splitLines(const std::vector<uint8_t>& text) {
  std::vector<std::string> lines;
  std::string cur;
  for (uint8_t b : text) {
    if (b == 0x1A) break;
    if (b == 0x0D) continue;
    if (b == 0x0A) {
      lines.push_back(cur + '\r');
      cur.clear();
      continue;
    }
    cur.push_back(char(b));
  }
  if (!cur.empty()) lines.push_back(cur + '\r');
  return lines;
}

namespace {

const uint8_t kLfoBase[12] = {0x7B, 0x83, 0x8A, 0x93, 0x9B, 0xA5, 0xAE, 0xB9, 0xC4, 0xD0, 0xDC, 0xE9};

enum LineKind { kNone, kSound, kLfo, kOp1, kOp3, kOp2, kOp4, kStr, kP1, kP2, kP3, kPD, kP4, kP5, kP6, kSsgEnv, kCont };

struct Classified {
  LineKind kind = kNone;
  size_t pos = 0;  // position after the keyword
};

Classified classify(const std::string& line) {
  Cursor c(line);
  c.skipDelims();
  Classified r;
  if (c.atEnd() || c.peek() == ';') return r;
  static const struct {
    const char* kw;
    LineKind k;
  } table[] = {{"SOUND:", kSound}, {"LFO:", kLfo}, {"OP1:", kOp1}, {"OP3:", kOp3}, {"OP2:", kOp2}, {"OP4:", kOp4},
               {"STR:", kStr},     {"1:", kP1},    {"2:", kP2},    {"3:", kP3},    {"D:", kPD},    {"4:", kP4},
               {"5:", kP5},        {"6:", kP6},    {"SSGENV:", kSsgEnv}, {"->", kCont}};
  for (auto& e : table) {
    Cursor t(line, c.p);
    if (t.matchKeyword(e.kw)) {
      r.kind = e.k;
      r.pos = t.p;
      return r;
    }
  }
  return r;
}

struct Macro {
  std::string name;
  int pos;
};

class Compiler {
 public:
  Song song;
  std::vector<Macro> macros;
  int lineNo = 0;

  void error(const std::string& msg) {
    ++song.errorCount;
    if (song.errors.size() < 32) song.errors.push_back("line " + std::to_string(lineNo) + ": " + msg);
  }

  void emit(uint8_t b) { song.code.push_back(b); }

  // Length reading (spec 02 §2.10).
  int readLength(Cursor& c) {
    if (uint8_t(c.peek()) > 0x39) return 15;
    c.skipDelims();
    static const struct {
      const char* s;
      int code;
    } lens[] = {{"32.", 0}, {"16.", 1}, {"8.", 2}, {"4.", 3}, {"2.", 4}, {"64", 5},
                {"32", 6},  {"16", 7},  {"8", 8},  {"4", 9},  {"2", 10}, {"1", 11}};
    for (auto& l : lens) {
      const size_t n = std::strlen(l.s);
      if (c.s.compare(c.p, n, l.s) == 0) {
        c.p += n;
        return l.code;
      }
    }
    return 15;
  }

  // Compiles a part line / macro body (spec 02 §2.9) and terminates it with `term`.
  void compileBody(Cursor& c, uint8_t term) {
    for (;;) {
      c.skipDelims();
      if (c.atEnd()) break;
      const char ch = upper(c.peek());
      ++c.p;
      bool ok = true;
      switch (ch) {
        case 'C': case 'D': case 'E': case 'F': case 'G': case 'A': case 'B': {
          static const int semis[7] = {9, 11, 0, 2, 4, 5, 7};  // A..G
          int semi = semis[ch - 'A'];
          if (c.peek() == '#') {
            if (semi < 11) ++semi;
            ++c.p;
          }
          if (c.peek() == '+') {
            if (semi < 11) ++semi;
            ++c.p;
          }
          if (c.peek() == '-') {
            if (semi > 0) --semi;
            ++c.p;
          }
          emit(uint8_t((semi << 4) | readLength(c)));
          break;
        }
        case 'R': emit(uint8_t(kOpRestBase | readLength(c))); break;
        case 'L': emit(uint8_t(kOpDefLen | readLength(c))); break;
        case 'O': {
          int n = c.readNum();
          if (n < 0 || n > 8) {
            error("O out of range");
            ok = false;
          } else {
            emit(uint8_t(kOpOctave | n));
          }
          break;
        }
        case 'V': emit(kOpVolume); emit(uint8_t(c.readNum())); break;
        case '>': emit(kOpOctUp); break;
        case '<': emit(kOpOctDown); break;
        case '&': emit(kOpTie); break;
        case '{': emit(kOpLoopStart); emit(uint8_t(c.readNum())); break;
        case '}': emit(kOpLoopEnd); break;
        case 'T': song.tempo = uint16_t(c.readNum()); break;
        case 'Q': emit(kOpGate); emit(uint8_t(c.readNum())); break;
        case '@': {
          int n = c.readNum();
          if (n < 0 || n > 20) {
            error("@ out of range");
            ok = false;
          } else {
            emit(kOpVoice);
            emit(uint8_t(n));
          }
          break;
        }
        case '$': {
          const Macro* found = nullptr;
          for (auto& m : macros) {
            if (c.s.compare(c.p, m.name.size(), m.name) == 0) {
              found = &m;
              break;
            }
          }
          if (!found) {
            error("undefined macro");
            ok = false;
          } else {
            c.p += found->name.size();
            emit(kOpCall);
            emit(uint8_t(found->pos & 0xFF));
            emit(uint8_t((found->pos >> 8) & 0xFF));
          }
          break;
        }
        case 'S': emit(kOpHwShape); emit(uint8_t(c.readNum())); break;
        case 'M': {
          uint16_t v = uint16_t(c.readNum());
          emit(kOpHwPeriod);
          emit(uint8_t(v & 0xFF));
          emit(uint8_t(v >> 8));
          break;
        }
        case 'Y': {
          int r = c.readNum();
          int d = c.readNum();
          emit(kOpExt); emit(0xF0); emit(uint8_t(r)); emit(uint8_t(d));
          break;
        }
        case 'W': emit(kOpExt); emit(uint8_t(readLength(c))); break;
        case 'N': {
          int n = c.readNum();
          const bool neg = n < 0;
          emit(kOpExt); emit(0xF1); emit(uint8_t((neg ? -n : n) & 0xFF)); emit(neg ? 1 : 0);
          break;
        }
        case 'P': emit(kOpExt); emit(0xF2); emit(uint8_t(c.readNum())); break;
        case 'U':
        case 'I': {
          int a = c.readNum(), b = c.readNum(), d = c.readNum();
          emit(kOpExt); emit(ch == 'U' ? 0xF3 : 0xF4); emit(uint8_t(a)); emit(uint8_t(b)); emit(uint8_t(d));
          break;
        }
        default:
          error(std::string("unknown command '") + ch + "'");
          ok = false;
          break;
      }
      if (!ok) break;
    }
    emit(term);
  }

  void passVoices(const std::vector<std::string>& lines) {
    int cur = 0;
    lineNo = 0;
    for (auto& line : lines) {
      ++lineNo;
      Classified k = classify(line);
      Cursor c(line, k.pos);
      Voice& v = song.voices[cur];
      switch (k.kind) {
        case kSound: {
          c.skipDelims();
          if (c.peek() == '@') ++c.p;
          int n = c.readNum();
          if (n >= 0 && n <= 20)
            cur = n;
          else
            error("SOUND: number out of range");
          break;
        }
        case kLfo: {
          int wf = c.readNum(), sp = c.readNum(), dp = c.readNum(), alg = c.readNum(), fb = c.readNum();
          v.wf = uint8_t(wf);
          v.speed = uint8_t(sp);
          v.depth = uint8_t(dp);
          v.algfb = uint8_t((alg & 7) + ((fb * 8) & 0xFF));
          for (int i = 0; i < 12; ++i) v.lfo[i] = int16_t(int(int8_t(kLfoBase[i])) * int(int8_t(v.depth)));
          v.lfo[12] = 0;
          break;
        }
        case kOp1: case kOp3: case kOp2: case kOp4: {
          const int slot = k.kind == kOp1 ? 0 : k.kind == kOp3 ? 1 : k.kind == kOp2 ? 2 : 3;
          int val[10];
          for (int& x : val) x = uint8_t(c.readNum());
          const int AR = val[0], DR = val[1], SR = val[2], RR = val[3], SL = val[4], TL = val[5], KS = val[6],
                    ML = val[7], DT = val[8], DT2 = val[9];
          v.dtml[slot] = uint8_t(ML | (DT * 16));
          v.tl[slot] = uint8_t(TL);
          v.ksar[slot] = uint8_t(AR | (KS * 64));
          v.dr[slot] = uint8_t(DR);
          v.sr[slot] = uint8_t(SR);
          v.slrr[slot] = uint8_t(RR | (SL * 16));
          v.dt2[slot] = uint8_t(std::min(DT2, 3));
          break;
        }
        default: break;
      }
    }
  }

  void passMacros(const std::vector<std::string>& lines) {
    lineNo = 0;
    for (auto& line : lines) {
      ++lineNo;
      Classified k = classify(line);
      if (k.kind != kStr) continue;
      Cursor c(line, k.pos);
      c.skipDelims();
      std::string name;
      while (!c.atEnd() && c.peek() != ' ' && c.peek() != '\t' && c.peek() != '=') {
        if (name.size() < 12) name.push_back(c.peek());
        ++c.p;
      }
      if (name.empty() || name.back() != '$') name.push_back('$');
      macros.push_back({name, int(song.code.size())});
      c.skipDelims();
      if (c.peek() == '=') ++c.p;
      compileBody(c, kOpReturn);
    }
  }

  void passEnvelopes(const std::vector<std::string>& lines) {
    lineNo = 0;
    auto readValues = [&](Cursor& c) {
      while (!c.atEnd()) {
        bool prog = false;
        int v = c.readNum(&prog);
        if (!prog) {
          // Neither a digit, a delimiter nor the line end: stop reading this line
          // (the reference implementation would hang here; spec 02 §2.8).
          Cursor t = c;
          t.skipDelims();
          if (!t.atEnd()) {
            error("invalid character in SSGENV");
            break;
          }
        }
        song.env.push_back(uint8_t(v));
      }
    };
    for (auto& line : lines) {
      ++lineNo;
      Classified k = classify(line);
      if (k.kind == kSsgEnv) {
        Cursor c(line, k.pos);
        c.skipDelims();
        if (c.peek() == '@') ++c.p;
        int n = c.readNum();
        if (n < 1 || n > 20) {
          error("SSGENV number out of range");
          continue;
        }
        song.env.push_back(0xFF);
        song.envStart[n] = int(song.env.size());
        readValues(c);
      } else if (k.kind == kCont) {
        Cursor c(line, k.pos);
        readValues(c);
      }
    }
    song.env.push_back(0xFF);
  }

  void passParts(const std::vector<std::string>& lines) {
    static const LineKind order[7] = {kP1, kP2, kP3, kPD, kP4, kP5, kP6};
    for (int ch = 0; ch < 7; ++ch) {
      song.partStart[ch] = int(song.code.size());
      lineNo = 0;
      for (auto& line : lines) {
        ++lineNo;
        Classified k = classify(line);
        if (k.kind != order[ch]) continue;
        Cursor c(line, k.pos);
        compileBody(c, kOpLineEnd);
      }
      emit(kOpPartEnd);
    }
  }
};

}  // namespace

Song compileMml(const std::vector<uint8_t>& text) {
  const auto lines = splitLines(text);
  Compiler comp;
  comp.passVoices(lines);
  comp.passMacros(lines);
  comp.emit(kOpPartEnd);
  comp.passEnvelopes(lines);
  comp.passParts(lines);
  return std::move(comp.song);
}

}  // namespace music
