// TEMPORARY stub: silent music player. Replaced by the real driver implementation (spec 02).
#include <cstring>

#include "music/player.h"

namespace music {

struct MusicPlayer::Impl {};

MusicPlayer::MusicPlayer(const std::string& gameRoot, int sampleRate) : root_(gameRoot), rate_(sampleRate) {}
MusicPlayer::~MusicPlayer() = default;

void MusicPlayer::push(const core::SoundRequest& req) {
  std::lock_guard<std::mutex> lk(mu_);
  queue_.push_back(req);
}

void MusicPlayer::apply(const core::SoundRequest&) {}

void MusicPlayer::generate(int16_t* stereo, int frames) {
  std::vector<core::SoundRequest> q;
  {
    std::lock_guard<std::mutex> lk(mu_);
    q.swap(queue_);
  }
  for (auto& r : q) apply(r);
  std::memset(stereo, 0, sizeof(int16_t) * 2 * frames);
}

std::vector<std::string> MusicPlayer::takeLog() {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<std::string> r;
  r.swap(log_);
  return r;
}

}  // namespace music
