#pragma once

#include <cstddef>
#include <cstdint>

namespace brick {
// Stereo Q16 linear gains shared by the launcher and the SDL playback callback.
class OutputGain {
 public:
  OutputGain() = default;
  OutputGain(const OutputGain&) = delete;
  OutputGain& operator=(const OutputGain&) = delete;
  ~OutputGain();
  bool open(const char* path);
  void apply(int16_t* samples, size_t frames) const;

 private:
  const uint64_t* gains_ = nullptr;
};
}  // namespace brick
