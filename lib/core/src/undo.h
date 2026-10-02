#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Ring of pattern snapshots; storage (kDepth entries) supplied by the caller (PSRAM on device).
class Undo {
 public:
  static constexpr int kDepth = 32;
  struct Entry {
    uint8_t pattern;
    Pattern data;
  };
  explicit Undo(Entry* storage) : buf_(storage) {}

  // Overwrites the oldest when full.
  void push(uint8_t pattern, const Pattern& p) {
    buf_[head_].pattern = pattern;
    buf_[head_].data = p;
    head_ = (head_ + 1) % kDepth;
    if (count_ < kDepth) ++count_;
  }
  // False when empty.
  bool pop(uint8_t& pattern, Pattern& out) {
    if (count_ == 0) return false;
    head_ = (head_ - 1 + kDepth) % kDepth;
    pattern = buf_[head_].pattern;
    out = buf_[head_].data;
    --count_;
    return true;
  }
  // Forgets the newest entry (an edit that was cancelled). False when empty.
  bool drop() {
    if (count_ == 0) return false;
    head_ = (head_ - 1 + kDepth) % kDepth;
    --count_;
    return true;
  }
  int size() const { return count_; }
  void clear() { count_ = 0; }

 private:
  Entry* buf_;
  int head_ = 0;  // next write slot
  int count_ = 0;
};

}  // namespace mt
