#include "synth_comp.h"
#include "fastmath.h"
#include "hot.h"
#include "synth_osc.h"

namespace mt {
namespace {
constexpr float kDbPerOctave = 6.0205999f;
constexpr int kTransition = (kSynthRate * 5 + 999) / 1000;
constexpr float kTransitionStep = 1.f / kTransition;

MT_INLINE float approach(float value, float target) {
  return value < target ? minf(value + kTransitionStep, target)
                        : maxf(value - kTransitionStep, target);
}
MT_INLINE float envelope(float level, float previous, float attack, float release) {
  return previous + (level > previous ? attack : release) * (level - previous);
}
MT_INLINE float reduction(float env, float threshold, float slope) {
  const float level = env > 1e-6f ? kDbPerOctave * fastLog2(env) : -120.f;
  const float over = level - threshold;
  if (over <= -3.f) return 0;
  if (over >= 3.f) return slope * over;
  const float knee = over + 3.f;
  return slope * knee * knee * (1.f / 12.f);
}
}  // namespace

// Out of line: an IRAM function inline in the header breaks the ESP32 link (l32r literals).
MT_HOT void Compressor::process(float* l, float* r, const float* scL, const float* scR, int n, uint8_t amt,
                                uint8_t rel, uint8_t scDepth, uint8_t attack) {
  if (n <= 0) return;
  if (amt == 0 && wet_ == 0) {
    reset();
    return;
  }
  const uint8_t amount = amt > 127 ? 127 : amt;
  const uint8_t release = rel > 127 ? 127 : rel;
  const uint8_t atk = attack > 127 ? 127 : attack;
  if (amount > 0 && amount != amt_) {
    amt_ = amount;
    const float a = amount * (1.f / 127.f);
    thrDb_ = -18.f * a;
    slope_ = 1.f - 1.f / (1.f + 3.f * a);
    parameterLeft_ = kTransition;
    controlLeft_ = 0;
  }
  if (release != rel_ || atk != attack_) {
    rel_ = release;
    attack_ = atk;
    const float attackMs = .5f * powf(100.f, atk * (1.f / 127.f));
    const float releaseMs = 20.f * powf(50.f, release * (1.f / 127.f));
    ca_ = 1.f - expf(-1.f / (attackMs * .001f * kSynthRate));
    cr_ = 1.f - expf(-1.f / (releaseMs * .001f * kSynthRate));
  }
  const float targetDepth = (scDepth > 127 ? 127 : scDepth) * (1.f / 127.f);
  const float targetSc = scL && targetDepth > 0 ? 1.f : 0.f;
  const float targetWet = amount > 0 ? 1.f : 0.f;
  for (int i = 0; i < n; ++i) {
    wet_ = approach(wet_, targetWet);
    if (wet_ == 0) {
      reset();  // bypass forgets the old reduction; the untouched remainder is bit-exact
      break;
    }
    scBlend_ = approach(scBlend_, targetSc);
    depth_ = approach(depth_, targetDepth);
    const float sl = scL ? scL[i] : 0, sr = scR ? scR[i] : 0;
    const float al = fabsf(l[i]), ar = fabsf(r[i]);
    const float asl = fabsf(sl), asr = fabsf(sr);
    envMix_ = envelope(al > ar ? al : ar, envMix_, ca_, cr_);
    envSc_ = envelope(asl > asr ? asl : asr, envSc_, ca_, cr_);
    if (controlLeft_ == 0) {
      if (envMix_ < 1e-20f) envMix_ = 0;
      if (envSc_ < 1e-20f) envSc_ = 0;
      const float mixReduction = scBlend_ < 1 ? reduction(envMix_, thrDb_, slope_) : 0;
      const float scReduction = scBlend_ > 0 ? reduction(envSc_, thrDb_, slope_) : 0;
      const float db = mixReduction * (1.f - scBlend_) + scReduction * scBlend_ * depth_;
      const float target = fastExp2(-db / kDbPerOctave);
      const float reciprocalSpan = parameterLeft_ > kHold ? fastRcp(static_cast<float>(parameterLeft_))
                                                         : 1.f / kHold;
      gainStep_ = (target - rawGain_) * reciprocalSpan;
      intervalTarget_ = rawGain_ + gainStep_ * kHold;
      controlLeft_ = kHold;
    }
    rawGain_ += gainStep_;
    if (--controlLeft_ == 0) rawGain_ = intervalTarget_;
    if (parameterLeft_ > 0) --parameterLeft_;
    gain_ = 1.f + wet_ * (rawGain_ - 1.f);
    const float keyGain = (1.f - gain_) * scBlend_;
    l[i] = l[i] * gain_ + sl * keyGain;
    r[i] = r[i] * gain_ + sr * keyGain;
  }
}
}  // namespace mt
