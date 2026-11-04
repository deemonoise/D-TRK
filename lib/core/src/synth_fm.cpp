#include "synth_fm.h"
#include "fastmath.h"
#include "hot.h"
#include <math.h>

#if defined(__IMXRT1062__)
#define FM_TABLE PROGMEM
#else
#define FM_TABLE MT_DATA
#endif

namespace mt {
namespace {
constexpr float kPhasePerHz = 4294967296.f / kSynthRate;
constexpr float kSamplesPerMs = kSynthRate * .001f;
MT_INLINE float safe(float x, float lo, float hi) {
  return isfinite(x) ? fclamp(x, lo, hi) : lo;
}
MT_INLINE float envelope(float ms, float sustain, float sample) {
  sustain = safe(sustain, 0, 1);
  if (sustain == 1) return 1;
  if (ms <= 0 || !isfinite(ms)) return 1;
  return sustain + (1-sustain)*fastExp(-6.9077553f*sample/(ms*kSamplesPerMs));
}
MT_INLINE uint32_t phaseOffset(float radians) {
  return static_cast<uint32_t>(static_cast<int32_t>(fclamp(radians,-96,96)*166886.0536f)) << 12;
}
MT_INLINE float blep(float t, float dt) {
  if (dt <= 0) return 0;
  if (t < dt) { const float u=t/dt; return u+u-u*u-1; }
  if (t > 1-dt) { const float u=(t-1)/dt; return u*u+u+u+1; }
  return 0;
}
// PolyBLEP treats the nominal oscillator frequency, not the PM derivative. Strong PM/feedback
// is not strictly band-limited; its residual must be checked against the high-pitch references.
// Harmonic magnitude candidates from the five WAVE reference cuts. Odd-harmonic recurrence
// needs one sine lookup; only the even-rich range needs a quadrature lookup. Intermediate
// values are interpolation candidates, not a reconstruction of Elektron's wavetable bank.
static FM_TABLE const float kReferenceHarmonics[128][6]={
  {0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0081250f, 0.0029219f, 0.0008938f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0162500f, 0.0058437f, 0.0017875f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0243750f, 0.0087656f, 0.0026813f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0325000f, 0.0116875f, 0.0035750f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0406250f, 0.0146094f, 0.0044688f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0487500f, 0.0175312f, 0.0053625f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0568750f, 0.0204531f, 0.0062562f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0650000f, 0.0233750f, 0.0071500f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0731250f, 0.0262969f, 0.0080438f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0812500f, 0.0292188f, 0.0089375f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0893750f, 0.0321406f, 0.0098312f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.0975000f, 0.0350625f, 0.0107250f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1056250f, 0.0379844f, 0.0116188f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1137500f, 0.0409062f, 0.0125125f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1218750f, 0.0438281f, 0.0134062f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1300000f, 0.0467500f, 0.0143000f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1381250f, 0.0496719f, 0.0151938f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1462500f, 0.0525938f, 0.0160875f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1543750f, 0.0555156f, 0.0169812f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1625000f, 0.0584375f, 0.0178750f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1706250f, 0.0613594f, 0.0187688f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1787500f, 0.0642812f, 0.0196625f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1868750f, 0.0672031f, 0.0205563f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.1950000f, 0.0701250f, 0.0214500f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.2031250f, 0.0730469f, 0.0223437f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.2112500f, 0.0759688f, 0.0232375f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.2193750f, 0.0788906f, 0.0241312f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.2275000f, 0.0818125f, 0.0250250f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.2356250f, 0.0847344f, 0.0259188f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.2437500f, 0.0876563f, 0.0268125f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.2518750f, 0.0905781f, 0.0277063f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.2600000f, 0.0935000f, 0.0286000f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.2743750f, 0.0992656f, 0.0303625f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.2887500f, 0.1050313f, 0.0321250f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.3031250f, 0.1107969f, 0.0338875f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.3175000f, 0.1165625f, 0.0356500f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.3318750f, 0.1223281f, 0.0374125f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.3462500f, 0.1280938f, 0.0391750f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.3606250f, 0.1338594f, 0.0409375f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.3750000f, 0.1396250f, 0.0427000f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.3893750f, 0.1453906f, 0.0444625f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.4037500f, 0.1511562f, 0.0462250f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.4181250f, 0.1569219f, 0.0479875f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.4325000f, 0.1626875f, 0.0497500f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.4468750f, 0.1684531f, 0.0515125f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.4612500f, 0.1742188f, 0.0532750f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.4756250f, 0.1799844f, 0.0550375f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.4900000f, 0.1857500f, 0.0568000f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.5043750f, 0.1915156f, 0.0585625f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.5187500f, 0.1972813f, 0.0603250f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.5331250f, 0.2030469f, 0.0620875f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.5475000f, 0.2088125f, 0.0638500f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.5618750f, 0.2145781f, 0.0656125f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.5762500f, 0.2203438f, 0.0673750f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.5906250f, 0.2261094f, 0.0691375f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.6050000f, 0.2318750f, 0.0709000f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.6193750f, 0.2376406f, 0.0726625f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.6337500f, 0.2434063f, 0.0744250f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.6481250f, 0.2491719f, 0.0761875f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.6625000f, 0.2549375f, 0.0779500f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.6768750f, 0.2607031f, 0.0797125f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.6912500f, 0.2664688f, 0.0814750f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.7056250f, 0.2722344f, 0.0832375f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.7200000f, 0.2780000f, 0.0850000f, 0.0000000f, 0.0000000f, 0.0000000f},
  {0.7042188f, 0.2705625f, 0.0828125f, 0.0000000f, 0.0000000f, 0.0126563f},
  {0.6884375f, 0.2631250f, 0.0806250f, 0.0000000f, 0.0000000f, 0.0253125f},
  {0.6726562f, 0.2556875f, 0.0784375f, 0.0000000f, 0.0000000f, 0.0379688f},
  {0.6568750f, 0.2482500f, 0.0762500f, 0.0000000f, 0.0000000f, 0.0506250f},
  {0.6410938f, 0.2408125f, 0.0740625f, 0.0000000f, 0.0000000f, 0.0632813f},
  {0.6253125f, 0.2333750f, 0.0718750f, 0.0000000f, 0.0000000f, 0.0759375f},
  {0.6095312f, 0.2259375f, 0.0696875f, 0.0000000f, 0.0000000f, 0.0885937f},
  {0.5937500f, 0.2185000f, 0.0675000f, 0.0000000f, 0.0000000f, 0.1012500f},
  {0.5779687f, 0.2110625f, 0.0653125f, 0.0000000f, 0.0000000f, 0.1139063f},
  {0.5621875f, 0.2036250f, 0.0631250f, 0.0000000f, 0.0000000f, 0.1265625f},
  {0.5464062f, 0.1961875f, 0.0609375f, 0.0000000f, 0.0000000f, 0.1392188f},
  {0.5306250f, 0.1887500f, 0.0587500f, 0.0000000f, 0.0000000f, 0.1518750f},
  {0.5148437f, 0.1813125f, 0.0565625f, 0.0000000f, 0.0000000f, 0.1645313f},
  {0.4990625f, 0.1738750f, 0.0543750f, 0.0000000f, 0.0000000f, 0.1771875f},
  {0.4832812f, 0.1664375f, 0.0521875f, 0.0000000f, 0.0000000f, 0.1898438f},
  {0.4675000f, 0.1590000f, 0.0500000f, 0.0000000f, 0.0000000f, 0.2025000f},
  {0.4517187f, 0.1515625f, 0.0478125f, 0.0000000f, 0.0000000f, 0.2151563f},
  {0.4359375f, 0.1441250f, 0.0456250f, 0.0000000f, 0.0000000f, 0.2278125f},
  {0.4201562f, 0.1366875f, 0.0434375f, 0.0000000f, 0.0000000f, 0.2404688f},
  {0.4043750f, 0.1292500f, 0.0412500f, 0.0000000f, 0.0000000f, 0.2531250f},
  {0.3885937f, 0.1218125f, 0.0390625f, 0.0000000f, 0.0000000f, 0.2657813f},
  {0.3728125f, 0.1143750f, 0.0368750f, 0.0000000f, 0.0000000f, 0.2784375f},
  {0.3570312f, 0.1069375f, 0.0346875f, 0.0000000f, 0.0000000f, 0.2910938f},
  {0.3412500f, 0.0995000f, 0.0325000f, 0.0000000f, 0.0000000f, 0.3037500f},
  {0.3254687f, 0.0920625f, 0.0303125f, 0.0000000f, 0.0000000f, 0.3164062f},
  {0.3096875f, 0.0846250f, 0.0281250f, 0.0000000f, 0.0000000f, 0.3290625f},
  {0.2939062f, 0.0771875f, 0.0259375f, 0.0000000f, 0.0000000f, 0.3417188f},
  {0.2781250f, 0.0697500f, 0.0237500f, 0.0000000f, 0.0000000f, 0.3543750f},
  {0.2623437f, 0.0623125f, 0.0215625f, 0.0000000f, 0.0000000f, 0.3670313f},
  {0.2465625f, 0.0548750f, 0.0193750f, 0.0000000f, 0.0000000f, 0.3796875f},
  {0.2307812f, 0.0474375f, 0.0171875f, 0.0000000f, 0.0000000f, 0.3923438f},
  {0.2150000f, 0.0400000f, 0.0150000f, 0.0000000f, 0.0000000f, 0.4050000f},
  {0.2080645f, 0.0387097f, 0.0145161f, 0.0068065f, 0.0043226f, 0.3919355f},
  {0.2011290f, 0.0374194f, 0.0140323f, 0.0136129f, 0.0086452f, 0.3788710f},
  {0.1941935f, 0.0361290f, 0.0135484f, 0.0204194f, 0.0129677f, 0.3658065f},
  {0.1872581f, 0.0348387f, 0.0130645f, 0.0272258f, 0.0172903f, 0.3527419f},
  {0.1803226f, 0.0335484f, 0.0125806f, 0.0340323f, 0.0216129f, 0.3396774f},
  {0.1733871f, 0.0322581f, 0.0120968f, 0.0408387f, 0.0259355f, 0.3266129f},
  {0.1664516f, 0.0309677f, 0.0116129f, 0.0476452f, 0.0302581f, 0.3135484f},
  {0.1595161f, 0.0296774f, 0.0111290f, 0.0544516f, 0.0345806f, 0.3004839f},
  {0.1525806f, 0.0283871f, 0.0106452f, 0.0612581f, 0.0389032f, 0.2874194f},
  {0.1456452f, 0.0270968f, 0.0101613f, 0.0680645f, 0.0432258f, 0.2743548f},
  {0.1387097f, 0.0258065f, 0.0096774f, 0.0748710f, 0.0475484f, 0.2612903f},
  {0.1317742f, 0.0245161f, 0.0091935f, 0.0816774f, 0.0518710f, 0.2482258f},
  {0.1248387f, 0.0232258f, 0.0087097f, 0.0884839f, 0.0561935f, 0.2351613f},
  {0.1179032f, 0.0219355f, 0.0082258f, 0.0952903f, 0.0605161f, 0.2220968f},
  {0.1109677f, 0.0206452f, 0.0077419f, 0.1020968f, 0.0648387f, 0.2090323f},
  {0.1040323f, 0.0193548f, 0.0072581f, 0.1089032f, 0.0691613f, 0.1959677f},
  {0.0970968f, 0.0180645f, 0.0067742f, 0.1157097f, 0.0734839f, 0.1829032f},
  {0.0901613f, 0.0167742f, 0.0062903f, 0.1225161f, 0.0778065f, 0.1698387f},
  {0.0832258f, 0.0154839f, 0.0058065f, 0.1293226f, 0.0821290f, 0.1567742f},
  {0.0762903f, 0.0141935f, 0.0053226f, 0.1361290f, 0.0864516f, 0.1437097f},
  {0.0693548f, 0.0129032f, 0.0048387f, 0.1429355f, 0.0907742f, 0.1306452f},
  {0.0624194f, 0.0116129f, 0.0043548f, 0.1497419f, 0.0950968f, 0.1175806f},
  {0.0554839f, 0.0103226f, 0.0038710f, 0.1565484f, 0.0994194f, 0.1045161f},
  {0.0485484f, 0.0090323f, 0.0033871f, 0.1633548f, 0.1037419f, 0.0914516f},
  {0.0416129f, 0.0077419f, 0.0029032f, 0.1701613f, 0.1080645f, 0.0783871f},
  {0.0346774f, 0.0064516f, 0.0024194f, 0.1769677f, 0.1123871f, 0.0653226f},
  {0.0277419f, 0.0051613f, 0.0019355f, 0.1837742f, 0.1167097f, 0.0522581f},
  {0.0208065f, 0.0038710f, 0.0014516f, 0.1905806f, 0.1210323f, 0.0391935f},
  {0.0138710f, 0.0025806f, 0.0009677f, 0.1973871f, 0.1253548f, 0.0261290f},
  {0.0069355f, 0.0012903f, 0.0004839f, 0.2041935f, 0.1296774f, 0.0130645f},
  {0.0000000f, 0.0000000f, 0.0000000f, 0.2110000f, 0.1340000f, 0.0000000f},
};
MT_INLINE float referenceWave(uint32_t phase,float wave,float inc) {
  const float sine=tableSine(phase);
  if(wave<=4)return sine;  // row zero is a pure sine; important for TONE without PNCH
  const float position=fclamp((wave-4)*127,0,127);
  const int index=static_cast<int>(position),next=index<127?index+1:index;
  const float fraction=position-index;
  const float* a=kReferenceHarmonics[index];const float* b=kReferenceHarmonics[next];
  const auto coefficient=[&](int k) {return a[k]+(b[k]-a[k])*fraction;};
  const float twiceCos2=2-4*sine*sine;float previous=sine,current=3*sine-4*sine*sine*sine,y=sine;
  if(inc*3<2147483648.f)y+=coefficient(0)*current;
  for(int harmonic=5;harmonic<=15;harmonic+=2) {
    const float following=twiceCos2*current-previous;previous=current;current=following;
    const float weight=harmonic==5?coefficient(1):harmonic==7?coefficient(2):harmonic==11?coefficient(3):harmonic==15?coefficient(4):0;
    if(inc*harmonic<2147483648.f)y+=weight*current;
  }
  const float even=coefficient(5);
  if(even!=0 && inc*2<2147483648.f)y+=even*2*sine*tableSine(phase+0x40000000u);
  return y;
}
MT_INLINE float waveform(uint32_t phase, float wave, float inc) {
  if(wave>=4)return referenceWave(phase,wave,inc);
  if(wave<=0)return tableSine(phase);
  const float t=static_cast<float>(phase)*(1.f/4294967296.f);
  float tri=0;
  if(wave<2) {
    float q=t+.25f;if(q>=1)q-=1;
    tri=1-4*fabsf(q-.5f);
    if(wave<1) {const float sine=tableSine(phase);return sine+(tri-sine)*wave;}
    if(wave==1)return tri;
  }
  const float dt=inc*(1.f/4294967296.f);
  const float edge=blep(t,dt);
  const float saw=1-2*t+edge;  // same fundamental polarity as sine/triangle
  if(wave<2)return tri+(saw-tri)*(wave-1);
  if(wave==2)return saw;
  float q=t+.5f;if(q>=1)q-=1;
  const float square=(t<.5f?1.f:-1.f)+edge-blep(q,dt);
  if(wave>=3)return square;
  return saw+(square-saw)*(wave-2);
}
MT_INLINE float white(uint32_t& state) {
  state ^= state << 13;state ^= state >> 17;state ^= state << 5;
  return static_cast<int32_t>(state)*(1.f/2147483648.f);
}
}

MT_CONTROL void FmVoice::trigger(bool keepPhase) {
  t_=0;fresh_=true;keepPhase_=keepPhase;
}

MT_CONTROL void FmVoice::control(const FmParams& p,int span) {
  if(span<1)span=1;
  const float inverse=1.f/span;
  const float start=static_cast<float>(t_),end=start+span;
  const float base=safe(p.hz,0,kSynthRate*.45f);
  const float pitch=safe(p.pitchEnv,-96,96);
  const float bend0=pitch!=0?fastExp2(pitch*envelope(p.pitchMs,0,start)/12.f):1;
  const float bend1=pitch!=0?fastExp2(pitch*envelope(p.pitchMs,0,end)/12.f):1;
  if(fresh_) {
    if(p.resetPhase) {
      for(auto& op:op_){op.phase=0;op.last=0;}
      if(keepPhase_){fadeFrom_=lastOut_;fadeLeft_=32;}else fadeLeft_=0;
    } else fadeLeft_=0;
    if(!keepPhase_){filter_.reset();noise2Filter_.reset();}
  }
  uint8_t required=0;
  for(int i=0;i<kFmOps;++i)if(p.op[i].mod>i && p.op[i].mod<kFmOps)required|=1u<<p.op[i].mod;
  for(int i=0;i<kFmOps;++i) {
    auto& s=op_[i];const auto& src=p.op[i];
    if(src.level==0 && fabsf(s.level)<1e-7f && !(required&(1u<<i))) {s.active=false;continue;}
    const float ratio=safe(src.ratio,0,128),level=safe(src.level,0,64);
    const float a0=level*envelope(src.decayMs,src.sustain,start);
    const float a1=level*envelope(src.decayMs,src.sustain,end);
    const float inc0=safe(base*ratio*bend0,0,kSynthRate*.45f)*kPhasePerHz;
    const float inc1=safe(base*ratio*bend1,0,kSynthRate*.45f)*kPhasePerHz;
    if(fresh_ && !keepPhase_) {s.inc=inc0;s.level=a0;}
    s.dInc=(inc1-s.inc)*inverse;s.dLevel=(a1-s.level)*inverse;
    const float feedback=safe(src.fb,0,8),wave=safe(src.wave,0,5);
    if(fresh_ && !keepPhase_){s.fb=feedback;s.wave=wave;}
    s.dFb=(feedback-s.fb)*inverse;s.dWave=(wave-s.wave)*inverse;
    s.mod=src.mod>i && src.mod<kFmOps?src.mod:-1;s.output=src.output;
    s.active=level>0 || fabsf(s.level)>1e-7f || (required&(1u<<i));
  }
  additive_=true;
  for(const auto& op:op_)if(op.active && (op.mod>=0 || op.fb!=0 || op.dFb!=0))additive_=false;
  paired_=op_[0].active && op_[0].output && op_[0].mod==4 &&
          op_[4].active && !op_[4].output && op_[4].mod<0 && op_[4].wave==0 && op_[4].dWave==0;
  for(int i=1;i<kFmOps;++i)if(i!=4 && op_[i].active)paired_=false;
  oneShot_=p.oneShot;attackSamples_=keepPhase_?0:safe(p.attackMs,0,1000)*kSamplesPerMs;
  const float amp0=envelope(p.ampMs,p.ampSustain,start);
  ampTarget_=envelope(p.ampMs,p.ampSustain,end);
  if(fresh_ && !keepPhase_)amp_=amp0;
  dAmp_=(ampTarget_-amp_)*inverse;
  bursts_=p.bursts;burstSamples_=safe(p.burstMs,0,1000)*kSamplesPerMs;tail_=safe(p.tail,0,4);
  const auto noiseAt=[&](float t) {
    if(bursts_ && burstSamples_>0) {
      const float burstEnd=bursts_*burstSamples_;
      if(t<burstEnd) {
        const float local=t-floorf(t/burstSamples_)*burstSamples_;
        return safe(p.noise,0,8)*envelope(p.noiseMs,0,local);
      }
      return safe(p.noise,0,8)*tail_*envelope(p.noiseMs,p.noiseSustain,t-burstEnd);
    }
    return safe(p.noise,0,8)*envelope(p.noiseMs,p.noiseSustain,t);
  };
  const float noise0=p.noise!=0?noiseAt(start):0,noise1=p.noise!=0?noiseAt(end):0;
  const float noise20=p.noise2!=0?safe(p.noise2,0,8)*envelope(p.noise2Ms,p.noise2Sustain,start):0;
  const float noise21=p.noise2!=0?safe(p.noise2,0,8)*envelope(p.noise2Ms,p.noise2Sustain,end):0;
  if(fresh_ && !keepPhase_){noise_=noise0;noise2_=noise20;}
  dNoise_=(noise1-noise_)*inverse;dNoise2_=(noise21-noise2_)*inverse;
  const float sat=safe(p.sat,0,1);
  const float gain=safe(p.outputGain,0,8)*(sat!=0?fastExp2(1.5f*sat):1);
  if(fresh_ && !keepPhase_){sat_=sat;outputGain_=gain;}
  dSat_=(sat-sat_)*inverse;dOutputGain_=(gain-outputGain_)*inverse;
  filterOn_=p.filterHz>0;filterAll_=p.filterAll;
  if(filterOn_)filter_.set(p.filterMode,safe(p.filterHz,20,kSynthRate*.45f),safe(p.filterQ,.5f,20));
  noise2FilterOn_=p.noise2FilterHz>0;
  if(noise2FilterOn_)noise2Filter_.set(p.noise2FilterMode,safe(p.noise2FilterHz,20,kSynthRate*.45f),safe(p.noise2FilterQ,.5f,20));
  const float comp=safe(p.overComp,-4,4);
  const float postTarget=safe(p.postGain,0,32)*(comp!=0 && sat!=0?fastExp2(comp*sat):1);
  if(fresh_ && !keepPhase_)postGain_=postTarget;
  dPostGain_=(postTarget-postGain_)*inverse;
  left_=span;fresh_=false;
}

// Independent oscillators can keep their recurrence in registers across the whole block.
// The summation order stays descending by operator, exactly as in the generic PM graph.
MT_HOT void FmVoice::additive(float* __restrict tonal,int n,int ramp) {
  for(int i=kFmOps-1;i>=0;--i) {
    auto& state=op_[i];if(!state.active)continue;
    uint32_t phase=state.phase;
    float inc=state.inc,level=state.level,wave=state.wave,last=state.last;
    const float dInc=state.dInc,dLevel=state.dLevel,dWave=state.dWave;
    for(int k=0;k<n;++k) {
      last=waveform(phase,wave,inc);
      const float contribution=last*level;
      if(state.output)tonal[k]+=contribution;
      phase+=static_cast<uint32_t>(inc);
      if(k<ramp){inc+=dInc;level+=dLevel;wave+=dWave;}
    }
    state.phase=phase;state.inc=inc;state.level=level;state.wave=wave;state.last=last;
  }
}

// The common two-operator PM pair keeps both recurrences in registers, without the
// eight-slot routing matrix. Eligibility is checked at control rate; all other graphs fall back.
MT_HOT void FmVoice::pair(float* __restrict tonal,int n,int ramp) {
  auto& modulator=op_[4];auto& carrier=op_[0];
  uint32_t mPhase=modulator.phase,cPhase=carrier.phase;
  float mInc=modulator.inc,mLevel=modulator.level,mFb=modulator.fb,mLast=modulator.last;
  float cInc=carrier.inc,cLevel=carrier.level,cFb=carrier.fb,cWave=carrier.wave,cLast=carrier.last;
  const float mDInc=modulator.dInc,mDLevel=modulator.dLevel,mDFb=modulator.dFb;
  const float cDInc=carrier.dInc,cDLevel=carrier.dLevel,cDFb=carrier.dFb,cDWave=carrier.dWave;
  const bool modFeedback=mFb!=0 || mDFb!=0,carrierFeedback=cFb!=0 || cDFb!=0;
  const bool carrierSine=(cWave==0 || cWave==4) && cDWave==0;
  const bool fixed=carrierSine && mFb==0 && cFb==0 &&
    mDInc==0 && mDLevel==0 && mDFb==0 && modulator.dWave==0 &&
    cDInc==0 && cDLevel==0 && cDFb==0 && cDWave==0;
  if(fixed) {
    const uint32_t mStep=static_cast<uint32_t>(mInc),cStep=static_cast<uint32_t>(cInc);
    for(int k=0;k<n;++k) {
      mLast=tableSine(mPhase);
      const float modulation=mLast*mLevel;
      mPhase+=mStep;
      cLast=tableSine(cPhase+phaseOffset(modulation));
      const float contribution=cLast*cLevel;
      tonal[k]+=contribution;
      cPhase+=cStep;
    }
    modulator.phase=mPhase;modulator.last=mLast;
    carrier.phase=cPhase;carrier.last=cLast;
    return;
  }
  for(int k=0;k<n;++k) {
    mLast=tableSine(mPhase+(modFeedback?phaseOffset(mFb*mLast):0));
    const float modulation=mLast*mLevel;
    mPhase+=static_cast<uint32_t>(mInc);
    if(k<ramp){mInc+=mDInc;mLevel+=mDLevel;mFb+=mDFb;}
    const uint32_t phase=cPhase+(carrierFeedback?phaseOffset(modulation+cFb*cLast):phaseOffset(modulation));
    cLast=carrierSine?tableSine(phase):waveform(phase,cWave,cInc);
    const float contribution=cLast*cLevel;
    tonal[k]+=contribution;
    cPhase+=static_cast<uint32_t>(cInc);
    if(k<ramp){cInc+=cDInc;cLevel+=cDLevel;cFb+=cDFb;cWave+=cDWave;}
  }
  modulator.phase=mPhase;modulator.inc=mInc;modulator.level=mLevel;modulator.fb=mFb;modulator.last=mLast;
  carrier.phase=cPhase;carrier.inc=cInc;carrier.level=cLevel;carrier.fb=cFb;carrier.wave=cWave;carrier.last=cLast;
}

MT_HOT void FmVoice::render(float* __restrict out,int n,float gain) {
  if(!out || n<=0)return;
  const float gainStep=gain*dPostGain_;
  gain*=postGain_;
  for(int offset=0;offset<n;offset+=32) {
    const int count=n-offset<32?n-offset:32;
    float tonal[32]={};
    if(additive_)additive(tonal,count,left_<count?left_:count);
    else if(paired_)pair(tonal,count,left_<count?left_:count);
    for(int k=0;k<count;++k) {
      float sum=tonal[k];
      if(!additive_ && !paired_) {
        float value[kFmOps]={};
        for(int i=kFmOps-1;i>=0;--i) {
          auto& s=op_[i];if(!s.active)continue;
          const float modulation=(s.mod>=0?value[s.mod]:0)+s.fb*s.last;
          const float sample=waveform(s.phase+phaseOffset(modulation),s.wave,s.inc);
          s.last=sample;value[i]=sample*s.level;
          if(s.output)sum+=value[i];
          s.phase+=static_cast<uint32_t>(s.inc);
          if(left_>0){s.inc+=s.dInc;s.level+=s.dLevel;s.fb+=s.dFb;s.wave+=s.dWave;}
        }
      }
      float noise=noise_>1e-10f?white(rng_)*noise_:0;
      if(filterOn_ && !filterAll_)noise=filter_.process(noise);
      sum+=noise;
      if(filterOn_ && filterAll_)sum=filter_.process(sum);
      if(noise2_>1e-10f) {
        float second=white(rng_)*noise2_;
        if(noise2FilterOn_)second=noise2Filter_.process(second);
        sum+=second;
      }
      const float attack=attackSamples_>0?fclamp(static_cast<float>(t_)/attackSamples_,0,1):1;
      float y=sum*amp_*attack*outputGain_;
      if(sat_>0) {
        const float drive=1+2*sat_;
        y+=sat_*(tanhSoft(y*drive)/drive-y);
      }
      if(fadeLeft_){const float oldWeight=fadeLeft_*(1.f/32.f);y=y*(1-oldWeight)+fadeFrom_*oldWeight;--fadeLeft_;}
      lastOut_=y;
      out[offset+k]+=y*gain;
      if(left_>0){amp_+=dAmp_;noise_+=dNoise_;noise2_+=dNoise2_;sat_+=dSat_;outputGain_+=dOutputGain_;postGain_+=dPostGain_;gain+=gainStep;--left_;}
      ++t_;
    }
  }
}
}  // namespace mt
