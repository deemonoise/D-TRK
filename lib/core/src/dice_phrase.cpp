#include "dice_phrase.h"
#include "gen_util.h"
#include "rng.h"
#include "chord_gen.h"
#include <algorithm>
#include <cstdlib>
namespace mt { namespace dice_detail {
namespace {
int bounded(int n,int lo=0,int hi=127) { return std::max(lo,std::min(hi,n)); }
bool melodic(const DiceVoice& v) {
  if(!diceEventCount(v) || diceChord(v)!=255) return false;
  const auto k=static_cast<DiceVoiceKind>(v.kind);
  return k==DiceVoiceKind::Lead || k==DiceVoiceKind::Bell || k==DiceVoiceKind::Stab || k==DiceVoiceKind::Bass;
}
int fillRank(const DiceVoice& v) {
  if(!diceEventCount(v)) return 0;
  switch(static_cast<DiceVoiceKind>(v.kind)) {
    case DiceVoiceKind::Perc:return 3;
    case DiceVoiceKind::Hat:case DiceVoiceKind::Shaker:return 2;
    case DiceVoiceKind::OpenHat:return 1;
    default:return 0;
  }
}
int latestEvent(const DiceVoice& v,const DiceRecipe& recipe,int tick,int& when) {
  const auto speed=toSpeed(v.speed);
  const int step=ticksPerStep(static_cast<Resolution>(recipe.resolution))*speedDiv(speed)/speedMul(speed);
  const int length=v.length ? v.length : recipe.length,period=length*step;
  int found=-1;when=-1;
  for(int i=0;i<diceEventCount(v);++i) {
    const auto& e=diceEvent(v,i);if(e.at>=length) continue;
    int at=tick/period*period+e.at*step;if(at>tick) at-=period;
    if(at>=0 && at>when) {when=at;found=i;}
  }
  return found;
}
int shift(const PhrasePlan& plan,const DiceVariant& variant,const DiceRecipe& recipe,int tick) {
  if(plan.generatedHarmony) return plan.progression[(tick/(4*kPpqn))%4];
  const int threshold=plan.melodicMask ? 50 : 1;
  if(plan.amount<threshold) return 0;
  if(plan.harmonicSource>=0) {
    int when;latestEvent(diceVoice(variant,plan.harmonicSource),recipe,tick,when);
    tick=std::max(0,when); // a held chord/pad changes harmony only on its next attack
  }
  return plan.harmonic[(tick/(4*kPpqn))%4];
}
int guideNote(int note,uint8_t root,uint8_t scale,const PhrasePlan& plan,int tick,
              const DiceVariant& variant,const DiceRecipe& recipe) {
  return moveDegrees(bounded(note),shift(plan,variant,recipe,tick),root,static_cast<ScaleType>(scale));
}
int harmony(const DiceVariant& variant,const DiceRecipe& recipe,int tick,uint8_t root,uint8_t scale,
            const PhrasePlan& plan,uint8_t notes[4],DiceChord* selected=nullptr) {
  int latest=-1,count=0;
  for(int source=0;source<kDiceRoles;++source) {
    const auto& v=diceVoice(variant,source);const int chord=diceChord(v);
    if(chord==255) continue;
    int when;const int event=latestEvent(v,recipe,tick,when);
    if(event<0 || when<latest) continue;
    int note=60+v.pitch+int(root)-variant.root+diceEvent(v,event).note;
    note=guideNote(note,root,scale,plan,when,variant,recipe);
    count=chordNotes(note,chord,root,static_cast<ScaleType>(scale),notes);latest=when;
    if(selected) *selected={static_cast<uint8_t>(note),static_cast<uint8_t>(chord)};
  }
  if(!count && plan.generatedHarmony) {
    const int note=moveDegrees(60+root,plan.progression[(tick/(4*kPpqn))%4],root,static_cast<ScaleType>(scale));
    count=chordNotes(note,kChordTriad,root,static_cast<ScaleType>(scale),notes);
    if(selected) *selected={static_cast<uint8_t>(note),kChordTriad};
  }
  return count;
}
int chooseNote(int center,int target,const uint8_t* chord,int count,uint8_t root,uint8_t scale,int span,bool exclude=false) {
  int best=center,score=10000;
  for(int note=bounded(center-span);note<=bounded(center+span);++note) {
    if(exclude && note==center) continue;
    bool allowed=!count && inScale(note,root,static_cast<ScaleType>(scale));
    for(int n=0;n<count;++n) allowed|=note%12==chord[n]%12;
    const int cost=std::abs(note-target)*4+std::abs(note-center);
    if(allowed && cost<score) {score=cost;best=note;}
  }
  return best;
}
int slot(int tick) { return (tick/(kPpqn/4))%16; }
void selectNotes(const Step* row,int length,int stepTicks,int source,const PhrasePlan& plan,bool selected[kMaxSteps]) {
  uint8_t attacks[kMaxSteps];int n=0;
  for(int at=0;at<length;++at) if(row[at].hasNote()) attacks[n++]=at;
  auto priority=[&](int at) {return plan.order[source][slot(at*stepTicks)]*kMaxSteps+at;};
  std::sort(attacks,attacks+n,[&](int a,int b){return priority(a)<priority(b);});
  const int count=std::min(n,(n*plan.amount+99)/100);
  for(int i=0;i<count;++i) selected[attacks[i]]=true;
}
void makeTheme(PhrasePlan& plan,int source) {
  Rng rng(plan.seed ^ (0xC2B2AE35u*(source+1)));int walk=rng.below(5);
  static constexpr int8_t motion[]={-2,-1,0,1,2};
  static constexpr int8_t anchors[]={0,2,4};
  for(int i=0;i<16;++i) {
    walk=i%4 ? bounded(walk+motion[rng.below(5)],0,6) : anchors[rng.below(3)];
    plan.theme[source][i]=walk;plan.order[source][i]=i;
  }
  for(int i=15;i>0;--i) std::swap(plan.order[source][i],plan.order[source][rng.below(i+1)]);
}
void ghost(Step* row,int begin,int end,int direction,const Step (*kitRows)[kMaxSteps]) {
  int model=-1;
  for(int at=begin;at<end;++at) if(row[at].hasNote()) model=at;
  if(model<0) return;
  for(int n=0;n<end-begin;++n) {
    const int at=direction>0 ? end-1-n : begin+n;
    if(!row[at].isEmpty()) continue;
    bool occupied=false;
    if(kitRows) for(int voice=0;voice<kDiceRoles;++voice) occupied|=kitRows[voice][at].hasNote();
    if(occupied) continue;
    row[at]=row[model];row[at].vel=std::max(1,int(row[model].vel)*2/3);
    for(auto& fx:row[at].fx) if(fx.cmd==Fx::RAT || fx.cmd==Fx::NDG) fx=FxSlot();
    return;
  }
}
void percussion(Step* row,int length,int stepTicks,const PhrasePlan& plan,const Step (*kitRows)[kMaxSteps]) {
  for(int bar=1;bar*4*kPpqn<length*stepTicks;bar+=2) {
    Rng rng(plan.seed ^ (0x85EBCA6Bu*(bar+1)));
    if(rng.below(100)>=static_cast<unsigned>(plan.amount)) continue;
    const int begin=(bar*4*kPpqn+3*kPpqn+stepTicks-1)/stepTicks;
    const int end=std::min(length,(bar+1)*4*kPpqn/stepTicks);
    ghost(row,begin,end,rng.below(2) ? 1 : -1,kitRows);
  }
}
bool sameNotes(const Step* a,const Step* b,int length) {
  for(int at=0;at<length;++at) if(a[at].note!=b[at].note) return false;
  return true;
}
void freshen(Step* row,const Step* prior,int length,int stepTicks,int source,const DiceRecipe& recipe,
             const DiceVariant& variant,uint8_t root,uint8_t scale,int transpose,const PhrasePlan& plan) {
  if(!prior || !sameNotes(row,prior,length)) return;
  Rng rng(plan.seed ^ (0x165667B1u*(source+1)));const int start=rng.below(length);
  for(int i=0;i<length;++i) {
    const int at=(start+i)%length;if(!row[at].hasNote()) continue;
    uint8_t notes[4];const int count=harmony(variant,recipe,at*stepTicks,root,scale,plan,notes);
    const int center=row[at].note+transpose;
    const int next=chooseNote(center,center+(rng.below(2) ? 5 : -5),notes,count,root,scale,12,true);
    if(next==center) continue;
    row[at].note=bounded(next-transpose);return;
  }
}
}
bool harmonyAt(const DiceVariant& v,const DiceRecipe& recipe,int tick,uint8_t root,uint8_t scale,const PhrasePlan& plan,DiceChord& chord) {
  uint8_t notes[4];return harmony(v,recipe,tick,root,scale,plan,notes,&chord)>0;
}
PhrasePlan phrasePlan(const DiceVariant& v,uint32_t seed,int amount) {
  PhrasePlan out;out.seed=seed;out.amount=bounded(amount,0,100);int drums=0;bool independent=false;
  Rng rng(seed ^ 0x27D4EB2Fu);
  out.harmonic[0]=out.harmonic[3]=0;
  out.harmonic[1]=(rng.below(2) ? 1 : -1)*(1+rng.below(2));
  out.harmonic[2]=(rng.below(2) ? 1 : -1)*(1+rng.below(2));
  for(int i=0;i<kDiceRoles;++i) {
    const auto& voice=diceVoice(v,i);const int d=fillRank(voice);
    if(melodic(voice)) out.melodicMask|=1u<<i;
    if(diceEventCount(voice) && diceChord(voice)!=255 && out.harmonicSource<0) out.harmonicSource=i;
    if(d>drums) {out.fill=i;drums=d;}
    if(!dicePercussion(voice) && (voice.length || voice.speed)) {out.harmonic[1]=out.harmonic[2]=0;independent=true;}
    makeTheme(out,i);
  }
  for(int i=0;i<kDiceRoles;++i) if(static_cast<DiceVoiceKind>(diceVoice(v,i).kind)==DiceVoiceKind::Pad && diceEventCount(diceVoice(v,i))) {out.harmonicSource=i;break;}
  const auto layout=diceLayout(v);bool nativeChord=false;
  for(int i=0;i<kDiceRoles;++i) nativeChord|=diceChord(diceVoice(v,i))!=255 && diceEventCount(diceVoice(v,i));
  if(!nativeChord) for(int t=0;t<kDiceRoles;++t) if(diceSupplement(layout.source[t]) && dicePartKind(layout.source[t])==DicePart::Chords) out.generatedHarmony=true;
  if(out.generatedHarmony && out.amount && !independent) {
    Progression prog;randomProgression(4,seed ^ 0x85EBCA77u,static_cast<ScaleType>(v.scale),prog);
    for(int i=0;i<4;++i) out.progression[i]=prog.c[i%prog.n].degree;
  }
  return out;
}
void developVoice(Step* row,int length,int stepTicks,int source,const DiceRecipe& recipe,
                  const DiceVariant& variant,uint8_t root,uint8_t scale,int transpose,const PhrasePlan& plan,
                  const Step (*kitRows)[kMaxSteps],const Step* prior) {
  if(!plan.amount) return;
  const auto& voice=diceVoice(variant,source);
  if(dicePercussion(voice)) {
    if(source==plan.fill) percussion(row,length,stepTicks,plan,kitRows);
    return;
  }
  const bool melody=plan.melodicMask&(1u<<source),bass=static_cast<DiceVoiceKind>(voice.kind)==DiceVoiceKind::Bass;
  bool selected[kMaxSteps]={};if(melody) selectNotes(row,length,stepTicks,source,plan,selected);
  for(int at=0;at<length;++at) if(row[at].hasNote()) {
    auto& step=row[at];const int tick=at*stepTicks,original=bounded(step.note+transpose);
    int next=guideNote(original,root,scale,plan,tick,variant,recipe);
    if(selected[at]) {
      uint8_t notes[4];const int count=harmony(variant,recipe,tick,root,scale,plan,notes);
      const int phase=slot(tick),degree=plan.theme[source][phase];
      const int tonic=next-((next-root)%12+12)%12;
      const int target=moveDegrees(bounded(tonic),degree,root,static_cast<ScaleType>(scale));
      next=chooseNote(next,target,notes,count,root,scale,bass ? 7 : 12);
    }
    step.note=bounded(next-transpose);
    if(const auto* chord=step.find(Fx::CHD)) {
      Rng rng(plan.seed ^ (0xC2B2AE35u*(source+1)) ^ (0x85EBCA6Bu*(tick/(4*kPpqn)+1)));
      if(rng.below(100)<static_cast<unsigned>(plan.amount)) putFx(step,Fx::CHD,chordValue(chordType(chord->val),rng.below(3)));
    }
  }
  if(melody) freshen(row,prior,length,stepTicks,source,recipe,variant,root,scale,transpose,plan);
}
}}
