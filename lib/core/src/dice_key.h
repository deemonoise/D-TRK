#pragma once
#include "dice.h"
namespace mt { namespace dice_detail {
struct RollKey { uint8_t root,scale; };
RollKey rollKey(const Project& project,const DiceVariant& variant,const DiceOpts& opts,uint32_t seed);
}}
