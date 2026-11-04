// Standalone USB hardware benchmark. Deliberately no AudioStream ISR/SD/PSRAM assertions.
#if defined(AUDIO_QUALITY_BENCH)
#include <Arduino.h>
#include <math.h>
#include <initializer_list>
#include <string.h>
#include "synth.h"
#include "synth_modfx.h"
#include "synth_shimmer.h"
#include "stack_mark.h"
namespace {
mt::SynthModel model;
mt::Synth synth(model);
// Constrain RAM1 stack to approximately the production desk image (15KB), rather than
// letting the standalone harness hide a large DSP stack frame in its otherwise empty RAM1.
volatile uint8_t stackReserve[233000];
DMAMEM int16_t sample[16384], delayLine[32768], modLine[mt::ModFx::kLineLen], shimLine[mt::PitchShift::kLen];
DMAMEM float reverb[mt::Reverb::kBufLen], shimWet[mt::Synth::kBlock];
int16_t left[mt::Synth::kBlock],right[mt::Synth::kBlock];
struct Bank:mt::SampleSource {bool find(const char*,mt::SampleData&d)const override {d=mt::SampleData{};d.key=1;d.frames=16384;d.rate=mt::kSynthRate;d.whole=sample;return true;}} bank;
void run(int voices,bool fx,int semis,int profile=0){
 synth.reset();synth.setVoiceBudget(profile>=10?mt::kVoiceBudgetTeensy:mt::kVoiceBudgetOff);const int shape=profile%10;model.masterVol=100;model.compAmt=fx?64:0;model.rvbLevel=fx?64:0;model.dlyLevel=fx?50:0;model.modType=0;model.eqLow=fx?75:64;
 for(int index=0;index<mt::kInstruments;++index){auto&m=model.instruments[index];mt::instrSetType(m,mt::InstrType::Sample);strcpy(m.sample,"bench");m.root=shape==4?0:((shape==5||shape==6)?60+index%4:60);m.loop=1;m.vol=40;m.attack=0;m.decay=0;m.sustain=127;m.release=0;m.send=fx?35:0;m.rsend=fx?40:0;m.fine=(shape==1||shape==3)?(index%8)*7:0;m.lfoDest=static_cast<uint8_t>(mt::LfoDest::Pitch);m.lfoDepth=(shape==2||shape==3)?2:0;m.lfoRate=50;}
 for(int t=0;t<mt::kTracks;++t){model.tracks[t].out=mt::TrackOut::Int;model.tracks[t].vol=127;model.tracks[t].instr=shape?t:0;}
 for(int i=0;i<voices;++i){if(shape==5||shape==6){uint8_t program[2]={0xc0,static_cast<uint8_t>(i)};synth.event(0,i/4,program,2);}uint8_t b[3]={0x90,static_cast<uint8_t>(60+semis+(i%4)),100};synth.event(0,i/4,b,3);}
 uint64_t sum=0;uint32_t peak=0,coldPeak=0;const int settle=semis>=24?30:150, blocks=semis>=24?100:600;
 for(int i=-settle;i<blocks;++i){if(shape==6&&i==0){for(auto&m:model.instruments)m.root=0;synth.setProfiler(+[]()->uint32_t{return ARM_DWT_CYCCNT;});}uint32_t t=ARM_DWT_CYCCNT;synth.render(left,right);uint32_t c=ARM_DWT_CYCCNT-t;if(shape==6&&i==0){uint32_t cycles[mt::Synth::kProfStages],counts[mt::Synth::kProfStages],blocks;synth.takeProfile(cycles,counts,blocks);for(int stage=0;stage<mt::Synth::kProfStages;++stage)Serial.printf("AQ burst_stage=%d cycles=%lu\n",stage,(unsigned long)cycles[stage]);synth.setProfiler(nullptr);}if(i>=0){sum+=c;if(c>peak)peak=c;}else if(c>coldPeak)coldPeak=c;}
 double budget=double(F_CPU_ACTUAL)*mt::Synth::kBlock/mt::kSynthRate;
 Serial.printf("AQ voices=%d active=%d fx=%d semis=%d profile=%d avg=%.3f%% peak=%.3f%% cold=%.3f%% cycles=%lu/%lu\n",voices,synth.activeVoices(),fx,semis,profile,100.*sum/blocks/budget,100.*peak/budget,100.*coldPeak/budget,(unsigned long)(sum/blocks),(unsigned long)peak);Serial.flush();
}
}
void setup(){stackmark::paint();stackReserve[0]=1;Serial.begin(115200);while(!Serial&&millis()<5000){};delay(1500);ARM_DEMCR|=ARM_DEMCR_TRCENA;ARM_DWT_CTRL|=ARM_DWT_CTRL_CYCCNTENA;
 for(int i=0;i<16384;++i)sample[i]=static_cast<int16_t>(12000*sinf(6.2831853f*37*i/16384));
 synth.setBank(&bank);synth.setDelayBuffer(delayLine,32768);synth.setReverbBuffer(reverb,mt::Reverb::kBufLen);synth.setModBuffer(modLine,mt::ModFx::kLineLen);synth.setShimmerBuffer(shimLine,mt::PitchShift::kLen,shimWet);
 Serial.printf("AQ BEGIN cpu=%lu voices=%d model=%u synth=%u\n",(unsigned long)F_CPU_ACTUAL,mt::kVoices,(unsigned)sizeof(model),(unsigned)sizeof(synth));
 for(bool fx:{false,true})for(int n:{0,4,8,16,24,32})run(n,fx,12);
 for(int semis:{0,4,24})run(32,false,semis);
 for(int profile:{1,2,3})run(32,true,12,profile);
 for(int semis:{24,36,48})for(int n:{1,4,8,16})run(n,true,semis);
 run(1,true,36,4);
 run(32,true,0,5);
 run(32,true,0,15);
 run(32,true,0,16);
 for(int profile:{10,11,12,13})run(32,true,12,profile);
 for(int semis:{24,36,48})run(32,true,semis,10);
 Serial.printf("AQ stack_free=%lu\n",(unsigned long)stackmark::minFree());Serial.println("AQ DONE");Serial.flush();}
void loop(){
 if(Serial.available() && Serial.read()=='r'){
  Serial.println("AQ REPEAT");
  for(bool fx:{false,true})for(int n:{0,4,8,16,24,32})run(n,fx,12);
  for(int semis:{0,4,24})run(32,false,semis);
 for(int profile:{1,2,3})run(32,true,12,profile);
 for(int semis:{24,36,48})for(int n:{1,4,8,16})run(n,true,semis);
 run(1,true,36,4);
 run(32,true,0,5);
 run(32,true,0,15);
 run(32,true,0,16);
 for(int profile:{10,11,12,13})run(32,true,12,profile);
 for(int semis:{24,36,48})run(32,true,semis,10);
  Serial.printf("AQ stack_free=%lu\n",(unsigned long)stackmark::minFree());Serial.println("AQ DONE");Serial.flush();
 }
 delay(10);
}
#endif
