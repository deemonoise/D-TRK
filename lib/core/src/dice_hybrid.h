#pragma once
#include "dice.h"
#include "dice_phrase.h"
namespace mt { namespace dice_detail {
struct HybridStyle { bool quiet=false,noise=false,acid=false; };
HybridStyle hybridStyle(const DiceVariant& variant);
void completeLayout(const DiceVariant& variant,DiceLayout& layout);
void buildCompanion(DicePart part,const DiceVariant& variant,Instrument& instrument,TrackCfg& track,bool clean,int variety,uint32_t seed);
bool companionPlayable(const TrackCfg& track,const Instrument* instrument,DicePart part);
void writeCompanion(DicePat& dst,int track,DicePart part,const DiceRecipe& recipe,const DiceVariant& variant,
                    const DiceParts& parts,const Instrument* instrument,const PhrasePlan& plan,DiceHybridWork& work,
                    bool preserveTiming,DiceForm form,int section,DiceDensity density);
}}
