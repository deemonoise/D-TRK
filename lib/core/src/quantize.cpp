#include "quantize.h"
#include "automation.h"
#include <string.h>
namespace mt {
namespace {
int floorDiv(int a, int b) { const int q=a/b; return a%b<0 ? q-1 : q; }
int roundedDiv(int a, int b) { return a>=0 ? (a+b/2)/b : -((-a+b/2)/b); }
bool validRange(const Pattern& p, const Sel& s) {
 return p.length>=kMinSteps && p.length<=kMaxSteps && s.t0<=s.t1 && s.t1<kTracks &&
        s.s0<=s.s1 && s.s1<p.length;
}
bool addFx(Step& out, FxSlot f) {
 for(auto& d:out.fx) if(d.cmd==f.cmd) return d.val==f.val;
 for(auto& d:out.fx) if(d.cmd==Fx::None) { d=f; return true; }
 return false;
}
}
void clearNotesSel(Pattern& p,const Sel& s) {
 if(!validRange(p,s)) return;
 for(int t=s.t0;t<=s.t1;++t) for(int n=s.s0;n<=s.s1;++n) clearNote(p.steps[t][n]);
}
void clearAutomationSel(Pattern& p,const Sel& s) {
 if(!validRange(p,s)) return;
 for(int t=s.t0;t<=s.t1;++t) for(int n=s.s0;n<=s.s1;++n) automationStrip(p.steps[t][n]);
}
QuantizeResult quantizeNotes(Pattern& p,Pattern& out,const Sel& s,QuantizeSpec spec) {
 if(&p==&out || !validRange(p,s) || spec.grid>=Resolution::Count) return QuantizeResult::Invalid;
 if(!spec.strength) return QuantizeResult::Unchanged;
 const int strength=spec.strength>100 ? 100 : spec.strength;
 out=p;
 for(int t=s.t0;t<=s.t1;++t) {
  const int len=p.trackLen[t] && p.trackLen[t]<p.length ? p.trackLen[t] : p.length;
  const int last=s.s1<len ? s.s1 : len-1;
  const auto speed=toSpeed(p.trackSpeed[t]);
  const int step=ticksPerStep(p.res)*speedDiv(speed)/speedMul(speed);
  const int grid=ticksPerStep(spec.grid)*100;
  const int low=((s.s0*step*100+grid-1)/grid)*grid;
  const int high=(last*step*100/grid)*grid;
  for(int n=s.s0;n<=last;++n) if(p.steps[t][n].hasNote()) {
    auto& dst=out.steps[t][n]; clearNote(dst);
    for(auto& f:dst.fx) if(!automationCommand(f.cmd)) f=FxSlot{};
  }
  for(int n=s.s0;n<=last;++n) {
   const auto& src=p.steps[t][n]; if(!src.hasNote()) continue;
   if(low>high) return QuantizeResult::Invalid;
   const auto* f=src.find(Fx::NDG);
   const int raw=f ? fxSigned(f->val) : 0;
   const int nudge=raw < -50 ? -50 : raw>50 ? 50 : raw;
   const int time=(n*100+nudge)*step;
   int target=floorDiv(time+grid/2,grid)*grid;
   if(target<low) target=low; if(target>high) target=high;
   const int moved=time+roundedDiv((target-time)*strength,100);
   const int dest=floorDiv(moved+step*50,step*100);
   if(dest<s.s0 || dest>last) return QuantizeResult::Invalid;
   auto& dst=out.steps[t][dest];
   if(dst.note!=kNoteEmpty) return QuantizeResult::Collision;
   dst.note=src.note; dst.vel=src.vel; dst.lanesHi=src.lanesHi;
   for(const auto& f:src.fx)
    if(f.cmd!=Fx::None && f.cmd!=Fx::NDG && !automationCommand(f.cmd) && !addFx(dst,f)) return QuantizeResult::NoSlot;
   const int ndg=roundedDiv(moved-dest*step*100,step);
   if(ndg && !addFx(dst,{Fx::NDG,static_cast<uint8_t>(static_cast<int8_t>(ndg))})) return QuantizeResult::NoSlot;
  }
 }
 if(memcmp(&p,&out,sizeof(p))==0) return QuantizeResult::Unchanged;
 p=out;
 return QuantizeResult::Ok;
}
}
