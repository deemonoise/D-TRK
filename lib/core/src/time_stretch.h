#pragma once
#include <stdint.h>

namespace mt {

// Frame positions are relative to the selected mono16 region, never the WAV file header.
struct TimeStretchReader {
  virtual bool read(uint32_t first, uint32_t frames, int16_t* out) = 0;
};

enum class TimeStretchResult : uint8_t { Running, Done, BadArg, NoMemory, ReadFail };

// Offline pitch-preserving duration change. Owns at most 8 KB of temporary workspace; no I/O in
// start/reset/destruction. Cancellation belongs to the owning job, which may reset at any time.
class TimeStretch {
 public:
  static constexpr uint32_t kMaxStepFrames = 1024;
  static constexpr uint32_t kWorkBytes = 8192;
  TimeStretch() = default;
  ~TimeStretch();
  TimeStretch(const TimeStretch&) = delete;
  TimeStretch& operator=(const TimeStretch&) = delete;

  // Explicit output length avoids floating-point duration drift. Both lengths must be nonzero,
  // output/input in [0.5,2], rate in 1..44100. Equal lengths bypass allocation and copy bit exactly.
  TimeStretchResult start(uint32_t inputFrames, uint32_t rate, uint32_t outputFrames);
  // Produces at most min(capacity,1024) frames and at most one synthesis hop. Done may accompany
  // the last nonempty piece. Errors are sticky until start/reset; subsequent calls do no reads.
  TimeStretchResult step(TimeStretchReader& reader, int16_t* out, uint32_t capacity, uint32_t& produced);
  void reset();
  uint32_t done() const { return done_; }
  uint32_t total() const { return outputFrames_; }

 private:
  TimeStretchResult state_ = TimeStretchResult::BadArg;
  bool prepare(TimeStretchReader& reader);
  uint32_t select(uint32_t nominal) const;
  int16_t* work_ = nullptr;
  uint32_t done_ = 0, outputFrames_ = 0, inputFrames_ = 0;
  uint32_t sourceFirst_ = 0, chosen_ = 0;
  uint16_t hop_ = 0, seek_ = 0, cursor_ = 0, grainFrames_ = 0;
  bool ready_ = false, haveTail_ = false, endGrain_ = false;
};

}  // namespace mt
