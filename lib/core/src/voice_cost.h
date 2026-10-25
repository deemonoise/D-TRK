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
constexpr int kCostFm = 225;      // bench-max 2026-10-25: TONE 4 ops + detune, 2.23 %/voice
constexpr int kCostDrum = 290;
// DRUM BDA..HHR (Drum2Kind order): bench-max group 3, 2026-10-25 (slope of the ladders' average
// load, 4 .. 16 voices, HHR 4 .. 12; the WT SYNTH reference in the same run gave 237 for its 240).
constexpr int kCostDrum2[] = {310, 355, 400, 360, 285, 310, 270, 455};
static_assert(sizeof(kCostDrum2) / sizeof(kCostDrum2[0]) ==
                  static_cast<unsigned>(DrumMachine::Count) - kDrum2First,
              "a cost per DRUM machine BDA..HHR");
constexpr int kCostString[] = {203, 228};  // StringModel order (MODAL: 6 modes)
static_assert(sizeof(kCostString) / sizeof(kCostString[0]) == static_cast<unsigned>(StringModel::Count),
              "a cost per STRING model");
// LOFI (estimates, not benched on the Teensy yet): BYTE runs a bytecode VM per sample (heavy), APC
// two counters (light, about CHIP).
constexpr int kCostLofiByte = 200, kCostLofiApc = 120;
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
