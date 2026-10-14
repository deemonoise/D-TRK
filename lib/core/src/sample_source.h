#pragma once
#include <stdint.h>

namespace mt {

constexpr int kMaxHeads = 40;  // heads per sample

// A run of a sample's frames kept in memory: frames [frame, frame + len) are d[0 .. len).
struct SampleHead {
  uint32_t frame = 0, len = 0;
  const int16_t* d = nullptr;
};

// What a voice needs to play a sample. Either whole (all frames in memory: flash bank, PSRAM, a test
// array) or streamable (frames load by key into a StreamPool slot; heads cover note starts).
struct SampleData {
  uint32_t key = 0;  // sample CRC: the pool's and the store's key
  uint32_t frames = 0, rate = 0;
  const int16_t* whole = nullptr;
  bool psram = false;  // whole points into PSRAM (dropped if it fails, see StreamPool::psramOk)
  const SampleHead* heads = nullptr;
  uint8_t nHeads = 0;
  bool streamable = false;
};

// Lookup by name for the synth (audio interrupt, note-on). Whatever it returns stays valid until the
// synth is parked.
struct SampleSource {
  virtual bool find(const char* name, SampleData& out) const = 0;
};

}  // namespace mt
