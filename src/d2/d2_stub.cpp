// TEMPORARY stub engine for D2 (replaced by the real implementation).
#include "core/util.h"
#include "d2/d2.h"

namespace d2 {
namespace {
class StubEngine : public core::Engine {
 public:
  StubEngine(core::Host& h) : host_(h) {}
  void run() override {
    core::Screen scr;
    for (;;) host_.vsync(scr);
  }
  core::Host& host_;
};
}  // namespace

std::unique_ptr<core::Engine> createEngine(core::Host& host, const std::string& root, const core::StartOptions&) {
  (void)root;
  return std::make_unique<StubEngine>(host);
}

std::string buildLabel(const std::string&) { return "1997-10"; }
}  // namespace d2
