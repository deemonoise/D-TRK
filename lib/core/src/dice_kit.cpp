#include "dice_kit.h"
#include <algorithm>
#include <cstring>
#include "gen_util.h"
#include "rng.h"
#include "presets_factory.h"
namespace mt { namespace dice_detail {
namespace {
void protect(const Project& p,int slot,bool* used) {
  if(slot>=kInstruments || used[slot]) return;
  used[slot]=true;
  if(p.instruments[slot].type==InstrType::Kit)
    for(const auto& lane:p.instruments[slot].kit) protect(p,lane.instr,used);
}
bool emptyRetiredTrack(const Project& p,int t) {
  if(t>=kDiceRoles || strcmp(p.tracks[t].name,"-") || !p.tracks[t].mute || p.gen[t].mode || p.var[t].mode) return false;
  for(const auto& pat:p.patterns) for(const auto& step:pat.steps[t]) if(!step.isEmpty()) return false;
  return true;
}
int take(int own,bool* used,const bool* owned,int lo,int hi) {
  int slot=own>=lo && own<hi && !used[own] ? own : kNoInstr;
  for(int i=lo;slot==kNoInstr && i<hi;++i) if(!used[i] && !owned[i]) slot=i;
  for(int i=lo;slot==kNoInstr && i<hi;++i) if(!used[i]) slot=i;
  if(slot!=kNoInstr) used[slot]=true;
  return slot;
}
bool passes(const Step& step,int bar,bool fill,Rng& rng) {
  if(const auto* f=step.find(Fx::CND)) {
    if(f->val==kCndFill && !fill) return false;
    if(f->val==kCndNoFill && fill) return false;
    const int a=f->val>>4,b=f->val&15;
    if(b && a && bar%b!=a-1) return false;
  }
  const auto* p=step.find(Fx::PRB);
  return !p || rng.below(100)<p->val;
}
bool common(const Step* const* cells,int n,Fx fx,uint8_t& value) {
  const auto* first=cells[0]->find(fx);
  if(!first) return false;
  value=first->val;
  for(int i=1;i<n;++i) { const auto* f=cells[i]->find(fx); if(!f || f->val!=value) return false; }
  return true;
}
void packStep(Step& out,const DicePat& source,const DiceLayout& layout,int at,uint32_t seed,bool fill) {
  const Step* cells[kDiceRoles]; int voices[kDiceRoles],n=0;
  for(int i=0;i<kDiceRoles;++i) if((layout.drums&(1u<<i)) && source.steps[i][at].hasNote()) {
    cells[n]=&source.steps[i][at]; voices[n++]=i;
  }
  if(!n) return;
  uint8_t probability=0,condition=0;
  const bool sharedProb=common(cells,n,Fx::PRB,probability);
  const bool sharedCond=common(cells,n,Fx::CND,condition);
  int kept=0;
  for(int i=0;i<n;++i) {
    Step decision=*cells[i];
    for(auto& f:decision.fx) if((sharedProb && f.cmd==Fx::PRB) || (sharedCond && f.cmd==Fx::CND)) f=FxSlot();
    Rng rng(seed ^ (0x9E3779B9u*(voices[i]+1)) ^ (0x85EBCA6Bu*(at+1)));
    const int bar=at*ticksPerStep(source.res)/(4*kPpqn);
    if(!passes(decision,bar,fill,rng)) continue;
    cells[kept]=cells[i];voices[kept++]=voices[i];
  }
  if(!kept) return;
  uint16_t mask=0,accent=0; int velocity=1;
  for(int i=0;i<kept;++i) { mask|=1u<<layout.lane[voices[i]]; velocity=std::max<int>(velocity,cells[i]->vel); }
  for(int i=0;i<kept;++i) if(cells[i]->vel*4>=velocity*3) accent|=1u<<layout.lane[voices[i]];
  out.note=velocity;setLaneMask(out,mask);
  if(sharedProb) putFx(out,Fx::PRB,probability);
  if(sharedCond) putFx(out,Fx::CND,condition);
  uint8_t value=0;
  if(common(cells,kept,Fx::RAT,value)) putFx(out,Fx::RAT,value);
  // Every KIT step shares its FX: conflicting lane locks must not modulate the kick.
  if(common(cells,kept,Fx::NDG,value)) putFx(out,Fx::NDG,value);
  if((mask&255)!=(accent&mask&255)) putFx(out,Fx::ACC,accent&255);
  if((mask>>8)!=((accent&mask)>>8)) putFx(out,Fx::AC2,accent>>8);
  for(Fx fx:{Fx::FLT,Fx::RES,Fx::DLY,Fx::RVB}) if(common(cells,kept,fx,value)) putFx(out,fx,value);
}
}
uint8_t protectedFormats(const Project& p,const DiceLayout& layout,uint8_t active,int count,bool sounds) {
  uint8_t protectedMask=0;
  for(int t=0;t<kDiceRoles;++t) {
    if(!(active&(1u<<t)) || layout.source[t]==kDiceUnusedTrack) continue;
    const bool desired=layout.source[t]==kDiceKitTrack;
    if(p.trackIsDrum(t)==desired) continue;
    bool outside=!sounds;
    for(int i=count;i<kPatterns;++i) for(const auto& step:p.patterns[i].steps[t]) outside|=!step.isEmpty();
    if(outside) protectedMask|=1u<<t;
  }
  return protectedMask;
}
Allocation allocate(const Project& p,const DiceLayout& layout,uint8_t active) {
  Allocation out;memset(&out,kNoInstr,sizeof(out));
  bool used[kInstruments]={},owned[kInstruments]={};
  for(int t=0;t<kTracks;++t) {
    if(t<kDiceRoles && (active&(1u<<t))) {
      if((layout.used&(1u<<t)) && p.tracks[t].instr<kInstruments) owned[p.tracks[t].instr]=true;
    } else if(p.tracks[t].out==TrackOut::Int && !emptyRetiredTrack(p,t)) protect(p,p.tracks[t].instr,used);
  }
  for(int t=0;t<kDiceRoles;++t) if((active&layout.used)&(1u<<t)) out.track[t]=take(p.tracks[t].instr,used,owned,0,kInstruments-kKitBlock);
  if(layout.drums && (active&1) && out.track[0]!=kNoInstr) {
    const Instrument* old=p.kitOf(0); bool failed=false;
    for(int i=0;i<kDiceRoles;++i) if(layout.drums&(1u<<i)) {
      const int own=old ? old->kit[layout.lane[i]].instr : kNoInstr;
      const int first=kInstruments-kKitBlock;
      const int preferred=own>=first && own<kInstruments ? own : first+layout.lane[i];
      out.drum[i]=take(preferred,used,owned,first,kInstruments);failed|=out.drum[i]==kNoInstr;
    }
    if(failed) out.track[0]=kNoInstr;  // KIT sound changes are atomic
  }
  return out;
}
void packKit(DicePat& out,const DicePat& source,const DiceLayout& layout,uint32_t seed,bool fill) {
  for(int i=0;i<out.length;++i) packStep(out.steps[0][i],source,layout,i,seed,fill);
}
}}
