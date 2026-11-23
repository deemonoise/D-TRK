#pragma once
#include "dice.h"
#include "dice_phrase.h"
#include "rng.h"

namespace mt {
namespace dice_detail {
BandPlan rollBand(const char *genre, const DiceRecipe &recipe, uint32_t seed,
                  int variety, int groove);
const char *bandPatch(const DiceVariant &variant, int source,
                      const PhrasePlan &plan);
void buildGuideVoice(const DiceVoice &guide, const char *patch,
                     Instrument &instrument, TrackCfg &track, bool clean,
                     Rng &rng, const PhrasePlan &plan);
bool composeBandLine(Step *row, int length, int ticks, int source,
                     const DiceVariant &variant, const DiceRecipe &recipe,
                     const DiceParts &parts, const PhrasePlan &plan,
                     int transpose, DiceDensity density);
void thinBreakSupport(Step *row, int length, DicePart part,
                      const PhrasePlan &plan, DiceDensity density);
bool bandRide(const DiceVoice &voice, const DiceRecipe &recipe,
              const BandPlan &band);
bool rideAccent(const BandPlan &band, int tick);
} // namespace dice_detail
} // namespace mt
