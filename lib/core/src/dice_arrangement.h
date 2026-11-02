#pragma once
#include "dice.h"

namespace mt {
struct DiceArrangeSpec {
  const DiceRecipe& recipe;
  const DiceMusicProfile& music;
  int genre;
  uint8_t nativeRoles, midiMask;
  int8_t lanes[kDiceLanes];
};

// ARRANGE only: shape the common theme after density/caps, without touching locked roles.
void diceArrangeDraft(const Project& original, const DiceOpts& options, const DiceArrangeSpec& spec,
                      uint32_t seed, DiceSnap& snap);
void diceArrangeSong(const Project& original, const DiceOpts& options, const DiceArrangeSpec& spec,
                     DiceSnap& snap);
}  // namespace mt
