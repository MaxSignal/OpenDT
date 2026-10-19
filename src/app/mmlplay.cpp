// mmlplay: compile/play MUSIC.COM MML and SOUND.DAT effects to WAV, or batch-compile all sample MML.
//
//   mmlplay --batch DIR                         compile every BGM/*.MML and DAT_BGM/*.MML under DIR
//   mmlplay GAMEDIR REL.MML [-t SEC] [-o out.wav]   play an MML (path relative to GAMEDIR)
//   mmlplay GAMEDIR --bgm N [-t SEC] [-o out.wav]   play BGM/BGMnn.MML (D1 numbering)
//   mmlplay GAMEDIR --sfx N [-t SEC] [-o out.wav]   play SOUND.DAT effect @N
//   --after-fade: set fade 15 and stop before the request (D1 BGM switch carry-over, spec 02 §3.3)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/util.h"
#include "music/mml.h"
#include "music/player.h"
#include "music/sounddat.h"

namespace {

std::string upper(std::string s) {
  for (auto& c : s)
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
  return s;
}

void walk(const std::string& dir, std::vector<std::string>& out) {
  std::vector<std::string> names = core::listDir(dir);
  std::sort(names.begin(), names.end());
  for (auto& n : names) {
    std::string p = core::joinPath(dir, n);
    if (core::isSymlink(p)) continue;
    if (core::isDirectory(p))
      walk(p, out);
    else if (core::isRegularFile(p)) {
      std::string u = upper(n);
      if (u.size() > 4 && u.substr(u.size() - 4) == ".MML") {
        const size_t k = dir.find_last_of("/\\");
        std::string parent = upper(k == std::string::npos ? dir : dir.substr(k + 1));
        if (parent == "BGM" || parent == "DAT_BGM") out.push_back(p);
      }
    }
  }
}

int batch(const std::string& dir) {
  std::vector<std::string> files;
  walk(dir, files);
  int withErr = 0, totalErr = 0;
  std::map<std::string, int> uniq;
  for (auto& f : files) {
    std::vector<uint8_t> text;
    if (!core::tryReadFile(f, "", text)) continue;
    uniq[std::string(text.begin(), text.end())]++;
    music::Song s = music::compileMml(text);
    if (s.errorCount) {
      ++withErr;
      totalErr += s.errorCount;
      std::printf("%s: %d errors\n", f.c_str(), s.errorCount);
      for (auto& e : s.errors) std::printf("    %s\n", e.c_str());
    }
  }
  // SOUND.DAT files at game roots
  std::printf("%zu MML files (%zu unique), %d with errors, %d errors total\n", files.size(), uniq.size(), withErr,
              totalErr);
  return withErr ? 1 : 0;
}

}  // namespace

int main(int argc_, char** argv_) {
  const std::vector<std::string> argv = core::utf8Args(argc_, argv_);
  const int argc = int(argv.size());
  if (argc >= 3 && argv[1] == "--batch") return batch(argv[2]);
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: mmlplay --batch DIR\n"
                 "       mmlplay GAMEDIR (REL.MML | --bgm N | --sfx N) [-t SEC] [-o out.wav]\n");
    return 2;
  }
  std::string game = argv[1];
  std::string out = "out.wav";
  double secs = 10;
  core::SoundRequest req;
  bool haveReq = false;
  bool afterFade = false;  // emulate D1 BGM switching: fade 0..15, stop, then the request
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-t" && i + 1 < argc)
      secs = std::atof(argv[++i].c_str());
    else if (a == "-o" && i + 1 < argc)
      out = argv[++i];
    else if (a == "--after-fade")
      afterFade = true;
    else if (a == "--bgm" && i + 1 < argc) {
      req.kind = core::SoundRequest::PlayNumber;
      req.value = std::atoi(argv[++i].c_str());
      haveReq = true;
    } else if (a == "--sfx" && i + 1 < argc) {
      req.kind = core::SoundRequest::Sfx;
      req.value = std::atoi(argv[++i].c_str());
      haveReq = true;
    } else {
      req.kind = core::SoundRequest::PlayFile;
      req.path = a;
      haveReq = true;
    }
  }
  if (!haveReq) return 2;
  if (req.kind == core::SoundRequest::PlayFile) {
    std::vector<uint8_t> text;
    if (core::tryReadFile(game, req.path, text)) {
      music::Song s = music::compileMml(text);
      std::fprintf(stderr, "tempo %u, q %d, %d errors\n", s.tempo, music::tempoToQ(s.tempo), s.errorCount);
      for (auto& e : s.errors) std::fprintf(stderr, "  %s\n", e.c_str());
    } else {
      std::fprintf(stderr, "cannot open %s\n", req.path.c_str());
      return 1;
    }
  }
  music::MusicPlayer p(game, 44100);
  p.setVerbose(true);
  std::fprintf(stderr, "MUSIC.COM %s\n", p.version221() ? "Ver 2.21" : "Ver 2.00");
  if (afterFade) {
    core::SoundRequest f;
    f.kind = core::SoundRequest::Fade;
    f.value = 15;
    p.push(f);
    core::SoundRequest st;
    st.kind = core::SoundRequest::Stop;
    p.push(st);
  }
  p.push(req);
  core::WavWriter w;
  if (!w.open(out)) {
    std::fprintf(stderr, "cannot write %s\n", out.c_str());
    return 1;
  }
  const int total = int(secs * 44100);
  std::vector<int16_t> buf(1024 * 2);
  for (int done = 0; done < total;) {
    int n = std::min(1024, total - done);
    p.generate(buf.data(), n);
    w.write(buf.data(), size_t(n));
    done += n;
  }
  for (auto& l : p.takeLog()) std::fprintf(stderr, "%s\n", l.c_str());
  w.close();
  return 0;
}
