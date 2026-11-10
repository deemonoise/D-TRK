#pragma once
#include "dice.h"
namespace mt {
// Blend a generated draft with the existing motif. Apply after grid conversion, before diceApply.
void diceVaryDraft(const Project& original, const DiceOpts& opts, uint32_t seed, DiceSnap& draft);
}
