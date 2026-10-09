#pragma once
#include <stdint.h>

namespace mt::link {

// Teensy side: maps ESP engine time to local micros(). offset = min over a 2 s window of
// (local arrival - esp stamp): the minimum drops queueing delay, the window follows drift.
class TimeSync {
 public:
  static constexpr uint32_t kWindowUs = 2000000;
  static constexpr int kSlots = 64;

  void sample(uint64_t espUs, uint64_t localUs);
  void reset() { n_ = 0; }  // the ESP rebooted: its clock restarted
  bool valid() const { return n_ > 0; }
  uint64_t toLocal(uint64_t espUs) const { return espUs + offset_; }  // espUs + offset

 private:
  struct Slot {
    uint64_t localUs;
    int64_t delta;
  };
  Slot ring_[kSlots];
  int head_ = 0, n_ = 0;  // oldest at head_
  int64_t offset_ = 0;
};

}  // namespace mt::link
