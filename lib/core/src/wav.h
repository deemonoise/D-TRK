#pragma once
#include <stdint.h>
#include "project_io.h"

namespace mt {

constexpr uint32_t kWavMaxRate = 32000;  // bank rate limit: higher rates are downsampled

struct WavInfo {
  uint16_t channels = 0, bits = 0;
  uint32_t rate = 0;
  uint32_t dataOffset = 0, dataBytes = 0;  // "data" payload, bytes from the file start
  bool hasRoot = false;                    // "smpl" chunk present
  uint8_t root = 60;                       // smpl MIDIUnityNote, 60 without one
  uint32_t frameBytes() const { return static_cast<uint32_t>(channels) * (bits / 8); }
  uint32_t frames() const { return frameBytes() ? dataBytes / frameBytes() : 0; }
};

enum class WavErr : uint8_t { Ok, NotWav, Unsupported, Truncated };

// Reads RIFF/WAVE chunks from src: "fmt " (PCM or WAVE_FORMAT_EXTENSIBLE with a PCM sub-format;
// 8/16/24 bit, any channel count), "data" (skipped, position recorded) and an optional "smpl"
// (root note) before or after it; other chunks (LIST, fact, cue...) are skipped.
WavErr wavParse(ByteSource& src, WavInfo& out);

// Converts frames of raw data (any channels, 8/16/24 bit PCM) to mono int16: channels averaged,
// 8 bit unsigned (128 = 0), 24 bit keeps the top 16 bits.
void wavToMono(const uint8_t* raw, uint32_t frames, const WavInfo& w, int16_t* out);

// Streaming resampler to <= 32 kHz: rates up to 32 kHz pass through; higher rates get a 2-tap
// average (cheap anti-alias) and linear interpolation at the 32 kHz grid.
class Downsampler {
 public:
  explicit Downsampler(uint32_t inRate) : rate_(inRate) {}
  uint32_t outRate() const { return rate_ > kWavMaxRate ? kWavMaxRate : rate_; }
  // Upper bound of push() output for n input frames.
  static uint32_t outFrames(uint32_t n, uint32_t inRate) {
    return inRate <= kWavMaxRate ? n : static_cast<uint32_t>(static_cast<uint64_t>(n) * kWavMaxRate / inRate) + 2;
  }
  // Consumes n input frames, writes up to outFrames(n) frames to out, returns the count.
  int push(const int16_t* in, int n, int16_t* out);

 private:
  uint32_t rate_;
  uint64_t inBase_ = 0;  // input frames consumed by earlier pushes
  uint64_t k_ = 0;       // next output frame
  int prevX_ = 0;        // last raw input frame
  int prevY_ = 0;        // last filtered input frame
};

}  // namespace mt
