// Music player: MUSIC.COM compatible driver + YM2203 emulation + output post-processing
// (spec 01 §8, spec 02).
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/host.h"

namespace music {

class Driver;   // MUSIC.COM compatible driver (spec 02)
class OpnChip;  // YM2203 wrapper

class MusicPlayer {
 public:
  // Creates chip and driver, loads SOUND.DAT and checks MUSIC.COM version (spec 01 §8.1).
  explicit MusicPlayer(const std::string& gameRoot, int sampleRate = 44100);
  ~MusicPlayer();

  // Thread-safe enqueue of a request (applied at the start of the next generate()).
  void push(const core::SoundRequest& req);
  // Produces `frames` stereo s16 sample frames (interleaved L,R).
  void generate(int16_t* stereo, int frames);

  void setVerbose(bool v) { verbose_ = v; }
  // Collected log lines (verbose) since last call; thread-safe.
  std::vector<std::string> takeLog();

 private:
  void apply(const core::SoundRequest& req);

  std::string root_;
  int rate_;
  bool verbose_ = false;
  std::mutex mu_;
  std::vector<core::SoundRequest> queue_;
  std::vector<std::string> log_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace music
