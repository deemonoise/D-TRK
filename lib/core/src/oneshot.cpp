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

static inline int16_t addSat(int16_t x, int32_t d) {
  const int32_t v = x + d;
  return static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

void OneShot::mixInto(int16_t* out, int n, float gain, int stride) {
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
    const int32_t d = static_cast<int32_t>(s * gain);
    int16_t* o = out + i * stride;
    o[0] = addSat(o[0], d);
    if (stride == 2) o[1] = addSat(o[1], d);
    pos_ += step_;
  }
  if ((pos_ >> 32) >= frames_) d_ = nullptr;
}

}  // namespace mt
