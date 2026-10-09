#pragma once
#include <stdint.h>
#include "hot.h"
#include "model.h"

namespace mt {

constexpr int kSynthRate = 44100;
constexpr int kWtLen = 32;
extern const int8_t kWavetable[kWavetables][kWtLen];  // -127..127

// One sine cycle + guard point, for phase-accumulator oscillators (FM, DRUM); internal RAM (DRAM).
constexpr int kSineBits = 10;
constexpr int kSineLen = 1 << kSineBits;
extern float gSine[kSineLen + 1];
// sin(2 pi x ph / 2^32), table with linear interpolation.
MT_INLINE float tableSine(uint32_t ph) {
  const uint32_t i = ph >> (32 - kSineBits);
  const float f = (ph & ((1u << (32 - kSineBits)) - 1)) * (1.f / (1u << (32 - kSineBits)));
  const float a = gSine[i];
  return a + (gSine[i + 1] - a) * f;
}

// Frequency of a (fractional) MIDI note, Hz. 69 = 440.
float noteHz(float note);

// One chip oscillator; phase in [0, 1).
struct ChipOsc {
  float phase = 0;
  uint32_t lfsr = 0x7FFF;
  float noiseVal = 1;
  // Next sample, -1..1. duty 0..1 for Pulse; inc = hz / kSynthRate.
  MT_INLINE float next(Wave w, float inc, float duty) {
    float v;
    switch (w) {
      case Wave::Pulse: v = phase < duty ? 1.f : -1.f; break;
      case Wave::Triangle: v = 4.f * (phase < 0.5f ? 0.5f - phase : phase - 0.5f) - 1.f; break;
      case Wave::Saw: v = 2.f * phase - 1.f; break;
      case Wave::Noise:
      case Wave::Metal: v = noiseVal; break;
      default: {
        int n = static_cast<int>(w) - static_cast<int>(Wave::Wt1);
        if (n >= kWavetables) n = 0;
        v = kWavetable[n][static_cast<int>(phase * kWtLen) & (kWtLen - 1)] * (1.f / 127.f);
      }
    }
    phase += inc;
    while (phase >= 1.f) {
      phase -= 1.f;
      if (w == Wave::Noise || w == Wave::Metal) stepLfsr(w == Wave::Metal ? 6 : 1);
    }
    return v;
  }

 private:
  // 15-bit LFSR as in the NES noise channel; tap 6 gives the short "metal" period (93).
  void stepLfsr(int tap) {
    const uint32_t fb = (lfsr ^ (lfsr >> tap)) & 1;
    lfsr = (lfsr >> 1) | (fb << 14);
    if (lfsr == 0) lfsr = 0x7FFF;
    noiseVal = (lfsr & 1) ? 1.f : -1.f;
  }
};

}  // namespace mt
