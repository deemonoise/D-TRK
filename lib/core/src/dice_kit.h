#pragma once
#include "dice.h"
namespace mt { namespace dice_detail {
struct Allocation { uint8_t track[kDiceRoles], drum[kDiceRoles]; };
// Mask of tracks whose output format cannot change without reinterpreting untouched data.
uint8_t protectedFormats(const Project& p,const DiceLayout& layout,uint8_t active,int count,bool sounds);
Allocation allocate(const Project& p,const DiceLayout& layout,uint8_t active);
void packKit(DicePat& out,const DicePat& sources,const DiceLayout& layout,uint32_t seed,bool fill);
}}
