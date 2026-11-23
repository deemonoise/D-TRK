#pragma once
#include "dice.h"
#include "dice_phrase.h"

namespace mt {
namespace dice_detail {

void drumFill(DicePat& dst, const DicePat& next, const Pattern& priorNext, const DiceParts& parts,
              const DiceLayout& layout, const DiceVariant& variant, const PhrasePlan& plan, uint8_t written);
void voiceArpeggios(DicePat& dst, const Pattern& prior, const DiceParts& parts, const DiceLayout& layout,
                    const DiceVariant& variant, uint8_t written);
void phraseExchange(DicePat& dst, const DiceParts& parts, const DiceLayout& layout, const DiceVariant& variant,
                    const DiceRecipe& recipe, const PhrasePlan& plan, uint8_t written);

}  // namespace dice_detail
}  // namespace mt
