// MusicPlayer: drives the MUSIC.COM compatible driver with the ymfm YM2203 model and converts the
// chip output to 44.1 kHz stereo (spec 01 §8, spec 02 §1.4, §5, §6).
#include "music/player.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/util.h"
#include "music/driver.h"
#include "music/mml.h"
#include "music/sounddat.h"
#include "ymfm_opn.h"

namespace music {

namespace {
constexpr double kChipClock = 3993600.0;
constexpr double kChipRate = kChipClock / 24.0;  // 166,400 Hz (OPN_FIDELITY_MIN)
constexpr double kVsyncHz = 56.4;

class Intf : public ymfm::ymfm_interface {};
}  // namespace

struct MusicPlayer::Impl {
  Intf intf;
  ymfm::ym2203 chip{intf};
  std::unique_ptr<Driver> driver;
  double vsyncLeft = 0;
  double timerLeft = 0;
  double pos = 0;
  double lastX = 0, prevX = 0, prevY = 0;

  Impl() {
    chip.set_fidelity(ymfm::OPN_FIDELITY_MIN);
    chip.reset();
    chip.write_address(0x2D);  // prescaler 1/6 (address write only)
    driver = std::make_unique<Driver>([this](uint8_t reg, uint8_t val) {
      if (reg >= 0x2D && reg <= 0x2F) {
        chip.write_address(reg);  // special registers: address write only
        return;
      }
      chip.write_address(reg);
      chip.write_data(val);
    });
  }

  int chipSample() {
    vsyncLeft -= 1;
    if (vsyncLeft <= 0) {
      vsyncLeft += kChipRate / kVsyncHz;
      driver->vsync();
    }
    timerLeft -= 1;
    if (timerLeft <= 0) {
      timerLeft += driver->timerQ() * 48.0;
      driver->timerB();
    }
    ymfm::ym2203::output_data out;
    chip.generate(&out, 1);
    return out.data[0] + (out.data[1] + out.data[2] + out.data[3]) / 2;
  }

  void restartTimer() { timerLeft = driver->timerQ() * 48.0; }
};

MusicPlayer::MusicPlayer(const std::string& gameRoot, int sampleRate)
    : root_(gameRoot), rate_(sampleRate), impl_(std::make_unique<Impl>()) {
  impl_->driver->stop();
  std::vector<uint8_t> data;
  if (core::tryReadFile(root_, "SOUND.DAT", data)) impl_->driver->setSoundDat(parseSoundDat(data));
  data.clear();
  if (core::tryReadFile(root_, "MUSIC.COM", data)) {
    static const char kVer[] = "Ver 2.21";
    const size_t n = sizeof(kVer) - 1;
    for (size_t i = 0; i + n <= data.size(); ++i)
      if (std::memcmp(data.data() + i, kVer, n) == 0) {
        impl_->driver->setVersion221(true);
        break;
      }
  }
}

MusicPlayer::~MusicPlayer() = default;

void MusicPlayer::push(const core::SoundRequest& req) {
  std::lock_guard<std::mutex> lk(mu_);
  queue_.push_back(req);
}

bool MusicPlayer::version221() const { return impl_->driver->version221(); }
bool MusicPlayer::bgmPlaying() const { return impl_->driver->playing(); }

void MusicPlayer::apply(const core::SoundRequest& req) {
  Driver& d = *impl_->driver;
  auto logLine = [&](const std::string& s) {
    if (!verbose_) return;
    std::lock_guard<std::mutex> lk(mu_);
    log_.push_back(s);
  };
  auto loadAndPlay = [&](const std::string& rel) -> bool {
    std::vector<uint8_t> text;
    if (!core::tryReadFile(root_, rel, text)) {
      logLine("missing MML: " + rel);
      return false;
    }
    Song s = compileMml(text);
    if (verbose_)
      for (auto& e : s.errors) logLine("MML error " + rel + " " + e);
    d.load(std::move(s));
    d.play();
    impl_->restartTimer();
    return true;
  };
  switch (req.kind) {
    case core::SoundRequest::PlayNumber: {
      char name[32];
      std::snprintf(name, sizeof(name), "BGM/BGM%02d.MML", req.value & 0xFF);
      loadAndPlay(name);
      break;
    }
    case core::SoundRequest::PlayFile:
      d.stop();
      if (!req.path.empty()) loadAndPlay(req.path);
      break;
    case core::SoundRequest::Stop:
      d.stop();
      break;
    case core::SoundRequest::Fade:
      d.setFade(req.value);
      break;
    case core::SoundRequest::Sfx:
      d.requestSfx(1, req.value);
      break;
  }
}

void MusicPlayer::generate(int16_t* stereo, int frames) {
  std::vector<core::SoundRequest> q;
  {
    std::lock_guard<std::mutex> lk(mu_);
    q.swap(queue_);
  }
  for (auto& r : q) apply(r);
  Impl& m = *impl_;
  const double step = kChipRate / double(rate_);
  for (int i = 0; i < frames; ++i) {
    m.pos += step;
    double sum = 0;
    int cnt = 0;
    while (m.pos >= 1.0) {
      sum += m.chipSample();
      ++cnt;
      m.pos -= 1.0;
    }
    const double x = cnt ? sum / cnt : m.lastX;
    m.lastX = x;
    const double y = x - m.prevX + 0.9985 * m.prevY;
    m.prevX = x;
    m.prevY = y;
    double t = std::trunc(y);
    if (t > 32767) t = 32767;
    if (t < -32768) t = -32768;
    stereo[2 * i] = stereo[2 * i + 1] = int16_t(t);
  }
}

std::vector<std::string> MusicPlayer::takeLog() {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<std::string> r;
  r.swap(log_);
  return r;
}

}  // namespace music
