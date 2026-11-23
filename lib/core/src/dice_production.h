#pragma once
#include "dice.h"
#include "dice_phrase.h"

namespace mt::dice_detail {

enum class TimbreProfile : uint8_t { Family, JumpUp, DarkPsy, MinimalTechno, DubTechno, Liquid };
TimbreProfile timbreProfile(const DiceRecipe& recipe);
bool usesFamilyProduction(TimbreProfile profile);
void produceTimbres(DiceParts& parts, const DiceLayout& layout, const DiceVariant& variant,
                    const PhrasePlan& plan, TimbreProfile profile);
void produceMix(DiceParts& parts, const DiceLayout& layout, const DiceVariant& variant,
                const PhrasePlan& plan, DiceFamily family);
void produceSection(DicePat& dst, const DiceParts& parts, const DiceLayout& layout,
                    const DiceVariant& variant, const PhrasePlan& plan, DiceFamily family,
                    DiceForm form, int section, DiceDensity density);
void produceDarkPsySection(DicePat& dst, const DiceLayout& layout, const DiceVariant& variant,
                           DiceForm form, int section, DiceDensity density);
bool ambientTexture(const DiceLayout& layout, const DiceVariant& variant, DiceFamily family);
void breatheTexture(Step* row, int length, int stepTicks, DiceDensity density);

}
