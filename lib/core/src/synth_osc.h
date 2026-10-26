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
// sin(2 pi x ph), ph in 0 .. 4 (whole turns wrap). ph x kSineLen is exact (a power of 2), so its int
// part and fraction are tableSine's index and fraction of the fixed-point phase (the same value) without
// the shifts and the second register transfer.
MT_INLINE float phaseSine(float ph) {
  const float x = ph * static_cast<float>(kSineLen);
  const int k = static_cast<int>(x);
  const float* s = gSine + (k & (kSineLen - 1));
  return s[0] + (s[1] - s[0]) * (x - static_cast<float>(k));
}

// Frequency of a (fractional) MIDI note, Hz. 69 = 440.
float noteHz(float note);

// AY-3-8910 volume / envelope levels: 16 steps 1.5 dB apart, 0 = silence (a formula, not measured data).
constexpr float ayLevel(int n) {
  float a = 1.f;
  for (int i = n; i < 15; ++i) a *= 0.8413951f;  // 10^(-1.5 / 20)
  return n == 0 ? 0.f : a;
}
constexpr float kAyLevel[16] = {ayLevel(0),  ayLevel(1),  ayLevel(2),  ayLevel(3),  ayLevel(4),  ayLevel(5),
                                ayLevel(6),  ayLevel(7),  ayLevel(8),  ayLevel(9),  ayLevel(10), ayLevel(11),
                                ayLevel(12), ayLevel(13), ayLevel(14), ayLevel(15)};
constexpr float ayLevelMean() {
  float s = 0;
  for (float a : kAyLevel) s += a;
  return s / 16.f;
}
// Mid-points between neighbouring AY levels (kAyMid[15]: above any envelope, never reached by ayStep).
constexpr float ayMid(int n) { return n < 15 ? (kAyLevel[n] + kAyLevel[n + 1]) * 0.5f : 2.f; }
constexpr float kAyMid[16] = {ayMid(0), ayMid(1), ayMid(2),  ayMid(3),  ayMid(4),  ayMid(5),  ayMid(6),  ayMid(7),
                              ayMid(8), ayMid(9), ayMid(10), ayMid(11), ayMid(12), ayMid(13), ayMid(14), ayMid(15)};
// CHIP STEP: envelope 0..1 -> the nearest AY level; binary search over the mid-points, 4 compares.
MT_INLINE float ayStep(float e) {
  int i = (e >= kAyMid[7]) * 8;
  i += (e >= kAyMid[i + 3]) * 4;
  i += (e >= kAyMid[i + 1]) * 2;
  i += (e >= kAyMid[i]);
  return kAyLevel[i];
}
// AY SAW / TRI (the envelope as oscillator) use every level equally often: DC-free with the mean, peak 1.
constexpr float kAyBuzzMean = ayLevelMean();
constexpr float kAyBuzzGain = 1.f / (1.f - kAyBuzzMean);

// One chip oscillator; phase in [0, 1).
struct ChipOsc {
  float phase = 0;
  uint32_t lfsr = 0x7FFF;
  float noiseVal = 1;
  float nphase = 0;  // AY TN: the noise clock, 8x the tone
  // AY TN levels: square AND noise is high 25 % of the time; DC-free, 0.6 x PULSE's RMS (fits +-1.05).
  static constexpr float kAyTnHigh = 1.0392305f;
  static constexpr float kAyTnLow = -0.3464102f;
  // Next sample, -1..1. duty 0..1 for Pulse; inc = hz / kSynthRate.
  MT_INLINE float next(Wave w, float inc, float duty) {
    float v;
    switch (w) {
      case Wave::Pulse: v = phase < duty ? 1.f : -1.f; break;
      case Wave::Triangle: v = 4.f * (phase < 0.5f ? 0.5f - phase : phase - 0.5f) - 1.f; break;
      case Wave::Saw: v = 2.f * phase - 1.f; break;
      case Wave::Noise:
      case Wave::Metal:
      case Wave::SidNoise:
      case Wave::AyNoise: v = noiseVal; break;
      case Wave::AySaw:  // 16 steps down
        v = (kAyLevel[15 - (static_cast<int>(phase * 16.f) & 15)] - kAyBuzzMean) * kAyBuzzGain;
        break;
      case Wave::AyTri: {  // 32 steps: down 15..0, then up 0..15
        const int s = static_cast<int>(phase * 32.f) & 31;
        v = (kAyLevel[s < 16 ? 15 - s : s - 16] - kAyBuzzMean) * kAyBuzzGain;
        break;
      }
      case Wave::AyToneNoise: v = (phase < duty && noiseVal > 0.f) ? kAyTnHigh : kAyTnLow; break;
      case Wave::SidTs:
      case Wave::SidPs:
      case Wave::SidPt:
      case Wave::SidPts: v = sidCombined(w, duty); break;
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
      else if (w == Wave::SidNoise) stepSid();
      else if (w == Wave::AyNoise) stepAy();
    }
    if (w == Wave::AyToneNoise) {  // its noise runs at 8x the tone, own phase
      nphase += inc * 8.f;
      while (nphase >= 1.f) {
        nphase -= 1.f;
        stepAy();
      }
    }
    return v;
  }

 private:
  // SID combined waves (TS PS PT PTS), per wave: DC of the raw AND over one cycle at duty 0.5, and the
  // gain that keeps the worst peak over every duty (pulse fully on / off, all 4096 phases) at 1
  // (test_chip_waves). The waves are spiky: at duty 0.5 their RMS is 9..16 dB under PULSE, as on the
  // chip - matching PULSE's RMS would clip.
  static constexpr float kSidMean[4] = {-0.6876679f, -0.8438339f, -0.6876679f, -0.9219780f};
  static constexpr float kSidGain[4] = {0.5927052f, 0.5423482f, 0.5927052f, 1.0854871f};
  // The 12-bit parts ANDed, then a set bit survives only next to another set bit (a cheap stand-in for
  // the chip's pull-down; no reSID data).
  MT_INLINE float sidCombined(Wave w, float duty) const {
    const uint32_t saw = static_cast<uint32_t>(phase * 4096.f) & 0xFFF;
    const uint32_t tri = ((saw & 0x800 ? ~saw : saw) << 1) & 0xFFF;  // MSB-folded saw, as the chip
    const uint32_t pul = phase < duty ? 0xFFF : 0;
    uint32_t c;
    switch (w) {
      case Wave::SidTs: c = saw & (saw << 1); break;  // unfolded tri: spikes in both halves, unlike PTS
      case Wave::SidPs: c = pul & saw; break;
      case Wave::SidPt: c = pul & tri; break;
      default: c = pul & tri & saw; break;
    }
    c &= (c << 1) | (c >> 1);
    const int k = static_cast<int>(w) - static_cast<int>(Wave::SidTs);
    return (static_cast<float>(c) * (1.f / 2047.5f) - 1.f - kSidMean[k]) * kSidGain[k];
  }
  // 15-bit LFSR as in the NES noise channel; tap 6 gives the short "metal" period (93).
  void stepLfsr(int tap) {
    const uint32_t fb = (lfsr ^ (lfsr >> tap)) & 1;
    lfsr = (lfsr >> 1) | (fb << 14);
    if (lfsr == 0) lfsr = 0x7FFF;
    noiseVal = (lfsr & 1) ? 1.f : -1.f;
  }
  // SID: 23 bits, taps 22 ^ 17 (period 2^23 - 1), seed 0x7FFFF8. The output is 8 register bits
  // (20 18 14 11 9 5 2 0 -> 7..0), so it is multi-level. lfsr holds the register with bit 23 set as a
  // marker: a state left by NES / AY noise has none, so the first SID step after a wave change re-seeds.
  void stepSid() {
    constexpr uint32_t kMark = 0x800000;
    if (!(lfsr & kMark)) lfsr = kMark | 0x7FFFF8;
    const uint32_t r = lfsr;
    const uint32_t s = ((r << 1) | (((r >> 22) ^ (r >> 17)) & 1)) & 0x7FFFFF;
    lfsr = kMark | s;
    const uint32_t o = ((s >> 13) & 0x80) | ((s >> 12) & 0x40) | ((s >> 9) & 0x20) | ((s >> 7) & 0x10) |
                       ((s >> 6) & 0x08) | ((s >> 3) & 0x04) | ((s >> 1) & 0x02) | (s & 0x01);
    noiseVal = static_cast<float>(o) * (1.f / 127.5f) - 1.f;
  }
  // AY / YM: 17 bits, feedback bit 0 ^ bit 3, output bit 0.
  void stepAy() {
    const uint32_t fb = (lfsr ^ (lfsr >> 3)) & 1;
    lfsr = ((lfsr >> 1) | (fb << 16)) & 0x1FFFF;
    if (lfsr == 0) lfsr = 1;
    noiseVal = (lfsr & 1) ? 1.f : -1.f;
  }
};

}  // namespace mt
