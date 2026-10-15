#include "synth_eq.h"
#include <math.h>
#include "hot.h"

namespace mt {
namespace {
float lin(uint8_t v) { return powf(10.f, MasterEq::db(v) * (1.f / 20.f)); }
// Boost: corner fixed. Cut (a < 1): low corner / a, high corner x a (inverse of the boost).
float lowCoef(float a) { return OnePoleTpt::coef(a < 1.f ? MasterEq::kLowHz / a : MasterEq::kLowHz); }
float highCoef(float a) { return OnePoleTpt::coef(a < 1.f ? MasterEq::kHighHz * a : MasterEq::kHighHz); }
}  // namespace

float MasterEq::db(uint8_t v) {
  if (v > 127) v = 127;
  return v >= 64 ? (v - 64) * (12.f / 63.f) : (v - 64) * (12.f / 64.f);
}

void MasterEq::reset() {
  for (int c = 0; c < 2; ++c) lo_[c].s = hi_[c].s = 0;
  gL_ = gH_ = tgtL_ = tgtH_ = 0;
  cL_ = tcL_ = lowCoef(1.f);
  cH_ = tcH_ = highCoef(1.f);
  low_ = high_ = 64;
}

// The loop, inlined into process (IRAM); a band off at compile time is skipped.
template <bool kLo, bool kHi>
MT_INLINE void MasterEq::run(float* l, float* r, int n) {
  const float inv = n == 128 ? 1.f / 128 : fastRcp(static_cast<float>(n));
  const float dL = (tgtL_ - gL_) * inv, dH = (tgtH_ - gH_) * inv;
  const float dcL = (tcL_ - cL_) * inv, dcH = (tcH_ - cH_) * inv;
  float gL = gL_, gH = gH_, cL = cL_, cH = cH_;
  OnePoleTpt l0 = lo_[0], l1 = lo_[1], h0 = hi_[0], h1 = hi_[1];
  for (int i = 0; i < n; ++i) {
    const float xl = l[i], xr = r[i];
    float yl = xl, yr = xr;
    if (kLo) {
      gL += dL;
      cL += dcL;
      yl += gL * l0.lp(xl, cL);
      yr += gL * l1.lp(xr, cL);
    }
    if (kHi) {
      gH += dH;
      cH += dcH;
      yl += gH * (xl - h0.lp(xl, cH));
      yr += gH * (xr - h1.lp(xr, cH));
    }
    l[i] = yl;
    r[i] = yr;
  }
  l0.flush(), l1.flush(), h0.flush(), h1.flush();
  lo_[0] = l0, lo_[1] = l1, hi_[0] = h0, hi_[1] = h1;
}

// A LOW / HIGH change: the targets (flash, per parameter change).
void MasterEq::retarget(uint8_t low, uint8_t high) {
  if (low != low_) {
    low_ = low;
    const float a = lin(low);
    tgtL_ = low == 64 ? 0.f : a - 1.f;
    tcL_ = lowCoef(a);
    if (gL_ == 0.f) cL_ = tcL_;  // from flat: no corner sweep
  }
  if (high != high_) {
    high_ = high;
    const float a = lin(high);
    tgtH_ = high == 64 ? 0.f : a - 1.f;
    tcH_ = highCoef(a);
    if (gH_ == 0.f) cH_ = tcH_;
  }
}

MT_HOT void MasterEq::process(float* l, float* r, int n, uint8_t low, uint8_t high) {
  if (n <= 0) return;
  if (low != low_ || high != high_) retarget(low, high);
  if (gL_ == 0.f && gH_ == 0.f && tgtL_ == 0.f && tgtH_ == 0.f) {
    // Flat: states restart from silence when a shelf comes back (its gain ramps from 0).
    lo_[0].s = lo_[1].s = hi_[0].s = hi_[1].s = 0;
    return;
  }
  // A band flat for the whole block (gain 0 to 0) is skipped; its states restart from silence when
  // it comes back (its gain ramps from 0), as both do after a flat block.
  const bool lo = gL_ != 0.f || tgtL_ != 0.f, hi = gH_ != 0.f || tgtH_ != 0.f;
  if (!lo) lo_[0].s = lo_[1].s = 0;
  if (!hi) hi_[0].s = hi_[1].s = 0;
  // Both bands always (one IRAM copy of the loop): a flat band adds 0 x its shelf, exactly
  // nothing, and its states restart from silence again after the block.
  run<true, true>(l, r, n);
  if (!lo) lo_[0].s = lo_[1].s = 0;
  if (!hi) hi_[0].s = hi_[1].s = 0;
  gL_ = tgtL_;  // exact at the block end (no float drift)
  gH_ = tgtH_;
  cL_ = tcL_;
  cH_ = tcH_;
}


}  // namespace mt
