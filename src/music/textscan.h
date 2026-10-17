// Shared text scanning rules for MML and SOUND.DAT (spec 02 §2.2, §2.3).
#pragma once
#include <cstdint>
#include <string>

namespace music {

inline bool isDelim(char c) { return c == ' ' || c == ':' || c == ',' || c == '\t'; }
inline char upper(char c) { return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c; }

// Cursor over one line that is terminated by '\r'.
struct Cursor {
  const std::string& s;
  size_t p = 0;
  explicit Cursor(const std::string& line, size_t pos = 0) : s(line), p(pos) {}
  char peek() const { return p < s.size() ? s[p] : '\r'; }
  bool atEnd() const { return peek() == '\r'; }
  void skipDelims() {
    while (p < s.size() && isDelim(s[p])) ++p;
  }
  // Case-insensitive prefix match; advances on success.
  bool matchKeyword(const char* kw) {
    size_t i = 0;
    for (; kw[i]; ++i) {
      if (p + i >= s.size() || upper(s[p + i]) != kw[i]) return false;
    }
    p += i;
    return true;
  }
  // Number reading (spec 02 §2.3). Result is a 16-bit signed value.
  // progressed is set when the cursor moved past anything other than delimiters.
  int16_t readNum(bool* progressed = nullptr) {
    skipDelims();
    const size_t start = p;
    bool neg = false;
    if (peek() == '-') {
      neg = true;
      ++p;
    }
    uint32_t v = 0;
    for (;;) {
      char c = peek();
      int d;
      if (c >= '0' && c <= '9')
        d = c - '0';
      else if (c == ':')
        d = 10;
      else
        break;
      v = (v * 10 + uint32_t(d)) & 0xFFFF;
      ++p;
    }
    if (progressed) *progressed = p != start;
    if (neg) v = (0x10000 - v) & 0xFFFF;
    return int16_t(uint16_t(v));
  }
};

}  // namespace music
