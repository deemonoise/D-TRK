#include "dice_hybrid.h"
#include "dice_band.h"
#include "dice_sounds.h"
#include "dice_feel.h"
#include "gen_util.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
namespace mt { namespace dice_detail {
namespace {
constexpr int harmonyTrack=kTracks-1;
int clamp(int n,int lo=0,int hi=127) { return std::max(lo,std::min(hi,n)); }
bool named(const char* text,const char* needle) {
  for(;*text;++text) {const char* a=text;const char* b=needle;while(*a && *b && std::tolower(static_cast<unsigned char>(*a))==std::tolower(static_cast<unsigned char>(*b))){++a;++b;}if(!*b)return true;}
  return false;
}
bool playable(const DiceVoice& v) {
  for(int i=0;i<diceEventCount(v);++i) if(!v.length || diceEvent(v,i).at<v.length) return true;
  return false;
}
const char* palette(DicePart part,const HybridStyle& style,const DiceEnsemble& ensemble) {
  switch(part) {
    case DicePart::Bass: return ensemble.bass;
    case DicePart::Chords: return ensemble.keys;
    case DicePart::Lead: return ensemble.lead;
    case DicePart::Arp: return style.acid ? "303" : ensemble.arp;
    case DicePart::Pad: return "WARM PAD";
    case DicePart::Texture: return "DRONE";
    case DicePart::Counter: return ensemble.arp;
    case DicePart::Pulse: return "PLUCK";
    default: return style.quiet ? "MALLET" : "PLUCK";
  }
}
const char* companionPatch(DicePart part,const HybridStyle& style,const PhrasePlan& plan,const DiceMix& mix,const char* band) {
  // Try both ensembles for this genre, then alternates with a suitable attack
  // and sustain. Prefer an unoccupied patch, even when rerolling a single part.
  static constexpr const char* alternatives[][4]={
    {"SUB","ANALOGBS","808 SUB","REESE"},        // Bass
    {"E PIANO","STAB","ORGAN","WARM PAD"},       // Chords
    {"PULSE","FM KEYS","PLUCK","MALLET"},        // Lead
    {"PLUCK","PULSE","FM KEYS","MALLET"},        // Arp
    {"MALLET","FM BELL","PLUCK","PULSE"},        // Fx
    {"DRONE","ORGAN","VOX FORM","E PIANO"},      // Pad
    {"MALLET","ACOU GTR","FM KEYS","PLUCK"},     // Counter
    {"WARM PAD","MALLET","FM BELL","ORGAN"},     // Texture
    {"PULSE","MALLET","PLUCK","FM KEYS"},        // Pulse
  };
  static_assert(sizeof(alternatives)/sizeof(*alternatives)==unsigned(DicePart::Count),"DicePart palettes");
  const auto* alt=alternatives[unsigned(part)];
  const auto& sound=directions[unsigned(plan.production.voice)];
  const unsigned accent=(plan.seed^(plan.seed>>16))&1;
  const bool upper=part==DicePart::Lead || (part==DicePart::Arp && !style.acid);
  const unsigned index=accent^(part==DicePart::Arp ? 1u : 0u);
  const char* choices[]={band,upper ? sound.accent[index] : palette(part,style,diceEnsemble(plan.feel,plan.seed)),
                        upper ? sound.accent[index^1] : palette(part,style,diceEnsemble(plan.feel,plan.seed^1)),alt[0],alt[1],alt[2],alt[3]};
  int fewest=kDiceRoles+1;
  const char* best=choices[0];
  for(const char* choice:choices) {
    if(!choice) continue;
    int used=0;
    for(const char* other:mix.patch) if(other && !std::strcmp(other,choice)) ++used;
    if(!used) return choice;
    if(used<fewest) {best=choice;fewest=used;}
  }
  return best;
}
bool centred(int source,const DiceVariant& v,const PhrasePlan& plan) {
  if(diceSupplement(source)) {
    const auto part=dicePartKind(source);
    return part==DicePart::Bass || part==DicePart::Lead;
  }
  if(source<0) return true;
  const auto& voice=diceVoice(v,source);
  return voice.kind==uint8_t(DiceVoiceKind::Bass) || !std::strcmp(voice.name,"Lead") ||
         (plan.feel==DiceFeel::Air && voice.kind==uint8_t(DiceVoiceKind::Pad) && source==plan.harmonicSource);
}
void clearWork(Pattern& p,const DicePat& target,int length) {
  p.length=length;p.res=target.res;p.swing=target.swing;p.groove=target.groove;
  std::fill(p.trackLen,p.trackLen+kTracks,0);std::fill(p.trackSpeed,p.trackSpeed+kTracks,0);
  for(auto& row:p.steps) std::fill(row,row+kMaxSteps,Step());
}
void harmonyRow(Pattern& p,int stepTicks,const DiceRecipe& recipe,const DiceVariant& variant,const DiceParts& parts,const PhrasePlan& plan) {
  DiceChord previous;bool have=false;
  for(int at=0;at<p.length;++at) {
    DiceChord chord;
    if(!harmonyAt(variant,recipe,at*stepTicks,parts.scaleRoot,parts.scaleType,plan,chord)) chord={static_cast<uint8_t>(60+parts.scaleRoot),kChordTriad};
    if(have && chord.root==previous.root && chord.chord==previous.chord) continue;
    auto& s=p.steps[harmonyTrack][at];s.note=chord.root;s.vel=80;putFx(s,Fx::CHD,chord.chord);
    previous=chord;have=true;
  }
}
int noteCount(const ArpPattern& pattern) {int n=0;for(int i=0;i<pattern.len;++i)n+=pattern.steps[i].kind==ArpKind::Note;return n;}
int choosePattern(DiceHybridWork& work,bool bass,int desired,Rng& rng,bool acid) {
  const int count=bass ? bassFactoryCount() : arpFactoryCount();int chosen=0,best=100000;
  for(int i=0;i<count;++i) {
    const char* name=bass ? bassFactoryName(i) : arpFactoryName(i);
    int hits=0,len=16;
    if(bass) {parseBassPattern(bassFactoryText(i),work.bass);len=work.bass.len;for(int j=0;j<len;++j)hits+=work.bass.steps[j].kind==ArpKind::Note;}
    else {parseArpPattern(arpFactoryText(i),work.arp);len=work.arp.len;hits=noteCount(work.arp);}
    if(!len || !hits) continue;
    int score=std::abs(hits*16/len-desired)*12+int(rng.below(25));
    if(acid && !named(name,"ACID")) score+=200;
    if(score<best) {best=score;chosen=i;}
  }
  return chosen;
}
int budget(DicePart part,const HybridStyle& style,const PhrasePlan& plan) {
  // Trade a few attacks between the two voices from roll to roll.
  const int turn=plan.amount ? int((plan.seed^(plan.seed>>8))%3)-1 : 0;
  if(part==DicePart::Arp) return clamp(plan.production.backingHits+turn,1,8);
  if(part==DicePart::Lead || part==DicePart::Counter)
    return clamp((plan.production.backingHits+1)/2-turn,1,5);
  if(style.quiet) return part==DicePart::Bass ? 2 : part==DicePart::Pulse ? 3 : 1;
  switch(part) {
    case DicePart::Bass:return 6;
    case DicePart::Chords:return 3;
    default:return 1;
  }
}
void capAttacks(Step* row,int length,int barSteps,int perBar) {
  for(int start=0;start<length;start+=barSteps) {
    int seen=0,n=0;for(int i=start;i<std::min(length,start+barSteps);++i)n+=row[i].hasNote();
    for(int i=start;i<std::min(length,start+barSteps);++i)if(row[i].hasNote()) {
      const bool keep=(seen+1)*perBar/std::max(1,n)!=seen*perBar/std::max(1,n);
      ++seen;if(n>perBar && !keep) row[i]=Step();
    }
  }
  // Generators can tie across an attack that the support budget removed.
  for(int at=0;at<length;++at)if(row[at].hasNote()) {
    for(auto& f:row[at].fx) if(f.cmd==Fx::TIE) f=FxSlot();
  }
}
void arrange(Step* row,int length,DicePart part,DiceForm form,int section,bool quiet,int barSteps) {
  if(section<0) return;
  const int main=form==DiceForm::Classic ? 0 : 1;
  const int variation=form==DiceForm::Classic ? 1 : form==DiceForm::Develop ? 2 : 4;
  int ordinal=0;
  for(int at=0;at<length;++at) if(row[at].hasNote()) {
    bool remove=false;
    if(section==main && (part==DicePart::Lead || part==DicePart::Counter)) remove=(ordinal++&1)!=0;
    if(form!=DiceForm::Classic && section==0 && (part==DicePart::Lead || part==DicePart::Arp || part==DicePart::Counter)) remove=at<length/2 || (ordinal++&1)!=0;
    if(form==DiceForm::Classic && section==2 && part==DicePart::Bass) remove=(ordinal++&1)!=0;
    if(section==variation && at>=length-barSteps) row[at].vel=clamp(row[at].vel+8);
    if(remove) row[at]=Step();
  }
  const bool outro=(form==DiceForm::Develop && section==3)||(form==DiceForm::Arrange && section==5);
  if(outro) {const int end=quiet ? length*3/4 : length/2;for(int i=end;i<length;++i)row[i]=Step();row[end].note=kNoteOff;}
}
}
HybridStyle hybridStyle(const DiceVariant& v) {
  bool rhythm=false,tonal=false,acid=false;
  for(int i=0;i<kDiceRoles;++i) {
    const auto& voice=diceVoice(v,i);if(!playable(voice)) continue;
    const auto k=static_cast<DiceVoiceKind>(voice.kind);
    rhythm|=k==DiceVoiceKind::Kick || k==DiceVoiceKind::Snare || k==DiceVoiceKind::Clap;
    tonal|=k==DiceVoiceKind::Bass || k==DiceVoiceKind::Lead || k==DiceVoiceKind::Bell || k==DiceVoiceKind::Stab;
    acid|=!strcmp(diceTimbre(voice).name,"303");
  }
  return {!rhythm || v.bpm<100,!tonal,acid};
}
void completeLayout(const DiceVariant& v,DiceLayout& layout) {
  bool present[static_cast<int>(DicePart::Count)]={};int basses=0;
  for(int t=0;t<kDiceRoles;++t) if(layout.source[t]>=0) {
    const auto& voice=diceVoice(v,layout.source[t]);
    if(!playable(voice)) {layout.source[t]=kDiceUnusedTrack;layout.used&=~(1u<<t);continue;}
    const auto k=static_cast<DiceVoiceKind>(voice.kind);
    present[int(DicePart::Bass)]|=k==DiceVoiceKind::Bass;basses+=k==DiceVoiceKind::Bass;
    present[int(DicePart::Chords)]|=diceChord(voice)!=255 || k==DiceVoiceKind::Pad;
    present[int(DicePart::Pad)]|=k==DiceVoiceKind::Pad;
    present[int(DicePart::Lead)]|=k==DiceVoiceKind::Lead || k==DiceVoiceKind::Bell || (k==DiceVoiceKind::Stab && diceChord(voice)==255);
    present[int(DicePart::Arp)]|=named(voice.name,"ARP") || named(voice.name,"SEQ");
    present[int(DicePart::Fx)]|=k==DiceVoiceKind::Vox;
  }
  present[int(DicePart::Arp)]|=basses>1;
  static constexpr DicePart normal[]={DicePart::Bass,DicePart::Chords,DicePart::Lead,DicePart::Arp,DicePart::Fx,DicePart::Counter,DicePart::Pad,DicePart::Texture,DicePart::Pulse};
  static constexpr DicePart quiet[]={DicePart::Bass,DicePart::Pad,DicePart::Counter,DicePart::Fx,DicePart::Arp,DicePart::Lead,DicePart::Chords,DicePart::Texture,DicePart::Pulse};
  static constexpr DicePart noise[]={DicePart::Texture,DicePart::Pulse,DicePart::Fx,DicePart::Counter,DicePart::Pad,DicePart::Arp,DicePart::Lead,DicePart::Chords,DicePart::Bass};
  const auto style=hybridStyle(v);const DicePart* order=style.noise ? noise : style.quiet ? quiet : normal;
  for(int t=0;t<kDiceRoles;++t) if(layout.source[t]==kDiceUnusedTrack) {
    for(int i=0;i<int(DicePart::Count);++i) if(!present[int(order[i])]) {layout.source[t]=dicePartSource(order[i]);layout.used|=1u<<t;present[int(order[i])]=true;break;}
  }
}
DiceMix planMix(const Project& p,const DiceVariant& v,const DiceLayout& layout,
                const uint8_t (&slots)[kDiceRoles],const PhrasePlan& plan) {
  DiceMix mix;
  for(int t=0;t<kDiceRoles;++t) {
    const auto& track=p.tracks[t];
    if(slots[t]==kNoInstr) {
      if(track.out==TrackOut::Int && track.instr<kInstruments) mix.patch[t]=p.instruments[track.instr].name;
    } else if(layout.source[t]>=0) {
      mix.patch[t]=bandPatch(v,layout.source[t],plan);
      if(!mix.patch[t]) mix.patch[t]=diceTimbre(diceVoice(v,layout.source[t])).name;
    }
  }
  const auto style=hybridStyle(v);
  // Equal-power track pan. Keep the kit, bass and hook in the middle; the
  // second pair of backing parts sits inside the first. No stereo delay.
  static constexpr uint8_t widths[]={22,24,36,26,28,22,26}; // Pocket .. Anthem
  static_assert(sizeof(widths)==1+unsigned(DiceFeel::Anthem),"DiceFeel widths");
  const int width=widths[unsigned(plan.feel)];
  int backing=0;
  for(int t=0;t<kDiceRoles;++t) {
    const int source=layout.source[t];
    if(diceSupplement(source) && slots[t]!=kNoInstr) mix.patch[t]=companionPatch(dicePartKind(source),style,plan,mix,bandPatch(v,source,plan));
    if(centred(source,v,plan)) continue;
    const int offset=width/(1+backing/2);
    mix.panOffset[t]=(backing++%2) ? offset : -offset;
  }
  return mix;
}
void buildCompanion(DicePart part,const char* patch,const DiceVariant& v,Instrument& m,TrackCfg& c,bool clean,const PhrasePlan& plan) {
  const auto style=hybridStyle(v);
  Rng rng(plan.seed ^ (0xC2B2AE35u*(1+static_cast<unsigned>(part))));
  const auto& voice=diceVoiceForTimbre(patch,rng.next());
  buildVoice(voice,m,c,clean,plan.amount,rng,plan.feel,part,plan.production);
  snprintf(c.name,sizeof(c.name),"%s",dicePartName(part));
  c.vol=part==DicePart::Bass ? 74 : part==DicePart::Chords || part==DicePart::Pad ? 52 : 60;
  c.drive=std::min<int>(c.drive,8);
  m.reso=std::min<int>(m.reso,part==DicePart::Arp && style.acid ? 78 : 40);
  m.lfoDepth=clamp(m.lfoDepth,-24,24);
  m.send=std::min<int>(m.send,35);m.rsend=std::min<int>(m.rsend,45);
  if(part==DicePart::Chords || part==DicePart::Pad) {m.mono=false;m.vol=std::min<int>(m.vol,38);c.defGate=115;}
  else {m.vol=std::min<int>(m.vol,72);m.release=std::min<int>(m.release,45);}
  if(part==DicePart::Bass) {
    m.send=0; m.rsend=0;
    if(plan.production.voice==DiceVoicing::Hard) c.vol=53;
  } else {
    // The supporting parts are added on top of an already composed guide.
    c.vol=c.vol*(76+3*plan.production.backingHits)/100;
  }
  if(part==DicePart::Bass || (part==DicePart::Arp && style.acid)) m.mono=true;
  if(style.noise || part==DicePart::Fx || part==DicePart::Texture) {m.vol=std::min<int>(m.vol,46);c.vol=48;}
}
bool companionPlayable(const TrackCfg& c,const Instrument* m,DicePart part) {
  if(c.out!=TrackOut::Int) return true;
  if(!m || m->type==InstrType::Kit || m->type==InstrType::Drum) return false;
  if(m->type==InstrType::Fm && m->machine!=uint8_t(FmMachine::Toy) && m->machine!=uint8_t(FmMachine::Tone)) return false;
  return !(m->mono && (part==DicePart::Chords || part==DicePart::Pad));
}
void writeCompanion(DicePat& dst,int t,DicePart part,const DiceRecipe& recipe,const DiceVariant& variant,
                    const DiceParts& parts,const Instrument* instrument,const PhrasePlan& plan,DiceHybridWork& work,
                    bool preserveTiming,DiceForm form,int section,DiceDensity density) {
  if(!preserveTiming) {dst.trackLen[t]=0;dst.trackSpeed[t]=0;}
  const int length=dst.trackLen[t] ? std::min<int>(dst.length,dst.trackLen[t]) : dst.length;
  const auto speed=toSpeed(dst.trackSpeed[t]);const int stepTicks=ticksPerStep(dst.res)*speedDiv(speed)/speedMul(speed);
  const int barSteps=std::max(1,4*kPpqn/stepTicks);
  const auto style=hybridStyle(variant);
  if(!preserveTiming && composeBandLine(dst.steps[t],length,stepTicks,dicePartSource(part),variant,recipe,
                                        parts,plan,instrument ? instrument->transpose : 0,density)) {
    arrange(dst.steps[t],length,part,form,section,style.quiet,barSteps);
    return;
  }
  clearWork(work.pattern,dst,length);auto& p=work.pattern;
  harmonyRow(p,stepTicks,recipe,variant,parts,plan);
  Rng rng(plan.seed ^ (0x9E3779B9u*(t+1)));
  const int desired=budget(part,style,plan)+(density==DiceDensity::Busy ? 2 : density==DiceDensity::Sparse ? -1 : 0);
  const Sel sel{harmonyTrack,harmonyTrack,0,static_cast<uint8_t>(length-1)};
  if(part==DicePart::Bass || (part==DicePart::Arp && style.acid)) {
    const int chosen=choosePattern(work,true,std::max(1,desired),rng,part==DicePart::Arp && style.acid);
    parseBassPattern(bassFactoryText(chosen),work.bass);
    BassSpec spec;spec.source=BassSource::Selection;spec.dest=t;spec.seed=rng.next();spec.octave=2;spec.gate=55;spec.rotate=rng.below(work.bass.len);spec.velLo=50;spec.velHi=90;
    applyBass(p,p,sel,spec,work.bass,parts.scaleRoot,static_cast<ScaleType>(parts.scaleType),nullptr,0);
  } else if(part==DicePart::Chords || part==DicePart::Pad) {
    const auto& ensemble=diceEnsemble(plan.feel,plan.seed);
    int chordAt=0;
    for(int at=0;at<length;++at) {
      const bool change=p.steps[harmonyTrack][at].hasNote();
      if(change) chordAt=at;
      const int slot=(at*stepTicks/(kPpqn/4))%16;
      // Quantize to the actual track grid, including slow and triplet tracks.
      bool stab=false;
      for(int hit=0;hit<std::min(3,std::max(0,desired-1));++hit) {
        const int bar=at*stepTicks/(4*kPpqn);
        const int slot=plan.hook.active(bar) ? plan.hook.accent(hit,bar,true) : ensemble.stabs[hit];
        const int onset=(bar*16+slot)*(kPpqn/4);
        stab|=onset/stepTicks==at;
      }
      const bool downbeat=at*stepTicks%(4*kPpqn)==0;
      if(!change && !(part==DicePart::Pad ? downbeat : stab)) continue;
      auto step=p.steps[harmonyTrack][chordAt];
      step.vel=downbeat ? 74 : slot>=12 ? 54 : 64;
      putFx(step,Fx::GAT,part==DicePart::Pad ? 200 : 82);
      p.steps[t][at]=step;
    }
  } else {
    const int chosen=choosePattern(work,false,std::max(1,desired),rng,false);parseArpPattern(arpFactoryText(chosen),work.arp);
    ArpSpec spec;spec.source=ArpSource::Selection;spec.dest=t;spec.seed=rng.next();spec.mode=static_cast<ArpMode>(rng.below(5));spec.rotate=rng.below(work.arp.len);spec.gate=part==DicePart::Arp ? 35 : 70;spec.velLo=44;spec.velHi=82;
    applyArp(p,p,sel,spec,work.arp,parts.scaleRoot,static_cast<ScaleType>(parts.scaleType));
  }
  auto* row=p.steps[t];
  if(part!=DicePart::Chords && part!=DicePart::Pad) {
    capAttacks(row,length,barSteps,std::max(1,desired));
    if(plan.hook.length && barSteps==16 && !preserveTiming) {
      if(part==DicePart::Bass) phraseRhythm(row,length,plan.hook,PhraseRole::Bass);
      if(part==DicePart::Lead || part==DicePart::Arp || part==DicePart::Counter)
        phraseRhythm(row,length,plan.hook,part==DicePart::Lead ? PhraseRole::Lead : PhraseRole::Reply);
    }
  }
  for(int at=0;at<length;++at) if(row[at].hasNote() && part!=DicePart::Chords && part!=DicePart::Pad) {
    if(part==DicePart::Bass || (part==DicePart::Arp && style.acid)) while(row[at].note>59) row[at].note-=12;
    else {while(row[at].note<60 && row[at].note<=115)row[at].note+=12;while(row[at].note>88)row[at].note-=12;}
    // REPEAT tokens can carry a previous chord's pitch through a harmony change.
    // Keep support voices on the current chord; bass approach notes remain intentional.
    if(part!=DicePart::Bass) {
      int chordAt=at;while(chordAt>0 && !p.steps[harmonyTrack][chordAt].hasNote())--chordAt;
      const auto& chord=p.steps[harmonyTrack][chordAt];uint8_t notes[4];
      const int n=chordNotes(chord.note,chord.find(Fx::CHD)->val,parts.scaleRoot,static_cast<ScaleType>(parts.scaleType),notes);
      int best=row[at].note,distance=128;
      for(int candidate=std::max(0,int(row[at].note)-6);candidate<=std::min(127,int(row[at].note)+6);++candidate)
        for(int i=0;i<n;++i)if(candidate%12==notes[i]%12 && std::abs(candidate-int(row[at].note))<distance) {best=candidate;distance=std::abs(candidate-int(row[at].note));}
      row[at].note=best;
    }
  }
  if(!preserveTiming && stepTicks==kPpqn/4) thinBreakSupport(row,length,part,plan,density);
  arrange(row,length,part,form,section,style.quiet,barSteps);
  const int transpose=instrument ? instrument->transpose : 0;
  for(int at=0;at<length;++at)if(row[at].hasNote())row[at].note=clamp(row[at].note-transpose);
  std::copy(row,row+kMaxSteps,dst.steps[t]);
}
}}
