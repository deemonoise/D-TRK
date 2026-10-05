#include "oneshot.h"

namespace mt {

void OneShot::start(const int16_t* d, uint32_t frames, uint32_t rate) {
  if (!d || frames == 0 || rate == 0) {
    stop();
    return;
  }
  frames_ = frames;
  pos_ = 0;
  step_ = (static_cast<uint64_t>(rate) << 32) / kSynthRate;
  d_ = d;
}

void OneShot::mix(int16_t* out, int n, float gain) {
  if (!d_) return;
  const uint32_t last = frames_ - 1;
  for (int i = 0; i < n; ++i) {
    const uint32_t idx = static_cast<uint32_t>(pos_ >> 32);
    if (idx >= frames_) {
      d_ = nullptr;
      return;
    }
    const int32_t a = d_[idx];
    const int32_t b = d_[idx < last ? idx + 1 : last];
    const float s = a + (b - a) * (static_cast<uint32_t>(pos_) * 2.3283064e-10f);
    int32_t v = out[i] + static_cast<int32_t>(s * gain);
    v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
    out[i] = static_cast<int16_t>(v);
    pos_ += step_;
  }
  if ((pos_ >> 32) >= frames_) d_ = nullptr;
}

}  // namespace mt
