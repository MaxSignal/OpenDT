// Acceptance checks from spec 02 (music driver).
#include <cstring>
#include <string>
#include <vector>

#include "core/util.h"
#include "music/driver.h"
#include "music/mml.h"
#include "music/player.h"
#include "music/sounddat.h"
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
  core::tryReadFile("/home/claude/samples/games/agasa/agasa", "SOUND.DAT", d);
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
  music::MusicPlayer p("/home/claude/samples/games/agasa/agasa", 44100);
  if (!core::fileExists("/home/claude/samples/games/agasa/agasa", "BGM/BGM01.MML")) return;
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
  music::MusicPlayer k("/home/claude/samples/games/kokoro/kokoro", 44100);
  CHECK(k.version221());
}

TEST(music_dc_cut_silence) {
  music::MusicPlayer p("/nonexistent", 44100);
  std::vector<int16_t> buf(2 * 44100);
  p.generate(buf.data(), 44100);
  CHECK_EQ(buf[2 * 44099], 0);
}
