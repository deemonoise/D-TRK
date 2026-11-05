#pragma once
#include <stdint.h>
#include "hot.h"
namespace mt {
// Per-voice oscillator-mix saturation, before the filter and amplitude envelope.
// Technical curve fits; no sampled Virus waves or factory audio in the firmware.
class SynSat {
 public:
  MT_CONTROL void set(uint8_t type, uint8_t level);
  bool on() const { return type_ != 0; }
  void reset() { x1_ = dc_ = 0; }
  void processBlock(float* x, int n);
 private:
  uint8_t type_ = 0, level_ = 255;
  float g_ = 1, bias_ = 0, scale_ = 1;
  float x1_ = 0, dc_ = 0;
};
// Analog filter's input drive and output compensation, at control rate.
MT_CONTROL void synAnalogDrive(float level, float& drive, float& gain);
}
