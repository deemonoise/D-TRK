#pragma once
#include "dice.h"
#include "dice_phrase.h"
namespace mt { namespace dice_detail {
struct HybridStyle { bool quiet=false,noise=false,acid=false; };
HybridStyle hybridStyle(const DiceVariant& variant);
void completeLayout(const DiceVariant& variant,DiceLayout& layout);
struct DiceMix {
  const char* patch[kDiceRoles]={};
  int8_t panOffset[kDiceRoles]={};
};
DiceMix planMix(const Project& project,const DiceVariant& variant,const DiceLayout& layout,
                const uint8_t (&slots)[kDiceRoles],const PhrasePlan& plan);
void buildCompanion(DicePart part,const char* patch,const DiceVariant& variant,Instrument& instrument,TrackCfg& track,bool clean,const PhrasePlan& plan);
bool companionPlayable(const TrackCfg& track,const Instrument* instrument,DicePart part);
void writeCompanion(DicePat& dst,int track,DicePart part,const DiceRecipe& recipe,const DiceVariant& variant,
                    const DiceParts& parts,const Instrument* instrument,const PhrasePlan& plan,DiceHybridWork& work,
                    bool preserveTiming,DiceForm form,int section,DiceDensity density);
}}
