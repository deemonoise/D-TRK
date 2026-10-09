#include "state_mirror.h"
#include <string.h>

namespace mt::link {

void StateMirror::invalidate() {
  for (int i = 0; i < kChunks; ++i) valid_[i] = inFlight_[i] = false;
}

bool StateMirror::dirty(int i, const SynthModel& live) const {
  if (!valid_[i]) return true;
  uint32_t off, len;
  chunkRange(chunkId(i), off, len);
  return memcmp(reinterpret_cast<const uint8_t*>(&live) + off, reinterpret_cast<const uint8_t*>(&shadow_) + off, len) != 0;
}

int StateMirror::nextDirty(const SynthModel& live) {
  for (int i = 0; i < kChunks; ++i) {
    if (inFlight_[i] || !dirty(i, live)) continue;
    inFlight_[i] = true;
    return chunkId(i);
  }
  return -1;
}

void StateMirror::acked(uint16_t chunk, const uint8_t* sent) {
  int i = chunkIndex(chunk);
  if (i < 0) return;
  uint32_t off, len;
  chunkRange(chunk, off, len);
  memcpy(reinterpret_cast<uint8_t*>(&shadow_) + off, sent, len);
  valid_[i] = true;
  inFlight_[i] = false;
}

void StateMirror::failed(uint16_t chunk) {
  int i = chunkIndex(chunk);
  if (i >= 0) inFlight_[i] = false;
}

bool StateMirror::synced(const SynthModel& live) const {
  for (int i = 0; i < kChunks; ++i)
    if (inFlight_[i] || dirty(i, live)) return false;
  return true;
}

bool StateMirror::inFlight() const {
  for (bool f : inFlight_)
    if (f) return true;
  return false;
}

}  // namespace mt::link
