#include "wt_mip.h"
#include <math.h>

namespace mt {
namespace {

// Float only: the ESP32-S3 FPU is single precision, soft-double trig would cost ~1 us per
// call. With the tables below one import (64 frames x 2 wtBuild passes; 2048-point frames)
// is ~70 M float MACs, ~0.5-1 s on the S3 at 240 MHz; synthesis of all levels ~10 M.
constexpr int kSynLen = kWtFrameLen;  // synthesis sine table (level lengths divide it)
constexpr int kAnaLen = 4096;         // analysis sine table (power-of-two frames <= 4096)
float gSynSin[kSynLen];
float gAnaSin[kAnaLen];
struct TabInit {
  TabInit() {
    for (int i = 0; i < kSynLen; ++i) gSynSin[i] = static_cast<float>(sin(6.283185307179586 * i / kSynLen));
    for (int i = 0; i < kAnaLen; ++i) gAnaSin[i] = static_cast<float>(sin(6.283185307179586 * i / kAnaLen));
  }
} tabInit;

constexpr bool isPow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

// Level k of one frame from its spectrum, float. Level lengths are powers of two <= kSynLen.
void synthLevel(const float* re, const float* im, int k, float* y) {
  const int len = wtLevelLen(k);
  const int top = kWtHarm >> k;
  const int step = kSynLen / len;
  for (int n = 0; n < len; ++n) {
    float acc = 0;
    const int base = n * step;
    for (int h = 1; h <= top; ++h) {
      const int i = (h * base) & (kSynLen - 1);
      acc += re[h - 1] * gSynSin[(i + kSynLen / 4) & (kSynLen - 1)] + im[h - 1] * gSynSin[i];
    }
    y[n] = acc;
  }
}

}  // namespace

void wtAnalyze(const int16_t* x, int len, float* re, float* im) {
  const bool tab = isPow2(len) && len <= kAnaLen;
  for (int h = 1; h <= kWtHarm; ++h) {
    re[h - 1] = im[h - 1] = 0;
    if (2 * h > len) continue;
    float c = 0, s = 0;
    if (tab) {
      const int step = h * (kAnaLen / len);
      for (int n = 0; n < len; ++n) {
        const int i = (n * step) & (kAnaLen - 1);
        c += x[n] * gAnaSin[(i + kAnaLen / 4) & (kAnaLen - 1)];
        s += x[n] * gAnaSin[i];
      }
    } else {
      // Rotation recurrence, reseeded exactly every 64 points to bound float drift.
      const float da = 6.2831853f * h / len;
      const float dc = cosf(da), ds = sinf(da);
      float rc = 1, rs = 0;
      for (int n = 0; n < len; ++n) {
        if ((n & 63) == 0) {
          const float a = 6.2831853f * static_cast<float>((static_cast<int64_t>(h) * n) % len) / len;
          rc = cosf(a);
          rs = sinf(a);
        }
        c += x[n] * rc;
        s += x[n] * rs;
        const float t = rc * dc - rs * ds;
        rs = rs * dc + rc * ds;
        rc = t;
      }
    }
    const float g = (2 * h == len ? 1.0f : 2.0f) / len;
    re[h - 1] = c * g;
    im[h - 1] = s * g;
  }
}

bool wtBuild(WtSpectrumSource& src, int16_t* out) {
  float re[kWtHarm], im[kWtHarm], y[kWtFrameLen];
  float peak = 0;
  for (int f = 0; f < kWtFrames; ++f) {
    src.spectrum(f, re, im);
    for (int k = 0; k < kWtLevels; ++k) {
      synthLevel(re, im, k, y);
      for (int n = 0; n < wtLevelLen(k); ++n) peak = fabsf(y[n]) > peak ? fabsf(y[n]) : peak;
    }
  }
  if (peak < 1e-6f) return false;
  const float g = kWtPeak / peak;
  for (int f = 0; f < kWtFrames; ++f) {
    src.spectrum(f, re, im);
    for (int k = 0; k < kWtLevels; ++k) {
      synthLevel(re, im, k, y);
      int16_t* d = out + f * kWtFramePts + wtLevelOff(k);
      for (int n = 0; n < wtLevelLen(k); ++n) d[n] = static_cast<int16_t>(lrintf(y[n] * g));
    }
  }
  return true;
}

void wtLevel0(const int16_t* table, int16_t* src) {
  for (int f = 0; f < kWtFrames; ++f)
    for (int n = 0; n < kWtFrameLen; ++n) src[f * kWtFrameLen + n] = table[f * kWtFramePts + n];
}

}  // namespace mt
