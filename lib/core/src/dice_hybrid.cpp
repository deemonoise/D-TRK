#include "dice_hybrid.h"
#include "dice_sounds.h"
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
const char* palette(DicePart part,const HybridStyle& style,Rng& rng) {
  static const char* bass[]={"ANALOGBS","SUB","808 SUB","REESE"};
  static const char* chords[]={"E PIANO","ORGAN","STAB","WARM PAD"};
  static const char* lead[]={"PLUCK","PULSE","ACOU GTR","FM BELL"};
  static const char* arp[]={"PLUCK","FM KEYS","MALLET","303"};
  static const char* fx[]={"TEXTURE","VOX FORM","DRONE","TEXTURE"};
  static const char* pads[]={"WARM PAD","DRONE","VOX FORM","WARM PAD"};
  static const char* counter[]={"FM BELL","ACOU GTR","MALLET","PLUCK"};
  if(style.noise) return "TEXTURE";
  const int pick=rng.below(4);
  switch(part) {
    case DicePart::Bass:return style.quiet ? "SUB" : bass[pick];
    case DicePart::Chords:return style.quiet ? pads[pick] : chords[pick];
    case DicePart::Lead:return lead[pick];
    case DicePart::Arp:return style.acid ? "303" : arp[pick%3];
    case DicePart::Pad:return pads[pick];
    case DicePart::Counter:return counter[pick];
    default:return fx[pick];
  }
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
int budget(DicePart part,const HybridStyle& style) {
  if(style.quiet) return part==DicePart::Bass ? 2 : part==DicePart::Arp || part==DicePart::Pulse ? 3 : 1;
  switch(part) {
    case DicePart::Bass:return 6;
    case DicePart::Arp:return style.acid ? 8 : 6;
    case DicePart::Lead:return 4;
    case DicePart::Chords:return 3;
    case DicePart::Counter:return 2;
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
void buildCompanion(DicePart part,const DiceVariant& v,Instrument& m,TrackCfg& c,bool clean,int variety,uint32_t seed) {
  const auto style=hybridStyle(v);Rng rng(seed);
  const char* name=palette(part,style,rng);
  const auto& voice=diceVoiceForTimbre(name,rng.next());
  buildVoice(voice,m,c,clean,variety,rng);
  snprintf(c.name,sizeof(c.name),"%s",dicePartName(part));
  c.vol=part==DicePart::Bass ? 74 : part==DicePart::Chords || part==DicePart::Pad ? 52 : 60;
  c.pan=part==DicePart::Arp ? 76 : part==DicePart::Counter ? 48 : 64;
  c.drive=std::min<int>(c.drive,8);m.reso=std::min<int>(m.reso,40);m.lfoDepth=clamp(m.lfoDepth,-24,24);
  m.send=std::min<int>(m.send,35);m.rsend=std::min<int>(m.rsend,45);
  if(part==DicePart::Chords || part==DicePart::Pad) {m.mono=false;m.vol=std::min<int>(m.vol,38);c.defGate=115;}
  else {m.vol=std::min<int>(m.vol,72);m.release=std::min<int>(m.release,45);}
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
  clearWork(work.pattern,dst,length);auto& p=work.pattern;
  harmonyRow(p,stepTicks,recipe,variant,parts,plan);
  const auto style=hybridStyle(variant);Rng rng(plan.seed ^ (0x9E3779B9u*(t+1)));
  const int desired=budget(part,style)+(density==DiceDensity::Busy ? 2 : density==DiceDensity::Sparse ? -1 : 0);
  const Sel sel{harmonyTrack,harmonyTrack,0,static_cast<uint8_t>(length-1)};
  if(part==DicePart::Bass || (part==DicePart::Arp && style.acid)) {
    const int chosen=choosePattern(work,true,std::max(1,desired),rng,part==DicePart::Arp && style.acid);
    parseBassPattern(bassFactoryText(chosen),work.bass);
    BassSpec spec;spec.source=BassSource::Selection;spec.dest=t;spec.seed=rng.next();spec.octave=2;spec.gate=55;spec.rotate=rng.below(work.bass.len);spec.mutate=plan.amount/4;spec.velLo=50;spec.velHi=90;
    applyBass(p,p,sel,spec,work.bass,parts.scaleRoot,static_cast<ScaleType>(parts.scaleType),nullptr,0);
  } else if(part==DicePart::Chords || part==DicePart::Pad) {
    const int barSteps=std::max(1,4*kPpqn/stepTicks);int phase=part==DicePart::Pad ? 0 : rng.below(std::max(1,barSteps/2));
    for(int at=phase;at<length;at+=part==DicePart::Pad ? barSteps : std::max(1,barSteps/std::max(1,desired))) {
      int ch=at;while(ch>0 && !p.steps[harmonyTrack][ch].hasNote())--ch;
      auto s=p.steps[harmonyTrack][ch];s.vel=64;putFx(s,Fx::GAT,part==DicePart::Pad ? 200 : 115);p.steps[t][at]=s;
    }
  } else {
    const int chosen=choosePattern(work,false,std::max(1,desired),rng,false);parseArpPattern(arpFactoryText(chosen),work.arp);
    ArpSpec spec;spec.source=ArpSource::Selection;spec.dest=t;spec.seed=rng.next();spec.mode=static_cast<ArpMode>(rng.below(5));spec.rotate=rng.below(work.arp.len);spec.gate=part==DicePart::Arp ? 35 : 70;spec.velLo=44;spec.velHi=82;spec.mutate=plan.amount/4;
    applyArp(p,p,sel,spec,work.arp,parts.scaleRoot,static_cast<ScaleType>(parts.scaleType));
  }
  auto* row=p.steps[t];const int barSteps=std::max(1,4*kPpqn/stepTicks);
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
  capAttacks(row,length,barSteps,std::max(1,desired));
  arrange(row,length,part,form,section,style.quiet,barSteps);
  const int transpose=instrument ? instrument->transpose : 0;
  for(int at=0;at<length;++at)if(row[at].hasNote())row[at].note=clamp(row[at].note-transpose);
  std::copy(row,row+kMaxSteps,dst.steps[t]);
}
}}
