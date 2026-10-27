#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Voice cost: hundredths of a percent of the ESP32-S3 audio budget (4000 us per 128-sample block at
// 32 kHz; 1 = 0.4 us per block) per sounding voice, envelope and track strip share included. allocVoice
// keeps the sum of the live voices' costs under Synth::voiceBudget(). AUDIO_BENCH_COST (audio.cpp),
// 2026-10-27, pool 24 / heavy 8: the slope of the average render time over 4 .. 16 sounding voices
// (heavy: 2 .. 8). The filter is an add-on (kCostFilter), the engines are priced with it off.
constexpr bool kVoiceCostEspMeasured = true;
constexpr int kCostChip = 163;         // PULSE 163, SAW 158 (templated loops), filter off
constexpr int kCostChipGeneric = 45;   // over kCostChip: NOISE 208, WT1 205 (the generic loop)
constexpr int kCostSample = 210;       // estimate (not benched: no project sample): about CHIP NOISE
constexpr int kCostSynBl = 265;        // SYNTH without a wavetable oscillator
constexpr int kCostSynWt = 374;        // SYNTH with a wavetable oscillator (both WT)
constexpr int kCostLofiByte = 457, kCostLofiApc = 222;
constexpr int kCostFilter = 50;        // the voice's SVF (LP / BP / HP): CHIP SAW LP 208 - SAW 158
// FM per machine (FmMachine order): KICK .. HAT; drums retriggered every 16th, DECAY 127.
constexpr int kCostFm[] = {533, 553, 541, 426, 472, 661, 357, 742};
static_assert(sizeof(kCostFm) / sizeof(kCostFm[0]) == static_cast<unsigned>(FmMachine::Count),
              "a cost per FM machine");
// DRUM BD8..CY9 (DrumMachine order), DECAY 127 retriggered every 16th, per sounding voice (RS8 ends
// early: 496 per sounding, 272 per played).
constexpr int kCostDrum[] = {464, 460, 395, 370, 496, 400, 452, 556, 548, 537, 474, 550, 371, 411, 527, 556};
static_assert(sizeof(kCostDrum) / sizeof(kCostDrum[0]) == static_cast<unsigned>(DrumMachine::Count),
              "a cost per DRUM machine");
// The load around the voices: the ladders' median intercept, 337 us (fx, master; silent fx only: 224 us).
constexpr int kCostFixedEsp = 843;
// The target load: 70 % under the CPU guard's 80 % (Synth::kLoadHigh, smoothed). The bench's peaks run
// ~+600 .. 900 us over the average (hits, note-ons): at 2800 us they stay under the 4000 us block.
constexpr int kLoadTargetEsp = 7000;
constexpr int kVoiceBudgetEsp = kLoadTargetEsp - kCostFixedEsp;
constexpr int kVoiceBudgetOff = 1 << 30;

// The price of one note of m (sample: a SAMPLE note or a KIT sampler lane).
int voiceCost(const Instrument& m, bool sample);

}  // namespace mt
