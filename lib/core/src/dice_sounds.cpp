#include "dice_sounds.h"
#include "dice_feel.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace mt { namespace dice_detail {
namespace {
int bounded(int value,int lo=0,int hi=127) { return std::max(lo,std::min(hi,value)); }
int detail(int value,int range,int amount,Rng& rng) {
  return bounded(value+(int(rng.below(range*2+1))-range)*amount/100);
}
void varyDrum(Instrument& m,int amount,Rng& rng,DiceVoiceKind kind,DiceProduction production) {
  if(!amount && production.voice==DiceVoicing::Default) return;
  m.cutoff=detail(m.cutoff,4,amount,rng);
  m.reso=detail(m.reso,2,amount,rng);
  m.release=detail(m.release,3,amount,rng);
  if(m.type==InstrType::Fm) m.fmParam[5]=std::min(100,detail(m.fmParam[5],2,amount,rng));
  if(kind!=DiceVoiceKind::Kick) {
    // Per-machine decay: FM percussion uses parameter D, DRUM uses DEC.
    if(kind==DiceVoiceKind::Snare || kind==DiceVoiceKind::Clap || kind==DiceVoiceKind::OpenHat) {
      const int tail=directions[unsigned(production.voice)].release;
      if(m.type==InstrType::Fm) m.fmParam[3]=bounded(m.fmParam[3]+tail,2,115);
      if(m.type==InstrType::Drum) m.macro[kMacDec]=bounded(m.macro[kMacDec]+tail,3,120);
    }
    return;
  }
  const auto& sound=directions[unsigned(production.voice)];
  const bool psy=production.voice==DiceVoicing::PsyRoll;
  const bool hard=production.voice==DiceVoicing::Hard;
  if(m.type==InstrType::Fm && m.machine==uint8_t(FmMachine::BdModern)) {
    // BD Modern is tuned around 65 Hz. Shape a brief FM attack separately
    // from the sine tail; a low modulation sustain keeps the body round.
    const int colour=detail(64,12,amount,rng)-64;
    m.fmParam[0]=bounded(m.fmParam[0]+sound.kickTune+colour/3);
    m.fmParam[1]=bounded(m.fmParam[1]+sound.sweep+colour);
    m.fmParam[5]=bounded(sound.attackTone+production.shade+colour,64,83);
    m.fmParam[6]=hard ? 21 : psy ? 8 : 14;
    m.fmParam[7]=bounded(m.fmParam[7]+sound.over,0,49);
    if(psy) m.fmParam[3]=bounded(m.fmParam[3]-7,6,22);
  } else if(m.type==InstrType::Drum && hard && m.machine==uint8_t(DrumMachine::Bd9)) {
    // The drum machine clips its body oscillator before the track insert.
    // A longer driven tail supplies the bass note of a gabber kick.
    const int bite=detail(102+4*production.shade,20,amount,rng);
    m.macro[kMacCol]=bounded(bite-4,84,119);
    m.macro[kMacShp]=bounded(bite+9,98,125);
    m.macro[kMacSwp]=bite;
    m.macro[kMacCon]=detail(38,12,amount,rng);
    m.macro[kMacDec]=bounded(m.macro[kMacDec]+15,62,98);
    m.cutoff=bounded(m.cutoff+8,104,120);
    m.reso=bounded(m.reso,18,32);
    m.vol=m.vol*3/4;
  } else if(m.type==InstrType::Drum && m.machine==uint8_t(DrumMachine::Bd8) && sound.sweep>=11) {
    // Leave room after the 808 transient for the next bass subdivision.
    m.macro[kMacDec]=m.macro[kMacDec]*(psy ? 52 : 75)/100;
  }
}

struct ToneBalance {
  int8_t cutoff, release, detune;
  // Voicing for the catalogue's generic analog bass. Dedicated sub, acid
  // and Reese patches retain their own oscillators.
  SynOsc bass1, bass2;
  uint8_t bassMix, bassDetune, bassSemitones;
};

using Osc=SynOsc;
constexpr ToneBalance balances[] = {
    {-2,-3, 0, Osc::Square,Osc::Tri,  52,64, 0}, // Pocket: hollow pulse over a rounded body
    { 0,-5, 4, Osc::Saw,   Osc::Saw,  56,78, 0}, // Broken: beating saws
    {-6, 7, 6, Osc::Sine,  Osc::Tri,  29,64,12}, // Air: quiet upper harmonic
    {-5, 2,-2, Osc::Tri,   Osc::Square,22,67,12}, // Keys: upper reed on the attack
    {-7,-4, 1, Osc::Tri,   Osc::Sine, 35,64, 0}, // Echo: rounded, short low end
    { 1,-6,-1, Osc::Saw,   Osc::Square,31,64,12}, // Motor: octave pulse
    { 3, 3, 6, Osc::Square,Osc::Saw,  29,67, 0}, // Anthem: low foundation for the hook
};
static_assert(sizeof(balances)/sizeof(*balances)==1+static_cast<unsigned>(DiceFeel::Anthem),"DiceFeel table");

void voiceSynth(Instrument& m,const DiceTimbre& recipe,DiceVoiceKind kind,DiceFeel feel,int colour,
                bool foreground,DiceVoicing voicing) {
  const auto& balance=balances[static_cast<unsigned>(feel)];
  if(!std::strcmp(recipe.name,"ANALOGBS")) {
    m.synOsc[0]=uint8_t(balance.bass1);
    m.synOsc[1]=uint8_t(balance.bass2);
    m.synSemi=balance.bassSemitones;
    m.macro[kMacMix]=balance.bassMix;
    m.macro[kMacDet]=balance.bassDetune;
  }
  m.synEnvDest=recipe.params[5];
  m.synEDec=bounded(recipe.params[6]-colour/3,8,84);
  m.synPhase[1]=recipe.params[7];
  m.macro[kMacSenv]=bounded(m.macro[kMacSenv]+colour/2,66,112);
  m.macro[kMacMix]=bounded(m.macro[kMacMix]+colour/2,4,92);
  // Keep the oscillator tuning locked on patches authored at exact intervals.
  if(m.macro[kMacDet]!=64) m.macro[kMacDet]=bounded(m.macro[kMacDet]+balance.detune+colour/3,65,94);
  if(kind==DiceVoiceKind::Bass && (!std::strcmp(recipe.name,"SUB") || !std::strcmp(recipe.name,"ANALOGBS"))) {
    m.synSatType=uint8_t(SynSatType::Light);
    m.fmSat=bounded(80+colour/3,68,91);
    m.vol=bounded(m.vol+10,1,114);
    m.synSub=m.synSub*3/5;
    if(!std::strcmp(recipe.name,"SUB")) {
      m.synOsc[1]=uint8_t(SynOsc::Tri);
      m.macro[kMacMix]=bounded(36+colour/2,24,48);
    } else if(voicing==DiceVoicing::Minimal) {
      m.synOsc[0]=uint8_t(SynOsc::Tri);
      m.synOsc[1]=uint8_t(SynOsc::Saw);
    }
    if(voicing==DiceVoicing::PsyRoll) {
      m.attack=0; m.decay=23; m.sustain=68; m.release=11;
      m.fDec=19; m.fenv=26; m.synPhaseMode=uint8_t(SynPhaseMode::Retrig);
    }
  }
  if(kind==DiceVoiceKind::Pad) {
    m.synPhaseMode=uint8_t(SynPhaseMode::Free);
    m.lfo[0]={uint8_t(LfoWave::Tri),9,12,uint8_t(LfoDest::Shp),kLfoTempo|kLfoFree}; // oscillator mix
    m.fAtk=26;
  }
  if(feel==DiceFeel::Anthem && foreground && !std::strcmp(recipe.name,"SAW LEAD")) {
    // The group has its own spread and density. MIX, osc2 and its envelope
    // routing have no effect on HyperSaw. A small density bloom shapes the attack.
    m.synOsc[0]=uint8_t(SynOsc::HyperSaw);
    m.synPhaseMode=uint8_t(SynPhaseMode::Random);
    m.macro[kMacShp1]=bounded(68+colour,44,89);
    m.macro[kMacDet]=bounded(71+colour/2,58,85);
    m.synEnvDest=uint8_t(SynEnvDest::Shape);
    m.macro[kMacSenv]=76;
    m.vol=m.vol*4/5;
  }
}

void voiceTonal(Instrument& m,const DiceVoice& voice,DiceFeel feel,int amount,Rng& rng,DicePart companion,DiceVoicing voicing) {
  const auto kind=static_cast<DiceVoiceKind>(voice.kind);
  const auto& balance=balances[static_cast<unsigned>(feel)];
  // One draw coordinates brightness, transient harmonics and decay.
  // Zero Variety keeps the authored settings for this genre.
  const int colour=(int(rng.below(65))-32)*amount/100;
  m.cutoff=bounded(m.cutoff+balance.cutoff+colour,24,116);
  m.release=bounded(m.release+balance.release-colour/3,12,88);
  // The cutoff LFO spans six octaves at full depth. Preserve the rhythmic
  // sweep without periodically filtering away the low end of the patch.
  if(m.lfoDest==uint8_t(LfoDest::Cutoff)) {
    const int depth=kind==DiceVoiceKind::Bass ? 24 : 16;
    m.lfoDepth=bounded(m.lfoDepth,-depth,depth);
  }
  m.velCut=kind==DiceVoiceKind::Pad ? 5 : 14;
  m.keytrack=kind==DiceVoiceKind::Bass ? 16 : 38;
  switch(m.type) {
    case InstrType::Synth: {
      const bool guideLead=kind==DiceVoiceKind::Lead && !std::strcmp(voice.name,"Lead");
      const bool foreground=companion==DicePart::Count ? guideLead : companion==DicePart::Lead;
      voiceSynth(m,diceTimbre(voice),kind,feel,colour,foreground,voicing);
      break;
    }
    case InstrType::Fm:
      m.fmParam[5]=bounded(m.fmParam[5]+colour,18,98);
      m.fmParam[6]=bounded(m.fmParam[6]+colour/2,20,114);
      break;
    case InstrType::String:
      m.macro[kMacBright]=bounded(m.macro[kMacBright]+colour,20,88);
      m.macro[kMacPos]=bounded(m.macro[kMacPos]-colour,12,72);
      break;
    default: break;
  }
}

}
void buildVoice(const DiceVoice& v,Instrument& m,TrackCfg& c,bool clean,int variety,Rng& rng,DiceFeel feel,DicePart companion,DiceProduction production) {
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
  // PITCH spans an octave in this engine. Sample vibrato on a pitched
  // instrument belongs on FINE (one semitone at full modulation).
  if(!dicePercussion(v) && m.lfoDepth && m.lfoDest==static_cast<uint8_t>(LfoDest::Pitch)) {
    m.lfoDest=static_cast<uint8_t>(LfoDest::Fine);
    m.lfoDepth=std::min<int>(m.lfoDepth,m.mono ? 12 : 24);
  }
  const int amount=bounded(variety,0,100);
  if(dicePercussion(v)) varyDrum(m,amount,rng,static_cast<DiceVoiceKind>(v.kind),production);
  else {
    voiceTonal(m,v,feel,amount,rng,companion,production.voice);
    const auto& sound=directions[unsigned(production.voice)];
    m.cutoff=bounded(m.cutoff+sound.tone+3*production.shade,24,118);
    // Fast bass releases already have their own calibrated envelope.
    if(!m.mono) m.release=bounded(m.release+sound.release,9,95);
  }
  if(clean) c=TrackCfg();
  snprintf(c.name,sizeof(c.name),"%s",v.name);
  c.out=TrackOut::Int;c.defVel=100;
  c.defGate=m.type==InstrType::String ? 115 : 55;
  c.vol=bounded(v.volume*3/2,20,115);c.drive=r.drive;
}
}}
