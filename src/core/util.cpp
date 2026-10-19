#include "core/util.h"

#include <cstring>
#include <algorithm>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace core {

namespace fs = std::filesystem;

// All host paths are UTF-8; convert explicitly so that Windows uses the wide-character APIs.
static fs::path P(const std::string& s) { return fs::u8path(s); }
static std::string S(const fs::path& p) { return p.u8string(); }

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
  std::error_code ec;
  return fs::exists(P(p), ec);
}

bool isDirectory(const std::string& p) {
  std::error_code ec;
  return fs::is_directory(P(p), ec);
}

bool isRegularFile(const std::string& p) {
  std::error_code ec;
  return fs::is_regular_file(P(p), ec);
}

bool isSymlink(const std::string& p) {
  std::error_code ec;
  return fs::is_symlink(fs::symlink_status(P(p), ec));
}

std::vector<std::string> listDir(const std::string& path) {
  std::vector<std::string> out;
  std::error_code ec;
  fs::directory_iterator it(P(path), fs::directory_options::skip_permission_denied, ec), end;
  if (ec) return out;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    out.push_back(S(it->path().filename()));
  }
  return out;
}

std::string joinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (a.back() == '/' || a.back() == '\\') return a + b;
  return a + "/" + b;
}

std::string resolvePath(const std::string& root, const std::string& rel) {
  const auto parts = splitRel(rel);
  std::string direct = root;
  for (auto& p : parts) direct = joinPath(direct, p);
  if (pathExists(direct)) return direct;
  std::string cur = root;
  for (auto& p : parts) {
    const std::string up = upperAscii(p);
    bool found = false;
    for (auto& name : listDir(cur)) {
      if (upperAscii(name) == up) {
        cur = joinPath(cur, name);
        found = true;
        break;
      }
    }
    if (!found) return direct;
  }
  return cur;
}

bool fileExists(const std::string& root, const std::string& rel) { return pathExists(resolvePath(root, rel)); }

FILE* openFile(const std::string& path, const char* mode) {
#ifdef _WIN32
  std::wstring wmode;
  for (const char* m = mode; *m; ++m) wmode.push_back(wchar_t(*m));
  return _wfopen(P(path).wstring().c_str(), wmode.c_str());
#else
  return std::fopen(path.c_str(), mode);
#endif
}

bool makeDirs(const std::string& path) {
  std::error_code ec;
  fs::create_directories(P(path), ec);
  return isDirectory(path);
}

void removeAll(const std::string& path) {
  std::error_code ec;
  fs::remove_all(P(path), ec);
}

bool copyTree(const std::string& from, const std::string& to) {
  std::error_code ec;
  fs::create_directories(P(to), ec);
  fs::copy(P(from), P(to), fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  return !ec;
}

std::vector<std::string> utf8Args(int argc, char** argv) {
  std::vector<std::string> out;
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  int n = 0;
  if (LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n)) {
    for (int i = 0; i < n; ++i) out.push_back(S(fs::path(w[i])));
    LocalFree(w);
    return out;
  }
#endif
  for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
  return out;
}

std::string currentDir() {
  std::error_code ec;
  fs::path p = fs::current_path(ec);
  return ec ? std::string() : S(p);
}

bool tryReadFile(const std::string& root, const std::string& rel, std::vector<uint8_t>& out) {
  const std::string p = resolvePath(root, rel);
  if (!isRegularFile(p)) return false;
  FILE* f = openFile(p, "rb");
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
  FILE* f = openFile(path, "wb");
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

// ---- PNG (spec 01 §9.1: stored, uncompressed zlib stream; no external library) ----
static uint32_t crc32(const uint8_t* p, size_t n) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

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
  putBE32(out, crc32(td.data(), td.size()));
}

bool writePng(const std::string& path, const uint8_t* rgb, int w, int h) {
  std::vector<uint8_t> raw;
  raw.reserve(size_t(h) * (w * 3 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb + size_t(y) * w * 3, rgb + size_t(y + 1) * w * 3);
  }
  // zlib stream made of stored deflate blocks (max 65535 bytes each) + Adler-32.
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  size_t pos = 0;
  do {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    const bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(uint8_t(n));
    z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n));
    z.push_back(uint8_t(~n >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  } while (pos < raw.size());
  putBE32(z, (b << 16) | a);
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  putBE32(ihdr, uint32_t(w));
  putBE32(ihdr, uint32_t(h));
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
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
  f_ = openFile(path, "wb");
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
