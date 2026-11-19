#include "dice_sounds.h"
#include <algorithm>
#include <cstdio>
namespace mt { namespace dice_detail {
namespace {
int bounded(int value,int lo=0,int hi=127) { return std::max(lo,std::min(hi,value)); }
int detail(int value,int range,int amount,Rng& rng) {
  return bounded(value+(int(rng.below(range*2+1))-range)*amount/100);
}
void variation(Instrument& m,int amount,Rng& rng) {
  if(!amount) return;
  m.cutoff=detail(m.cutoff,4,amount,rng);
  m.reso=detail(m.reso,2,amount,rng);
  m.release=detail(m.release,3,amount,rng);
  if(m.type==InstrType::Fm) m.fmParam[5]=std::min(100,detail(m.fmParam[5],2,amount,rng));
  if(m.type==InstrType::String) m.macro[1]=detail(m.macro[1],2,amount,rng);
}
}
void buildVoice(const DiceVoice& v,Instrument& m,TrackCfg& c,bool clean,int variety,Rng& rng) {
  const auto& r=diceTimbre(v);
  m=Instrument();instrSetType(m,static_cast<InstrType>(r.type));
  m.machine=r.machine;
  std::copy(r.params,r.params+8,m.fmParam);
  std::copy(r.params,r.params+5,m.macro);
  std::copy(r.osc,r.osc+2,m.synOsc);
  m.wave=r.osc[0];m.synSemi=r.semi;m.synSub=r.sub;m.synNoise=r.noise;
  m.synSubWave=static_cast<uint8_t>(SynSubWave::Sine);
  m.attack=r.env[0];m.decay=r.env[1];m.sustain=r.env[2];m.release=r.env[3];
  m.synEDec=r.fdec;m.glide=r.glide;m.vol=r.level;
  m.fltMode=r.filter;m.cutoff=r.cutoff;m.reso=r.resonance;m.fenv=r.fenv;m.fDec=r.fdec;
  m.mono=static_cast<DiceVoiceKind>(v.kind)==DiceVoiceKind::Bass;
  m.send=v.delay;m.rsend=v.reverb;
  snprintf(m.name,sizeof(m.name),"%s",r.name);
  const uint8_t* lfo=v.lfo[3] ? v.lfo : r.lfo;
  m.lfoDest=lfo[0];m.lfoWave=lfo[1];m.lfoRate=lfo[2];m.lfoDepth=lfo[3];m.lfoSync=kLfoTempo;
  if(m.type==InstrType::Synth && m.lfoDest==static_cast<uint8_t>(LfoDest::Dec))
    m.lfoDest=static_cast<uint8_t>(LfoDest::Cutoff);
  variation(m,bounded(variety,0,100),rng);
  if(clean) c=TrackCfg();
  snprintf(c.name,sizeof(c.name),"%s",v.name);
  c.out=TrackOut::Int;c.defVel=100;
  c.defGate=m.type==InstrType::String ? 115 : 55;
  c.vol=bounded(v.volume*3/2,20,115);c.drive=r.drive;
}
}}
