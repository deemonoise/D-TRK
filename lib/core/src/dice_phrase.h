#pragma once
#include "dice_hook.h"
namespace mt { namespace dice_detail {
enum class BandFamily : uint8_t { Guide, Dnb, Gabber };
struct BandPlan {
  BandFamily family=BandFamily::Guide;
  bool rephraseBass=false;
  uint8_t sound=0, rhythm=0, turn=0, groove=0;
};
struct PhrasePlan {
  BandPlan band;
  uint32_t seed;
  DiceFeel feel;
  DiceProduction production;
  RollMotif hook;
  int8_t bassSource=-1, leadSource=-1;
  uint8_t melodicMask=0;
  int fill=-1,amount=0,harmonicSource=-1;
  int8_t harmonic[4]={},theme[kDiceRoles][16]={};
  uint8_t order[kDiceRoles][16]={};
  bool generatedHarmony=false, padHarmony=false;
  int8_t progression[4]={};
};
struct DiceChord { uint8_t root=60,chord=0; };
bool harmonyAt(const DiceVariant& variant,const DiceRecipe& recipe,int tick,uint8_t root,uint8_t scale,const PhrasePlan& plan,DiceChord& chord);
PhrasePlan phrasePlan(const DiceVariant& variant,uint32_t seed,int amount,DiceFeel feel,bool newParts=false,bool sparse=false);
void developVoice(Step* row,int length,int stepTicks,int source,const DiceRecipe& recipe,
                  const DiceVariant& variant,uint8_t root,uint8_t scale,int transpose,const PhrasePlan& plan,const Step (*kitRows)[kMaxSteps]=nullptr,const Step* prior=nullptr);
}}
