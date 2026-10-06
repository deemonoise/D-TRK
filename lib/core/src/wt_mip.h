#pragma once
#include <stdint.h>

namespace mt {

// Mip-mapped wavetable: kWtFrames frames, each stored at kWtLevels band-limited levels.
// Level k keeps harmonics 1..(kWtHarm >> k) in wtLevelLen(k) points. Frame-major layout:
// table[f * kWtFramePts + wtLevelOff(k) + i]. Level 0 of all frames is the canonical source.
constexpr int kWtFrames = 64;
constexpr int kWtFrameLen = 256;
constexpr int kWtHarm = kWtFrameLen / 2;
constexpr int kWtLevels = 8;
constexpr int kWtSrcSamples = kWtFrames * kWtFrameLen;
constexpr int kWtPeak = 30000;  // normalized peak over the whole table
constexpr int wtLevelLen(int k) { return (kWtFrameLen >> k) < 64 ? 64 : (kWtFrameLen >> k); }
constexpr int wtLevelOff(int k) { return k <= 0 ? 0 : wtLevelOff(k - 1) + wtLevelLen(k - 1); }
constexpr int kWtFramePts = wtLevelOff(kWtLevels);
constexpr int kWtTableSamples = kWtFrames * kWtFramePts;
static_assert(kWtFramePts == 768, "layout");

// Harmonics 1..kWtHarm of frame f (index h - 1): x(n) = sum re cos(2 pi h n / N) + im sin(...).
struct WtSpectrumSource {
  virtual void spectrum(int f, float* re, float* im) = 0;
};

// DFT of one cycle of len points (len >= 2): harmonics 1..min(kWtHarm, len / 2), the rest 0.
// The Nyquist bin (h = len / 2) gets 1 / len, the others 2 / len. DC is dropped.
void wtAnalyze(const int16_t* x, int len, float* re, float* im);
// All frames and levels into out (kWtTableSamples), scaled so the table peak is kWtPeak.
// Two passes over src (peak, then write). False if the table is silent (out untouched).
bool wtBuild(WtSpectrumSource& src, int16_t* out);
// Level 0 of every frame (the canonical source) into src (kWtSrcSamples).
void wtLevel0(const int16_t* table, int16_t* src);

}  // namespace mt
