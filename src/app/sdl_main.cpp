// SDL2 desktop frontend: window, browser, sessions (spec 01 §7.4-7.6, §8.2, §10).
#include <SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>

#include "app/games.h"
#include "core/host.h"
#include "core/text.h"
#include "core/util.h"
#include "d1/d1.h"
#include "d2/d2.h"
#include "music/player.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Video {
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  SDL_Texture* tex = nullptr;
  bool fullscreen = false;
  std::vector<uint8_t> last = std::vector<uint8_t>(640 * 400 * 3, 0);
  int shotNo = 0;

  bool init(int scale, bool fs) {
    win = SDL_CreateWindow(app::kAppName, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640 * scale, 400 * scale,
                           SDL_WINDOW_RESIZABLE | (fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) return false;
    fullscreen = fs;
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) return false;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetLogicalSize(ren, 640, 400);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 640, 400);
    return tex != nullptr;
  }
  void present(const uint8_t* rgb) {
    std::memcpy(last.data(), rgb, last.size());
    SDL_UpdateTexture(tex, nullptr, rgb, 640 * 3);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
  }
  void toggleFullscreen() {
    fullscreen = !fullscreen;
    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  }
  void screenshot() {
    char name[64];
    std::snprintf(name, sizeof(name), "opendt_%03d.png", shotNo++);
    core::writePng(name, last.data(), 640, 400);
  }
};

// Session host: the game thread calls vsync(); the UI thread polls events.
class SdlHost : public core::Host {
 public:
  std::mutex mu;
  std::vector<uint8_t> frame = std::vector<uint8_t>(640 * 400 * 3, 0);
  bool newFrame = false;
  core::InputState in;
  bool ff = false;
  std::atomic<bool> quit{false};
  music::MusicPlayer* player = nullptr;
  Clock::time_point next = Clock::now();
  bool verbose = false;

  void vsync(const core::Screen& scr) override {
    std::vector<uint8_t> rgb(640 * 400 * 3);
    scr.compose(rgb.data());
    {
      std::lock_guard<std::mutex> lk(mu);
      frame.swap(rgb);
      newFrame = true;
    }
    if (quit) throw core::QuitGame();
    const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 56.4));
    next += period;
    const auto now = Clock::now();
    bool fast;
    {
      std::lock_guard<std::mutex> lk(mu);
      fast = ff;
    }
    if (fast) {
      next = now;
    } else if (next > now) {
      std::this_thread::sleep_until(next);
    } else if (now - next > period * 8) {
      next = now;
    }
    if (quit) throw core::QuitGame();
  }
  core::InputState input() override {
    std::lock_guard<std::mutex> lk(mu);
    return in;
  }
  bool fastForward() override {
    std::lock_guard<std::mutex> lk(mu);
    return ff;
  }
  void sound(const core::SoundRequest& r) override {
    if (player) player->push(r);
  }
  void log(const std::string& line) override {
    if (verbose) std::fprintf(stderr, "%s\n", line.c_str());
  }
};

music::MusicPlayer* gAudioPlayer = nullptr;
std::mutex gAudioMu;

void audioCallback(void*, Uint8* stream, int len) {
  std::lock_guard<std::mutex> lk(gAudioMu);
  const int frames = len / 4;
  if (gAudioPlayer)
    gAudioPlayer->generate(reinterpret_cast<int16_t*>(stream), frames);
  else
    std::memset(stream, 0, size_t(len));
}

core::InputState readKeys(bool* ff) {
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  core::InputState s;
  s.up = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_KP_8];
  s.down = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_KP_2];
  s.left = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_KP_4];
  s.right = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_KP_6];
  s.ok = k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER] || k[SDL_SCANCODE_SPACE];
  s.cancel = k[SDL_SCANCODE_X] || k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_BACKSPACE];
  s.fast = k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
  s.debug = k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL];
  s.rollUp = k[SDL_SCANCODE_PAGEUP];      // ROLL UP (01 §4.3 extension)
  s.rollDown = k[SDL_SCANCODE_PAGEDOWN];  // ROLL DOWN
  if (k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT]) s.ok = false;
  *ff = k[SDL_SCANCODE_TAB];
  return s;
}

enum class SessionResult { Closed, Failed, Ended };

SessionResult runSession(Video& v, const std::string& root, const core::StartOptions& opt, bool mute,
                         std::string& err) {
  SdlHost host;
  host.verbose = opt.verbose;
  std::unique_ptr<core::Engine> eng;
  std::unique_ptr<music::MusicPlayer> player;
  try {
    player = std::make_unique<music::MusicPlayer>(root, 44100);
    player->setVerbose(opt.verbose);
    host.player = player.get();
    if (app::engineForRun(root) == core::EngineKind::D2)
      eng = d2::createEngine(host, root, opt);
    else
      eng = d1::createEngine(host, root, opt);
  } catch (std::exception& e) {
    err = core::utf8ToSjis(e.what());
    return SessionResult::Failed;
  }
  SDL_AudioDeviceID dev = 0;
  if (!mute) {
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audioCallback;
    {
      std::lock_guard<std::mutex> lk(gAudioMu);
      gAudioPlayer = player.get();
    }
    dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);
  }
  std::atomic<bool> done{false};
  std::thread game([&] {
    try {
      eng->run();
    } catch (core::QuitGame&) {
    } catch (std::exception& e) {
      std::fprintf(stderr, "game error: %s\n", e.what());
    }
    done = true;
  });
  bool closed = false;
  std::vector<uint8_t> buf;
  while (!done && !closed) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) closed = true;
      if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_F12) v.screenshot();
        if (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) v.toggleFullscreen();
      }
    }
    bool ff;
    core::InputState in = readKeys(&ff);
    bool have = false;
    {
      std::lock_guard<std::mutex> lk(host.mu);
      host.in = in;
      host.ff = ff;
      if (host.newFrame) {
        buf = host.frame;
        host.newFrame = false;
        have = true;
      }
    }
    if (have) v.present(buf.data());
    SDL_Delay(1);
  }
  host.quit = true;
  game.join();
  if (dev) SDL_CloseAudioDevice(dev);
  {
    std::lock_guard<std::mutex> lk(gAudioMu);
    gAudioPlayer = nullptr;
  }
  return closed ? SessionResult::Closed : SessionResult::Ended;
}

void usage() {
  std::fprintf(stderr, "usage: opendt [GAMEDIR | FOLDER] [--no-title] [--scale N] [--fullscreen] [--mute] [-v]\n");
}

// Directory of the executable (UTF-8), used when the current directory is unusable (spec 01 §7.5).
std::string exeDir() {
  std::string r;
  if (char* b = SDL_GetBasePath()) {
    r = b;
    SDL_free(b);
  }
  return r.empty() ? std::string(".") : r;
}

// Default folder: the current directory, unless it is empty or a filesystem root (e.g. when started from a
// desktop shortcut or a macOS app bundle); then the executable's directory.
std::string defaultFolder() {
  const std::string cwd = core::currentDir();
  if (!cwd.empty()) {
    const std::filesystem::path p = std::filesystem::u8path(cwd);
    if (p != p.root_path()) return cwd;
  }
  return exeDir();
}

}  // namespace

int main(int argc, char** argv) {
  std::string folder;
  int scale = 2;
  bool fs = false, mute = false;
  core::StartOptions opt;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--no-title")
      opt.noTitle = true;
    else if (a == "--scale" && i + 1 < argc)
      scale = std::max(1, std::atoi(argv[++i]));
    else if (a == "--fullscreen")
      fs = true;
    else if (a == "--mute")
      mute = true;
    else if (a == "-v")
      opt.verbose = true;
    else if (a == "-h" || a == "--help") {
      usage();
      return 0;
    } else if (!a.empty() && a[0] == '-') {
      usage();
      return 2;
    } else
      folder = a;
  }
  if (folder.empty()) folder = defaultFolder();
  if (!core::isDirectory(folder)) {
    std::fprintf(stderr, "error: %s is not a directory\n", folder.c_str());
    return 1;
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  Video v;
  if (!v.init(scale, fs)) {
    std::fprintf(stderr, "video init failed: %s\n", SDL_GetError());
    return 1;
  }
  int code = 0;
  if (app::detectGame(folder) != core::EngineKind::None) {
    std::string err;
    auto r = runSession(v, folder, opt, mute, err);
    if (r == SessionResult::Failed) {
      std::fprintf(stderr, "error: %s\n", core::sjisToUtf8(err).c_str());
      code = 1;
    }
  } else {
    app::Browser browser(folder, app::scanGames(folder));
    app::RgbCanvas canvas;
    bool running = true;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
    }
    while (running) {
      SDL_Event e2;
      int start = -1;
      while (SDL_PollEvent(&e2)) {
        using E = app::Browser::Event;
        E ev = E::None;
        int cx = 0, cy = 0, clicks = 1;
        if (e2.type == SDL_QUIT) ev = E::Quit;
        if (e2.type == SDL_KEYDOWN) {
          const bool rep = e2.key.repeat != 0;
          switch (e2.key.keysym.sym) {
            case SDLK_UP: case SDLK_KP_8: ev = E::Up; break;
            case SDLK_DOWN: case SDLK_KP_2: ev = E::Down; break;
            case SDLK_LEFT: case SDLK_KP_4: ev = E::Left; break;
            case SDLK_RIGHT: case SDLK_KP_6: ev = E::Right; break;
            case SDLK_PAGEUP: ev = E::PageUp; break;
            case SDLK_PAGEDOWN: ev = E::PageDown; break;
            case SDLK_HOME: ev = E::Home; break;
            case SDLK_END: ev = E::End; break;
            case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_z: case SDLK_SPACE:
              if (e2.key.keysym.sym == SDLK_RETURN && (e2.key.keysym.mod & KMOD_ALT)) {
                if (!rep) v.toggleFullscreen();
              } else if (!rep) {
                ev = E::Ok;
              }
              break;
            case SDLK_ESCAPE: case SDLK_x: if (!rep) ev = E::Cancel; break;
            case SDLK_F12: if (!rep) ev = E::Shot; break;
            default: break;
          }
        }
        if (e2.type == SDL_MOUSEBUTTONDOWN && e2.button.button == SDL_BUTTON_LEFT) {
          ev = E::Click;
          cx = e2.button.x;
          cy = e2.button.y;
          clicks = e2.button.clicks;
        }
        if (e2.type == SDL_MOUSEWHEEL) ev = e2.wheel.y > 0 ? E::WheelUp : E::WheelDown;
        if (ev == E::Shot) {
          v.screenshot();
          continue;
        }
        if (ev == E::None) continue;
        bool q = false;
        int s = browser.handle(ev, cx, cy, clicks, &q);
        if (q) {
          running = false;
          break;
        }
        if (s >= 0) start = s;
      }
      if (!running) break;
      if (start >= 0) {
        std::string err;
        auto r = runSession(v, browser.games()[start].root, opt, mute, err);
        if (r == SessionResult::Closed) break;
        browser.setMessage(r == SessionResult::Failed ? "Error: " + err : std::string());
        while (SDL_PollEvent(&e)) {
        }
      }
      browser.render(canvas);
      v.present(canvas.rgb.data());
      SDL_Delay(1);
    }
  }
  SDL_Quit();
  return code;
}
