// MUSIC.COM compatible driver: sequencer (spec 02 §3) and effect sounds (spec 02 §4).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "music/mml.h"
#include "music/sounddat.h"

namespace music {

class Driver {
 public:
  using WriteFn = std::function<void(uint8_t reg, uint8_t val)>;
  explicit Driver(WriteFn w);

  void setVersion221(bool v) { ver221_ = v; }
  bool version221() const { return ver221_; }
  void setSoundDat(SoundDat sd) { sfx_ = std::move(sd); }

  // Replace the compiled song (tempo takes effect immediately).
  void load(Song song);
  void play();   // spec 02 §3.3
  void stop();   // spec 02 §3.21
  void setFade(int level);           // spec 02 §3.22
  void requestSfx(int prio, int n);  // spec 02 §4.4; n < 0 -> no sound (priority only)

  void vsync();   // spec 02 §4.5
  void timerB();  // spec 02 §1.2
  void tick();    // one sequencer tick (spec 02 §3.4), exposed for tests

  bool playing() const { return playing_; }
  int timerQ() const { return tempoToQ(song_.tempo); }
  // Timer-B period in seconds (spec 02 §1.3).
  double timerPeriodSec() const { return timerQ() * 1152.0 / 3993600.0; }
  int fade() const { return fade_; }
  bool sfxActive() const { return sfxRemain_ != 0; }
  uint8_t shadow(int reg) const { return shadow_[reg & 0xFF]; }
  const Song& song() const { return song_; }

 private:
  struct Chan {
    // reset at song start
    // Driver start-up values are 0 (spec 02 §3.2 "ドライバ起動時の初期値はいずれも 0"); the song-start
    // values are set by initChannel(). This matters before the first song: a fade request writes the
    // SSG volume registers from these states (spec 02 §3.22), e.g. D1 fading out while no BGM ever played.
    int start = -1, pos = -1;
    int defLen = 0;
    int remain = 0;
    int octave = 0;
    int note = 0;
    bool tie = false;
    uint8_t vol = 0;
    int envVal = 0;
    bool envOn = false;
    std::vector<int> callStack;
    std::vector<std::pair<int, int>> loopStack;  // body position, remaining count
    uint16_t gate = 0;
    uint8_t q = 0;
    uint8_t detMag = 0;
    bool detNeg = false;
    uint8_t porta = 0;
    uint8_t portaRemain = 0;
    int prevOct = 0, prevNote = 0;
    uint8_t tremDepth = 0, vibDepth = 0;
    bool keyState = false;
    // FM voice / soft LFO
    int voice = 0;
    bool voiceLoaded = false;
    int alg = 0;
    int16_t lfoCur = 0;
    uint8_t lfoDepth = 0, lfoSpeed = 0, lfoWave = 0;
    int lfoShift = 0;
    // persistent across songs
    int lfoAcc = 0;
    int lfoStep = 0;
    bool hwOn = false;
    uint8_t hwShape = 0;
    int envRate = 0, envPos = 0, envCounter = 0, envVolStart = 0;
    int tremCur = 0, tremPeriod = 1, tremDelay = 0, tremWait = 0, tremCount = 0;
    int vibPeriod = 1, vibDelay = 0, vibWait = 0, vibCount = 0;
    bool vibUp = true;  // PROVISIONAL(Q2): start-up direction of vibrato is unspecified
    int portaCur = 0, portaInc = 0, portaBlock = 0;
  };

  struct SfxState {
    int cur = 0;
    int inc = 0;
    int counter = 0;
    int reload = 0;
    int start = 0;
    int mode = 0;
  };

  // register access
  void write(uint8_t reg, uint8_t val);   // always written
  void writeM(uint8_t reg, uint8_t val);  // music SSG write (suppressible under Ver 2.21)

  uint8_t codeAt(int p) const {
    return (p >= 0 && p < int(song_.code.size())) ? song_.code[p] : uint8_t(kOpPartEnd);
  }

  void initChannel(int ch);
  void loadVoice(int ch, int n);
  void volumeUpdate(int ch);
  void freqWrite(int ch);
  int fmOffset(const Chan& c, int note) const;
  int ssgOffset(const Chan& c, int note) const;
  int fmDetune(const Chan& c, int note) const;
  int ssgDetune(const Chan& c, int note) const;
  void keyOn(int ch);
  void keyOff(int ch);
  void channelTick(int ch);
  void execute(int ch);
  void doNote(int ch, int semi, int code);
  void gatePrep(Chan& c, int nextPos);
  void portaPrep(int ch);
  void lfoAdvance(int ch);

  void sfxStart(int idx);
  void sfxEnd();
  int sfxUpdate(SfxState& s, int maxv);

  WriteFn w_;
  bool ver221_ = false;
  Song song_;
  SoundDat sfx_;
  Chan ch_[7];
  bool lineEnd_[7] = {};
  bool playing_ = false;
  bool phase_ = false;
  int fade_ = 0;
  uint8_t shadow_[256] = {};
  bool suppress_ = false;

  // effect sound state
  int sfxPrio_ = 0;
  int sfxPending_ = -1;
  int sfxRec_ = -1;
  int sfxRemain_ = 0;
  SfxState sTone_[2], sVol_[2], sNoise_;
};

}  // namespace music
