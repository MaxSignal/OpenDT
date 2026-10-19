// Minimal unit test framework.
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace t {
struct Case {
  const char* name;
  std::function<void()> fn;
};
inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
inline int& failures() {
  static int f = 0;
  return f;
}
struct Reg {
  Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};
}  // namespace t

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                       \
  static void name();                                                    \
  static t::Reg TEST_CAT(reg_, name)(#name, name);                       \
  static void name()
#define CHECK(cond)                                                                   \
  do {                                                                                \
    if (!(cond)) {                                                                    \
      std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      ++t::failures();                                                                \
    }                                                                                 \
  } while (0)
#define CHECK_EQ(a, b)                                                                                \
  do {                                                                                                \
    auto va_ = (a);                                                                                   \
    auto vb_ = (b);                                                                                   \
    if (!(va_ == vb_)) {                                                                              \
      std::fprintf(stderr, "  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b,     \
                   (long long)va_, (long long)vb_);                                                   \
      ++t::failures();                                                                                \
    }                                                                                                 \
  } while (0)

#include <cstdlib>
#include <filesystem>
namespace t {
// Root of the sample games (env OPENDT_SAMPLES, default "samples" relative to the working directory).
// Layout: <root>/games/<archive>/... and <root>/d1/<name>/...
inline std::string samples() {
  const char* e = std::getenv("OPENDT_SAMPLES");
  std::string s = (e && *e) ? e : "samples";
  if (s.back() != '/') s += '/';
  return s;
}
// True when the sample games are available. Tests that need them start with `if (!t::needSamples()) return;`
// so that they are reported as skipped (not failed) when the samples are absent, e.g. in CI.
inline bool& skipped() {
  static bool s = false;
  return s;
}
bool samplesAvailable();
inline bool needSamples() {
  if (samplesAvailable()) return true;
  skipped() = true;
  return false;
}
// Scratch directory for tests (env OPENDT_TMP, default: the system temporary directory).
inline std::string tmp() {
  const char* e = std::getenv("OPENDT_TMP");
  std::string s = (e && *e) ? std::string(e) : std::filesystem::temp_directory_path().u8string();
  if (s.back() != '/') s += '/';
  return s;
}
}  // namespace t
