#include "synth_resample.h"
#include "fastmath.h"
#include "model.h"
#include <string.h>
#include <assert.h>

namespace mt {
struct SampleFirBank { const int16_t* data; uint16_t taps; };
struct SampleFirKernelBank { const int16_t* half; uint16_t radius; };
#include "synth_resample_table.inc"

namespace {
constexpr int kCommonBanks=12,kCommonMaxTaps=24;
MT_RAM2 int16_t commonCoefficients[kCommonBanks*(kSampleFirPhases+1)*kCommonMaxTaps];
const int16_t* commonPointers[kCommonBanks];
bool commonInitialized=false;
uint32_t cacheAge=0;
static_assert(sizeof kSampleFirBand11<=33*kCommonMaxTaps*sizeof(int16_t),"common coefficient cache capacity");
constexpr int kKernelCacheEntries=4,kKernelMaxTaps=2048;
MT_RAM2 int16_t kernelCache[kKernelCacheEntries][kKernelMaxTaps];
struct KernelEntry { uint32_t age=0; uint16_t key=0,taps=0; };
KernelEntry kernelEntries[kKernelCacheEntries];

MT_INLINE SampleFirPlan kernelCachedPlan(int entry,float rate) {
  SampleFirPlan p;
  p.quantized=kernelCache[entry];
  p.quantizedB=kSampleFirBanks[32].data;
  p.aTaps=kernelEntries[entry].taps;
  p.bTaps=kSampleFirBanks[32].taps;
  p.radius=p.aTaps/2;
  p.phaseBits=0;
  p.kernelShift=true;
  p.blend=minf(1.f,(rate-16.f)*4.f);
  return p;
}

MT_CONTROL SampleFirPlan kernelPlan(float actualRate) {
  const float logarithm=64.f*fastLog2(actualRate);
  unsigned key=static_cast<unsigned>(logarithm);
  if(key<logarithm) ++key;
  if(key<257) key=257;
  if(key>512) key=512;
  const uint32_t now=++cacheAge;
  int oldest=0;uint32_t oldestDistance=0;
  for(int i=0;i<kKernelCacheEntries;++i) {
    if(kernelEntries[i].key==key) {
      kernelEntries[i].age=now;
      return kernelCachedPlan(i,actualRate);
    }
    const uint32_t distance=kernelEntries[i].key?now-kernelEntries[i].age:UINT32_MAX;
    if(distance>=oldestDistance) {oldestDistance=distance;oldest=i;}
  }
  const SampleFirKernelBank& bank=kSampleFirKernelBanks[key-257];
  assert(bank.radius<=kKernelMaxTaps/2);
  const int radius=bank.radius;
  int16_t* out=kernelCache[oldest];
  out[radius-1]=bank.half[0];
  for(int distance=1;distance<radius;++distance) {
    const int16_t value=bank.half[distance];
    out[radius-1-distance]=value;
    out[radius-1+distance]=value;
  }
  out[2*radius-1]=bank.half[radius];
  kernelEntries[oldest]={now,static_cast<uint16_t>(key),static_cast<uint16_t>(2*radius)};
  return kernelCachedPlan(oldest,actualRate);
}

MT_INLINE int32_t firDotQ15(const int16_t* coefficients, int taps, const int16_t* samples) {
  int32_t sum = 0;
#if defined(__ARM_FEATURE_DSP)
  int32_t s0=0,s1=0,s2=0,s3=0;
  int j=0;
  for (;j+8<=taps;j+=8) {
    uint32_t x0,x1,x2,x3,c0,c1,c2,c3;
    __builtin_memcpy(&x0,samples+j,4); __builtin_memcpy(&c0,coefficients+j,4);
    __builtin_memcpy(&x1,samples+j+2,4); __builtin_memcpy(&c1,coefficients+j+2,4);
    __builtin_memcpy(&x2,samples+j+4,4); __builtin_memcpy(&c2,coefficients+j+4,4);
    __builtin_memcpy(&x3,samples+j+6,4); __builtin_memcpy(&c3,coefficients+j+6,4);
    s0=__builtin_arm_smlad(static_cast<int32_t>(x0),static_cast<int32_t>(c0),s0);
    s1=__builtin_arm_smlad(static_cast<int32_t>(x1),static_cast<int32_t>(c1),s1);
    s2=__builtin_arm_smlad(static_cast<int32_t>(x2),static_cast<int32_t>(c2),s2);
    s3=__builtin_arm_smlad(static_cast<int32_t>(x3),static_cast<int32_t>(c3),s3);
  }
  sum=(s0+s1)+(s2+s3);
  for (;j<taps;j+=2) {
    uint32_t input,coeff;
    __builtin_memcpy(&input,samples+j,4); __builtin_memcpy(&coeff,coefficients+j,4);
    sum=__builtin_arm_smlad(static_cast<int32_t>(input),static_cast<int32_t>(coeff),sum);
  }
#else
  for (int j=0;j<taps;++j) sum+=int32_t(samples[j])*coefficients[j];
#endif
  return sum;
}

MT_INLINE void dotPhasePair(const int16_t* input,const int16_t* a,const int16_t* b,int32_t& x,int32_t& y) {
#if defined(__ARM_FEATURE_DSP)
  uint32_t sample,ca,cb;
  __builtin_memcpy(&sample,input,4);__builtin_memcpy(&ca,a,4);__builtin_memcpy(&cb,b,4);
  x=__builtin_arm_smlad(static_cast<int32_t>(sample),static_cast<int32_t>(ca),x);
  y=__builtin_arm_smlad(static_cast<int32_t>(sample),static_cast<int32_t>(cb),y);
#else
  x+=int32_t(input[0])*a[0]+int32_t(input[1])*a[1];
  y+=int32_t(input[0])*b[0]+int32_t(input[1])*b[1];
#endif
}
MT_INLINE void firDotPhasesQ15(const int16_t* a,const int16_t* b,int taps,const int16_t* input,
                               int32_t& x,int32_t& y) {
  x=y=0;int j=0;
  for(;j+8<=taps;j+=8) {
    dotPhasePair(input+j,a+j,b+j,x,y);
    dotPhasePair(input+j+2,a+j+2,b+j+2,x,y);
    dotPhasePair(input+j+4,a+j+4,b+j+4,x,y);
    dotPhasePair(input+j+6,a+j+6,b+j+6,x,y);
  }
  for(;j<taps;j+=2) dotPhasePair(input+j,a+j,b+j,x,y);
}
MT_INLINE void dotShiftPair(const int16_t* input,const int16_t* coefficients,int32_t& x,int32_t& y) {
#if defined(__ARM_FEATURE_DSP)
  uint32_t a,b,c;
  __builtin_memcpy(&a,input,4);__builtin_memcpy(&b,input+1,4);__builtin_memcpy(&c,coefficients,4);
  x=__builtin_arm_smlad(static_cast<int32_t>(a),static_cast<int32_t>(c),x);
  y=__builtin_arm_smlad(static_cast<int32_t>(b),static_cast<int32_t>(c),y);
#else
  x+=int32_t(input[0])*coefficients[0]+int32_t(input[1])*coefficients[1];
  y+=int32_t(input[1])*coefficients[0]+int32_t(input[2])*coefficients[1];
#endif
}
MT_INLINE void firDotShiftQ15(const int16_t* coefficients,int taps,const int16_t* input,
                              int32_t& x,int32_t& y) {
  x=y=0;int j=0;
  for(;j+8<=taps;j+=8) {
    dotShiftPair(input+j,coefficients+j,x,y);
    dotShiftPair(input+j+2,coefficients+j+2,x,y);
    dotShiftPair(input+j+4,coefficients+j+4,x,y);
    dotShiftPair(input+j+6,coefficients+j+6,x,y);
  }
  for(;j<taps;j+=2) dotShiftPair(input+j,coefficients+j,x,y);
}

template<int kBits>
MT_INLINE float qValue(const int16_t* bank,int taps,uint32_t fraction,const int16_t* data) {
  constexpr unsigned shift=32-kBits, mask=(uint32_t{1}<<shift)-1;
  constexpr float inverse=1.f/static_cast<float>(uint32_t{1}<<shift);
  const unsigned phase=fraction>>shift;
  const float t=(fraction&mask)*inverse;
  const int16_t* a=bank+phase*taps;
  if(t==0) return firDotQ15(a,taps,data)*(1.f/32768.f);
  int32_t ix,iy;
  firDotPhasesQ15(a,a+taps,taps,data,ix,iy);
  const float x=ix*(1.f/32768.f),y=iy*(1.f/32768.f);
  return x+(y-x)*t;
}

template<int kBits,bool kBlend>
MT_HOT __attribute__((noinline)) void qBlock(const SampleFirPlan& p,int64_t pos,int64_t step,
                                             const int16_t* data,uint32_t firstFrame,float* out,int n) {
  const int16_t* a=p.quantized;
  const int16_t* b=p.quantizedB;
  const int at=p.aTaps,bt=p.bTaps;
  const int ao=1-at/2,bo=1-bt/2;
  const float blend=p.blend;
  const int64_t initial=pos;
  for(int i=0;i<n;i++,pos+=step) {
    const int16_t* center=data+((pos>>32)-firstFrame);
    const uint32_t fraction=static_cast<uint32_t>(pos);
    const float x=qValue<kBits>(a,at,fraction,center+ao);
    if(kBlend) {
      const float y=qValue<kBits>(b,bt,fraction,center+bo);
      out[i]=x+(y-x)*blend;
    } else out[i]=x;
  }
  if(p.wet<1) {
    pos=initial;
    const float wet=p.wet;
    for(int i=0;i<n;i++,pos+=step) {
      const int16_t* center=data+((pos>>32)-firstFrame);
      const float linear=center[0]+(int32_t(center[1])-center[0])*
                         (static_cast<uint32_t>(pos)*(1.f/4294967296.f));
      out[i]=linear+(out[i]-linear)*wet;
    }
  }
}
template<bool kTransition>
MT_HOT __attribute__((noinline)) void qKernelBlock(const SampleFirPlan& p,int64_t pos,int64_t step,
                                                  const int16_t* data,uint32_t firstFrame,float* out,int n) {
  const int16_t* coefficients=p.quantized;
  const int taps=p.aTaps,offset=1-p.radius;
  const float blend=p.blend;
  for(int i=0;i<n;++i,pos+=step) {
    const int16_t* center=data+((pos>>32)-firstFrame);
    const int16_t* first=center+offset;
    const uint32_t fraction=static_cast<uint32_t>(pos);
    float value;
    if(fraction) {
      int32_t ix,iy;
      firDotShiftQ15(coefficients,taps,first,ix,iy);
      const float a=ix*(1.f/32768.f),b=iy*(1.f/32768.f);
      value=a+(b-a)*(fraction*(1.f/4294967296.f));
    } else value=firDotQ15(coefficients,taps,first)*(1.f/32768.f);
    if(kTransition) {
      const float poly=qValue<5>(p.quantizedB,p.bTaps,fraction,center+1-p.bTaps/2);
      value=poly+(value-poly)*blend;
    }
    out[i]=value;
  }
}

}  // namespace

MT_CONTROL void sampleFirInit() {
  if(commonInitialized) return;
  unsigned offset=0;
  for(int bank=0;bank<kCommonBanks;++bank) {
    const unsigned count=(kSampleFirPhases+1)*kSampleFirBanks[bank].taps;
    commonPointers[bank]=commonCoefficients+offset;
    memcpy(commonCoefficients+offset,kSampleFirBanks[bank].data,count*sizeof(int16_t));
    offset+=count;
  }
  commonInitialized=true;
}

MT_CONTROL SampleFirPlan sampleFirPlan(int64_t step) {
  const uint64_t magnitude=step<0?uint64_t(-(step+1))+1:uint64_t(step);
  if(magnitude<=(uint64_t{1}<<32)) return {};
  float rate=static_cast<float>(magnitude)*(1.f/4294967296.f);
  if(rate>256.f) rate=256.f;
  SampleFirPlan p;
  if(rate>16.f) return kernelPlan(rate);
  const float band=8.f*fastLog2(rate);
  int first=static_cast<int>(band);
  if(first<0) first=0;
  if(first>32) first=32;
  const int next=first<32?first+1:first;
  sampleFirInit();
  p.quantized=first<kCommonBanks?commonPointers[first]:kSampleFirBanks[first].data;
  p.aTaps=kSampleFirBanks[first].taps;
  p.blend=band-first;
  p.quantizedB=next<kCommonBanks?commonPointers[next]:kSampleFirBanks[next].data;
  p.bTaps=kSampleFirBanks[next].taps;
  p.radius=(p.blend==0?p.aTaps:p.bTaps)/2;
  p.wet=minf(1.f,(rate-1.f)*50.f);
  return p;
}

MT_HOT void sampleFirQ15Block(const SampleFirPlan& p,int64_t pos,int64_t step,const int16_t* data,
                              uint32_t firstFrame,float* out,int n) {
  if(p.kernelShift) {
    if(p.blend<1) qKernelBlock<true>(p,pos,step,data,firstFrame,out,n);
    else qKernelBlock<false>(p,pos,step,data,firstFrame,out,n);
    return;
  }
  if(p.phaseBits==4) {
    if(p.blend==0) qBlock<4,false>(p,pos,step,data,firstFrame,out,n);
    else qBlock<4,true>(p,pos,step,data,firstFrame,out,n);
  } else {
    if(p.blend==0) qBlock<5,false>(p,pos,step,data,firstFrame,out,n);
    else qBlock<5,true>(p,pos,step,data,firstFrame,out,n);
  }
}

MT_HOT float sampleFirContiguous(const SampleFirPlan& p,uint32_t fraction,const int16_t* first) {
  float out;
  const int64_t pos=(int64_t(p.radius-1)<<32)|fraction;
  sampleFirQ15Block(p,pos,0,first,0,&out,1);
  return out;
}

namespace {
// Fill one cycle (or a shorter prefix) starting at an arbitrary periodic phase. At most three
// runs cross a wrap/reflection: only descending runs need a scalar reverse copy.
MT_INLINE void stageCycle(int16_t* out,int n,const int16_t* pcm,uint32_t lo,uint32_t span,
                           uint64_t phase,bool pingpong) {
  if(!pingpong) {
    const int first=n<static_cast<int64_t>(span-phase)?n:static_cast<int>(span-phase);
    memcpy(out,pcm+lo+phase,first*sizeof(int16_t));
    if(first<n) memcpy(out+first,pcm+lo,(n-first)*sizeof(int16_t));
    return;
  }
  const uint64_t length=span-1,period=2*length;
  int done=0;
  while(done<n) {
    if(phase<=length) {
      const uint64_t available=length-phase+1;
      const int count=available<uint64_t(n-done)?static_cast<int>(available):n-done;
      memcpy(out+done,pcm+lo+phase,count*sizeof(int16_t));
      done+=count;phase+=count;
    } else {
      const uint64_t available=period-phase;
      const int count=available<uint64_t(n-done)?static_cast<int>(available):n-done;
      const uint32_t first=static_cast<uint32_t>(lo+period-phase);
      for(int j=0;j<count;++j) out[done+j]=pcm[first-j];
      done+=count;phase+=count;
    }
    if(phase==period) phase=0;
  }
}

MT_INLINE void stagePeriodic(int16_t* out,int n,const int16_t* pcm,uint32_t lo,uint32_t span,
                              int64_t frame,bool pingpong) {
  if(span==1) {
    const int16_t value=pcm[lo];
    for(int j=0;j<n;++j) out[j]=value;
    return;
  }
  const int64_t period=pingpong?2*(int64_t(span)-1):span;
  int64_t phase=(frame-lo)%period;
  if(phase<0) phase+=period;
  const int first=period<n?static_cast<int>(period):n;
  stageCycle(out,first,pcm,lo,span,phase,pingpong);
  // Short loops are repeated by doubling an already staged cycle, never by a modulo/call per tap.
  for(int done=first;done<n;) {
    const int count=done<n-done?done:n-done;
    memcpy(out+done,out,count*sizeof(int16_t));
    done+=count;
  }
}
}  // namespace

MT_HOT float sampleFirRam(const SampleFirPlan& p,uint32_t fraction,uint32_t center,const int16_t* pcm,
                           uint32_t from,uint32_t to,uint32_t lo,uint32_t hi,uint8_t mode,bool looped,
                           int64_t step) {
  constexpr int kMaxWindow=2049;
  assert(p.taps()>0&&p.taps()<=kMaxWindow);
  if(p.taps()<=0||p.taps()>kMaxWindow||to<=from) return 0;
  int16_t window[kMaxWindow];
  const bool loop=(mode==static_cast<uint8_t>(LoopMode::Forward)||
                   mode==static_cast<uint8_t>(LoopMode::PingPong))&&hi>lo;
  const bool pingpong=mode==static_cast<uint8_t>(LoopMode::PingPong);
  int64_t frame=int64_t(center)+p.first();
  int done=0;
  while(done<p.taps()) {
    int count=p.taps()-done;
    const bool periodic=loop&&(looped||(step<0?frame<hi:frame>=lo));
    if(periodic) {
      if(!looped&&step<0&&int64_t(hi)-frame<count) count=static_cast<int>(int64_t(hi)-frame);
      stagePeriodic(window+done,count,pcm,lo,hi-lo,frame,pingpong);
    } else if(frame<from) {
      if(int64_t(from)-frame<count) count=static_cast<int>(int64_t(from)-frame);
      const int16_t value=pcm[from];
      for(int j=0;j<count;++j) window[done+j]=value;
    } else if(frame>=to) {
      const int16_t value=pcm[to-1];
      for(int j=0;j<count;++j) window[done+j]=value;
    } else {
      if(int64_t(to)-frame<count) count=static_cast<int>(int64_t(to)-frame);
      if(loop&&!looped&&step>=0&&frame<lo&&int64_t(lo)-frame<count)
        count=static_cast<int>(int64_t(lo)-frame);
      memcpy(window+done,pcm+frame,count*sizeof(int16_t));
    }
    done+=count;frame+=count;
  }
  return sampleFirContiguous(p,fraction,window);
}

MT_HOT uint32_t sampleFirFrame(int64_t frame, uint32_t from, uint32_t to, uint32_t lo, uint32_t hi,
                              uint8_t mode, bool looped, int64_t step) {
  if (to <= from) return from;
  const bool below = frame < lo && (looped || step < 0);
  const bool above = frame >= hi && (looped || step >= 0);
  if (below || above) {
    if (mode == static_cast<uint8_t>(LoopMode::Forward) && hi > lo) {
      const int64_t span = int64_t(hi) - lo;
      if (span == 1) return lo;
      if (frame < lo && frame >= int64_t(lo) - span) frame += span;
      else if (frame >= hi && frame < int64_t(hi) + span) frame -= span;
      else {
        int64_t phase = (frame - lo) % span;
        if (phase < 0) phase += span;
        frame = lo + phase;
      }
    } else if (mode == static_cast<uint8_t>(LoopMode::PingPong) && hi > lo) {
      const int64_t length = int64_t(hi) - lo - 1;
      if (length == 0) return lo;
      int64_t phase = (frame - lo) % (2 * length);
      if (phase < 0) phase += 2 * length;
      frame = lo + (phase <= length ? phase : 2 * length - phase);
    }
  }
  return frame < from ? from : (frame >= to ? to - 1 : static_cast<uint32_t>(frame));
}

MT_HOT bool sampleFirInterior(const SampleFirPlan& p, uint32_t center, uint32_t from, uint32_t to, uint32_t lo, uint32_t hi,
                              uint8_t mode, bool looped, int64_t step) {
  const bool loop = (mode == static_cast<uint8_t>(LoopMode::Forward) ||
                     mode == static_cast<uint8_t>(LoopMode::PingPong)) && hi > lo;
  const uint32_t lower = loop && (looped || step < 0) ? lo : from;
  const uint32_t upper = loop && (looped || step >= 0) ? hi : to;
  return int64_t(center) + p.first() >= lower && uint64_t(center) + p.last() < upper;
}
}  // namespace mt
