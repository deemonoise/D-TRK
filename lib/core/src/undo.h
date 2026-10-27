#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Ring of pattern snapshots; storage (kDepth entries) supplied by the caller (PSRAM on device).
// kDepth (+1 scratch on device) x ~28 KB ≈ 260 KB of PSRAM; the live project itself is ≈ 460 KB.
// An entry may also carry one track's GenCfg (genTrack >= 0; Capture live turns that track's GEN off).
class Undo {
 public:
  static constexpr int kDepth = 8;
  struct Entry {
    uint8_t pattern;
    int8_t genTrack = -1;  // -1: no GenCfg
    GenCfg gen;
    Pattern data;
  };
  explicit Undo(Entry* storage) : buf_(storage) {}

  // Overwrites the oldest when full.
  void push(uint8_t pattern, const Pattern& p, int genTrack = -1, const GenCfg& gen = GenCfg()) {
    buf_[head_].pattern = pattern;
    buf_[head_].genTrack = static_cast<int8_t>(genTrack >= 0 && genTrack < kTracks ? genTrack : -1);
    buf_[head_].gen = gen;
    buf_[head_].data = p;
    head_ = (head_ + 1) % kDepth;
    if (count_ < kDepth) ++count_;
  }
  // False when empty. genTrack / gen (optional): the entry's GenCfg, genTrack -1 when it has none.
  bool pop(uint8_t& pattern, Pattern& out, int* genTrack = nullptr, GenCfg* gen = nullptr) {
    if (count_ == 0) return false;
    head_ = (head_ - 1 + kDepth) % kDepth;
    pattern = buf_[head_].pattern;
    out = buf_[head_].data;
    if (genTrack) *genTrack = buf_[head_].genTrack;
    if (gen) *gen = buf_[head_].gen;
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
