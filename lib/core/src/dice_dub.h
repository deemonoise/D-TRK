#pragma once
#include "dice.h"

namespace mt::dice_detail {

void produceDubChords(DiceParts& parts, const DiceLayout& layout, const DiceVariant& variant,
                      int sections, uint32_t seed, DiceDensity density);

}
