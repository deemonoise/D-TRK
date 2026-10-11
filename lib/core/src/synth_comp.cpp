#include "synth_comp.h"
#include "fastmath.h"
#include "hot.h"

namespace mt {

// Out of line: an IRAM function inline in the header breaks the ESP32 link (l32r literals).
MT_HOT void Compressor::process(float* l, float* r, const float* scL, const float* scR, int n, uint8_t amt,
                                 uint8_t rel, uint8_t scDepth) {
  if (amt == 0) return;
  if (amt != amt_ || rel != rel_) {  // constants per setting, not per block
    amt_ = amt;
    rel_ = rel;
    const float a = amt > 127 ? 127 : amt;
    thrDb_ = -6.f - 24.f * a / 127.f;
    makeup_ = exp2f(-thrDb_ * 0.25f / 6.02f);
    const float relMs = 20.f * powf(50.f, (rel > 127 ? 127 : rel) / 127.f);
    ca_ = 1.f - expf(-1.f / (0.001f * kRate));
    cr_ = 1.f - expf(-1.f / (relMs * 0.001f * kRate));
  }
  const float scW = (scDepth > 127 ? 127 : scDepth) * (4.f / 127.f);
  for (int i = 0; i < n; ++i) {
    // No fmaxf: a newlib call from IRAM on the ESP32.
    const float al = fabsf(l[i]), ar = fabsf(r[i]);
    float key = al > ar ? al : ar;
    if (scL) {
      const float sl = fabsf(scL[i]), sr = fabsf(scR[i]);
      key += (sl > sr ? sl : sr) * scW;
    }
    env_ += (key > env_ ? ca_ : cr_) * (key - env_);
    if (++hold_ >= kHold) {
      hold_ = 0;
      if (env_ < 1e-20f) env_ = 0;  // silence: no denormal tail
      const float envDb = env_ > 1e-6f ? 6.02f * fastLog2(env_) : -120.f;
      const float over = envDb - thrDb_;
      gain_ = (over > 0 ? fastExp2(-over * 0.75f / 6.02f) : 1.f) * makeup_;
    }
    l[i] *= gain_;
    r[i] *= gain_;
  }
}

}  // namespace mt
