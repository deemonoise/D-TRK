#include "wt_file.h"
#include <math.h>
#include "wt_mip.h"

namespace mt {

WtErr wtDetect(uint32_t n, uint16_t clm, WtFormat& out) {
  int len = 0;
  if (clm && n % clm == 0) len = clm;
  else if (n % kWtFrameLen == 0 && n / kWtFrameLen <= static_cast<uint32_t>(kWtFrames)) len = kWtFrameLen;
  else if (n % 2048 == 0) len = 2048;
  if (!len || n == 0 || n > kWtMaxSrcSamples) return WtErr::BadLength;
  out.frameLen = len;
  out.frames = static_cast<int>(n / len);
  return WtErr::Ok;
}

namespace {

// Output frame j: picked source frame (n > 64) or spectra interpolated between neighbours.
struct FileSrc final : WtSpectrumSource {
  const int16_t* d;
  WtFormat f;
  void spectrum(int j, float* re, float* im) override {
    if (f.frames == 1) {
      wtAnalyze(d, f.frameLen, re, im);
      return;
    }
    const double pos = j * (f.frames - 1) / double(kWtFrames - 1);
    if (f.frames > kWtFrames) {
      wtAnalyze(d + static_cast<long>(lrint(pos)) * f.frameLen, f.frameLen, re, im);
      return;
    }
    const int a = static_cast<int>(pos);
    const int b = a + 1 < f.frames ? a + 1 : a;
    const float t = static_cast<float>(pos - a);
    float re2[kWtHarm], im2[kWtHarm];
    wtAnalyze(d + a * f.frameLen, f.frameLen, re, im);
    wtAnalyze(d + b * f.frameLen, f.frameLen, re2, im2);
    for (int h = 0; h < kWtHarm; ++h) {
      re[h] += (re2[h] - re[h]) * t;
      im[h] += (im2[h] - im[h]) * t;
    }
  }
};

}  // namespace

WtErr wtImport(const int16_t* mono, uint32_t n, uint16_t clm, int16_t* table) {
  FileSrc s;
  s.d = mono;
  const WtErr e = wtDetect(n, clm, s.f);
  if (e != WtErr::Ok) return e;
  return wtBuild(s, table) ? WtErr::Ok : WtErr::Silent;
}

}  // namespace mt
