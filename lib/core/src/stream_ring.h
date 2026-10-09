#pragma once
#include <atomic>
#include <stdint.h>

namespace mt {

// Linear interpolation from inRate to outRate, streaming (mono int16). Output frame k is input
// position k * inRate / outRate.
class LinearResampler {
 public:
  LinearResampler(uint32_t inRate = 1, uint32_t outRate = 1) : in_(inRate ? inRate : 1), out_(outRate ? outRate : 1) {}
  // Upper bound of push() output for n input frames.
  uint32_t maxOut(uint32_t n) const { return static_cast<uint32_t>(static_cast<uint64_t>(n) * out_ / in_) + 2; }
  // Input frames whose output surely fits in space frames (0 if space is too small).
  uint32_t inFor(uint32_t space) const {
    return space > 2 ? static_cast<uint32_t>(static_cast<uint64_t>(space - 2) * in_ / out_) : 0;
  }
  // Consumes n input frames, writes up to maxOut(n) frames to out, returns the count.
  int push(const int16_t* in, int n, int16_t* out);

 private:
  uint32_t in_, out_;
  uint64_t inBase_ = 0;  // input frames consumed by earlier pushes
  uint64_t k_ = 0;       // next output frame
  int prev_ = 0;         // input frame inBase_ - 1
};

// Single producer (loop()) / single consumer (the audio interrupt) ring of mono int16 at the output
// rate: the producer fills it from a file, the consumer adds it to the output. A consumer that finds
// too little (and no finish()) plays silence for the rest of the block and counts an underrun.
// start() / stop() only from the producer, which the consumer may interrupt but not the reverse.
class StreamRing {
 public:
  static constexpr uint32_t kLen = 8192;  // 16 KB

  void stop() { active_.store(false); }  // the consumer reads nothing after this returns
  void reset();                          // empty, not finished; stop() first
  void start() { active_.store(true); }  // after reset() and the first write()s
  bool playing() const { return active_.load(); }

  uint32_t space() const { return kLen - avail(); }
  uint32_t avail() const { return w_.load() - r_.load(); }
  void write(const int16_t* d, uint32_t n);  // n <= space()
  void finish() { done_.store(true); }       // no more data: the consumer stops once it is played

  // Consumer: adds the next n samples times gain to out, saturating.
  void mix(int16_t* out, int n, float gain) { mix(out, nullptr, n, gain); }
  // Consumer, stereo: the same (mono) samples into l and r (nullptr = l only).
  void mix(int16_t* l, int16_t* r, int n, float gain);
  uint32_t takeUnderruns() { return underruns_.exchange(0); }

 private:
  int16_t buf_[kLen];
  std::atomic<uint32_t> w_{0}, r_{0};  // free-running counters
  std::atomic<bool> active_{false}, done_{false};
  std::atomic<uint32_t> underruns_{0};
};

}  // namespace mt
