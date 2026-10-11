#include "synth_filter.h"

namespace mt {

// Out of line: an IRAM function inline in the header breaks the ESP32 link (l32r literals).
MT_HOT void Svf::set(Mode m, float hz, float q) {
  if (m == mode_ && hz == hz_ && q == q_) return;
  mode_ = m;
  hz_ = hz;
  q_ = q;
  const float maxHz = kSynthRate * 0.45f;
  hz = hz < 20.f ? 20.f : (hz > maxHz ? maxHz : hz);
  q = q < 0.5f ? 0.5f : q;
  const float g = fastTan(hz * (3.14159265f / kSynthRate));
  k_ = 1.f / q;
  a1_ = 1.f / (1.f + g * (g + k_));
  a2_ = g * a1_;
  a3_ = g * a2_;
}

}  // namespace mt
