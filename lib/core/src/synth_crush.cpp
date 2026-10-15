#include "synth_crush.h"
#include "hot.h"

namespace mt {

MT_HOT void Crush::change(uint8_t bits, uint8_t rate) {
  bits_ = bits;
  rate_ = rate;
  // 16 bits at 1 down to 2 at 127; the signal is about +-1 at full scale.
  const int b = 16 - (bits_ * 14 + 63) / 127;
  step_ = bits_ ? 2.f / static_cast<float>(1 << (b > 1 ? b - 1 : 0)) : 0.f;
  rstep_ = bits_ ? static_cast<float>(1 << (b > 1 ? b - 1 : 0)) * 0.5f : 0.f;  // 1 / step_, exact
  inc_ = 1.f / (1.f + static_cast<float>(rate_) * rate_ / 256.f);
}

}  // namespace mt
