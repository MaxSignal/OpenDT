// Acceptance checks from spec 02 (music driver).
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/util.h"
#include "music/driver.h"
#include "music/mml.h"
#include "music/player.h"
#include "music/sounddat.h"
#include "music/textscan.h"
#include "test.h"

#define CHECK_STR(a, b)                                                                                     \
  do {                                                                                                      \
    std::string sa_ = (a), sb_ = (b);                                                                       \
    if (sa_ != sb_) {                                                                                       \
      std::fprintf(stderr, "  FAIL %s:%d:\n    got  %s\n    want %s\n", __FILE__, __LINE__, sa_.c_str(),       \
                   sb_.c_str());                                                                            \
      ++t::failures();                                                                                      \
    }                                                                                                       \
  } while (0)

namespace {

using W = std::pair<int, int>;

struct Rig {
  std::vector<W> log;
  music::Driver d{[this](uint8_t r, uint8_t v) { log.push_back({r, v}); }};
  void load(const std::string& mml) {
    std::vector<uint8_t> t(mml.begin(), mml.end());
    d.load(music::compileMml(t));
  }
  // Runs one tick and returns the writes made during it.
  std::vector<W> tick() {
    log.clear();
    d.tick();
    return log;
  }
  std::vector<W> vsync() {
    log.clear();
    d.vsync();
    return log;
  }
};

std::string fmt(const std::vector<W>& v) {
  std::string s;
  char b[16];
  for (auto& w : v) {
    std::snprintf(b, sizeof b, "%02X=%02X ", w.first, w.second);
    s += b;
  }
  if (!s.empty()) s.pop_back();
  return s;
}

bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

// value of the last write to reg in v, or -1
int lastWrite(const std::vector<W>& v, int reg) {
  int r = -1;
  for (auto& w : v)
    if (w.first == reg) r = w.second;
  return r;
}

bool hasWrite(const std::vector<W>& v, int reg, int val) {
  for (auto& w : v)
    if (w.first == reg && w.second == val) return true;
  return false;
}

const char* kVoice1 =
    "SOUND:@1\nLFO:0,0,0,4,7\nOP1:31,0,0,15,0,20,0,1,0,0\nOP2:31,0,0,15,0,30,0,1,0,0\n"
    "OP3:31,0,0,15,0,40,0,1,0,0\nOP4:31,0,0,15,0,10,0,1,0,0\n";

}  // namespace

TEST(music_tempo_table) {
  CHECK_EQ(music::tempoToQ(120), 49);
  CHECK_EQ(music::tempoToQ(30), 196);
  CHECK_EQ(music::tempoToQ(10), 196);
  CHECK_EQ(music::tempoToQ(60), 98);
  CHECK_EQ(music::tempoToQ(90), 65);
  CHECK_EQ(music::tempoToQ(230), 25);
  CHECK_EQ(music::tempoToQ(5880), 1);
  CHECK_EQ(music::tempoToQ(5881), 256);
  CHECK_EQ(music::tempoToQ(uint16_t(-5)), 256);
}

TEST(music_number_and_length_parsing) {
  // C:4 -> C (default length) then '4' error; C 4 / C,4 -> quarter
  auto s = music::compileMml(std::vector<uint8_t>{'1', ':', 'C', ' ', '4', ',', 'D', ',', '8', '\n'});
  CHECK_EQ(s.errorCount, 0);
  CHECK_EQ(s.code[s.partStart[0]], (0 << 4) | 9);
  CHECK_EQ(s.code[s.partStart[0] + 1], (2 << 4) | 8);
  std::string t = "1:C:4\n";
  auto s2 = music::compileMml(std::vector<uint8_t>(t.begin(), t.end()));
  CHECK_EQ(s2.errorCount, 1);
  CHECK_EQ(s2.code[s2.partStart[0]], (0 << 4) | 15);
  // 12 -> whole note then '2' error; 1. -> error; accidentals
  std::string t3 = "1:B+ C- C#+ E+4\n";
  auto s3 = music::compileMml(std::vector<uint8_t>(t3.begin(), t3.end()));
  CHECK_EQ(s3.errorCount, 0);
  const uint8_t* p = &s3.code[s3.partStart[0]];
  CHECK_EQ(p[0] >> 4, 11);
  CHECK_EQ(p[1] >> 4, 0);
  CHECK_EQ(p[2] >> 4, 2);
  CHECK_EQ(p[3], (5 << 4) | 9);
  // macros: self reference allowed, forward reference error, T in macro sets tempo
  std::string t4 = "STR:T=T90\nSTR:A=CD$B$\nSTR:B=E\n1:$T$ $A$\n";
  auto s4 = music::compileMml(std::vector<uint8_t>(t4.begin(), t4.end()));
  CHECK_EQ(s4.errorCount, 1);
  CHECK_EQ(s4.tempo, 90);
}

TEST(music_ssgenv_compile) {
  std::string t = "SSGENV:@1,2,15,12,9\nSSGENV:@2,1,15,14,\n->3\n";
  auto s = music::compileMml(std::vector<uint8_t>(t.begin(), t.end()));
  // FF 2 15 12 9 FF 1 15 14 0 3 FF
  std::vector<uint8_t> e = {0xFF, 2, 15, 12, 9, 0xFF, 1, 15, 14, 0, 3, 0xFF};
  CHECK(s.env == e);
  CHECK_EQ(s.envStart[1], 1);
  CHECK_EQ(s.envStart[2], 6);
}

TEST(music_start_sequence_and_ticks) {
  Rig r;
  r.load(std::string(kVoice1) + "1:T120 O4 L8 Q7 C D\n4:V15 O4 C4\n");
  r.d.play();
  const std::string s = fmt(r.log);
  CHECK(contains(s, "B0=3C 30=01 34=01 38=01 3C=01 40=14 44=28 48=1E 4C=0A 50=1F 54=1F 58=1F 5C=1F"));
  CHECK(contains(s, "80=0F 84=0F 88=0F 8C=0F 4C=1A 48=2E A4=22 A0=69 B1=00"));
  CHECK(contains(s, "4D=10 A5=22 A1=69"));
  CHECK(contains(s, "A6=22 A2=69 AE=22 AA=69 AC=22 A8=69 AD=22 A9=69"));
  CHECK(contains(s, "00=DD 01=01 02=DD 03=01 04=DD 05=01 08=00 09=00 0A=00 27=40 26=CF 27=7A"));
  CHECK_STR(fmt(r.tick()), std::string("28=00 28=F0 A4=22 A0=69 08=0F 07=01 07=00 00=DD 01=01"));
  for (int t = 2; t <= 7; ++t) r.tick();
  CHECK_STR(fmt(r.tick()), std::string("28=00"));  // tick 8: gate
  CHECK_STR(fmt(r.tick()), std::string("28=00 28=F0 A4=22 A0=B6"));  // tick 9
  CHECK(r.d.timerPeriodSec() > 0.0141346 && r.d.timerPeriodSec() < 0.0141347);
}

static std::vector<int> keyOnTicks(Rig& r, int nticks, int reg28val) {
  std::vector<int> ticks;
  for (int t = 1; t <= nticks; ++t) {
    auto w = r.tick();
    if (hasWrite(w, 0x28, reg28val)) ticks.push_back(t);
  }
  return ticks;
}

TEST(music_default_length) {
  Rig r;
  r.load("1:C D\n");
  r.d.play();
  auto k = keyOnTicks(r, 40, 0xF0);
  CHECK(k.size() >= 2 && k[0] == 1 && k[1] == 33);
}

TEST(music_gate) {
  Rig r;
  r.load("1:Q4 C4 D4\n");
  r.d.play();
  std::vector<int> on, off;
  for (int t = 1; t <= 34; ++t) {
    auto w = r.tick();
    if (hasWrite(w, 0x28, 0xF0)) on.push_back(t);
    if (w.size() == 1 && w[0] == W(0x28, 0)) off.push_back(t);
  }
  CHECK(on.size() >= 3 && on[0] == 1 && on[1] == 17 && on[2] == 33);
  CHECK(off.size() >= 2 && off[0] == 9 && off[1] == 25);
}

TEST(music_line_sync) {
  Rig r;
  r.load("1:C4\n1:E4\n4:V10 C2\n4:D4\n");
  r.d.play();
  auto k = keyOnTicks(r, 60, 0xF0);
  CHECK(k.size() >= 3 && k[0] == 1 && k[1] == 33 && k[2] == 49);
}

TEST(music_soft_lfo) {
  Rig r;
  r.load("SOUND:@1\nLFO:1,128,10,4,7\nOP1:31,0,0,15,0,20,0,1,0,0\nOP2:31,0,0,15,0,30,0,1,0,0\n"
         "OP3:31,0,0,15,0,40,0,1,0,0\nOP4:31,0,0,15,0,10,0,1,0,0\n1:L16 A2\n");
  r.d.play();
  const int expect[12] = {0x13, -1, 0x11, -1, 0x10, -1, 0x0E, -1, 0x0C, -1, 0x13, -1};
  for (int t = 1; t <= 11; ++t) {
    auto w = r.tick();
    if (expect[t - 1] >= 0) {
      CHECK_EQ(lastWrite(w, 0xA0), expect[t - 1]);
      CHECK_EQ(lastWrite(w, 0xA4), 0x24);
    }
  }
}

TEST(music_portamento) {
  Rig r;
  r.load("1:P3 L16 C >C4\n4:V10 P3 L16 C >C4\n");
  r.d.play();
  const int fm[9][2] = {{0x20, 0x26}, {0x20, 0xB6}, {0x21, 0x46}, {0x21, 0xD6}, {0x22, 0x69},
                        {0x29, 0x34}, {0x29, 0x81}, {0x29, 0xCE}, {0x2A, 0x1B}};
  const int ssg[9][2] = {{0xD0, 0x1D}, {0xD4, 0x16}, {0xD8, 0x0F}, {0xDC, 0x08}, {0xDD, 0x01},
                         {0xA1, 0x01}, {0x65, 0x01}, {0x2A, 0x01}, {-1, -1}};
  for (int t = 1; t <= 8; ++t) {
    auto w = r.tick();
    // tick 5 writes the target first, then the new note's start
    std::vector<W> fmw, ssw;
    for (auto& x : w) {
      if (x.first == 0xA4 || x.first == 0xA0) fmw.push_back(x);
      if (x.first == 0 || x.first == 1) ssw.push_back(x);
    }
    if (t < 5) {
      CHECK_EQ(lastWrite(w, 0xA4), fm[t - 1][0]);
      CHECK_EQ(lastWrite(w, 0xA0), fm[t - 1][1]);
      CHECK_EQ(lastWrite(w, 0x00), ssg[t - 1][0]);
      CHECK_EQ(lastWrite(w, 0x01), ssg[t - 1][1]);
    } else if (t == 5) {
      CHECK(fmw.size() == 4 && fmw[0] == W(0xA4, 0x22) && fmw[1] == W(0xA0, 0x69) && fmw[2] == W(0xA4, 0x29) &&
            fmw[3] == W(0xA0, 0x34));
      CHECK(ssw.size() >= 2 && ssw[0] == W(0x00, 0xDD) && ssw[1] == W(0x01, 0x01));
    } else {
      CHECK_EQ(lastWrite(w, 0xA4), fm[t][0]);
      CHECK_EQ(lastWrite(w, 0xA0), fm[t][1]);
      if (ssg[t - 1][0] >= 0) {
        CHECK_EQ(lastWrite(w, 0x00), ssg[t - 1][0]);
        CHECK_EQ(lastWrite(w, 0x01), ssg[t - 1][1]);
      }
    }
  }
}

TEST(music_vibrato_detune_tremolo) {
  Rig r;
  r.load("1:I128,2,3 N64 L8 A\n4:U3,2,0 V10 L8 A\n");
  r.d.play();
  std::vector<int> a0(10, -1), v8(10, -1), t0(10, -1);
  bool t9has02 = false;
  for (int t = 1; t <= 9; ++t) {
    auto w = r.tick();
    if (t == 9) t9has02 = hasWrite(w, 0xA0, 0x02);
    a0[t] = lastWrite(w, 0xA0);
    v8[t] = lastWrite(w, 0x08);
    t0[t] = lastWrite(w, 0x00);
  }
  CHECK_EQ(a0[1], 0x1F);
  CHECK_EQ(a0[4], 0x3E);
  CHECK_EQ(a0[6], 0x02);
  CHECK_EQ(a0[8], 0x3E);
  CHECK(t9has02);  // round 2 of tick 9 flips the vibrato before the part loops
  CHECK_EQ(v8[1], 0x0A);
  CHECK_EQ(v8[3], 0x07);
  CHECK_EQ(v8[5], 0x0D);
  CHECK_EQ(v8[7], 0x07);
  CHECK_EQ(v8[9], 0x0D);
  CHECK_EQ(t0[1], 0x1B);
}

TEST(music_soft_envelope) {
  Rig r;
  r.load("SSGENV:@1,2,15,12,9\n4:@1 V13 L16 C D\n");
  r.d.play();
  auto w1 = r.tick();
  int n = 0;
  for (auto& x : w1)
    if (x == W(0x08, 0x0D)) ++n;
  CHECK_EQ(n, 2);
  r.tick();
  CHECK_EQ(lastWrite(r.tick(), 0x08), 0x0A);  // T3
  r.tick();
  auto w5 = r.tick();  // T5: env 0x07, then next note key on -> 0x0D
  CHECK(fmt(w5).find("08=07") != std::string::npos);
  CHECK_EQ(lastWrite(w5, 0x08), 0x0D);
}

static std::vector<uint8_t> agasaSound() {
  std::vector<uint8_t> d;
  core::tryReadFile((t::samples() + "games/agasa/agasa"), "SOUND.DAT", d);
  return d;
}

TEST(music_sfx_agasa_11_and_10) {
  auto data = agasaSound();
  if (data.empty()) {
    std::printf("  (agasa sample missing, skipped)\n");
    return;
  }
  Rig r;
  r.d.setSoundDat(music::parseSoundDat(data));
  r.d.stop();
  r.d.requestSfx(1, 11);
  CHECK_STR(fmt(r.vsync()), std::string("07=BC 00=68 01=01 08=0E 02=7C 03=01 09=0B 06=0D"));
  CHECK_STR(fmt(r.vsync()), std::string("00=68 01=01 08=0E 02=7C 03=01 09=0B 06=0C 07=BF 08=00 09=00"));
  r.d.requestSfx(1, 10);
  CHECK_STR(fmt(r.vsync()), std::string("07=BC 00=28 01=01 08=0C 02=22 03=01 09=0C 06=0D"));
  CHECK_STR(fmt(r.vsync()), std::string("00=6E 01=01 08=0C 02=68 03=01 09=0C 06=0C"));
  CHECK_STR(fmt(r.vsync()), std::string("00=28 01=01 08=0D 02=22 03=01 09=0D 06=0A"));
  CHECK_STR(fmt(r.vsync()), std::string("00=6E 01=01 08=0D 02=68 03=01 09=0D 06=09 07=BF 07=BC"));
  CHECK_STR(fmt(r.vsync()), std::string("00=00 01=00 08=00 02=00 03=00 09=00 06=0D"));
  CHECK_STR(fmt(r.vsync()), std::string("00=00 01=00 08=00 02=00 03=00 09=00 06=0C 07=BF 07=BC"));
  CHECK_STR(fmt(r.vsync()), std::string("00=95 01=00 08=0C 02=9C 03=00 09=0C 06=0D"));
  std::string last;
  for (int i = 0; i < 5; ++i) last = fmt(r.vsync());
  CHECK_STR(last, std::string("00=95 01=00 08=0C 02=88 03=00 09=0C 06=06 07=BF 08=00 09=00"));
}

TEST(music_sfx_priority) {
  Rig r;
  std::string sd = "SOUND:@0,10,1,1\nTONE1:100,0,1,0\nVOL1:10,0,1,1\nSOUND:@20,10,1,1\nTONE1:200,0,1,0\nVOL1:12,0,1,1\n";
  r.d.setSoundDat(music::parseSoundDat(std::vector<uint8_t>(sd.begin(), sd.end())));
  r.d.requestSfx(1, 20);
  r.vsync();
  r.d.requestSfx(0, 0);  // D part cannot interrupt game sound
  CHECK_EQ(lastWrite(r.vsync(), 0x00), 200);
  for (int i = 0; i < 9; ++i) r.vsync();
  CHECK(!r.d.sfxActive());
  r.d.requestSfx(0, 0);
  r.vsync();
  r.d.requestSfx(1, 20);  // game sound interrupts D part sound
  CHECK_EQ(lastWrite(r.vsync(), 0x00), 200);
}

TEST(music_fade_carry_over) {
  Rig r;
  r.load(std::string(kVoice1) + "1:C\n");
  r.d.play();
  r.d.setFade(15);
  r.d.stop();
  r.log.clear();
  r.d.play();
  const std::string s = fmt(r.log);
  CHECK(contains(s, "4C=4A 48=5E"));
}

TEST(music_ver221_restore) {
  auto data = agasaSound();
  if (data.empty()) return;
  for (int ver = 0; ver < 2; ++ver) {
    Rig r;
    r.d.setVersion221(ver == 1);
    r.d.setSoundDat(music::parseSoundDat(data));
    r.d.stop();
    r.load("4:V12 L4 C D\n");
    r.d.play();
    r.tick();
    r.d.requestSfx(1, 11);
    r.vsync();
    std::string end = fmt(r.vsync());
    if (ver == 1)
      CHECK(contains(end, "07=BF 00=DD 01=01 08=0C 07=BE 02=DD 03=01 09=0C"));
    else
      CHECK(contains(end, "07=BF 08=00 09=00"));
    // next key on: Ver 2.00 does not write the volume
    std::vector<W> w;
    for (int t = 0; t < 16; ++t) {
      auto x = r.tick();
      w.insert(w.end(), x.begin(), x.end());
    }
    if (ver == 0) CHECK_EQ(lastWrite(w, 0x08), -1);
  }
}

TEST(music_d_part_mapping) {
  Rig r;
  std::string sd = "SOUND:@2,3,1,1\nTONE1:300,0,1,0\nVOL1:10,0,1,1\n";
  r.d.setSoundDat(music::parseSoundDat(std::vector<uint8_t>(sd.begin(), sd.end())));
  r.load("D:C4 C#4\n");
  r.d.play();
  r.tick();
  CHECK_EQ(lastWrite(r.vsync(), 0x00), 0x2C);  // 300 = 0x12C
}

TEST(music_player_requests) {
  // agasa (D1, Ver 2.00): BGM number with missing file keeps the current song.
  music::MusicPlayer p((t::samples() + "games/agasa/agasa"), 44100);
  if (!core::fileExists((t::samples() + "games/agasa/agasa"), "BGM/BGM01.MML")) return;
  CHECK(!p.version221());
  std::vector<int16_t> buf(2048);
  core::SoundRequest r;
  r.kind = core::SoundRequest::PlayNumber;
  r.value = 1;
  p.push(r);
  p.generate(buf.data(), 1024);
  CHECK(p.bgmPlaying());
  r.value = 99;  // BGM99.MML does not exist
  p.push(r);
  p.generate(buf.data(), 1024);
  CHECK(p.bgmPlaying());
  core::SoundRequest f;
  f.kind = core::SoundRequest::PlayFile;
  f.path = "DAT_BGM/NOSUCH.MML";
  p.push(f);
  p.generate(buf.data(), 1024);
  CHECK(!p.bgmPlaying());
  // kokoro (D2) is Ver 2.21
  music::MusicPlayer k((t::samples() + "games/kokoro/kokoro"), 44100);
  CHECK(k.version221());
}

TEST(music_dc_cut_silence) {
  music::MusicPlayer p("/nonexistent", 44100);
  std::vector<int16_t> buf(2 * 44100);
  p.generate(buf.data(), 44100);
  CHECK_EQ(buf[2 * 44099], 0);
}

// ---------------------------------------------------------------------------
// Additional spec-conformance checks (review of spec 02).

static music::Song compileStr(const std::string& s) { return music::compileMml(std::vector<uint8_t>(s.begin(), s.end())); }

static std::vector<uint8_t> partCode(const music::Song& s, int ch) {
  std::vector<uint8_t> v;
  for (int p = s.partStart[ch]; p < int(s.code.size()); ++p) {
    v.push_back(s.code[p]);
    if (s.code[p] == music::kOpPartEnd) break;
  }
  return v;
}

TEST(music_voice_definition_packing) {
  // §2.5: SOUND: out of range keeps the current voice; LFO depth keeps only the low 8 bits (s8);
  // FB is not masked; OP values are shifted without masking; DT2 is min(DT2, 3).
  auto s = compileStr("SOUND:@1\nLFO:1,2,900,4,8\nOP1:31,0,0,15,0,20,0,1,-3,5\nSOUND:@21\nOP2:1,2,3,4,5,6,7,8,9\n");
  CHECK_EQ(s.errorCount, 1);
  const music::Voice& v = s.voices[1];
  CHECK_EQ(v.wf, 1);
  CHECK_EQ(v.speed, 2);
  CHECK_EQ(v.depth, 0x84);
  CHECK_EQ(v.lfo[0], -15252);  // §2.6 example
  CHECK_EQ(v.lfo[9], 5952);    // A: -48 * -124
  CHECK_EQ(v.lfo[12], 0);
  CHECK_EQ(v.algfb, 0x44);
  CHECK_EQ(v.dtml[0], 0xD1);  // DT -3 -> 0xD0 | ML 1
  CHECK_EQ(v.dt2[0], 3);
  // OP2 (slot index 2) went to voice 1
  CHECK_EQ(v.ksar[2], 0xC1);
  CHECK_EQ(v.slrr[2], 0x54);
  CHECK_EQ(v.dtml[2], 0x98);
  CHECK_EQ(v.tl[2], 6);
  CHECK_EQ(v.dt2[2], 0);
  CHECK_EQ(s.voices[0].algfb, 0);
}

TEST(music_part_compile_edge_cases) {
  // §2.9/§2.10/§2.13: O9 discards the rest of the line; {256 = {0; N-300 -> 44 negative;
  // W8 -> FB 08; C& -> default length then tie; C12 -> whole note then '2' is an error.
  auto s = compileStr("1:O9 C D\n1:C{256}N-300 W8 C&\n2:C12 E\n3:C1. D\n");
  CHECK_EQ(s.errorCount, 3);
  std::vector<uint8_t> p1 = {0xFD, 0x0F, 0xF5, 0x00, 0xF4, 0xFB, 0xF1, 44, 1, 0xFB, 0x08, 0x0F, 0xF3, 0xFD, 0xFF};
  CHECK(partCode(s, 0) == p1);
  std::vector<uint8_t> p2 = {0x0B, 0xFD, 0xFF};
  CHECK(partCode(s, 1) == p2);
  std::vector<uint8_t> p3 = {0x0B, 0xFD, 0xFF};
  CHECK(partCode(s, 2) == p3);
  // empty part line makes the part valid (§2.12); missing part is invalid
  auto e = compileStr("1:\n");
  CHECK(e.partValid(0));
  CHECK(!e.partValid(1));
  // STR name keeps a trailing '$'; names longer than 12 characters cannot be referenced (§2.7)
  auto m = compileStr("STR:2$=D\nSTR:ABCDEFGHIJKLMNOP=C\n1:$2$\n2:$ABCDEFGHIJKLMNOP$\n");
  CHECK_EQ(m.errorCount, 1);
  CHECK_EQ(partCode(m, 0)[0], music::kOpCall);
  CHECK_EQ(partCode(m, 1)[0], music::kOpLineEnd);
  // `:` counts as digit value 10 inside a number (§2.3)
  auto c = compileStr("1:V12:\n");
  CHECK_EQ(partCode(c, 0)[1], 130);
}

TEST(music_ssgenv_edge_cases) {
  // §2.8: invalid character stops the line; out-of-range number drops the line and a following
  // `->` appends to the previous definition.
  auto s = compileStr("SSGENV:@1,2,15;9\nSSGENV:@0,1,2\n->7\n");
  std::vector<uint8_t> e = {0xFF, 2, 15, 7, 0xFF};
  CHECK(s.env == e);
  CHECK_EQ(s.envStart[1], 1);
}

TEST(music_start_sequence_exact) {
  // Acceptance 1, full write list of the play operation.
  Rig r;
  r.load(std::string(kVoice1) + "1:T120 O4 L8 Q7 C D\n4:V15 O4 C4\n");
  r.d.play();
  std::string want = "B0=3C 30=01 34=01 38=01 3C=01 40=14 44=28 48=1E 4C=0A 50=1F 54=1F 58=1F 5C=1F "
                     "60=00 64=00 68=00 6C=00 70=00 74=00 78=00 7C=00 80=0F 84=0F 88=0F 8C=0F 4C=1A 48=2E A4=22 A0=69";
  char b[16];
  for (int ch = 1; ch <= 2; ++ch) {
    std::snprintf(b, sizeof b, " B%X=00", ch);
    want += b;
    for (int g = 0x30; g <= 0x80; g += 0x10)
      for (int o = 0; o < 16; o += 4) {
        std::snprintf(b, sizeof b, " %02X=00", g + o + ch);
        want += b;
      }
    std::snprintf(b, sizeof b, " %02X=10", 0x4C + ch);
    want += b;
    if (ch == 1) want += " A5=22 A1=69";
  }
  want += " A6=22 A2=69 AE=22 AA=69 AC=22 A8=69 AD=22 A9=69 00=DD 01=01 02=DD 03=01 04=DD 05=01 08=00 09=00 0A=00 "
          "27=40 26=CF 27=7A";
  CHECK_STR(fmt(r.log), want);
}

TEST(music_wait_gate) {
  // §3.7.3: W neither keys off nor on; with Q<8 key off comes floor((N-1)Q/8)+1 ticks after W starts.
  Rig r;
  r.load("1:C4 Q4 W4 C4\n");
  r.d.play();
  std::vector<std::vector<W>> w(40);
  for (int t = 1; t <= 33; ++t) w[t] = r.tick();
  CHECK(hasWrite(w[1], 0x28, 0xF0));
  CHECK(w[17].empty());
  CHECK_STR(fmt(w[25]), std::string("28=00"));
  for (int t = 18; t <= 32; ++t)
    if (t != 25) CHECK(w[t].empty());
  CHECK(hasWrite(w[33], 0x28, 0xF0));
}

TEST(music_stop_writes) {
  // §3.21
  for (int ver = 0; ver < 2; ++ver) {
    Rig r;
    r.d.setVersion221(ver == 1);
    r.d.stop();
    std::string want = "28=02 28=01 28=00 07=BF 0A=00 09=00 08=00";
    if (ver == 1)
      for (int x = 0x90; x <= 0x9E; ++x) {
        char b[16];
        std::snprintf(b, sizeof b, " %02X=00", x);
        want += b;
      }
    CHECK_STR(fmt(r.log), want);
  }
}

TEST(music_sfx_fade_and_priority_only_request) {
  // Acceptance 12 / §4.5: the fade applies to priority-0 (D part) effects only.
  Rig r;
  std::string sd = "SOUND:@0,10,1,1\nVOL1:10,0,1,1\nSOUND:@20,10,1,1\nVOL1:10,0,1,1\n";
  r.d.setSoundDat(music::parseSoundDat(std::vector<uint8_t>(sd.begin(), sd.end())));
  r.d.setFade(5);
  r.d.requestSfx(0, 0);
  CHECK_EQ(lastWrite(r.vsync(), 0x08), 5);
  r.d.requestSfx(1, 20);
  CHECK_EQ(lastWrite(r.vsync(), 0x08), 10);
  // 01 §8.1: a request without a number changes only the priority.
  for (int i = 0; i < 10; ++i) r.vsync();
  CHECK(!r.d.sfxActive());
  r.d.requestSfx(0, 0);
  r.vsync();
  r.d.requestSfx(1, -1);
  CHECK_EQ(lastWrite(r.vsync(), 0x08), 10);  // the D part effect keeps playing, priority now 1 -> no fade
  r.d.requestSfx(0, 0);                     // rejected: 0 < 1 while playing
  auto w = r.vsync();
  CHECK(!hasWrite(w, 0x07, 0xBE) && !hasWrite(w, 0x07, 0xBC));
}

TEST(music_ver221_suppression) {
  // §4.8: under Ver 2.21 the music's SSG A/B writes are discarded while an effect plays; SSG C is not.
  auto data = agasaSound();
  if (data.empty()) return;
  for (int ver = 0; ver < 2; ++ver) {
    Rig r;
    r.d.setVersion221(ver == 1);
    r.d.setSoundDat(music::parseSoundDat(data));
    r.d.stop();
    r.load("4:V12 L16 C\n6:V12 L16 C\n");
    r.d.play();
    r.d.requestSfx(1, 11);
    r.vsync();
    auto w = r.tick();
    const bool ssgA = lastWrite(w, 0x00) >= 0 || lastWrite(w, 0x08) >= 0;
    CHECK_EQ(ssgA, ver == 0);
    CHECK_EQ(lastWrite(w, 0x04), 0xDD);
    CHECK_EQ(lastWrite(w, 0x0A), 0x0C);
    if (ver == 1) CHECK_STR(fmt(w), std::string("0A=0C 07=BC 07=B8 04=DD 05=01"));
  }
}

#include <dirent.h>
#include <sys/stat.h>

static void collectMml(const std::string& dir, std::vector<std::string>& out) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  while (dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n == "." || n == "..") continue;
    std::string p = dir + "/" + n;
    struct stat st;
    if (lstat(p.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      collectMml(p, out);
    } else if (S_ISREG(st.st_mode) && n.size() > 4) {
      std::string ext = n.substr(n.size() - 4);
      for (auto& ch : ext) ch = music::upper(ch);
      if (ext == ".MML") out.push_back(p);
    }
  }
  closedir(d);
}

TEST(music_compile_all_samples) {
  // Acceptance 14: every sample *.MML compiles without errors.
  std::vector<std::string> files;
  collectMml(t::samples(), files);
  if (files.empty()) {
    std::printf("  (samples missing, skipped)\n");
    return;
  }
  int bad = 0;
  for (auto& f : files) {
    std::vector<uint8_t> text;
    FILE* fp = std::fopen(f.c_str(), "rb");
    if (!fp) continue;
    int c;
    while ((c = std::fgetc(fp)) != EOF) text.push_back(uint8_t(c));
    std::fclose(fp);
    auto s = music::compileMml(text);
    if (s.errorCount) {
      if (bad < 5) std::fprintf(stderr, "  %s: %s\n", f.c_str(), s.errors.empty() ? "" : s.errors[0].c_str());
      ++bad;
    }
  }
  CHECK(files.size() > 900);
  CHECK_EQ(bad, 0);
}

// ---- spec v1.1 acceptance items 18-21 ----

TEST(music_v11_18_fade_before_first_play) {
  auto data = agasaSound();
  for (int ver = 0; ver < 2; ++ver) {
    Rig r;
    r.d.setVersion221(ver == 1);
    if (!data.empty()) r.d.setSoundDat(music::parseSoundDat(data));
    r.d.stop();
    // Uninitialised: fades write nothing, with or without an effect playing.
    r.log.clear();
    for (int f = 0; f <= 15; ++f) r.d.setFade(f);
    CHECK(r.log.empty());
    CHECK_EQ(r.d.fade(), 15);
    if (!data.empty()) {
      r.d.requestSfx(1, 15);
      r.vsync();
      r.log.clear();
      for (int f = 0; f <= 15; ++f) r.d.setFade(f);
      CHECK(r.log.empty());
    }
    // Loading without playing does not initialise.
    r.load(std::string(kVoice1) + "1:C\n4:V12 C\n");
    r.log.clear();
    r.d.setFade(3);
    CHECK(r.log.empty());
    // After one play + stop, every fade writes FM carrier TL and 8, 9, 10 (also during an effect).
    r.d.play();
    r.d.stop();
    if (!data.empty()) {
      r.d.requestSfx(1, 15);
      r.vsync();
    }
    r.log.clear();
    r.d.setFade(5);
    CHECK(hasWrite(r.log, 0x4C, uint8_t(std::min(64 - 7 * 4 + 10, 127))));  // V12-5=7 -> base 36, OP4 TL 10
    CHECK(lastWrite(r.log, 0x08) >= 0 && lastWrite(r.log, 0x09) >= 0 && lastWrite(r.log, 0x0A) >= 0);
  }
}

TEST(music_v11_19_ssg_pitch) {
  // Render SSG period 477 (O4 C) and 283 (O4 A) through the real player and measure zero crossings.
  const std::string dir = t::tmp() + "music_pitch_test";
  std::string bgm = std::string(dir) + "/BGM";
  std::string cmd = std::string("mkdir -p ") + bgm;
  if (std::system(cmd.c_str()) != 0) return;
  const char* songs[2] = {"4:T120 V15 O4 Q8 L1 {C}\n", "4:T120 V15 O4 Q8 L1 {A}\n"};
  const double expect[2] = {261.6, 441.0};
  for (int k = 0; k < 2; ++k) {
    std::string p = bgm + "/BGM0" + std::to_string(k) + ".MML";
    core::writeFileAbs(p, std::vector<uint8_t>(songs[k], songs[k] + std::strlen(songs[k])));
    music::MusicPlayer pl(dir, 44100);
    core::SoundRequest r;
    r.kind = core::SoundRequest::PlayNumber;
    r.value = k;
    pl.push(r);
    std::vector<int16_t> buf(2 * 44100);
    pl.generate(buf.data(), 44100);  // settle
    pl.generate(buf.data(), 44100);
    int cross = 0;
    for (int i = 1; i < 44100; ++i)
      if ((buf[2 * i] >= 0) != (buf[2 * i - 2] >= 0)) ++cross;
    const double hz = cross / 2.0;
    CHECK(hz > expect[k] - 3 && hz < expect[k] + 3);
  }
}

TEST(music_v11_20_ver221_restore_hw_envelope) {
  auto data = agasaSound();
  if (data.empty()) return;
  Rig r;
  r.d.setVersion221(true);
  r.d.setSoundDat(music::parseSoundDat(data));
  r.d.stop();
  r.load("4:S8 M500 L1 C D\n");
  r.d.play();
  r.tick();
  r.d.requestSfx(1, 11);
  r.vsync();
  const std::string end = fmt(r.vsync());
  CHECK(contains(end, "07=BF 00=DD 01=01 08=0C 07=BE 02=DD 03=01 09=0C"));
  CHECK(!contains(end, "08=10") && !contains(end, "0D="));
  std::vector<W> w;
  for (int t = 0; t < 70; ++t) {
    auto x = r.tick();
    w.insert(w.end(), x.begin(), x.end());
  }
  CHECK(!hasWrite(w, 0x08, 0x10));
  CHECK_EQ(lastWrite(w, 0x0D), -1);
}

TEST(music_v11_21_number_sign) {
  {
    Rig r;
    r.load("1:N40000 A\n");
    r.d.play();
    auto w = r.tick();
    CHECK_EQ(lastWrite(w, 0xA4), 0x23);
    CHECK_EQ(lastWrite(w, 0xA0), 0xE4);
  }
  {
    Rig r;
    r.load("1:N-40000 A\n");
    r.d.play();
    auto w = r.tick();
    CHECK_EQ(lastWrite(w, 0xA4), 0x24);
    CHECK_EQ(lastWrite(w, 0xA0), 0x3E);
  }
  // N32768 -> magnitude 0, negative flag 1; O40000 -> range error
  std::string t = "1:N32768 O40000 C\n";
  auto s = music::compileMml(std::vector<uint8_t>(t.begin(), t.end()));
  const uint8_t* p = &s.code[s.partStart[0]];
  CHECK(p[0] == music::kOpExt && p[1] == 0xF1 && p[2] == 0 && p[3] == 1);
  CHECK_EQ(s.errorCount, 1);
  CHECK_EQ(p[4], music::kOpLineEnd);
  // §2.8 v1.1: stop at an invalid character without adding a value
  std::string e = "SSGENV:@1,2,15,12;x\n";
  auto s2 = music::compileMml(std::vector<uint8_t>(e.begin(), e.end()));
  std::vector<uint8_t> want = {0xFF, 2, 15, 12, 0xFF};
  CHECK(s2.env == want);
}
