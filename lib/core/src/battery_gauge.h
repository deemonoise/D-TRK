#pragma once

namespace mt {

// Percent 0..100 of a LiPo cell at rest (open-circuit voltage), linear between table points.
int lipoPercent(float ocvVolts);

// Battery percent from INA226 samples: cell voltage plus current (+ discharge, - charge).
// The voltage is corrected for sag under load, smoothed, and while discharging the shown percent
// never goes up (so a load drop does not make it jump back). For the first kWarmS seconds the
// shown percent follows the filter both ways and no low alert fires, so a bad first sample (boot
// under a load surge) does not latch a low reading.
// With the R100 shunt the charging current clips at -0.82 A, so the IR correction is too small
// while charging and the percent reads high (100 % early in CV). Acceptable.
class BatteryGauge {
 public:
  static constexpr float kRint = 0.15f;      // cell + wiring, ohm
  static constexpr float kChargeA = -0.02f;  // current below this = charging
  static constexpr float kTauS = 30.f;       // voltage smoothing
  static constexpr float kTauIS = 5.f;       // current smoothing
  static constexpr float kWarmS = 60.f;      // warm-up after the first sample
  static constexpr int kLowPct = 10, kRearmPct = 15;

  // One sample: volts at the cell, amps (+ discharge, - charge), seconds since the previous one.
  // The first sample initialises the filters. Non-finite samples are ignored; dtS is clamped to
  // 0..3600.
  void update(float volts, float amps, float dtS);
  int percent() const { return pct_; }  // -1 before the first sample
  bool charging() const { return started_ && amps_ < kChargeA; }
  bool takeLowAlert();  // true once per crossing; clears itself

 private:
  bool started_ = false;
  float ocv_ = 0.f;   // filtered open-circuit voltage
  float amps_ = 0.f;  // filtered current
  float age_ = 0.f;   // seconds since the first sample
  int pct_ = -1;      // shown percent
  bool armed_ = true;
  bool alert_ = false;
};

}  // namespace mt
