#include <cstring>
#include <exception>

#include "test.h"

int main(int argc, char** argv) {
  int run = 0, skipped = 0;
  for (auto& c : t::registry()) {
    if (argc > 1 && !std::strstr(c.name, argv[1])) continue;
    const int before = t::failures();
    t::skipped() = false;
    try {
      c.fn();
    } catch (std::exception& e) {
      std::fprintf(stderr, "  FAIL %s: uncaught exception: %s\n", c.name, e.what());
      ++t::failures();
    } catch (...) {
      std::fprintf(stderr, "  FAIL %s: uncaught exception\n", c.name);
      ++t::failures();
    }
    ++run;
    if (t::skipped() && t::failures() == before) {
      ++skipped;
      std::printf("skip %s (sample games not found; set OPENDT_SAMPLES)\n", c.name);
    } else {
      std::printf("%s %s\n", t::failures() == before ? "ok  " : "FAIL", c.name);
    }
  }
  std::printf("%d tests, %d failures, %d skipped\n", run, t::failures(), skipped);
  return t::failures() ? 1 : 0;
}

#include "core/util.h"
bool t::samplesAvailable() {
  static const bool have = core::isDirectory(t::samples() + "games") || core::isDirectory(t::samples() + "d1");
  return have;
}
