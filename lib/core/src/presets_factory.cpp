#include "presets_factory.h"
#include <stdio.h>

namespace mt {
namespace {

// Times: envTimeMs (40 ~ 18 ms, 60 ~ 80 ms, 80 ~ 330 ms, 100 ~ 1.4 s); cutoff: cutoffHz
// (40 ~ 160 Hz, 70 ~ 740 Hz, 90 ~ 2.1 kHz, 110 ~ 5.8 kHz). Starting points, tuned by ear.
using I = Instrument;
constexpr uint8_t W(Wave w) { return static_cast<uint8_t>(w); }
constexpr uint8_t F(FltMode f) { return static_cast<uint8_t>(f); }
constexpr uint8_t D(LfoDest d) { return static_cast<uint8_t>(d); }
constexpr uint8_t Wt(int n) { return static_cast<uint8_t>(static_cast<int>(Wave::Wt1) + n); }  // wavetables.cpp
void fm(I& m, FmMachine mc) { fmSetMachine(m, static_cast<uint8_t>(mc)); }
void dr(I& m, DrumMachine mc) { drumSetMachine(m, static_cast<uint8_t>(mc)); }
// DECAY, COLOR, SHAPE, SWEEP, CONTOUR.
void mac(I& m, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e) {
  m.macro[0] = a; m.macro[1] = b; m.macro[2] = c; m.macro[3] = d; m.macro[4] = e;
}

constexpr uint8_t LW(LfoWave w) { return static_cast<uint8_t>(w); }
// SYNTH: type with its default macros, oscillators 1, 2 and their tables (built-in "*NAME" or nullptr).
void sy(I& m, SynOsc o1, const char* wt1, SynOsc o2, const char* wt2) {
  instrSetType(m, InstrType::Synth);
  m.synOsc[0] = static_cast<uint8_t>(o1);
  m.synOsc[1] = static_cast<uint8_t>(o2);
  snprintf(m.synWt[0], sizeof(m.synWt[0]), "%s", wt1 ? wt1 : "");
  snprintf(m.synWt[1], sizeof(m.synWt[1]), "%s", wt2 ? wt2 : "");
}
// LP filter: cutoff, resonance, env amount, env decay.
void lp(I& m, uint8_t cut, uint8_t res, int8_t env, uint8_t dec) {
  m.fltMode = F(FltMode::Lp); m.cutoff = cut; m.reso = res; m.fenv = env; m.fDec = dec;
}

constexpr FactoryPreset kAll[] = {
    // CHIP
    {InstrType::Chip, "LEAD", "SQ LEAD", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 25; m.decay = 60; m.sustain = 100; m.release = 50;
       m.mono = true; m.glide = 8; m.lfoDest = D(LfoDest::Pitch); m.lfoRate = 75; m.lfoDepth = 2; }},
    {InstrType::Chip, "LEAD", "ARP PLK", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 12; m.decay = 70; m.sustain = 0; m.release = 50;
       m.fltMode = F(FltMode::Lp); m.cutoff = 75; m.reso = 30; m.fenv = 35; m.fDec = 65; m.keytrack = 64; }},
    {InstrType::Chip, "BASS", "TRI BASS", [](I& m) {
       m.wave = W(Wave::Triangle); m.transpose = -12; m.decay = 60; m.sustain = 110; m.release = 30;
       m.mono = true; }},
    {InstrType::Chip, "BASS", "ACID", [](I& m) {
       m.wave = W(Wave::Saw); m.transpose = -12; m.decay = 70; m.sustain = 90; m.release = 30;
       m.mono = true; m.glide = 15; m.fltMode = F(FltMode::Lp); m.cutoff = 38; m.reso = 105;
       m.fenv = 48; m.fDec = 68; m.keytrack = 64; m.vol = 55; }},  // resonance is loud
    {InstrType::Chip, "PAD", "PWM PAD", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 50; m.pwmRate = 20; m.pwmDepth = 35;
       m.attack = 75; m.decay = 80; m.sustain = 100; m.release = 85;
       m.fltMode = F(FltMode::Lp); m.cutoff = 85; m.reso = 20; }},
    {InstrType::Chip, "PAD", "WT PAD", [](I& m) {  // vocal A/O
       m.wave = Wt(6); m.attack = 80; m.decay = 90; m.sustain = 100; m.release = 90;
       m.lfoDest = D(LfoDest::Cutoff); m.lfoRate = 25; m.lfoDepth = 20;
       m.fltMode = F(FltMode::Lp); m.cutoff = 80; }},
    {InstrType::Chip, "KEYS", "WT BELL", [](I& m) {  // bell 1 + 2.76
       m.wave = Wt(13); m.decay = 95; m.sustain = 0; m.release = 80; }},
    {InstrType::Chip, "PERC", "NOIS HH", [](I& m) {
       m.wave = W(Wave::Noise); m.transpose = 24; m.decay = 50; m.sustain = 0; m.release = 40;
       m.fltMode = F(FltMode::Hp); m.cutoff = 108; }},
    {InstrType::Chip, "PERC", "NOIS SN", [](I& m) {
       m.wave = W(Wave::Noise); m.transpose = 12; m.decay = 65; m.sustain = 0; m.release = 40;
       m.fltMode = F(FltMode::Bp); m.cutoff = 90; m.reso = 30; m.fenv = 20; m.fDec = 50; }},
    {InstrType::Chip, "PERC", "METALBL", [](I& m) {
       m.wave = W(Wave::Metal); m.decay = 85; m.sustain = 0; m.release = 60;
       m.fltMode = F(FltMode::Bp); m.cutoff = 95; m.reso = 60; m.vol = 55; }},
    // FM
    {InstrType::Fm, "DRUMS", "KICK", [](I& m) { fm(m, FmMachine::Kick); }},
    {InstrType::Fm, "DRUMS", "SNARE", [](I& m) { fm(m, FmMachine::Snare); }},
    {InstrType::Fm, "DRUMS", "CLAP", [](I& m) { fm(m, FmMachine::Clap); }},
    {InstrType::Fm, "DRUMS", "HAT C", [](I& m) { fm(m, FmMachine::Hat); }},
    {InstrType::Fm, "DRUMS", "HAT O", [](I& m) { fm(m, FmMachine::Hat); m.macro[kMacDec] = 90; }},
    {InstrType::Fm, "DRUMS", "WOODBLK", [](I& m) { fm(m, FmMachine::Perc); mac(m, 45, 90, 80, 20, 30); }},
    {InstrType::Fm, "KEYS", "BELL", [](I& m) { fm(m, FmMachine::Metal); mac(m, 100, 60, 0, 0, 64); }},
    {InstrType::Fm, "KEYS", "E.PIANO", [](I& m) {  // sine + decaying index: tine attack
       fm(m, FmMachine::Tone); mac(m, 70, 50, 0, 60, 40);
       m.decay = 95; m.sustain = 40; m.release = 60; }},
    {InstrType::Fm, "KEYS", "CHORD M7", [](I& m) {  // SHAPE 68: MAJ7 (fmChordName)
       fm(m, FmMachine::Chord); m.macro[kMacShp] = 68; m.attack = 10; m.release = 70; }},
    {InstrType::Fm, "BASS", "FM BASS", [](I& m) {  // SHAPE 40: feedback saw
       fm(m, FmMachine::Tone); mac(m, 60, 70, 40, 50, 30); m.transpose = -12; m.mono = true;
       m.fltMode = F(FltMode::Lp); m.cutoff = 70; m.fenv = 20; }},
    // DRUM
    {InstrType::Drum, "808", "BD808", [](I& m) { dr(m, DrumMachine::Bd8); }},
    {InstrType::Drum, "808", "BD808 L", [](I& m) {  // long boom, driven, low-passed
       dr(m, DrumMachine::Bd8); mac(m, 115, 10, 40, 30, 60);
       m.fltMode = F(FltMode::Lp); m.cutoff = 95; }},
    {InstrType::Drum, "808", "SD808", [](I& m) { dr(m, DrumMachine::Sd8); }},
    {InstrType::Drum, "808", "CH808", [](I& m) { dr(m, DrumMachine::Hh8); }},
    {InstrType::Drum, "808", "OH808", [](I& m) { dr(m, DrumMachine::Hh8); m.macro[kMacDec] = 85; }},
    {InstrType::Drum, "808", "CP808", [](I& m) { dr(m, DrumMachine::Cp8); }},
    {InstrType::Drum, "808", "CB808", [](I& m) { dr(m, DrumMachine::Cb8); }},
    {InstrType::Drum, "909", "BD909", [](I& m) {
       dr(m, DrumMachine::Bd9); m.fltMode = F(FltMode::Lp); m.cutoff = 112; m.reso = 20; }},
    {InstrType::Drum, "909", "SD909", [](I& m) { dr(m, DrumMachine::Sd9); }},
    {InstrType::Drum, "909", "CH909", [](I& m) { dr(m, DrumMachine::Hh9); }},
    {InstrType::Drum, "909", "OH909", [](I& m) {
       dr(m, DrumMachine::Hh9); m.macro[kMacDec] = 85; m.fltMode = F(FltMode::Hp); m.cutoff = 90; }},
    {InstrType::Drum, "909", "CP909", [](I& m) { dr(m, DrumMachine::Cp9); }},
    // SYNTH: built-in wavetables only. Macros SHP1, SHP2, MIX (0 = osc 1 only), DET, SENV (64 = 0).
    {InstrType::Synth, "BASS", "BASS", [](I& m) {  // saw + square an octave down
       sy(m, SynOsc::Saw, nullptr, SynOsc::Square, nullptr); m.synSemi = -12; m.macro[kMacMix] = 40;
       lp(m, 50, 0, 30, 40); m.decay = 30; m.mono = true; }},
    {InstrType::Synth, "BASS", "ACID", [](I& m) {
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); lp(m, 40, 100, 45, 25);
       m.mono = true; m.glide = 10; m.vol = 60; }},  // resonance is loud
    {InstrType::Synth, "BASS", "SUBBASS", [](I& m) {  // triangle + sub an octave down
       sy(m, SynOsc::Tri, nullptr, SynOsc::Saw, nullptr); m.synSub = 100; m.synSubOct = 0;
       lp(m, 40, 0, 0, 40); m.mono = true; }},
    {InstrType::Synth, "LEAD", "LEAD", [](I& m) {  // two detuned saws
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.macro[kMacMix] = 64; m.macro[kMacDet] = 72;
       lp(m, 90, 0, 0, 40); }},
    {InstrType::Synth, "LEAD", "SYNCLD", [](I& m) {  // osc 2 synced a fifth up, swept by env -> SHAPE
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.synSync = true; m.synSemi = 7;
       m.macro[kMacMix] = 64; m.macro[kMacSenv] = 110; m.synEDec = 60; }},
    {InstrType::Synth, "PAD", "PAD", [](I& m) {
       sy(m, SynOsc::Wt, "*SAWSQR", SynOsc::Wt, "*FORMANT"); m.macro[kMacMix] = 64;
       m.attack = 70; m.release = 80;
       m.lfoWave = LW(LfoWave::Tri); m.lfoDest = D(LfoDest::Dec); m.lfoRate = 30; m.lfoDepth = 20; }},  // SHP1
    {InstrType::Synth, "PAD", "PWMSTR", [](I& m) {  // PWM strings
       sy(m, SynOsc::Square, nullptr, SynOsc::Square, nullptr); m.macro[kMacMix] = 64; m.macro[kMacDet] = 70;
       m.attack = 50; m.release = 70;
       m.lfoWave = LW(LfoWave::Sine); m.lfoDest = D(LfoDest::Dec); m.lfoRate = 40; m.lfoDepth = 30; }},
    {InstrType::Synth, "PAD", "WTSWEEP", [](I& m) {  // sine -> saw sweep through the table
       sy(m, SynOsc::Wt, "*SINSAW", SynOsc::Saw, nullptr); m.macro[kMacSenv] = 127;
       m.synEAtk = 0; m.synEDec = 80; m.release = 60; }},
    {InstrType::Synth, "KEYS", "PLUCK", [](I& m) {
       sy(m, SynOsc::Square, nullptr, SynOsc::Saw, nullptr); m.macro[kMacSenv] = 100; m.synEDec = 25;
       m.decay = 35; m.sustain = 0; m.release = 40; }},
    {InstrType::Synth, "KEYS", "BELL", [](I& m) {
       sy(m, SynOsc::Wt, "*BELL", SynOsc::Saw, nullptr); m.macro[kMacShp1] = 40;
       m.decay = 70; m.sustain = 0; m.release = 70; }},
};

}  // namespace

int factoryCount(InstrType t) {
  int n = 0;
  for (const auto& p : kAll) n += p.type == t;
  return n;
}

const FactoryPreset& factoryPreset(InstrType t, int i) {
  for (const auto& p : kAll)
    if (p.type == t && i-- == 0) return p;
  return kAll[0];
}

void factoryBuild(InstrType t, int i, Instrument& out) {
  const FactoryPreset& p = factoryPreset(t, i);
  out = Instrument();
  out.type = p.type;
  snprintf(out.name, sizeof(out.name), "%s", p.name);
  p.fill(out);
}

}  // namespace mt
