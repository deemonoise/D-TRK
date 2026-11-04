#include "synth_fm.h"
#include "synth_fm_tonal.h"
#include "model.h"
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
MT_CONTROL float raw(float x) { return isfinite(x) ? fclamp(x,0,127) : 0; }
MT_CONTROL float unit(float x) { return raw(x)*(1.f/127.f); }
MT_CONTROL float knots(const float* xs,const float* ys,int n,float x) {
  x=raw(x);for(int i=1;i<n;++i)if(x<=xs[i])return ys[i-1]+(ys[i]-ys[i-1])*(x-xs[i-1])/(xs[i]-xs[i-1]);return ys[n-1];
}
// Measured carrier reference; TUNE's intermediate law is a candidate between five sampled points.
MT_CONTROL float tuned(float note,float tune) {
  return noteHz(note+fclamp(raw(tune)-64,-24,24))*(261.55485f/261.625565f);
}
// 118 entries inferred from paired first sidebands. Ten unresolved points use explicitly nominal
// candidates: 0, .99/.999/1/1.01, 1.98/1.998/2/2.002/2.02. They need targeted reference checks.
static FM_TABLE const float kRatio[128]={
  0.0f, 0.07495136f, 0.1249402f, 0.2499234f, 0.499934f, 0.7499452f, 0.8999775f, 0.99f,
  0.999f, 1.0f, 1.01f, 1.099978f, 1.149966f, 1.199956f, 1.249944f, 1.299999f,
  1.399977f, 1.499956f, 1.599999f, 1.749966f, 1.800001f, 1.899992f, 1.98f, 1.998f,
  2.0f, 2.002f, 2.02f, 2.099983f, 2.149988f, 2.199975f, 2.249966f, 2.30002f,
  2.399999f, 2.499977f, 2.599999f, 2.749966f, 2.80002f, 2.899999f, 2.970005f, 2.996979f,
  2.999977f, 3.003017f, 3.030014f, 3.10002f, 3.150009f, 3.199999f, 3.249988f, 3.30002f,
  3.399998f, 3.499976f, 3.60002f, 3.749987f, 3.800042f, 3.90002f, 3.960007f, 3.996016f,
  3.999998f, 4.004023f, 4.040033f, 4.10002f, 4.150009f, 4.199997f, 4.249987f, 4.300042f,
  4.400019f, 4.499997f, 4.600041f, 4.750008f, 4.800063f, 4.900041f, 4.95003f, 4.995075f,
  4.999998f, 5.005007f, 5.050052f, 5.100041f, 5.15003f, 5.200019f, 5.250008f, 5.300062f,
  5.400041f, 5.500019f, 5.600062f, 5.750029f, 5.800062f, 5.900041f, 5.940032f, 5.994046f,
  6.000019f, 6.006057f, 6.060093f, 6.100062f, 6.150051f, 6.20004f, 6.250029f, 6.300084f,
  6.400062f, 6.50004f, 6.600062f, 6.750029f, 6.800084f, 6.900062f, 6.930099f, 6.993083f,
  7.00004f, 7.007062f, 7.070047f, 7.100084f, 7.150073f, 7.200062f, 7.250051f, 7.300106f,
  7.400083f, 7.50004f, 7.600084f, 7.75005f, 7.800105f, 7.900083f, 7.920101f, 7.992055f,
  8.000061f, 9.000083f, 10.00008f, 11.0001f, 12.00012f, 13.00015f, 14.00015f, 15.00017f,
};
struct Chord { uint8_t count; float interval[4]; };
// Selected strong low-frequency peak signatures, not a claim about internal oscillator count.
static FM_TABLE const Chord kChord[38]={
  {2, {-0.5f, 0.5f}},
  {3, {-0.5f, -0.0f, 0.5f}},
  {4, {-0.5f, -0.2f, 0.2f, 0.5f}},
  {3, {-9.0f, -5.0f, -0.0f}},
  {3, {-8.0f, -5.0f, -0.0f}},
  {3, {-10.0f, -5.0f, -0.0f}},
  {3, {-7.0f, -5.0f, -0.0f}},
  {4, {-9.0f, -5.0f, -2.0f, -0.0f}},
  {4, {-8.0f, -5.0f, -2.0f, -0.0f}},
  {4, {-9.0f, -5.0f, -1.0f, -0.0f}},
  {4, {-8.0f, -5.0f, -1.0f, -0.0f}},
  {4, {-7.0f, -5.0f, -2.0f, -0.0f}},
  {4, {-9.0f, -6.0f, -3.0f, -0.0f}},
  {4, {-9.0f, -5.0f, -0.0f, 2.0f}},
  {4, {-8.0f, -5.0f, -0.0f, 2.0f}},
  {4, {-9.0f, -5.0f, -3.0f, -0.0f}},
  {4, {-8.0f, -5.0f, -3.0f, -0.0f}},
  {3, {-9.0f, -6.0f, -0.0f}},
  {3, {-8.0f, -6.0f, -0.0f}},
  {4, {-9.0f, -6.0f, -2.0f, -0.0f}},
  {4, {-8.0f, -6.0f, -2.0f, -0.0f}},
  {3, {-8.0f, -4.0f, -0.0f}},
  {4, {-9.0f, -4.0f, -2.0f, -0.0f}},
  {4, {-8.0f, -4.0f, -2.0f, -0.0f}},
  {4, {-9.0f, -5.0f, -4.0f, -0.0f}},
  {4, {-9.0f, -2.0f, -0.0f, 2.0f}},
  {4, {-8.0f, -2.0f, -0.0f, 2.0f}},
  {4, {-8.0f, -6.0f, -0.0f, 2.0f}},
  {4, {-8.0f, -6.0f, -1.0f, -0.0f}},
  {4, {-8.0f, -2.0f, -0.0f, 1.0f}},
  {4, {-11.0f, -7.0f, -4.0f, -0.0f}},
  {4, {-7.0f, -5.0f, -4.0f, -0.0f}},
  {4, {-8.0f, -6.0f, -5.0f, -0.0f}},
  {4, {-8.0f, -7.0f, -3.0f, -0.0f}},
  {4, {-8.0f, -3.0f, -1.0f, -0.0f}},
  {4, {-8.0f, -1.0f, -0.0f, 2.0f}},
  {2, {-7.0f, -0.0f}},
  {2, {-5.0f, -0.0f}},
};
MT_CONTROL void tone(const float* a,float note,FmParams& p) {
  p.hz=tuned(note,a[0]);p.op[0].level=1;p.op[0].mod=4;
  const int ratio=static_cast<int>(raw(a[1])+.5f);
  p.op[4].ratio=kRatio[ratio];p.op[4].output=false;
  const float mx[]={0,32,64,96,127},my[]={0,.0437f,.1600f,.4696f,1.2548f};
  const float mod=knots(mx,my,5,a[5]),env=unit(a[6]);
  const float ex[]={0,32,64,96,127},ey[]={0,.05f,4,10,11.5f};
  const float modUnit=unit(a[5]),modSquare=modUnit*modUnit;
  const float boost=knots(ex,ey,5,a[6])*(1-.10f*(modSquare*modSquare));
  p.op[4].level=mod*(1+boost);p.op[4].sustain=1/(1+boost);
  const float feedback=unit(a[4]);
  p.op[4].decayMs=1500+6500*env;p.op[4].fb=1.2f*((feedback*feedback)*feedback);
  const float dx[]={0,32,64,80,96,110,127},dy[]={15,600,2820,4950,9000,12000,15090};
  p.ampMs=knots(dx,dy,7,a[3]);p.ampSustain=0;p.oneShot=true;
  p.attackMs=a[2]>0?.12f:.36f;
  p.pitchEnv=a[2]>0?2.f:0;p.pitchMs=8;
  p.op[0].wave=a[2]>0?4+32.f/127.f:4;
  p.outputGain=a[2]>0?.1637f:.18f;p.postGain=1.466977f;p.overComp=.46f;
}
static FM_TABLE const float kChordLevel[38]={1.0140603f, 0.9953655f, 0.9172658f, 1.0005770f, 0.9977592f, 1.0048103f, 0.9982708f, 1.0031631f, 0.9989587f, 1.0084642f, 1.0010327f, 1.0020913f, 0.9961467f, 1.0000778f, 1.0012018f, 0.9984109f, 1.0000000f, 0.9965519f, 1.0063389f, 0.9985579f, 1.0060690f, 0.9984108f, 0.9984941f, 1.0042743f, 1.0056544f, 0.9984013f, 1.0019142f, 1.0003433f, 1.0050121f, 0.9979510f, 0.9937831f, 1.0050547f, 0.9888580f, 1.0050941f, 0.9966608f, 1.0038609f, 1.2321803f, 1.5831958f};
MT_CONTROL void chord(const float* a,float note,FmParams& p) {
  p.hz=tuned(note,a[0])*(261.5268f/261.55485f);
  const int type=static_cast<int>(fclamp(raw(a[4]),0,37));const auto& c=kChord[type];
  const float bx[]={0,32,64,96,127},octave[]={0,12,0,12,24},detune[]={0,.17f,1,2.286f,4};
  const float balance=raw(a[5]),voicing=knots(bx,octave,5,balance),spread=knots(bx,detune,5,balance);
  const float voiceMix=fclamp(balance/32.f,0,1);
  const float level=kChordLevel[type];const float waveX[]={0,32,64,96,127},waveLevel[]={.8f,1,.5f,.65f,.6f};
  const float levelWave=knots(waveX,waveLevel,5,a[1]);
  for(int i=0;i<c.count;++i) {
    auto& carrier=p.op[i];const float interval=c.interval[i];
    carrier.ratio=fastExp2((type<3?interval*spread:interval+(interval<-.1f?voicing:0))/12.f);
    const float strength=type<3?level:interval<-.1f?voiceMix*level:1+(level-1)*voiceMix;
    const float upperBlend=type==37?fclamp((balance-96)/31.f,0,1):0;
    const float upperGain=1-upperBlend*(interval<-.1f?.04f:.52f);
    carrier.level=.5f*strength*levelWave*upperGain;carrier.wave=4+raw(a[1])*(1.f/127.f);
  }
  const float env=unit(a[6]);
  // FENV is weak at the baseline but stronger at extreme BAL; retain an independent temporal layer.
  for(int i=0;i<c.count;++i) {
    auto& partial=p.op[i+4];partial.ratio=p.op[i].ratio*3;
    partial.level=p.op[i].level*env*.03f*unit(a[5]);partial.sustain=0;partial.decayMs=300+env*6000;
  }
  const float dx[]={0,32,64,80,96,127},dy[]={30,400,1800,4500,8000,12000};
  p.ampMs=knots(dx,dy,6,a[3]);p.ampSustain=.6f;p.oneShot=false;
  p.attackMs=a[2]>0?.12f:.36f;p.outputGain=a[2]>0?.15f:.18f;p.postGain=1.980463f;
}
MT_CONTROL void toy(const float* a,float note,FmParams& p) {
  p.hz=tuned(note,a[0]);p.op[0].level=1;
  const float x[]={0,32,64,96,127};
  const float mode2[]={720.92f,621.235f,522.99f,515.45f,523.10f};
  const float mode3[]={1413.f,1096.364f,788.33f,1018.14f,1046.22f};
  const float mode4[]={2335.79f,1685.923f,1048.98f,1229.70f,1569.34f};
  const float ratios[]={1,knots(x,mode2,5,a[1])/261.55765f,knots(x,mode3,5,a[1])/261.55765f,
    knots(x,mode4,5,a[1])/261.55765f,324.59f/261.55765f,2517.31f/261.55765f,3938.26f/261.55765f,5359.22f/261.55765f};
  static const float modeDb[7][5]={
    {-33,-31.6f,-22.624f,-16.3f,-13.1f},{-36,-34.8f,-28.444f,-21.1f,-17.1f},
    {-39,-37.9f,-32.358f,-25,-19.2f},{-80,-70,-55,-36.8f,-26.1f},
    {-80,-65,-52,-37.3f,-27.1f},{-90,-75,-65,-48,-37.1f},{-100,-85,-75,-58,-46.8f}};
  const float brightness=(raw(a[5])-64)*(19.9f/127.f);
  for(int i=1;i<kFmOps;++i) {
    p.op[i].ratio=ratios[i];p.op[i].level=2*fastExp2((knots(x,modeDb[i-1],5,a[6])+brightness)/6.0206f);
    p.op[i].decayMs=1800;
  }
  const float dx[]={0,32,64,80,96,127},dy[]={44,220,760,1290,1800,3030};
  p.ampMs=knots(dx,dy,6,a[3]);
  const float impact=unit(a[4]),impact2=impact*impact,impact4=impact2*impact2;
  p.noise=.001f*(impact4*impact4);p.noiseMs=30;
  p.filterMode=Svf::Mode::Hp;p.filterHz=6000;
  p.outputGain=a[2]>0?.07149f:.09f;p.postGain=2.020983f;p.attackMs=.36f;p.oneShot=true;
}
}  // namespace

MT_CONTROL float fmToneRatio(float value) {
  return kRatio[static_cast<int>(raw(value)+.5f)];
}

MT_CONTROL int fmChordSignature(uint8_t rawType,float out[4]) {
  const auto& chord=kChord[rawType<38?rawType:37];
  if(out)for(int i=0;i<chord.count;++i)out[i]=chord.interval[i];
  return chord.count;
}

MT_CONTROL bool fmTonalMachine(uint8_t machine,const float rawParams[8],float note,FmParams& p) {
  if(!rawParams)return false;
  switch(static_cast<FmMachine>(machine)) {
    case FmMachine::Tone:p=FmParams();tone(rawParams,note,p);break;
    case FmMachine::Chord:p=FmParams();chord(rawParams,note,p);break;
    case FmMachine::Toy:p=FmParams();toy(rawParams,note,p);break;
    default:return false;
  }
  p.sat=unit(rawParams[7]);return true;
}
}  // namespace mt
