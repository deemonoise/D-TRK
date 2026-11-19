#include "dice_key.h"
#include "rng.h"
namespace mt { namespace dice_detail {
namespace {
void modes(uint8_t source,uint8_t out[4]) {
  using S=ScaleType;
  const S minor[]={S::Minor,S::Dorian,S::Phrygian,S::HarmonicMinor};
  const S major[]={S::Major,S::Lydian,S::Mixolydian,S::PentatonicMajor};
  const bool bright=source==uint8_t(S::Major) || source==uint8_t(S::Lydian) ||
    source==uint8_t(S::Mixolydian) || source==uint8_t(S::PentatonicMajor);
  for(int i=0;i<4;++i) out[i]=uint8_t(bright ? major[i] : minor[i]);
  if(source==uint8_t(S::Blues) || source==uint8_t(S::PentatonicMinor)) {out[0]=source;out[3]=uint8_t(source==uint8_t(S::Blues) ? S::PentatonicMinor : S::Blues);}
  if(source==uint8_t(S::Locrian) || source==uint8_t(S::MelodicMinor)) out[0]=source;
}
}
RollKey rollKey(const Project& p,const DiceVariant& v,const DiceOpts& o,uint32_t seed) {
  if(o.keepKey) return {p.scaleRoot,p.scaleType};
  Rng rng(seed ^ 0xD1B54A35u);uint8_t choices[4];modes(v.scale,choices);
  int root=o.root>=0 && o.root<12 ? o.root : int(rng.below(12));
  int scale=rng.below(4);
  if(!o.seed) {
    if(o.root<0 && root==p.scaleRoot) root=(root+1+rng.below(11))%12;
    if(choices[scale]==p.scaleType) scale=(scale+1+rng.below(3))%4;
  }
  return {static_cast<uint8_t>(root),choices[scale]};
}
}}
