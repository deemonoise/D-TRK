#include "battery_gauge.h"

#include <cmath>

namespace mt {
namespace {

struct OcvPoint {
  float v;
  int pct;
};

// Typical LiPo resting voltage per charge level.
constexpr OcvPoint kOcv[] = {
    {3.30f, 0},  {3.50f, 5},  {3.61f, 10}, {3.69f, 20}, {3.74f, 30},  {3.77f, 40},
    {3.80f, 50}, {3.84f, 60}, {3.90f, 70}, {3.98f, 80}, {4.06f, 90}, {4.18f, 100},
};
constexpr int kOcvN = sizeof(kOcv) / sizeof(kOcv[0]);

int roundPos(float x) { return static_cast<int>(x + 0.5f); }

}  // namespace

int lipoPercent(float v) {
  if (!(v > kOcv[0].v)) return kOcv[0].pct;  // also NaN
  if (v >= kOcv[kOcvN - 1].v) return kOcv[kOcvN - 1].pct;
  int i = 1;
  while (v > kOcv[i].v) ++i;
  const OcvPoint& a = kOcv[i - 1];
  const OcvPoint& b = kOcv[i];
  return roundPos(a.pct + (b.pct - a.pct) * (v - a.v) / (b.v - a.v));
}

void BatteryGauge::update(float volts, float amps, float dtS) {
  if (!std::isfinite(volts) || !std::isfinite(amps) || !std::isfinite(dtS)) return;
  if (dtS < 0.f) dtS = 0.f;
  if (dtS > 3600.f) dtS = 3600.f;
  const float ocv = volts + amps * kRint;  // undo the sag (or the rise while charging)
  if (!started_) {
    started_ = true;
    ocv_ = ocv;
    amps_ = amps;
  } else {
    age_ += dtS;
    ocv_ += (ocv - ocv_) * dtS / (kTauS + dtS);
    amps_ += (amps - amps_) * dtS / (kTauIS + dtS);
  }
  const int p = lipoPercent(ocv_);
  const bool chg = charging();
  const bool warm = age_ < kWarmS;
  // Warm-up or charging: follow the filter. Discharging: only ever down.
  pct_ = (warm || chg || p < pct_) ? p : pct_;

  // Charging re-arms; > kRearmPct is a safety net (the shown percent cannot rise while discharging).
  if (chg || pct_ > kRearmPct) {
    armed_ = true;
  } else if (!warm && armed_ && pct_ <= kLowPct) {
    alert_ = true;
    armed_ = false;
  }
}

bool BatteryGauge::takeLowAlert() {
  const bool a = alert_;
  alert_ = false;
  return a;
}

}  // namespace mt
