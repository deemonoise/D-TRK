#pragma once
#include "dice_recipes.h"
#include "rng.h"
namespace mt { namespace dice_detail {
void buildVoice(const DiceVoice& voice,Instrument& out,TrackCfg& track,bool clean,int variety,Rng& rng);
}}
