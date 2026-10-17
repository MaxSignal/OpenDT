// RNG, file access, PNG/WAV output (spec 01 §5, §6, §9).
#pragma once
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace core {

// Borland Turbo C rand() compatible LCG (spec 01 §5.1).
class Rng {
 public:
  uint32_t state = 1;
  void seed(uint32_t s) { state = s; }
  int next() {
    state = state * 0x015A4E35u + 1u;
    return int((state >> 16) & 0x7FFF);
  }
};

// Data loading failure; message is UTF-8.
struct DataError : std::runtime_error {
  explicit DataError(const std::string& m) : std::runtime_error(m) {}
};

// Case-insensitive path resolution (spec 01 §6.1). root is a host path, rel may use / or \.
std::string resolvePath(const std::string& root, const std::string& rel);
bool fileExists(const std::string& root, const std::string& rel);
bool isDirectory(const std::string& path);
bool isRegularFile(const std::string& path);
// Reads an entire file; throws DataError("cannot open <rel>") if it cannot be opened.
std::vector<uint8_t> readFile(const std::string& root, const std::string& rel);
bool tryReadFile(const std::string& root, const std::string& rel, std::vector<uint8_t>& out);
bool writeFileAbs(const std::string& path, const std::vector<uint8_t>& data);
std::vector<std::string> listDir(const std::string& path);  // names only, excluding . and ..

// 48-byte palette file (spec 01 §6.3): 16 x (R,G,B) & 0x0F.
void loadPalette48(const std::vector<uint8_t>& data, uint8_t out[16][3]);

inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline int16_t rds16(const uint8_t* p) { return int16_t(rd16(p)); }
inline uint32_t rd32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }
inline void wr16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}
inline void wr32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}

// PNG writer (RGB 8-bit).
bool writePng(const std::string& path, const uint8_t* rgb, int w, int h);

// WAV writer (44100 Hz stereo s16).
class WavWriter {
 public:
  ~WavWriter() { close(); }
  bool open(const std::string& path);
  void write(const int16_t* stereo, size_t frames);
  void close();
  bool isOpen() const { return f_ != nullptr; }
  size_t frames() const { return frames_; }

 private:
  FILE* f_ = nullptr;
  size_t frames_ = 0;
};

}  // namespace core
