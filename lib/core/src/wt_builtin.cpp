#include "wt_builtin.h"
#include <math.h>
#include <strings.h>

namespace mt {
namespace {

constexpr float kPi = 3.14159265f;
constexpr int kTimeLen = 2048;   // one cycle for the time-domain built-ins
constexpr float kTimeAmp = 20000;
const char* const kNames[kWtBuiltins] = {"*SAWSQR", "*PWM", "*SINSAW", "*TRISQR",
                                         "*FORMANT", "*ORGAN", "*SYNC", "*BELL"};
constexpr int kOrganHarm[6] = {1, 2, 3, 4, 6, 8};
// Time-domain cycle; static to keep 4 KB off the (UI / boot) task stack. Not reentrant.
int16_t gCycle[kTimeLen];

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

void analyzeCycle(float* re, float* im) { wtAnalyze(gCycle, kTimeLen, re, im); }

}  // namespace

const char* wtBuiltinName(int i) { return i >= 0 && i < kWtBuiltins ? kNames[i] : nullptr; }

int wtBuiltinFind(const char* name) {
  if (!name) return -1;
  for (int i = 0; i < kWtBuiltins; ++i)
    if (strcasecmp(name, kNames[i]) == 0) return i;
  return -1;
}

void WtBuiltinSrc::spectrum(int f, float* re, float* im) {
  const float t = f / 63.0f;
  for (int h = 1; h <= kWtHarm; ++h) re[h - 1] = im[h - 1] = 0;
  switch (id) {
    case 0:  // saw -> square: even harmonics fade out
      for (int h = 1; h <= kWtHarm; ++h) im[h - 1] = ((h & 1) ? 1 : 1 - t) / h;
      break;
    case 1: {  // pulse, width 0.5 -> 0.05
      const float w = 0.5f - 0.45f * t;
      for (int h = 1; h <= kWtHarm; ++h) re[h - 1] = sinf(kPi * h * w) / h;
      break;
    }
    case 2:  // sine -> saw: harmonics enter one by one
      for (int h = 1; h <= kWtHarm; ++h) im[h - 1] = clamp01(t * 127 - (h - 1) + 1) / h;
      break;
    case 3:  // triangle -> square, odd harmonics only
      for (int h = 1; h <= kWtHarm; h += 2) {
        const float tri = (((h - 1) / 2) & 1 ? -1.0f : 1.0f) / (h * h);
        im[h - 1] = tri + (1.0f / h - tri) * t;
      }
      break;
    case 4: {  // fundamental + a formant peak sweeping harmonics 2..32
      const float c = 2 + 30 * t;
      for (int h = 1; h <= kWtHarm; ++h) {
        const float d = (h - c) / 3;
        im[h - 1] = (h == 1 ? 0.3f : 0.0f) + expf(-d * d);
      }
      break;
    }
    case 5:  // drawbars {1,2,3,4,6,8}, weights rotating with the frame
      for (int k = 0; k < 6; ++k) {
        const int h = kOrganHarm[k];
        const float w = 0.5f + 0.5f * cosf(2 * kPi * (t + k / 6.0f));
        im[h - 1] = w / sqrtf(static_cast<float>(h));
      }
      break;
    case 6: {  // hard-synced saw, slave ratio 1 -> 8
      const float r = 1 + 7 * t;
      for (int n = 0; n < kTimeLen; ++n) {
        const float p = r * n / kTimeLen;
        gCycle[n] = static_cast<int16_t>(lrintf(kTimeAmp * (2 * (p - floorf(p)) - 1)));
      }
      analyzeCycle(re, im);
      break;
    }
    case 7: {  // 1:3 FM, index 0 -> 4
      const float ix = 4 * t;
      for (int n = 0; n < kTimeLen; ++n) {
        const float a = 2 * kPi * n / kTimeLen;
        gCycle[n] = static_cast<int16_t>(lrintf(kTimeAmp * sinf(a + ix * sinf(3 * a))));
      }
      analyzeCycle(re, im);
      break;
    }
    default:
      break;
  }
}

}  // namespace mt
