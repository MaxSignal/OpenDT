// TEMPORARY stub engine for D1 (replaced by the real implementation).
#include "core/util.h"
#include "d1/d1.h"

namespace d1 {
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
}  // namespace d1
