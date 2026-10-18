// Headless test runner (spec 01 §9.2).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "app/games.h"
#include "core/host.h"
#include "core/text.h"
#include "core/util.h"
#include "d1/d1.h"
#include "d2/d2.h"
#include "music/player.h"

namespace {

struct Step {
  enum Kind { Wait, Keys, Shot, Quit } kind;
  int frames = 0;
  core::InputState in;
  std::string name;
};

std::vector<Step> parseScript(const std::string& text) {
  std::vector<Step> steps;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream ws(line);
    std::string w;
    while (ws >> w) {
      if (w[0] == '#') break;
      Step s;
      if (w[0] >= '0' && w[0] <= '9') {
        s.kind = Step::Wait;
        s.frames = std::atoi(w.c_str());
        steps.push_back(s);
      } else if (w.compare(0, 5, "shot:") == 0) {
        s.kind = Step::Shot;
        s.name = w.substr(5);
        steps.push_back(s);
      } else if (w == "quit") {
        s.kind = Step::Quit;
        steps.push_back(s);
      } else {
        s.kind = Step::Keys;
        s.frames = 4;
        auto star = w.find('*');
        std::string keys = w.substr(0, star);
        if (star != std::string::npos) s.frames = std::atoi(w.c_str() + star + 1);
        for (char c : keys) {
          switch (c) {
            case 'z': s.in.ok = true; break;
            case 'x': s.in.cancel = true; break;
            case 'u': s.in.up = true; break;
            case 'd': s.in.down = true; break;
            case 'l': s.in.left = true; break;
            case 'r': s.in.right = true; break;
            case 'q': s.in.fast = true; break;
            case 'c': s.in.debug = true; break;
            case 'p': s.in.rollUp = true; break;    // ROLL UP (01 §4.3 extension)
            case 'n': s.in.rollDown = true; break;  // ROLL DOWN
            default: break;
          }
        }
        steps.push_back(s);
        Step rel;
        rel.kind = Step::Wait;
        rel.frames = 4;
        steps.push_back(rel);
      }
    }
  }
  return steps;
}

struct EndRun {};

class HeadlessHost : public core::Host {
 public:
  std::vector<Step> steps;
  size_t pc = 0;
  long frame = 0;
  long maxFrames = 600;
  std::string outDir = ".";
  core::WavWriter wav;
  music::MusicPlayer* player = nullptr;
  double sampleAcc = 0;
  core::InputState cur;
  bool verbose = false;

  void vsync(const core::Screen& scr) override {
    ++frame;
    if (player) {
      if (wav.isOpen()) {
        sampleAcc += 44100.0 / 56.4;
        int n = int(sampleAcc);
        sampleAcc -= n;
        std::vector<int16_t> buf(size_t(n) * 2);
        if (n) player->generate(buf.data(), n);
        wav.write(buf.data(), size_t(n));
      }
      if (verbose)
        for (auto& l : player->takeLog()) std::fprintf(stderr, "%s\n", l.c_str());
    }
    cur = core::InputState();
    for (;;) {
      if (pc >= steps.size()) {
        if (frame >= maxFrames) throw EndRun();
        return;
      }
      Step& s = steps[pc];
      if (s.kind == Step::Shot) {
        std::vector<uint8_t> rgb(640 * 400 * 3);
        scr.compose(rgb.data());
        std::string path = outDir + "/" + s.name + ".png";
        core::writePng(path, rgb.data(), 640, 400);
        std::fprintf(stderr, "frame %ld: %s\n", frame, path.c_str());
        ++pc;
        continue;
      }
      if (s.kind == Step::Quit) throw EndRun();
      if (s.frames <= 0) {
        ++pc;
        continue;
      }
      --s.frames;
      if (s.kind == Step::Keys) cur = s.in;
      return;
    }
  }
  core::InputState input() override { return cur; }
  void sound(const core::SoundRequest& r) override {
    if (verbose) {
      switch (r.kind) {
        case core::SoundRequest::PlayNumber: std::fprintf(stderr, "[bgm %d]\n", r.value); break;
        case core::SoundRequest::PlayFile: std::fprintf(stderr, "[bgm %s]\n", r.path.c_str()); break;
        case core::SoundRequest::Sfx: std::fprintf(stderr, "[se %d]\n", r.value); break;
        default: break;
      }
    }
    if (player) player->push(r);
  }
  void log(const std::string& line) override {
    if (verbose) std::fprintf(stderr, "%s\n", line.c_str());
  }
};

void usage() {
  std::fprintf(stderr,
               "usage: dante98cr-headless GAMEDIR [-s script] [-k \"script\"] [-o outdir] [-w out.wav] [-n frames]\n"
               "       [--no-title] [--no-quit] [-v] [--warp M,X,Y] [--battle G] [--gold N] [--item ID]... "
               "[--seed N]\n"
               "       dante98cr-headless --games DIR [-o outdir]\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::string gameDir, scriptText, outDir = ".", wavPath, gamesDir;
  long maxFrames = 600;
  core::StartOptions opt;
  bool haveGames = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto need = [&](const char* what) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing argument for %s\n", what);
        usage();
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "-s") {
      std::ifstream f(need("-s"));
      std::stringstream ss;
      ss << f.rdbuf();
      scriptText += ss.str() + "\n";
    } else if (a == "-k") {
      scriptText += need("-k") + "\n";
    } else if (a == "-o") {
      outDir = need("-o");
    } else if (a == "-w") {
      wavPath = need("-w");
    } else if (a == "-n") {
      maxFrames = std::atol(need("-n").c_str());
    } else if (a == "--no-title") {
      opt.noTitle = true;
    } else if (a == "--no-quit") {
      opt.noQuit = true;
    } else if (a == "-v") {
      opt.verbose = true;
    } else if (a == "--warp") {
      std::string v = need("--warp");
      if (std::sscanf(v.c_str(), "%d,%d,%d", &opt.warpMap, &opt.warpX, &opt.warpY) == 3) opt.warp = true;
    } else if (a == "--battle") {
      opt.battle = std::atoi(need("--battle").c_str());
    } else if (a == "--gold") {
      opt.gold = std::atol(need("--gold").c_str());
    } else if (a == "--item") {
      opt.items.push_back(std::atoi(need("--item").c_str()));
    } else if (a == "--seed") {
      opt.seed = uint32_t(std::strtoul(need("--seed").c_str(), nullptr, 0));
    } else if (a == "--games") {
      gamesDir = need("--games");
      haveGames = true;
    } else if (a == "-h" || a == "--help") {
      usage();
      return 0;
    } else if (!a.empty() && a[0] == '-') {
      usage();
      return 2;
    } else {
      gameDir = a;
    }
  }

  if (haveGames) {
    auto games = app::scanGames(gamesDir);
    for (auto& g : games)
      std::printf("%s\t%s\n", g.kind == core::EngineKind::D2 ? "Dante98 II" : "Dante98", g.root.c_str());
    app::Browser b(gamesDir, games);
    app::RgbCanvas c;
    b.render(c);
    core::writePng(outDir + "/games.png", c.rgb.data(), 640, 400);
    return 0;
  }
  if (gameDir.empty()) {
    usage();
    return 2;
  }

  HeadlessHost host;
  host.steps = parseScript(scriptText);
  host.maxFrames = maxFrames;
  host.outDir = outDir;
  host.verbose = opt.verbose;
  std::unique_ptr<music::MusicPlayer> player;
  try {
    player = std::make_unique<music::MusicPlayer>(gameDir, 44100);
    player->setVerbose(opt.verbose);
    host.player = player.get();
    if (!wavPath.empty() && !host.wav.open(wavPath)) std::fprintf(stderr, "cannot write %s\n", wavPath.c_str());
    std::unique_ptr<core::Engine> eng;
    if (app::engineForRun(gameDir) == core::EngineKind::D2)
      eng = d2::createEngine(host, gameDir, opt);
    else
      eng = d1::createEngine(host, gameDir, opt);
    try {
      eng->run();
    } catch (EndRun&) {
    } catch (core::QuitGame&) {
    }
  } catch (core::DataError& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  host.wav.close();
  return 0;
}
