#pragma once
#include "model.h"

namespace mt {
constexpr int kModSources = static_cast<int>(ModSource::Count);
constexpr int kModTargets = static_cast<int>(LfoDest::Count);
bool modDestValid(uint8_t dest);
uint8_t modDestStep(uint8_t dest, int delta, InstrType type);
bool modBipolar(uint8_t source);
const char* modSourceName(uint8_t source);
// Amount overrides use the same unsigned two's-complement bytes as signed FX.
// Only lockMask bits kLockMatrix0..+7 affect the route depths.
void modEvaluate(const Instrument& m, const float (&sources)[kModSources], float (&out)[kModTargets],
                 uint32_t lockMask = 0, const uint8_t* locks = nullptr);
uint8_t modLfoMask(const Instrument& m);
uint16_t modSourceMask(const Instrument& m);
uint8_t modCostPotential(const Instrument& m);
}  // namespace mt
