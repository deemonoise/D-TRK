#pragma once
#include <stdint.h>
#include "hot.h"
#include "fastmath.h"

namespace mt {
// Source-domain antialias FIR for pitch-up. Unity/downpitch retain linear interpolation.
// Q15polyphase banks through16x; higher rates unpack precomputed symmetric kernels. Source windows
// grow with rate; at128..256x the window tapers4.5->4 and cutoff.45->.4 to fit<=2049frames.
constexpr int kSampleFirTaps = 20;
constexpr int kSampleFirFirst = 1 - kSampleFirTaps / 2;
constexpr int kSampleFirLast = kSampleFirFirst + kSampleFirTaps - 1;
constexpr int kSampleFirPhases = 32;
struct SampleFirPlan {
  const int16_t *quantized = nullptr, *quantizedB = nullptr;
  uint16_t aTaps = 20, bTaps = 20, radius = 10;
  uint8_t phaseBits = 5;
  bool kernelShift = false;
  float blend = 0, wet = 1;
  bool active() const { return quantized != nullptr; }
  int first() const { return 1 - radius; }
  int last() const { return radius + kernelShift; }
  int taps() const { return 2 * radius + kernelShift; }
};
// Common rawQ15banks are immutable, byte-identical RAM2copies of flash. Call init before audio.
// High-rate plans are consumed immediately by one audio owner (4entrykernel cache).
// SignedQ32 frame step. No per-voice RAM allocation.
void sampleFirInit();
SampleFirPlan sampleFirPlan(int64_t step);
float sampleFirContiguous(const SampleFirPlan& p, uint32_t fraction, const int16_t* first);
// RAM loop/region edge: stage at most2049frames in bounded stack storage, then use the same
// SIMD convolution as an interior window. PCM has the full played[from,to)region available.
float sampleFirRam(const SampleFirPlan& p,uint32_t fraction,uint32_t center,const int16_t* pcm,
                    uint32_t from,uint32_t to,uint32_t lo,uint32_t hi,uint8_t mode,bool looped,int64_t step);
// Whole-segment fast path. All referenced source frames must lie in data[], whose first frame
// has the absolute index firstFrame. Only Q15plans; pos/step are signedQ32, out is replaced.
void sampleFirQ15Block(const SampleFirPlan& p, int64_t pos, int64_t step, const int16_t* data,
                       uint32_t firstFrame, float* out, int n);
// Reader copies rawint16 sample values into float exactly, before another streamed cache load.
// A missing required tap leaves output untouched.
template <class Reader>
MT_INLINE bool sampleFirReadQBank(const int16_t* bank, int taps, uint8_t phaseBits, uint32_t fraction,
                                  int64_t center, Reader read, float& out) {
  const unsigned shift = 32 - phaseBits, phase = fraction >> shift;
  const float t = (fraction & ((uint32_t{1} << shift) - 1)) *
                  (1.f / static_cast<float>(uint32_t{1} << shift));
  const int16_t* a = bank + phase * taps;
  const int16_t* b = a + taps;
  int32_t x = 0, y = 0;
  for (int j = 0; j < taps; ++j) {
    float value;
    if (!read(center + 1 - taps / 2 + j, value)) return false;
    const int32_t sample = static_cast<int32_t>(value);
    x += sample * a[j];
    if (t != 0) y += sample * b[j];
  }
  const float value = x * (1.f / 32768.f);
  out = t == 0 ? value : value + (y * (1.f / 32768.f) - value) * t;
  return true;
}
template <class Reader>
MT_INLINE bool sampleFirRead(const SampleFirPlan& p, uint32_t fraction, int64_t center, Reader read, float& out) {
  float filtered;
  if (p.kernelShift) {
    int32_t x=0,y=0;
    for(int j=0;j<p.aTaps;++j) {
      float a,b;
      if(!read(center+p.first()+j,a)||!read(center+p.first()+j+1,b)) return false;
      x+=static_cast<int32_t>(a)*p.quantized[j];
      y+=static_cast<int32_t>(b)*p.quantized[j];
    }
    const float a=x*(1.f/32768.f),b=y*(1.f/32768.f);
    filtered=a+(b-a)*(fraction*(1.f/4294967296.f));
    if(p.blend<1) {
      float poly;
      if(!sampleFirReadQBank(p.quantizedB,p.bTaps,5,fraction,center,read,poly)) return false;
      filtered=poly+(filtered-poly)*p.blend;
    }
  } else {
    float a, b;
    if (!sampleFirReadQBank(p.quantized,p.aTaps,p.phaseBits,fraction,center,read,a)) return false;
    if (p.blend == 0) filtered = a;
    else {
      if (!sampleFirReadQBank(p.quantizedB,p.bTaps,p.phaseBits,fraction,center,read,b)) return false;
      filtered = a + (b - a) * p.blend;
    }
  }
  if (p.wet < 1) {
    float a, b;
    if (!read(center, a) || !read(center + 1, b)) return false;
    const float linear = a + (b - a) * (fraction * (1.f / 4294967296.f));
    filtered = linear + (filtered - linear) * p.wet;
  }
  out = filtered;
  return true;
}
uint32_t sampleFirFrame(int64_t frame, uint32_t from, uint32_t to, uint32_t lo, uint32_t hi,
                        uint8_t mode, bool looped, int64_t step);
bool sampleFirInterior(const SampleFirPlan& p, uint32_t center, uint32_t from, uint32_t to,
                        uint32_t lo, uint32_t hi, uint8_t mode, bool looped, int64_t step);
MT_INLINE bool sampleFirInterior(uint32_t center, uint32_t from, uint32_t to, uint32_t lo, uint32_t hi,
                                 uint8_t mode, bool looped, int64_t step) {
  return sampleFirInterior(SampleFirPlan{},center,from,to,lo,hi,mode,looped,step);
}
}  // namespace mt
