#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Voice cost: hundredths of a percent of the Teensy 4.1 CPU per voice (bench-max 2026-10-15, LP
// filter, envelope and track strip share included). allocVoice keeps the sum of the live voices'
// costs under Synth::voiceBudget().
constexpr int kCostSample = 100;  // original-rate/slower sample playback
// FIR prices, in 0.01% CPU/voice. USB Teensy 4.1 audit 2026-11-02:
// common bucket includes distinct pitches and cold-start headroom; pitch modulation costs extra.
// Kept separate from the 32-voice pool size; the existing budget prices actual work.
constexpr int kCostSampleFirCommon = 300;  // >1 .. 2.5 source frames/output
constexpr int kCostSampleFirMedium = 400;  // >2.5 .. 4
constexpr int kCostSampleFirHigh = 750;    // >4 .. 8
constexpr int kCostSampleFirWide = 1100;   // >8 .. 16
constexpr int kCostSampleFirExtremeStep = 20;  // per extra whole frame/output above 16
constexpr int kCostSamplePitchMod = 100;  // active cutoff/phase modulation reserve
constexpr int kCostChip = 119;
constexpr int kCostSynSat = 80, kCostSynAnalog = 100;  // USB audit: reserve0.8/1.0% per voice above the existing oscillator budget
constexpr int kCostSynBl = 200;   // USB audit 2026-11-01: ~2.0%/voice incl. gain modulation
constexpr int kCostSynWt = 240;
constexpr int kCostSynLpLimit = 100;  // high resonance: state-energy bound and its control work
constexpr int kCostSynHyper = 400;  // USB 2026-11-05: nine saws, including high pitch/density modulation margin
// Conservative upper prices from the ten-machine USB CPU audit, 2026-11-03.
// Warm/cold blocks at 4/8/16 actual voices, baseline and high-pitch/LFO extremes.
// These upper prices retain headroom after the bit-exact renderer optimizations;
// they are budget reservations, not the average CPU load. OVER reserves extra activation work.
constexpr int kCostFmSat = 40;
constexpr int kCostFmTonePunch = 300;  // measured harmonic-carrier branch when SY TONE PNCH is on
constexpr int kCostFmMachines[] = {775, 925, 975, 900, 650, 1025, 1625, 1000, 775, 900};
static_assert(sizeof(kCostFmMachines)/sizeof(kCostFmMachines[0]) == static_cast<int>(FmMachine::Count),
              "one CPU price per reference machine");
constexpr int kCostFm = kCostFmMachines[0];  // default BD MODERN, retained for common cost callers
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
// LOFI USB audit 2026-11-01: full-rate MODMIX (work score 53) ~10.5%/voice,
// APC ~1.7%. BYTE scales by formula work and its evaluation-rate upper bucket.
// This is a conservative calibrated model, not a cycle-accurate bound for every formula.
constexpr int kCostByteBase = 130, kCostByteWork = 18;
constexpr int kCostLofiByte = kCostByteBase + 53 * kCostByteWork; // reference full-rate MODMIX
constexpr int kCostLofiApc = 180;
constexpr int kCostLadder = 35, kCostMs20 = 23, kCostComb = 34, kCostFormant = 40;  // over LP
constexpr int kCostSid = 38;  // over LP; measured: CHIP SID MOVE - CHIP LFO, bench-max group 6
// CHIP waves over kCostChip (measured on SAW: PULSE / TRI / SAW run templated loops), bench-max group 6,
// 2026-10-26, each the whole add-on over SAW. Generic: the shared loop's per-sample wave switch (NOISE,
// METAL, WT, SID NZ, AY SAW / TRI / NZ; measured NOISE / WT). SID TS / PS / PT / PTS (measured TS) and
// AY TN run that loop too: their figures include it.
constexpr int kCostChipGeneric = 10, kCostChipSid = 31;
// USB audit 2026-11-01: AY TN grows with its noise clock (8x effective pitch).
// Add-ons over CHIP: <=1 clock/sample, <=16, and extreme/unknown. Extreme stress: 18 live voices at ~63% total CPU with the standard FX chain.
constexpr int kCostChipAyTn = 80, kCostChipAyTnHigh = 130, kCostChipAyTnExtreme = 230;
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

// Potential per-note modulation not present in the instrument: follower / LFO destination locks.
enum VoiceCostPotential : uint8_t {
  kCostPotentialFold = 1, kCostPotentialRing = 2, kCostPotentialSat = 4,
  kCostPotentialSamplePitch = 8, kCostPotentialSynLpLimit = 16
};
// Source frames consumed per output sample; signed steps price reverse identically. Zero/default
// preserves the original base cost. Pitch modulation reserves FIR work even at the unity boundary.
MT_CONTROL int samplePlaybackCost(float frameStep, bool pitchModulated = false);

// Includes configured SENV/LFO destinations even when their current depth is neutral:
// CON/depth locks may activate them. `potential` adds per-note/follower destinations.
// effectiveHz: BYTE pitch including SYNC, or CHIP AY TN pitch after every pitch modulator.
// <=0/unknown reserves full-rate BYTE / the extreme AY TN bucket.
// Live voices latch engine type/machine at note-on; UI may change m.type/machine while
// they still ring. Overrides price that actual engine without copying the large Instrument.
// sampleFrameStep: current source frames/output after root/sample-rate, pitch, ARP, bend and LFO;
// caller must pass the newly resolved step, not the previous control period's value.
MT_CONTROL int voiceCost(const Instrument& m, bool sample, uint8_t potential = 0, float effectiveHz = 0,
                         InstrType actualType = InstrType::Count, uint8_t actualMachine = 255,
                         float sampleFrameStep = 1.f);

}  // namespace mt
