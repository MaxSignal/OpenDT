#include <cstring>

#include "test.h"

int main(int argc, char** argv) {
  int run = 0;
  for (auto& c : t::registry()) {
    if (argc > 1 && !std::strstr(c.name, argv[1])) continue;
    const int before = t::failures();
    c.fn();
    ++run;
    std::printf("%s %s\n", t::failures() == before ? "ok  " : "FAIL", c.name);
  }
  std::printf("%d tests, %d failures\n", run, t::failures());
  return t::failures() ? 1 : 0;
}
