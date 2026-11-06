#include "mod_matrix.h"
#include "voice_cost.h"
#include "hot.h"

namespace mt {
bool modDestValid(uint8_t d) {
  return d < kModTargets && !(d >= uint8_t(LfoDest::Rtrg1) && d <= uint8_t(LfoDest::Rtrg4));
}
uint8_t modDestStep(uint8_t dest, int delta, InstrType type) {
  const int dir = delta < 0 ? -1 : 1;
  for (int n = delta < 0 ? -delta : delta; n > 0; --n) {
    uint8_t next = dest;
    for (int tries = 0; tries < kModTargets; ++tries) {
      const uint8_t candidate = lfoDestStep(next, dir, type);
      if (candidate == next) break;
      next = candidate;
      if (modDestValid(next)) { dest = next; break; }
    }
  }
  return dest;
}
bool modBipolar(uint8_t s) { return (s >= uint8_t(ModSource::Lfo1) && s <= uint8_t(ModSource::Lfo4)) || s == uint8_t(ModSource::Random); }
const char* modSourceName(uint8_t s) {
  static const char* const names[] = {"OFF", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "AMP ENV", "FLT ENV", "VELOCITY", "NOTE", "RANDOM", "FOLLOWER"};
  static_assert(sizeof names / sizeof names[0] == kModSources, "source names");
  return names[s < kModSources ? s : 0];
}
namespace {
MT_INLINE float unipolar(float v, uint8_t s) { return modBipolar(s) ? (v + 1.f) * .5f : v; }
}
MT_HOT void modEvaluate(const Instrument& m, const float (&src)[kModSources], float (&out)[kModTargets],
                        uint32_t mask, const uint8_t* locks) {
  for (float& v : out) v = 0;
  for (int i = 0; i < kModRoutes; ++i) {
    const ModRoute& r = m.mod[i];
    if (!r.source || r.source >= kModSources || !modDestValid(r.dest)) continue;
    const int a = locks && (mask & (1u << (kLockMatrix0 + i))) ? fxSigned(locks[kLockMatrix0 + i]) : r.amount;
    if (!a) continue;
    float v = src[r.source];
    if (r.polarity == uint8_t(ModPolarity::Unipolar)) v = unipolar(v, r.source);
    else if (r.polarity == uint8_t(ModPolarity::Bipolar) && !modBipolar(r.source)) v = v * 2.f - 1.f;
    if (r.via && r.via < kModSources) v *= unipolar(src[r.via], r.via);
    const int amount = a < -64 ? -64 : (a > 63 ? 63 : a);
    out[r.dest] += v * (amount * (1.f / 64.f));
  }
}
uint16_t modSourceMask(const Instrument& m) {
  uint16_t mask = 0;
  for (const ModRoute& r : m.mod) {
    if (!r.source || r.source >= kModSources || !modDestValid(r.dest)) continue;
    mask |= 1u << r.source;
    if (r.via && r.via < kModSources) mask |= 1u << r.via;
  }
  return mask;
}
uint8_t modLfoMask(const Instrument& m) { return (modSourceMask(m) >> 1) & 15; }
uint8_t modCostPotential(const Instrument& m) {
  uint8_t flags = 0;
  for (const ModRoute& r : m.mod) {
    if (!r.source || r.source >= kModSources) continue;
    const auto d = static_cast<LfoDest>(r.dest);
    if (d == LfoDest::Fold) flags |= kCostPotentialFold;
    if (d == LfoDest::Ring) flags |= kCostPotentialRing;
    if (d == LfoDest::Sat) flags |= kCostPotentialSat;
    if (d == LfoDest::Reso) flags |= kCostPotentialSynLpLimit;
    if (d == LfoDest::Pitch || d == LfoDest::Fine) flags |= kCostPotentialSamplePitch;
  }
  return flags;
}
}  // namespace mt
