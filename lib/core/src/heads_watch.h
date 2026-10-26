#pragma once
#include <stdint.h>

namespace mt {

struct SynthModel;

// The model's sample set (count and crcs, order ignored) as one word: a change means samples the
// synth board has no heads / PSRAM copies for (added on the fly: a render, a take, an import).
uint32_t sampleSetSig(const SynthModel& m);

// When to redo the heads and the PSRAM fill (bank: the synth parked) for a changed sample set: once
// the set has held for kSettleMs (StateSet pieces, a BankSync after them) and the board has been
// idle that long (no job, nothing sounding). done(): the set the heads are written for.
class HeadsWatch {
 public:
  static constexpr uint32_t kSettleMs = 500;

  void done(uint32_t sig) {
    done_ = seen_ = sig;
    idle_ = false;
  }
  // True: refresh now (then done(sig)).
  bool due(uint32_t sig, bool idle, uint32_t nowMs);
  bool pending() const { return seen_ != done_; }

 private:
  uint32_t done_ = 0, seen_ = 0, since_ = 0, idleFrom_ = 0;
  bool idle_ = false;
};

}  // namespace mt
