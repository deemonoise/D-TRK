// Isolated CPU benchmark: production Synth, no SD access or project writes.
#if defined(FM_REFERENCE_BENCH)
#include <Arduino.h>
#include <initializer_list>
#include "synth.h"
#include "stack_mark.h"
namespace {
mt::SynthModel model;
mt::Synth synth(model);
// Approximate production RAM1 stack headroom; the final desk image is checked separately.
volatile uint8_t stackReserve[230000];
DMAMEM int16_t delayLine[32768],modLine[mt::ModFx::kLineLen],shimLine[mt::PitchShift::kLen];
DMAMEM float reverb[mt::Reverb::kBufLen],shimWet[mt::Synth::kBlock];
int16_t left[mt::Synth::kBlock],right[mt::Synth::kBlock];
void strike(int voices,int note) {
  for(int i=0;i<voices;++i){
    const uint8_t pitch=static_cast<uint8_t>(note+i%4);
    uint8_t off[3]={0x80,pitch,0},on[3]={0x90,pitch,100};
    synth.event(0,i,off,3);synth.event(1,i,on,3);
  }
}
void run(int machine,int voices,bool stress,bool budgetOn=false,bool profile=false,int hatTone=-1) {
  synth.reset();synth.setVoiceBudget(budgetOn?mt::kVoiceBudgetTeensy:mt::kVoiceBudgetOff);
  synth.setProfiler(profile?+[]()->uint32_t{return ARM_DWT_CYCCNT;}:nullptr);
  for(int t=0;t<mt::kTracks;++t){
    auto& m=model.instruments[t];mt::instrSetType(m,mt::InstrType::Fm);mt::fmSetMachine(m,machine);
    m.mono=true;m.fmParam[3]=127;m.vol=24;m.attack=0;m.sustain=127;m.release=0;m.send=30;m.rsend=40;
    if(stress){
      // Exercise the expensive sweep, punch, waveform and long-transient branches too.
      m.fmParam[1]=machine==6?96:(machine<=3?64:127);
      m.fmParam[2]=127;
      m.fmParam[4]=machine==6?16:127;m.fmParam[5]=127;m.fmParam[6]=127;m.fmParam[7]=127;
      m.lfoWave=static_cast<uint8_t>(mt::LfoWave::Tri);m.lfoRate=96;m.lfoDepth=40;m.lfoDest=static_cast<uint8_t>(mt::LfoDest::Col);}
    if(machine==9 && hatTone>=0)m.fmParam[1]=hatTone;
    model.tracks[t].out=mt::TrackOut::Int;model.tracks[t].vol=90;model.tracks[t].instr=t;
  }
  for(int t=0;t<mt::kTracks;++t)synth.startTrack(t);
  strike(voices,stress?84:60);
  uint64_t sum=0;uint32_t peak=0,cold=0;int maxLive=0;
  constexpr int settle=20,blocks=180;
  for(int i=-settle;i<blocks;++i){
    if(stress && i>=0 && i%48==0)strike(voices,84);
    const uint32_t start=ARM_DWT_CYCCNT;synth.render(left,right);const uint32_t used=ARM_DWT_CYCCNT-start;
    if(i<0){if(used>cold)cold=used;}else{sum+=used;if(used>peak)peak=used;}
    const int live=synth.activeVoices();if(live>maxLive)maxLive=live;
  }
  const double budget=double(F_CPU_ACTUAL)*mt::Synth::kBlock/mt::kSynthRate;
  Serial.printf("FMREF machine=%d voices=%d active=%d stress=%d budget=%d avg=%.3f peak=%.3f cold=%.3f maxlive=%d\n",machine,voices,synth.activeVoices(),stress,budgetOn,100.*sum/blocks/budget,100.*peak/budget,100.*cold/budget,maxLive);
  if(!budgetOn && maxLive>voices)Serial.println("FMREF INVALID extra polyphonic voices");
  Serial.flush();
  if(profile){
    uint32_t cycles[mt::Synth::kProfStages],counts[mt::Synth::kProfStages],frames;
    synth.takeProfile(cycles,counts,frames);
    for(int stage=0;stage<mt::Synth::kProfStages;++stage)
      if(cycles[stage])Serial.printf("FMREF PROFILE machine=%d stage=%d cpu=%.3f calls=%lu\n",machine,stage,100.*cycles[stage]/frames/budget,(unsigned long)counts[stage]);
    synth.setProfiler(nullptr);Serial.flush();
  }
}
void pairSuite(){
  Serial.printf("FMREF BEGIN cpu=%lu voice=%u engine=%u model=%u synth=%u pairs=1\n",(unsigned long)F_CPU_ACTUAL,(unsigned)sizeof(mt::Voice),(unsigned)sizeof(mt::VoiceEngine),(unsigned)sizeof(model),(unsigned)sizeof(synth));
  for(int machine:{0,8})for(bool stress:{false,true})for(int voices:{0,4,8,16})run(machine,voices,stress);
  for(int machine:{0,8})run(machine,16,true,true);
  Serial.printf("FMREF stack_free=%lu\n",(unsigned long)stackmark::minFree());Serial.println("FMREF DONE");Serial.flush();
}
void hatSuite(){
  Serial.println("FMREF BEGIN hats=1");
  for(int tone:{0,32,64,127}) {
    Serial.printf("FMREF TONE=%d\n",tone);
    run(9,8,false,false,false,tone);
    run(9,16,true,false,false,tone);
    run(9,16,true,true,false,tone);
  }
  Serial.printf("FMREF stack_free=%lu\n",(unsigned long)stackmark::minFree());Serial.println("FMREF DONE");Serial.flush();
}
void suite(){
  Serial.printf("FMREF BEGIN cpu=%lu voice=%u engine=%u model=%u synth=%u\n",(unsigned long)F_CPU_ACTUAL,(unsigned)sizeof(mt::Voice),(unsigned)sizeof(mt::VoiceEngine),(unsigned)sizeof(model),(unsigned)sizeof(synth));
  for(int machine=0;machine<static_cast<int>(mt::FmMachine::Count);++machine)
    for(bool stress:{false,true})for(int voices:{0,4,8,16})run(machine,voices,stress);
  for(int machine=0;machine<static_cast<int>(mt::FmMachine::Count);++machine)run(machine,16,true,true);
  Serial.printf("FMREF stack_free=%lu\n",(unsigned long)stackmark::minFree());Serial.println("FMREF DONE");Serial.flush();
}
}
void setup(){
  stackmark::paint();stackReserve[0]=1;Serial.begin(115200);while(!Serial&&millis()<5000){}delay(1200);
  ARM_DEMCR|=ARM_DEMCR_TRCENA;ARM_DWT_CTRL|=ARM_DWT_CTRL_CYCCNTENA;
  synth.setDelayBuffer(delayLine,32768);synth.setReverbBuffer(reverb,mt::Reverb::kBufLen);
  synth.setModBuffer(modLine,mt::ModFx::kLineLen);synth.setShimmerBuffer(shimLine,mt::PitchShift::kLen,shimWet);
  model.masterVol=100;model.compAmt=64;model.rvbLevel=64;model.dlyLevel=50;model.eqLow=75;
  const char initial=Serial.available()?Serial.read():0;
  if(initial=='t')pairSuite();else if(initial=='h')hatSuite();else suite();
}
void loop(){if(Serial.available()){const char cmd=Serial.read();if(cmd=='r')suite();if(cmd=='h')hatSuite();if(cmd=='t')pairSuite();if(cmd=='p'){for(int machine:{0,7,8,9})run(machine,16,true,false,true);Serial.println("FMREF PROFILE DONE");Serial.flush();}}delay(10);}
#endif
