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
// DRUM BD8..CY9 (DrumMachine order): bench-max group 3, 2026-10-26, after the recalibration (3 tones,
// h3 / h5, two-part envelopes, the soft clip). Slope of the DRUM <machine> ladders' average load, 4 .. 16
// voices, DECAY 127 (the hits ring through the 16th); RS8 still ends early (9 of 16 sounding): per
// sounding voice (per played: 202). The WT SYNTH reference in the same run gave 237 for its 240.
constexpr int kCostDrum[] = {304, 301, 262, 259, 335, 262, 329, 397, 391, 344, 306, 356, 258, 279, 360, 394};
static_assert(sizeof(kCostDrum) / sizeof(kCostDrum[0]) == kDrum2First, "a cost per DRUM machine BD8..CY9");
// DRUM BDA..HHR (Drum2Kind order): bench-max group 3, 2026-10-26, with the soft clip (slope of the
// ladders' average load, 4 .. 16 voices, HHR 4 .. 12, each ladder from silence; the same WT SYNTH
// reference). Index 6 (Unused22, was BDF) is not read: that slot plays and costs as BDA (its value).
constexpr int kCostDrum2[] = {352, 367, 409, 368, 300, 322, 352, 499};
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
constexpr int kCostSid = 38;  // over LP; measured: CHIP SID MOVE - CHIP LFO, bench-max group 6
// CHIP waves over kCostChip (measured on SAW: PULSE / TRI / SAW run templated loops), bench-max group 6,
// 2026-10-26, each the whole add-on over SAW. Generic: the shared loop's per-sample wave switch (NOISE,
// METAL, WT, SID NZ, AY SAW / TRI / NZ; measured NOISE / WT). SID TS / PS / PT / PTS (measured TS) and
// AY TN run that loop too: their figures include it.
constexpr int kCostChipGeneric = 10, kCostChipSid = 31, kCostChipAyTn = 19;
// CHIP STEP: every wave in the generic loop (PULSE / TRI / SAW pay kCostChipGeneric too), plus ayStep
// once per 8 samples. Measured: CHIP STEP - CHIP PULSE - generic, 8-sample chunks, bench-max group 6.
constexpr int kCostChipStep = 13;
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
