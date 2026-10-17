// Millisecond wait conversion with carry-over (spec 01 §3.3).
#pragma once

namespace core {

constexpr double kVsyncHz = 56.4;
constexpr double kD1MsHz = 56.4;
constexpr double kD2MsHz = 56.42;

class MsBalance {
 public:
  explicit MsBalance(double hz) : hz_(hz) {}
  // Adds m milliseconds and returns the number of whole VSYNCs to wait now.
  int add(double ms) {
    bal_ += ms * hz_ / 1000.0;
    int n = 0;
    while (bal_ >= 1.0) {
      bal_ -= 1.0;
      ++n;
    }
    return n;
  }
  double balance() const { return bal_; }

 private:
  double hz_;
  double bal_ = 0.0;
};

}  // namespace core
