// Game detection, folder scan and the game browser screen (spec 01 §7).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/host.h"

namespace app {

constexpr const char* kAppName = "OpenDT";

core::EngineKind detectGame(const std::string& dir);   // §7.1 (directory listing check)
core::EngineKind engineForRun(const std::string& dir); // D2 if DAT_RPG/PLAYER.DAT && DAT_MAP dir, else D1

struct GameEntry {
  std::string root;
  std::string displayName;  // Shift-JIS
  std::string location;     // Shift-JIS
  core::EngineKind kind = core::EngineKind::None;
};

std::vector<GameEntry> scanGames(const std::string& start, int maxDepth = 6);

// 24-bit RGB canvas helpers for the browser.
struct RgbCanvas {
  std::vector<uint8_t> rgb = std::vector<uint8_t>(640 * 400 * 3, 0);
  void fill(int x, int y, int w, int h, uint32_t color);
  // Draws SJIS text (no bold, transparent). Returns final x.
  int text(int x, int y, const std::string& sjis, uint32_t color, int rightLimit = 640, bool truncate = true);
};

class Browser {
 public:
  Browser(std::string startDir, std::vector<GameEntry> games);
  enum class Event { None, Up, Down, Left, Right, PageUp, PageDown, Home, End, WheelUp, WheelDown, Ok, Cancel, Quit,
                     Shot, Click };
  // Returns index of game to start or -1. quit set on cancel/quit.
  int handle(Event ev, int clickX = 0, int clickY = 0, int clicks = 1, bool* quit = nullptr);
  void render(RgbCanvas& c);
  void setMessage(const std::string& sjis) { message_ = sjis; }
  int selected() const { return sel_; }
  const std::vector<GameEntry>& games() const { return games_; }

 private:
  void select(int s);
  struct Preview {
    bool has = false;
    std::vector<uint8_t> rgb;  // 320x200
    std::string info;
  };
  Preview& preview(int idx);

  std::string start_;
  std::vector<GameEntry> games_;
  int sel_ = 0, top_ = 0;
  std::string message_;
  std::map<int, Preview> previews_;
};

}  // namespace app
