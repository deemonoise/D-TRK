#include "pattern_tools.h"
#include "automation.h"
#include "rng.h"
#include <string.h>
namespace mt {
namespace {
int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
bool valid(const Pattern& p) { return p.length >= kMinSteps && p.length <= kMaxSteps; }
bool valid(const Pattern& p, const Sel& s) {
  return valid(p) && s.t0 <= s.t1 && s.t1 < kTracks && s.s0 <= s.s1 && s.s1 < p.length;
}
bool valid(StepPart part) { return part >= StepPart::All && part <= StepPart::Automation; }
bool replacePart(Step& dst, const Step& src, StepPart part) {
  if (part == StepPart::All) { dst = src; return true; }
  if (part == StepPart::Notes) {
    dst.note = src.note; dst.vel = src.vel; dst.lanesHi = src.lanesHi;
    return true;
  }
  automationStrip(dst);
  for (const auto& f : src.fx) {
    if (!automationCommand(f.cmd)) continue;
    int slot = 0;
    while (slot < kFxSlots && dst.fx[slot].cmd != Fx::None) ++slot;
    if (slot == kFxSlots) return false;
    dst.fx[slot] = f;
  }
  return true;
}
ToolResult commit(Pattern& p, const Pattern& scratch) {
  if (!memcmp(&p, &scratch, sizeof(p))) return ToolResult::Unchanged;
  p = scratch; return ToolResult::Changed;
}
}
ToolResult rotateSteps(Pattern& p, Pattern& scratch, const Sel& s, int amount, StepPart part) {
  if (&p == &scratch || !valid(p,s) || !valid(part)) return ToolResult::Invalid;
  const int size = s.s1 - s.s0 + 1;
  const int shift = (amount % size + size) % size;
  if (!shift) return ToolResult::Unchanged;
  scratch = p;
  for (int t=s.t0; t<=s.t1; ++t)
    for (int i=0; i<size; ++i)
      if (!replacePart(scratch.steps[t][s.s0+(i+shift)%size], p.steps[t][s.s0+i], part)) return ToolResult::NoSlot;
  return commit(p,scratch);
}
ToolResult pastePart(Pattern& p, Pattern& scratch, const Clipboard& clip, int track, int step, StepPart part) {
  if (&p == &scratch || !valid(p) || !valid(part) || track<0 || track>=kTracks || step<0 || step>=p.length ||
      !clip.tracks || clip.tracks>kTracks || !clip.steps || clip.steps>kMaxSteps) return ToolResult::Invalid;
  scratch = p;
  for (int t=0; t<clip.tracks && track+t<kTracks; ++t)
    for (int i=0; i<clip.steps && step+i<p.length; ++i)
      if (!replacePart(scratch.steps[track+t][step+i],clip.data[t][i],part)) return ToolResult::NoSlot;
  return commit(p,scratch);
}
ToolResult duplicateSteps(Pattern& p, const Sel& s) {
  if (!valid(p,s)) return ToolResult::Invalid;
  const int size=s.s1-s.s0+1, dest=s.s1+1;
  if (dest+size>p.length) return ToolResult::NoRoom;
  bool changed=false;
  for (int t=s.t0; t<=s.t1; ++t)
    for (int i=0; i<size; ++i) {
      auto& dst=p.steps[t][dest+i]; const auto& src=p.steps[t][s.s0+i];
      changed |= memcmp(&dst,&src,sizeof(dst)) != 0;
      dst=src;
    }
  return changed ? ToolResult::Changed : ToolResult::Unchanged;
}
ToolResult doublePattern(Pattern& p) {
  if (!valid(p)) return ToolResult::Invalid;
  const int size=p.length;
  if (size*2>kMaxSteps) return ToolResult::NoRoom;
  for (int t=0; t<kTracks; ++t)
    for (int i=0; i<size; ++i) p.steps[t][size+i]=p.steps[t][i];
  p.length=static_cast<uint8_t>(size*2);
  return ToolResult::Changed;
}
ToolResult changeVelocity(Pattern& p, const Sel& s, VelocitySpec spec,
                          const uint8_t* defaults, const bool* drums) {
  if (!valid(p,s) || !defaults || !drums || spec.mode<VelocityMode::Set || spec.mode>VelocityMode::Humanize)
    return ToolResult::Invalid;
  if ((spec.mode==VelocityMode::Add || spec.mode==VelocityMode::Humanize) && !spec.amount)
    return ToolResult::Unchanged;
  Rng rng(spec.seed); bool changed=false;
  const int start=clamp(spec.amount,1,127), end=clamp(spec.end,1,127);
  const int spread=clamp(spec.amount,0,127), count=s.s1-s.s0;
  for (int t=s.t0; t<=s.t1; ++t)
    for (int i=s.s0; i<=s.s1; ++i) {
      auto& st=p.steps[t][i];
      if (!st.hasNote() || (drums[t] && !laneMask(st))) continue;
      uint8_t& value=drums[t] ? st.note : st.vel;
      const int current=value ? value : clamp(defaults[t],1,127);
      int next=start;
      switch (spec.mode) {
        case VelocityMode::Set: break;
        case VelocityMode::Add: next=current+clamp(spec.amount,-127,127); break;
        case VelocityMode::Ramp: next=count ? start+(end-start)*(i-s.s0)/count : start; break;
        case VelocityMode::Humanize: next=current+static_cast<int>(rng.below(spread*2+1))-spread; break;
      }
      next=clamp(next,1,127);
      changed |= value!=next; value=static_cast<uint8_t>(next);
    }
  return changed ? ToolResult::Changed : ToolResult::Unchanged;
}
}
