#pragma once
#include "dice_recipes.h"
namespace mt { namespace dice_detail {
struct PhrasePlan {
  uint32_t seed;
  uint8_t melodicMask=0;
  int fill=-1,amount=0,harmonicSource=-1;
  int8_t harmonic[4]={},theme[kDiceRoles][16]={};
  uint8_t order[kDiceRoles][16]={};
  bool generatedHarmony=false;
  int8_t progression[4]={};
};
struct DiceChord { uint8_t root=60,chord=0; };
bool harmonyAt(const DiceVariant& variant,const DiceRecipe& recipe,int tick,uint8_t root,uint8_t scale,const PhrasePlan& plan,DiceChord& chord);
PhrasePlan phrasePlan(const DiceVariant& variant,uint32_t seed,int amount);
void developVoice(Step* row,int length,int stepTicks,int source,const DiceRecipe& recipe,
                  const DiceVariant& variant,uint8_t root,uint8_t scale,int transpose,const PhrasePlan& plan,const Step (*kitRows)[kMaxSteps]=nullptr,const Step* prior=nullptr);
}}
