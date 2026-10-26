#include "heads_watch.h"
#include "model.h"

namespace mt {

uint32_t sampleSetSig(const SynthModel& m) {
  const int n = m.sampleCount < kProjSamples ? m.sampleCount : kProjSamples;
  uint32_t s = static_cast<uint32_t>(n) * 0x9E3779B9u;
  for (int i = 0; i < n; ++i) {  // a sum of mixed crcs: the order does not count
    uint32_t x = m.samples[i].crc;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    s += x;
  }
  return s;
}

bool HeadsWatch::due(uint32_t sig, bool idle, uint32_t nowMs) {
  if (sig == done_) {
    seen_ = sig;
    return false;
  }
  if (sig != seen_) {
    seen_ = sig;
    since_ = nowMs;
  }
  if (!idle) {
    idle_ = false;
    return false;
  }
  if (!idle_) {
    idle_ = true;
    idleFrom_ = nowMs;
  }
  return nowMs - since_ >= kSettleMs && nowMs - idleFrom_ >= kSettleMs;
}

}  // namespace mt
