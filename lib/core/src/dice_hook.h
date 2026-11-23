#pragma once
#include "dice_recipes.h"

namespace mt { namespace dice_detail {
// One musical idea per roll. The lead and its answers share the degrees and accent pattern.
struct RollMotif {
  int8_t degrees[8]{};
  uint8_t length=0, rhythm=0, phase=0, answerBars=2, bassRhythm=0;
  uint8_t rewrite[4]{};
  bool sparse=false;
  bool active(int bar) const { return length && rewrite[bar%4]; }
  int degree(int bar,int attack,bool reply) const;
  int accent(int rank,int bar,bool reply) const;
};
RollMotif rollMotif(uint32_t seed,DiceFeel feel,int variety,bool sparse);
enum class PhraseRole { Lead, Reply, Bass, RollingBass };
void phraseRhythm(Step* row,int length,const RollMotif& motif,PhraseRole role);
}}
