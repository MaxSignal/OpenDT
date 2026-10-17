#include "core/util.h"

#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>

#include <cstring>

namespace core {

static std::string upperAscii(const std::string& s) {
  std::string r = s;
  for (auto& c : r)
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
  return r;
}

static std::vector<std::string> splitRel(const std::string& rel) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : rel) {
    if (c == '/' || c == '\\') {
      if (!cur.empty()) parts.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) parts.push_back(cur);
  return parts;
}

static bool pathExists(const std::string& p) {
  struct stat st;
  return ::stat(p.c_str(), &st) == 0;
}

bool isDirectory(const std::string& p) {
  struct stat st;
  return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isRegularFile(const std::string& p) {
  struct stat st;
  return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::vector<std::string> listDir(const std::string& path) {
  std::vector<std::string> out;
  DIR* d = ::opendir(path.c_str());
  if (!d) return out;
  while (dirent* e = ::readdir(d)) {
    if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
    out.emplace_back(e->d_name);
  }
  ::closedir(d);
  return out;
}

static std::string join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (a.back() == '/') return a + b;
  return a + "/" + b;
}

std::string resolvePath(const std::string& root, const std::string& rel) {
  const auto parts = splitRel(rel);
  std::string direct = root;
  for (auto& p : parts) direct = join(direct, p);
  if (pathExists(direct)) return direct;
  std::string cur = root;
  for (auto& p : parts) {
    const std::string up = upperAscii(p);
    bool found = false;
    for (auto& name : listDir(cur)) {
      if (upperAscii(name) == up) {
        cur = join(cur, name);
        found = true;
        break;
      }
    }
    if (!found) return direct;
  }
  return cur;
}

bool fileExists(const std::string& root, const std::string& rel) { return pathExists(resolvePath(root, rel)); }

bool tryReadFile(const std::string& root, const std::string& rel, std::vector<uint8_t>& out) {
  const std::string p = resolvePath(root, rel);
  if (!isRegularFile(p)) return false;
  FILE* f = std::fopen(p.c_str(), "rb");
  if (!f) return false;
  out.clear();
  uint8_t buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
  std::fclose(f);
  return true;
}

std::vector<uint8_t> readFile(const std::string& root, const std::string& rel) {
  std::vector<uint8_t> out;
  if (!tryReadFile(root, rel, out)) throw DataError("cannot open " + rel);
  return out;
}

bool writeFileAbs(const std::string& path, const std::vector<uint8_t>& data) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  ok = (std::fclose(f) == 0) && ok;
  return ok;
}

void loadPalette48(const std::vector<uint8_t>& data, uint8_t out[16][3]) {
  if (data.size() < 48) throw DataError("palette file too short");
  for (int i = 0; i < 16; ++i)
    for (int k = 0; k < 3; ++k) out[i][k] = data[i * 3 + k] & 0x0F;
}

// ---- PNG ----
static void putBE32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(uint8_t(x >> 24));
  v.push_back(uint8_t(x >> 16));
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x));
}

static void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  putBE32(out, uint32_t(data.size()));
  std::vector<uint8_t> td(type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  out.insert(out.end(), td.begin(), td.end());
  putBE32(out, uint32_t(crc32(0, td.data(), uInt(td.size()))));
}

bool writePng(const std::string& path, const uint8_t* rgb, int w, int h) {
  std::vector<uint8_t> raw;
  raw.reserve(size_t(h) * (w * 3 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb + size_t(y) * w * 3, rgb + size_t(y + 1) * w * 3);
  }
  uLongf clen = compressBound(uLong(raw.size()));
  std::vector<uint8_t> comp(clen);
  if (compress2(comp.data(), &clen, raw.data(), uLong(raw.size()), 6) != Z_OK) return false;
  comp.resize(clen);
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  putBE32(ihdr, uint32_t(w));
  putBE32(ihdr, uint32_t(h));
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", comp);
  chunk(out, "IEND", {});
  return writeFileAbs(path, out);
}

// ---- WAV ----
static void wavHeader(FILE* f, size_t frames) {
  uint8_t h[44];
  std::memcpy(h, "RIFF", 4);
  wr32(h + 4, uint32_t(36 + frames * 4));
  std::memcpy(h + 8, "WAVEfmt ", 8);
  wr32(h + 16, 16);
  wr16(h + 20, 1);
  wr16(h + 22, 2);
  wr32(h + 24, 44100);
  wr32(h + 28, 176400);
  wr16(h + 32, 4);
  wr16(h + 34, 16);
  std::memcpy(h + 36, "data", 4);
  wr32(h + 40, uint32_t(frames * 4));
  std::fwrite(h, 1, 44, f);
}

bool WavWriter::open(const std::string& path) {
  close();
  f_ = std::fopen(path.c_str(), "wb");
  if (!f_) return false;
  frames_ = 0;
  wavHeader(f_, 0);
  return true;
}

void WavWriter::write(const int16_t* stereo, size_t frames) {
  if (!f_) return;
  for (size_t i = 0; i < frames * 2; ++i) {
    uint8_t b[2];
    wr16(b, uint16_t(stereo[i]));
    std::fwrite(b, 1, 2, f_);
  }
  frames_ += frames;
}

void WavWriter::close() {
  if (!f_) return;
  std::fseek(f_, 0, SEEK_SET);
  wavHeader(f_, frames_);
  std::fclose(f_);
  f_ = nullptr;
}

}  // namespace core
