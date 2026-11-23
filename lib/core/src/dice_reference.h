#pragma once
#include "dice.h"

namespace mt::dice_detail {

void applyReferenceEdits(DiceParts& parts, const DiceRecipe& recipe, const DiceVariant& variant,
                         const DiceLayout& layout, DiceForm form, int sections, DiceDensity density);

}
