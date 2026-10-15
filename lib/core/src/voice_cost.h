#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Voice cost: hundredths of a percent of the Teensy 4.1 CPU per voice (bench-max 2026-10-15, LP
// filter, envelope and track strip share included). allocVoice keeps the sum of the live voices'
// costs under Synth::voiceBudget().
constexpr int kCostSample = 100;  // SD / flash stream (plus the filter add-ons, as every type)
constexpr int kCostChip = 119;
constexpr int kCostSynBl = 130;   // SYNTH without a wavetable oscillator (estimate, below WT)
constexpr int kCostSynWt = 240;
constexpr int kCostFm = 195;
constexpr int kCostDrum = 290;
constexpr int kCostMacro[] = {148, 233, 166, 219, 191, 229, 174, 174};  // MacroModel order
static_assert(sizeof(kCostMacro) / sizeof(kCostMacro[0]) == static_cast<unsigned>(MacroModel::Count),
              "a cost per MACRO model");
constexpr int kCostString[] = {203, 228};  // StringModel order (MODAL: 6 modes)
static_assert(sizeof(kCostString) / sizeof(kCostString[0]) == static_cast<unsigned>(StringModel::Count),
              "a cost per STRING model");
constexpr int kCostLadder = 35, kCostMs20 = 23, kCostComb = 34, kCostFormant = 40;  // over LP
constexpr int kCostFold = 39, kCostRing = 9;  // SYNTH, only when on
// The load around the voices with every track playing: fx, track strips (bench-max group 5,
// 2026-10-15: load = 7.6 % + 1.00 x the live voices' cost, steps of 16 .. 27 live voices).
constexpr int kCostFixedTeensy = 760;
// The target load: the plan's <= 75 % with room for control-rate peaks, under the guard's 80 % (at a
// budget of 7000 the 32-voice step settled at ~77.5 % and the guard shed).
constexpr int kLoadTargetTeensy = 7300;
constexpr int kVoiceBudgetTeensy = kLoadTargetTeensy - kCostFixedTeensy;  // 6540: ~65 % for voices
constexpr int kVoiceBudgetOff = 1 << 30;

// The price of one note of m (sample: a SAMPLE note or a KIT sampler lane).
int voiceCost(const Instrument& m, bool sample);

}  // namespace mt
