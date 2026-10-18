// Engine <-> host boundary (spec 01 §10.1).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/screen.h"

namespace core {

// The 8 input booleans the engines see (spec 01 §4.2).
struct InputState {
  bool up = false, down = false, left = false, right = false;
  bool ok = false, cancel = false, fast = false, debug = false;
  // Optional extension (01 §4.3, v1.1): PC-98 ROLL UP / ROLL DOWN, used only by the D2 debug
  // menu's variable editor.
  bool rollUp = false, rollDown = false;
};

// Sound requests (spec 01 §8.1).
struct SoundRequest {
  enum Kind { PlayNumber, PlayFile, Stop, Fade, Sfx } kind = Stop;
  int value = 0;      // BGM number / fade level / SFX number (0-based)
  std::string path;   // PlayFile: path relative to game root
};

// Thrown from Host::vsync when the host asks the game to terminate.
struct QuitGame {};

// Debug/test start options (spec 01 §9.2).
struct StartOptions {
  bool noTitle = false;
  bool noQuit = false;
  bool verbose = false;
  bool warp = false;
  int warpMap = 0, warpX = 0, warpY = 0;
  int battle = -1;
  long gold = -1;
  std::vector<int> items;
  uint32_t seed = 1;
};

class Host {
 public:
  virtual ~Host() = default;
  // Present the current screen and wait until the next VSYNC. May throw QuitGame.
  virtual void vsync(const Screen& scr) = 0;
  // Latest input state.
  virtual InputState input() = 0;
  // Queue a sound request.
  virtual void sound(const SoundRequest& req) = 0;
  // Verbose log line (only printed with -v).
  virtual void log(const std::string& line) { (void)line; }
  // True while fast-forward (Tab) is held; engines normally don't need this.
  virtual bool fastForward() { return false; }
};

// Interface implemented by D1 and D2 engines.
class Engine {
 public:
  virtual ~Engine() = default;
  // Runs the game until it ends by itself (D2 quit) or QuitGame is thrown.
  virtual void run() = 0;
};

enum class EngineKind { None, D1, D2 };

}  // namespace core
