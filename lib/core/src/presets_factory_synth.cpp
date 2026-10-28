// Factory SYNTH presets (see presets_factory.h: how to add one).
#include "presets_factory_fill.h"

namespace mt {
namespace {
using namespace pf;

// Helpers for the genre rows below. Two oscillators with MIX and DET.
void two(I& m, SynOsc a, SynOsc b, uint8_t mix, uint8_t det) {
  sy(m, a, nullptr, b, nullptr); m.macro[kMacMix] = mix; m.macro[kMacDet] = det;
}
// 24 dB ladder low-pass (round, warm): cutoff, resonance, env amount, env decay.
void ld(I& m, uint8_t cut, uint8_t res, int8_t env, uint8_t dec) {
  lp(m, cut, res, env, dec); m.fltMode = F(FltMode::Ladder);
}
// LFO 1 tempo synced: division index (4 = 1/8, 6 = 1/4, 8 = 1 bar, 9 = 2 bars, 10 = 4 bars).
void lfoT(I& m, LfoWave w, LfoDest d, uint8_t div, int8_t depth) { lfo1(m, w, d, div, depth); m.lfoSync = kLfoTempo; }

constexpr FactoryPreset kTable[] = {
    // Built-in wavetables only. Macros SHP1, SHP2, MIX (0 = osc 1 only), DET (64 = 0), SENV (64 = 0).
    {InstrType::Synth, "BASS", "BASS", [](I& m) {  // saw + square an octave down
       sy(m, SynOsc::Saw, nullptr, SynOsc::Square, nullptr); m.synSemi = -12; m.macro[kMacMix] = 40;
       lp(m, 50, 0, 30, 40); m.decay = 30; m.mono = true; }},
    {InstrType::Synth, "BASS", "ACID", [](I& m) {
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); lp(m, 40, 100, 45, 25);
       m.mono = true; m.glide = 10; m.vol = 60; }},  // resonance is loud
    {InstrType::Synth, "BASS", "SUBBASS", [](I& m) {  // triangle + sub an octave down
       sy(m, SynOsc::Tri, nullptr, SynOsc::Saw, nullptr); m.synSub = 100; m.synSubOct = 0;
       lp(m, 40, 0, 0, 40); m.mono = true; }},
    {InstrType::Synth, "BASS", "MOOGISH", [](I& m) {  // two saws, sub, filter env
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.macro[kMacMix] = 64; m.macro[kMacDet] = 68;
       m.synSub = 70; m.transpose = -12; lp(m, 45, 50, 40, 55); m.keytrack = 40; m.mono = true; m.glide = 4; m.vol = 45; }},
    {InstrType::Synth, "BASS", "REESE", [](I& m) {  // detuned saws, slow beating
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.macro[kMacMix] = 64; m.macro[kMacDet] = 78;
       m.transpose = -12; lp(m, 70, 20, 0, 40); m.drive = 25; m.mono = true; m.vol = 55; }},
    {InstrType::Synth, "BASS", "PLUCKBAS", [](I& m) {
       sy(m, SynOsc::Square, nullptr, SynOsc::Saw, nullptr); m.macro[kMacMix] = 50; m.transpose = -12;
       lp(m, 40, 30, 55, 50); env(m, 0, 70, 0, 40); m.mono = true; }},
    {InstrType::Synth, "BASS", "WOBBLE", [](I& m) {  // LFO on the cutoff, tempo synced 1/8
       sy(m, SynOsc::Saw, nullptr, SynOsc::Square, nullptr); m.macro[kMacMix] = 50; m.transpose = -12;
       lp(m, 55, 70, 0, 40); m.mono = true; m.vol = 75;
       lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 4, 40); m.lfoSync = kLfoTempo; }},
    {InstrType::Synth, "BASS", "FM GROWL", [](I& m) {  // sync sweep + drive
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.synSync = true; m.synSemi = 12; m.macro[kMacMix] = 80;
       m.transpose = -12; m.macro[kMacSenv] = 100; m.synEDec = 55; m.drive = 45; lp(m, 80, 30, 0, 40); m.mono = true; m.vol = 30; }},
    {InstrType::Synth, "BASS", "DEEPSUB", [](I& m) {  // clean sub, round
       two(m, SynOsc::Tri, SynOsc::Tri, 0, 64); m.synSub = 70; lp(m, 48, 0, 10, 50); env(m, 0, 70, 115, 45); m.mono = true; m.glide = 3; m.vol = 95; }},
    {InstrType::Synth, "BASS", "808 BASS", [](I& m) {  // long 808 tail
       two(m, SynOsc::Tri, SynOsc::Tri, 0, 64); m.synSub = 60; m.synFold = 12; lp(m, 60, 0, 0, 40); env(m, 0, 98, 0, 75); m.mono = true; m.glide = 6; m.drive = 15; m.vol = 90; }},
    {InstrType::Synth, "BASS", "808 GLD", [](I& m) {  // trap 808 with slide
       two(m, SynOsc::Tri, SynOsc::Square, 15, 64); m.synSub = 50; m.synFold = 22; lp(m, 65, 0, 0, 40); env(m, 0, 104, 0, 80); m.mono = true; m.glide = 16; m.drive = 20; m.vol = 80; }},
    {InstrType::Synth, "BASS", "DEEPHSE", [](I& m) {  // warm deep house
       two(m, SynOsc::Saw, SynOsc::Square, 40, 66); m.synSub = 40; ld(m, 52, 25, 22, 55); env(m, 0, 72, 60, 45); m.mono = true; m.vol = 80; }},
    {InstrType::Synth, "BASS", "ACIDSOFT", [](I& m) {  // 303, tamed resonance
       two(m, SynOsc::Saw, SynOsc::Saw, 0, 64); ld(m, 46, 82, 38, 50); env(m, 0, 70, 90, 35); m.mono = true; m.glide = 8; m.velCut = 12; m.vol = 62; }},
    {InstrType::Synth, "BASS", "ACIDSQ", [](I& m) {  // square 303
       two(m, SynOsc::Square, SynOsc::Square, 0, 64); m.macro[kMacShp1] = 20; ld(m, 44, 78, 40, 45); env(m, 0, 68, 80, 35); m.mono = true; m.glide = 8; m.velCut = 12; m.vol = 62; }},
    {InstrType::Synth, "BASS", "REESEWRM", [](I& m) {  // slow beating, soft top
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 75); m.synSub = 50; ld(m, 62, 12, 0, 40); env(m, 0, 70, 120, 45); m.mono = true; m.vol = 66; lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 25, 6); }},
    {InstrType::Synth, "BASS", "NEURO", [](I& m) {  // growl, 1/16 filter
       two(m, SynOsc::Saw, SynOsc::Saw, 70, 70); m.synSync = true; m.synSemi = 7; m.synFold = 35; m.synSub = 50; ld(m, 72, 40, 0, 40); m.mono = true; lfoT(m, LfoWave::Sine, LfoDest::Cutoff, 2, 22); lfoN(m, 1, LfoWave::Tri, LfoDest::Fold, 30, 20); m.drive = 25; m.vol = 42; }},
    {InstrType::Synth, "BASS", "WOBBLE2", [](I& m) {  // round wobble 1/8
       two(m, SynOsc::Square, SynOsc::Saw, 50, 68); m.synSub = 60; ld(m, 50, 35, 0, 40); m.mono = true; lfoT(m, LfoWave::Sine, LfoDest::Cutoff, 4, 30); m.vol = 70; }},
    {InstrType::Synth, "BASS", "DUBTECH", [](I& m) {  // soft, round dub
       two(m, SynOsc::Tri, SynOsc::Saw, 20, 66); m.synSub = 70; lp(m, 52, 10, 12, 60); env(m, 0, 78, 50, 55); m.mono = true; fx(m, 10, 30); m.vol = 90; }},
    {InstrType::Synth, "BASS", "PSYBASS", [](I& m) {  // tight rolling 1/16
       two(m, SynOsc::Saw, SynOsc::Saw, 0, 64); ld(m, 55, 30, 34, 40); env(m, 0, 57, 0, 32); m.mono = true; m.velCut = 8; m.vol = 82; }},
    {InstrType::Synth, "BASS", "TRANCEBS", [](I& m) {  // offbeat pluck bass
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 70); ld(m, 58, 20, 32, 46); env(m, 0, 62, 0, 40); m.mono = true; m.vol = 76; }},
    {InstrType::Synth, "BASS", "GOABASS", [](I& m) {  // sync zap bass
       two(m, SynOsc::Saw, SynOsc::Saw, 60, 64); m.synSync = true; m.synSemi = 12; m.macro[kMacSenv] = 90; m.synEDec = 40; ld(m, 62, 28, 28, 40); env(m, 0, 60, 0, 35); m.mono = true; m.vol = 66; }},
    {InstrType::Synth, "BASS", "DISCOBAS", [](I& m) {  // funky octave bass
       two(m, SynOsc::Square, SynOsc::Saw, 45, 66); m.macro[kMacShp1] = 30; lp(m, 72, 15, 22, 50); env(m, 0, 70, 50, 40); m.mono = true; m.vol = 82; }},
    {InstrType::Synth, "BASS", "ELECBASS", [](I& m) {  // electro, snappy
       two(m, SynOsc::Square, SynOsc::Saw, 35, 64); ld(m, 58, 35, 32, 48); env(m, 0, 72, 70, 40); m.mono = true; m.glide = 4; m.drive = 10; m.vol = 72; }},
    {InstrType::Synth, "BASS", "OUTRUNBS", [](I& m) {  // 16th synthwave bass
       two(m, SynOsc::Saw, SynOsc::Saw, 50, 68); lp(m, 62, 18, 28, 50); env(m, 0, 62, 0, 42); m.mono = true; m.vol = 82; }},
    {InstrType::Synth, "BASS", "DARKBASS", [](I& m) {  // darksynth grit
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 70); m.synSub = 60; m.fltMode = F(FltMode::Ms20); m.cutoff = 56; m.reso = 45; m.fenv = 22; m.fDec = 50; env(m, 0, 72, 90, 45); m.mono = true; m.drive = 20; m.vol = 52; }},
    {InstrType::Synth, "BASS", "JUNGLSUB", [](I& m) {  // deep rolling sub
       two(m, SynOsc::Tri, SynOsc::Tri, 0, 64); m.synSub = 90; lp(m, 50, 0, 0, 40); env(m, 0, 85, 80, 55); m.mono = true; m.glide = 6; m.vol = 92; }},
    {InstrType::Synth, "BASS", "MINIBASS", [](I& m) {  // short minimal blip
       two(m, SynOsc::Square, SynOsc::Tri, 30, 64); m.macro[kMacShp1] = 20; lp(m, 52, 12, 20, 40); env(m, 0, 55, 0, 35); m.mono = true; m.vol = 88; }},
    {InstrType::Synth, "BASS", "RUMBLE", [](I& m) {  // hard techno rumble
       two(m, SynOsc::Saw, SynOsc::Tri, 50, 64); m.synSub = 70; lp(m, 46, 20, 10, 60); env(m, 0, 85, 0, 60); m.mono = true; m.drive = 30; fx(m, 0, 60); m.vol = 55; }},
    {InstrType::Synth, "BASS", "LOFIBASS", [](I& m) {  // muted upright-ish
       two(m, SynOsc::Tri, SynOsc::Saw, 15, 64); m.synSub = 40; lp(m, 55, 5, 10, 50); env(m, 0, 78, 60, 50); m.mono = true; m.glide = 4; m.vol = 92; }},
    {InstrType::Synth, "BASS", "UKGBASS", [](I& m) {  // 2step slide bass
       two(m, SynOsc::Square, SynOsc::Tri, 30, 64); m.synSub = 50; ld(m, 56, 25, 22, 55); env(m, 0, 75, 90, 40); m.mono = true; m.glide = 10; m.vol = 76; }},
    {InstrType::Synth, "BASS", "MOOGFAT", [](I& m) {  // fat prog bass
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 70); m.synSub = 60; ld(m, 50, 32, 30, 55); m.keytrack = 30; env(m, 0, 75, 90, 45); m.mono = true; m.glide = 4; m.vol = 56; }},
    {InstrType::Synth, "LEAD", "LEAD", [](I& m) {  // two detuned saws
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.macro[kMacMix] = 64; m.macro[kMacDet] = 72;
       lp(m, 90, 0, 0, 40); }},
    {InstrType::Synth, "LEAD", "SYNCLD", [](I& m) {  // osc 2 synced a fifth up, swept by env -> SHAPE
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.synSync = true; m.synSemi = 7;
       m.macro[kMacMix] = 64; m.macro[kMacSenv] = 110; m.synEDec = 60; }},
    {InstrType::Synth, "LEAD", "SUPERSAW", [](I& m) {
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.macro[kMacMix] = 64; m.macro[kMacDet] = 84;
       m.synSub = 40; lp(m, 105, 0, 0, 40); fx(m, 40, 50); env(m, 10, 70, 110, 60); }},
    {InstrType::Synth, "LEAD", "SQ LEAD", [](I& m) {
       sy(m, SynOsc::Square, nullptr, SynOsc::Square, nullptr); m.synSemi = 12; m.macro[kMacMix] = 35;
       m.macro[kMacShp1] = 40; lp(m, 95, 20, 15, 60); m.mono = true; m.glide = 6;
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 80, 2); fx(m, 40, 20); }},
    {InstrType::Synth, "LEAD", "SOLO", [](I& m) {  // saw + octave square, portamento, filter env
       sy(m, SynOsc::Saw, nullptr, SynOsc::Square, nullptr); m.synSemi = 12; m.macro[kMacMix] = 40;
       lp(m, 75, 45, 30, 60); m.mono = true; m.glide = 12; fx(m, 50, 30); }},
    {InstrType::Synth, "LEAD", "WT LEAD", [](I& m) {
       sy(m, SynOsc::Wt, "*SINSAW", SynOsc::Wt, "*SINSAW"); m.macro[kMacShp1] = 70; m.macro[kMacShp2] = 90;
       m.macro[kMacMix] = 64; m.macro[kMacDet] = 70; m.mono = true; m.glide = 5;
       lfo1(m, LfoWave::Tri, LfoDest::Dec, 50, 15); fx(m, 40, 30); }},
    {InstrType::Synth, "LEAD", "HOOVER", [](I& m) {  // PWM saws, pitch scoop
       sy(m, SynOsc::Wt, "*PWM", SynOsc::Saw, nullptr); m.macro[kMacMix] = 64; m.macro[kMacDet] = 80;
       m.mono = true; m.glide = 20; lfo1(m, LfoWave::Sine, LfoDest::Dec, 55, 40); fx(m, 30, 40); }},
    {InstrType::Synth, "LEAD", "SOFTLEAD", [](I& m) {  // mellow vibrato
       two(m, SynOsc::Tri, SynOsc::Saw, 30, 66); lp(m, 85, 5, 0, 40); env(m, 20, 70, 110, 60); m.mono = true; m.glide = 6; lfo1(m, LfoWave::Sine, LfoDest::Pitch, 75, 2); fx(m, 40, 40); m.vol = 82; }},
    {InstrType::Synth, "LEAD", "UPLIFT", [](I& m) {  // wide supersaw
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 82); m.synSub = 30; lp(m, 100, 8, 0, 40); env(m, 5, 70, 115, 78); lfoN(m, 1, LfoWave::Sine, LfoDest::Fine, 60, 5); fx(m, 50, 75); m.vol = 62; }},
    {InstrType::Synth, "LEAD", "SAWSOFT", [](I& m) {  // warm supersaw
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 76); ld(m, 82, 10, 10, 60); env(m, 8, 70, 110, 65); fx(m, 40, 60); m.vol = 66; }},
    {InstrType::Synth, "LEAD", "PSYLEAD", [](I& m) {  // sync scream, delay
       two(m, SynOsc::Square, SynOsc::Saw, 64, 64); m.synSync = true; m.synSemi = 12; m.macro[kMacSenv] = 100; m.synEDec = 60; lp(m, 90, 25, 0, 40); m.mono = true; m.glide = 5; fx(m, 60, 35); m.vol = 58; }},
    {InstrType::Synth, "LEAD", "GOALEAD", [](I& m) {  // acidic fifth lead
       two(m, SynOsc::Saw, SynOsc::Square, 45, 64); m.synSemi = 7; ld(m, 82, 35, 10, 60); m.mono = true; m.glide = 8; lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 55, 10); fx(m, 60, 45); m.vol = 60; }},
    {InstrType::Synth, "LEAD", "OUTRUNLD", [](I& m) {  // 80s poly lead
       two(m, SynOsc::Saw, SynOsc::Square, 45, 72); lp(m, 88, 15, 15, 60); env(m, 5, 70, 110, 65); m.mono = true; m.glide = 8; lfo1(m, LfoWave::Sine, LfoDest::Pitch, 70, 3); fx(m, 50, 65); m.vol = 68; }},
    {InstrType::Synth, "LEAD", "FLUTE", [](I& m) {  // breathy
       two(m, SynOsc::Tri, SynOsc::Tri, 0, 64); m.synNoise = 14; lp(m, 92, 0, 0, 40); env(m, 30, 70, 110, 60); m.mono = true; m.glide = 4; lfo1(m, LfoWave::Sine, LfoDest::Pitch, 70, 2); fx(m, 30, 70); m.vol = 90; }},
    {InstrType::Synth, "LEAD", "SINELEAD", [](I& m) {  // pure, chillwave
       two(m, SynOsc::Tri, SynOsc::Tri, 20, 64); m.synSemi = 12; env(m, 5, 70, 120, 65); m.mono = true; m.glide = 10; fx(m, 40, 60); m.vol = 92; }},
    {InstrType::Synth, "LEAD", "MELOLEAD", [](I& m) {  // melodic techno lead
       sy(m, SynOsc::Wt, "*SINSAW", SynOsc::Saw, nullptr); m.macro[kMacShp1] = 50; m.macro[kMacMix] = 40; m.macro[kMacDet] = 68; m.macro[kMacSenv] = 82; m.synEDec = 70; ld(m, 80, 22, 10, 60); env(m, 5, 75, 105, 70); fx(m, 60, 75); m.vol = 70; }},
    {InstrType::Synth, "LEAD", "BRAINLD", [](I& m) {  // twitchy PWM
       two(m, SynOsc::Square, SynOsc::Tri, 30, 64); m.macro[kMacShp1] = 40; lp(m, 90, 10, 0, 40); m.mono = true; m.glide = 3; lfo1(m, LfoWave::Random, LfoDest::Dec, 90, 20); fx(m, 50, 40); m.vol = 72; }},
    {InstrType::Synth, "LEAD", "GLIDELD", [](I& m) {  // trap portamento
       two(m, SynOsc::Saw, SynOsc::Square, 35, 66); lp(m, 80, 10, 0, 40); env(m, 3, 70, 110, 55); m.mono = true; m.glide = 20; fx(m, 40, 50); m.vol = 72; }},
    {InstrType::Synth, "LEAD", "DARKLEAD", [](I& m) {  // gritty MS20
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 70); m.fltMode = F(FltMode::Ms20); m.cutoff = 75; m.reso = 40; m.fenv = 12; m.fDec = 55; m.mono = true; m.glide = 6; m.drive = 22; fx(m, 40, 50); m.vol = 46; }},
    {InstrType::Synth, "LEAD", "ELECTRLD", [](I& m) {  // electro PWM
       two(m, SynOsc::Square, SynOsc::Square, 35, 64); m.synSemi = 12; m.macro[kMacShp1] = 30; lp(m, 85, 22, 15, 50); m.mono = true; lfo1(m, LfoWave::Tri, LfoDest::Dec, 45, 18); fx(m, 50, 30); m.vol = 68; }},
    {InstrType::Synth, "LEAD", "PROGLEAD", [](I& m) {  // soft PWM lead
       sy(m, SynOsc::Wt, "*PWM", SynOsc::Saw, nullptr); m.macro[kMacMix] = 50; m.macro[kMacDet] = 70; lp(m, 90, 12, 0, 40); env(m, 10, 75, 100, 70); lfo1(m, LfoWave::Tri, LfoDest::Dec, 35, 15); fx(m, 60, 70); m.vol = 66; }},
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
    {InstrType::Synth, "PAD", "WARM PAD", [](I& m) {
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.macro[kMacMix] = 64; m.macro[kMacDet] = 74;
       env(m, 78, 90, 110, 95); lp(m, 70, 15, 0, 40); lfo1(m, LfoWave::Tri, LfoDest::Cutoff, 28, 15); fx(m, 20, 80); }},
    {InstrType::Synth, "PAD", "CHOIR", [](I& m) {
       sy(m, SynOsc::Wt, "*FORMANT", SynOsc::Wt, "*FORMANT"); m.macro[kMacShp1] = 40; m.macro[kMacShp2] = 80;
       m.macro[kMacMix] = 64; m.macro[kMacDet] = 70; env(m, 75, 90, 110, 90);
       lfo1(m, LfoWave::Sine, LfoDest::Dec, 30, 20); fx(m, 0, 90); }},
    {InstrType::Synth, "PAD", "ORGAN", [](I& m) {
       sy(m, SynOsc::Wt, "*ORGAN", SynOsc::Wt, "*ORGAN"); m.macro[kMacShp1] = 50; m.synSemi = 12; m.macro[kMacMix] = 40;
       env(m, 10, 60, 120, 45); lfo1(m, LfoWave::Sine, LfoDest::Vol, 85, 8); fx(m, 0, 40); }},
    {InstrType::Synth, "PAD", "DARK PAD", [](I& m) {
       sy(m, SynOsc::Wt, "*TRISQR", SynOsc::Saw, nullptr); m.synSemi = -12; m.macro[kMacMix] = 50; m.macro[kMacDet] = 68;
       env(m, 80, 90, 110, 100); lp(m, 55, 30, 0, 40); lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 20, 20);
       lfoN(m, 1, LfoWave::Tri, LfoDest::Dec, 35, 25); fx(m, 30, 90); }},
    {InstrType::Synth, "PAD", "SHIMMER", [](I& m) {  // octave up, bright, long reverb
       sy(m, SynOsc::Wt, "*BELL", SynOsc::Saw, nullptr); m.synSemi = 12; m.macro[kMacMix] = 50; m.macro[kMacDet] = 70;
       env(m, 70, 90, 100, 100); hp(m, 60, 10); fx(m, 50, 110); }},
    {InstrType::Synth, "PAD", "DREAMPAD", [](I& m) {  // lush, slow
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 76); lp(m, 78, 5, 0, 40); env(m, 90, 100, 110, 105); lfo1(m, LfoWave::Tri, LfoDest::Cutoff, 25, 10); lfoN(m, 1, LfoWave::Sine, LfoDest::Fine, 45, 5); fx(m, 30, 110); m.vol = 66; }},
    {InstrType::Synth, "PAD", "DEEP PAD", [](I& m) {  // deep house warmth
       two(m, SynOsc::Tri, SynOsc::Saw, 40, 70); lp(m, 66, 10, 0, 40); env(m, 85, 95, 110, 95); lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 22, 8); fx(m, 20, 90); m.vol = 82; }},
    {InstrType::Synth, "PAD", "VOX PAD", [](I& m) {  // vowel aah
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 72); m.fltMode = F(FltMode::Formant); m.cutoff = 30; m.reso = 30; env(m, 80, 95, 110, 95); lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 18, 10); fx(m, 20, 100); m.vol = 70; }},
    {InstrType::Synth, "PAD", "STRINGS", [](I& m) {  // ensemble strings
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 76); lp(m, 85, 0, 0, 40); env(m, 72, 90, 115, 85); lfo1(m, LfoWave::Sine, LfoDest::Fine, 60, 6); fx(m, 10, 85); m.vol = 66; }},
    {InstrType::Synth, "PAD", "STRNGDRK", [](I& m) {  // dark low strings
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 74); ld(m, 64, 10, 0, 40); env(m, 80, 95, 115, 95); lfo1(m, LfoWave::Sine, LfoDest::Fine, 55, 6); fx(m, 10, 95); m.vol = 72; }},
    {InstrType::Synth, "PAD", "TRANCEPD", [](I& m) {  // big supersaw pad
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 84); m.synSub = 25; lp(m, 95, 5, 0, 40); env(m, 70, 90, 115, 95); fx(m, 40, 100); m.vol = 58; }},
    {InstrType::Synth, "PAD", "DRONE", [](I& m) {  // evolving drone
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 68); m.synSemi = -12; ld(m, 55, 20, 0, 40); env(m, 98, 100, 127, 115); lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 10, 14); lfoN(m, 1, LfoWave::Tri, LfoDest::Swp, 12, 8); fx(m, 30, 110); m.vol = 70; }},
    {InstrType::Synth, "PAD", "DRONEDRK", [](I& m) {  // dark ring drone
       two(m, SynOsc::Tri, SynOsc::Square, 50, 66); m.synSemi = -12; m.synRing = 30; lp(m, 50, 15, 0, 40); env(m, 98, 100, 127, 118); lfo1(m, LfoWave::Sine, LfoDest::Ring, 8, 15); fx(m, 30, 115); m.vol = 76; }},
    {InstrType::Synth, "PAD", "GLASSPAD", [](I& m) {  // glassy shimmer
       sy(m, SynOsc::Wt, "*BELL", SynOsc::Tri, nullptr); m.macro[kMacShp1] = 30; m.synSemi = 12; m.macro[kMacMix] = 40; lp(m, 100, 0, 0, 40); env(m, 80, 100, 100, 100); lfo1(m, LfoWave::Tri, LfoDest::Dec, 20, 12); fx(m, 50, 110); m.vol = 68; }},
    {InstrType::Synth, "PAD", "CHILLPAD", [](I& m) {  // washed, tape wow
       sy(m, SynOsc::Wt, "*SINSAW", SynOsc::Saw, nullptr); m.macro[kMacShp1] = 40; m.macro[kMacMix] = 40; m.macro[kMacDet] = 70; lp(m, 75, 5, 0, 40); env(m, 75, 95, 110, 95); lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 30, 10); lfoN(m, 1, LfoWave::Sine, LfoDest::Fine, 38, 9); fx(m, 40, 100); m.vol = 72; }},
    {InstrType::Synth, "PAD", "OUTRUNPD", [](I& m) {  // 80s poly pad
       two(m, SynOsc::Saw, SynOsc::Square, 50, 76); lp(m, 85, 10, 0, 40); env(m, 70, 90, 115, 95); lfo1(m, LfoWave::Sine, LfoDest::Fine, 50, 5); fx(m, 30, 90); m.vol = 64; }},
    {InstrType::Synth, "PAD", "PROGPAD", [](I& m) {  // 2-bar filter swell
       sy(m, SynOsc::Wt, "*PWM", SynOsc::Saw, nullptr); m.macro[kMacMix] = 50; m.macro[kMacDet] = 72; lp(m, 78, 15, 0, 40); env(m, 75, 95, 110, 95); lfoT(m, LfoWave::Tri, LfoDest::Cutoff, 9, 14); fx(m, 30, 95); m.vol = 66; }},
    {InstrType::Synth, "PAD", "SPACEPAD", [](I& m) {  // airy ambient
       sy(m, SynOsc::Wt, "*FORMANT", SynOsc::Wt, "*BELL"); m.macro[kMacShp1] = 50; m.macro[kMacShp2] = 30; m.macro[kMacMix] = 50; m.macro[kMacDet] = 70; hp(m, 45, 0); env(m, 95, 100, 110, 110); lfo1(m, LfoWave::Sine, LfoDest::Dec, 15, 20); fx(m, 50, 120); m.vol = 66; }},
    {InstrType::Synth, "PAD", "ABYSS", [](I& m) {  // deep dark wash
       two(m, SynOsc::Tri, SynOsc::Saw, 50, 68); m.synSemi = -12; ld(m, 52, 25, 0, 40); env(m, 92, 100, 120, 110); lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 12, 12); fx(m, 20, 110); m.vol = 78; }},
    {InstrType::Synth, "PAD", "HALO", [](I& m) {  // soft sine halo
       two(m, SynOsc::Tri, SynOsc::Tri, 50, 70); m.synSemi = 12; env(m, 90, 100, 115, 110); lfo1(m, LfoWave::Sine, LfoDest::Vol, 25, 6); fx(m, 30, 120); m.vol = 88; }},
    {InstrType::Synth, "KEYS", "PLUCK", [](I& m) {
       sy(m, SynOsc::Square, nullptr, SynOsc::Saw, nullptr); m.macro[kMacSenv] = 100; m.synEDec = 25;
       m.decay = 35; m.sustain = 0; m.release = 40; }},
    {InstrType::Synth, "KEYS", "BELL", [](I& m) {
       sy(m, SynOsc::Wt, "*BELL", SynOsc::Saw, nullptr); m.macro[kMacShp1] = 40;
       m.decay = 70; m.sustain = 0; m.release = 70; }},
    {InstrType::Synth, "KEYS", "STAB", [](I& m) {  // house stab
       sy(m, SynOsc::Saw, nullptr, SynOsc::Square, nullptr); m.synSemi = 7; m.macro[kMacMix] = 55;
       lp(m, 60, 40, 45, 50); env(m, 0, 68, 0, 50); fx(m, 50, 40); m.vol = 75; }},
    {InstrType::Synth, "KEYS", "E.PIANO", [](I& m) {
       sy(m, SynOsc::Wt, "*SINSAW", SynOsc::Tri, nullptr); m.macro[kMacShp1] = 20; m.synSemi = 12; m.macro[kMacMix] = 25;
       m.macro[kMacSenv] = 90; m.synEDec = 40; env(m, 0, 95, 30, 65); lfo1(m, LfoWave::Sine, LfoDest::Vol, 70, 6); fx(m, 20, 40); }},
    {InstrType::Synth, "KEYS", "KALIMBA", [](I& m) {
       sy(m, SynOsc::Tri, nullptr, SynOsc::Wt, "*BELL"); m.synSemi = 24; m.macro[kMacMix] = 30;
       env(m, 0, 80, 0, 65); fx(m, 30, 50); }},
    {InstrType::Synth, "KEYS", "HARP", [](I& m) {
       sy(m, SynOsc::Tri, nullptr, SynOsc::Saw, nullptr); m.macro[kMacMix] = 25; lp(m, 85, 0, 30, 60);
       env(m, 0, 90, 0, 80); fx(m, 30, 70); }},
    {InstrType::Synth, "KEYS", "EP SOFT", [](I& m) {  // soft tine EP
       two(m, SynOsc::Tri, SynOsc::Tri, 20, 64); m.synSemi = 12; m.synRing = 15; env(m, 0, 95, 30, 70); lfo1(m, LfoWave::Sine, LfoDest::Vol, 75, 8); m.velCut = 8; fx(m, 20, 50); m.vol = 90; }},
    {InstrType::Synth, "KEYS", "EP DEEP", [](I& m) {  // deep house EP, auto-pan
       sy(m, SynOsc::Wt, "*SINSAW", SynOsc::Tri, nullptr); m.macro[kMacShp1] = 15; m.macro[kMacMix] = 35; lp(m, 85, 0, 0, 40); env(m, 0, 100, 40, 75); lfo1(m, LfoWave::Sine, LfoDest::Pan, 60, 20); fx(m, 30, 60); m.vol = 85; }},
    {InstrType::Synth, "KEYS", "HOUSEORG", [](I& m) {  // 90s house organ
       sy(m, SynOsc::Wt, "*ORGAN", SynOsc::Tri, nullptr); m.macro[kMacShp1] = 80; m.macro[kMacMix] = 20; lp(m, 95, 0, 0, 40); env(m, 0, 62, 100, 40); fx(m, 20, 30); m.vol = 70; }},
    {InstrType::Synth, "KEYS", "DUBCHORD", [](I& m) {  // dub techno chord, echo
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 72); lp(m, 56, 28, 30, 55); env(m, 0, 70, 0, 60); fx(m, 90, 80); m.vol = 74; }},
    {InstrType::Synth, "KEYS", "KORGPNO", [](I& m) {  // house piano
       two(m, SynOsc::Square, SynOsc::Saw, 40, 64); m.synSemi = 12; m.synRing = 20; lp(m, 92, 0, 20, 60); env(m, 0, 85, 20, 60); fx(m, 20, 40); m.vol = 72; }},
    {InstrType::Synth, "KEYS", "RAVESTB", [](I& m) {  // rave stab
       two(m, SynOsc::Square, SynOsc::Saw, 50, 70); m.synSemi = 12; lp(m, 80, 20, 28, 55); env(m, 0, 70, 0, 55); fx(m, 40, 50); m.vol = 66; }},
    {InstrType::Synth, "KEYS", "TRANCSTB", [](I& m) {  // supersaw stab
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 84); lp(m, 85, 10, 25, 55); env(m, 0, 72, 20, 60); fx(m, 50, 70); m.vol = 62; }},
    {InstrType::Synth, "KEYS", "CLAV", [](I& m) {  // funky clav
       two(m, SynOsc::Square, SynOsc::Saw, 40, 64); m.macro[kMacShp1] = 70; m.synSemi = 12; lp(m, 88, 18, 28, 40); env(m, 0, 62, 0, 35); m.velCut = 10; m.vol = 78; }},
    {InstrType::Synth, "KEYS", "VIBES", [](I& m) {  // vibraphone
       sy(m, SynOsc::Tri, nullptr, SynOsc::Wt, "*BELL"); m.synSemi = 12; m.macro[kMacMix] = 30; env(m, 0, 90, 0, 80); lfo1(m, LfoWave::Sine, LfoDest::Vol, 70, 10); fx(m, 30, 70); m.vol = 85; }},
    {InstrType::Synth, "KEYS", "LOFIKEYS", [](I& m) {  // dusty wobbly keys
       sy(m, SynOsc::Tri, nullptr, SynOsc::Wt, "*SINSAW"); m.macro[kMacShp2] = 20; m.macro[kMacMix] = 30; lp(m, 70, 0, 0, 40); env(m, 0, 92, 40, 70); lfo1(m, LfoWave::Sine, LfoDest::Fine, 35, 10); fx(m, 20, 60); m.vol = 92; }},
    {InstrType::Synth, "KEYS", "POLYSYN", [](I& m) {  // synthwave chords
       two(m, SynOsc::Saw, SynOsc::Square, 50, 72); lp(m, 74, 15, 25, 70); env(m, 5, 80, 70, 70); fx(m, 30, 60); m.vol = 68; }},
    {InstrType::Synth, "KEYS", "BRASS", [](I& m) {  // 80s synth brass
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 70); lp(m, 60, 10, 35, 75); m.fAtk = 55; env(m, 30, 80, 100, 60); fx(m, 20, 60); m.vol = 66; }},
    {InstrType::Synth, "PLUCK", "PLUCK WT", [](I& m) {
       sy(m, SynOsc::Wt, "*SAWSQR", SynOsc::Wt, "*PWM"); m.macro[kMacMix] = 64; m.macro[kMacSenv] = 115; m.synEDec = 35;
       env(m, 0, 68, 0, 55); fx(m, 50, 40); }},
    {InstrType::Synth, "PLUCK", "PING", [](I& m) {
       sy(m, SynOsc::Square, nullptr, SynOsc::Tri, nullptr); m.synSemi = 12; m.macro[kMacMix] = 40;
       lp(m, 50, 60, 55, 45); env(m, 0, 60, 0, 50); fx(m, 70, 30); m.vol = 60; }},
    {InstrType::Synth, "PLUCK", "CHIPPLK", [](I& m) {
       sy(m, SynOsc::Square, nullptr, SynOsc::Square, nullptr); m.macro[kMacShp1] = 20; m.synSemi = 12; m.macro[kMacMix] = 30;
       env(m, 0, 58, 0, 40); m.crushBits = 60; }},
    {InstrType::Synth, "PLUCK", "SYNCPLK", [](I& m) {
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.synSync = true; m.synSemi = 19; m.macro[kMacMix] = 90;
       m.macro[kMacSenv] = 30; m.synEDec = 45; env(m, 0, 65, 0, 50); fx(m, 40, 30); }},
    {InstrType::Synth, "PLUCK", "SOFTPLK", [](I& m) {  // gentle pluck
       two(m, SynOsc::Tri, SynOsc::Saw, 30, 66); lp(m, 78, 5, 25, 50); env(m, 0, 70, 0, 60); m.velCut = 8; fx(m, 40, 60); m.vol = 86; }},
    {InstrType::Synth, "PLUCK", "MARIMBA", [](I& m) {  // wooden mallet
       two(m, SynOsc::Tri, SynOsc::Tri, 30, 64); m.synSemi = 24; m.synRing = 25; env(m, 0, 72, 0, 60); fx(m, 20, 50); m.vol = 90; }},
    {InstrType::Synth, "PLUCK", "DEEPPLK", [](I& m) {  // muted deep pluck
       two(m, SynOsc::Saw, SynOsc::Tri, 40, 66); ld(m, 58, 20, 30, 50); env(m, 0, 72, 0, 60); fx(m, 60, 60); m.vol = 80; }},
    {InstrType::Synth, "PLUCK", "MELOPLK", [](I& m) {  // melodic techno arp
       sy(m, SynOsc::Wt, "*SINSAW", SynOsc::Saw, nullptr); m.macro[kMacShp1] = 60; m.macro[kMacMix] = 45; m.macro[kMacDet] = 70; ld(m, 68, 25, 30, 55); env(m, 0, 75, 0, 65); fx(m, 60, 80); m.vol = 74; }},
    {InstrType::Synth, "PLUCK", "ARPPLK", [](I& m) {  // trance / outrun arp
       two(m, SynOsc::Square, SynOsc::Saw, 50, 66); m.macro[kMacShp1] = 30; lp(m, 74, 22, 30, 42); env(m, 0, 62, 0, 45); fx(m, 50, 40); m.vol = 76; }},
    {InstrType::Synth, "PLUCK", "PSYPLK", [](I& m) {  // psy zap pluck
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 64); m.synSync = true; m.synSemi = 12; m.macro[kMacSenv] = 90; m.synEDec = 35; lp(m, 85, 25, 0, 40); env(m, 0, 58, 0, 40); fx(m, 60, 30); m.vol = 64; }},
    {InstrType::Synth, "PLUCK", "GLITCHPL", [](I& m) {  // metallic glitch
       two(m, SynOsc::Square, SynOsc::Square, 50, 64); m.synSemi = 7; m.synRing = 60; env(m, 0, 55, 0, 35); lfo1(m, LfoWave::Random, LfoDest::Pitch, 100, 2); fx(m, 50, 30); m.vol = 66; }},
    {InstrType::Synth, "PLUCK", "BLIP", [](I& m) {  // tiny sine blip
       two(m, SynOsc::Tri, SynOsc::Tri, 0, 64); m.transpose = 24; env(m, 0, 50, 0, 30); fx(m, 50, 30); m.vol = 85; }},
    {InstrType::Synth, "PLUCK", "DREAMPLK", [](I& m) {  // bell, long reverb
       sy(m, SynOsc::Wt, "*BELL", SynOsc::Tri, nullptr); m.macro[kMacShp1] = 20; m.macro[kMacMix] = 50; env(m, 0, 85, 0, 85); fx(m, 50, 110); m.vol = 80; }},
    {InstrType::Synth, "PLUCK", "UKGPLK", [](I& m) {  // 2step organ pluck
       sy(m, SynOsc::Wt, "*ORGAN", SynOsc::Square, nullptr); m.macro[kMacShp1] = 60; m.macro[kMacMix] = 25; lp(m, 85, 10, 15, 45); env(m, 0, 62, 0, 45); fx(m, 40, 40); m.vol = 74; }},
    {InstrType::Synth, "PLUCK", "PROGPLK", [](I& m) {  // wide prog pluck
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 76); lp(m, 70, 12, 30, 60); env(m, 0, 75, 0, 70); fx(m, 50, 80); m.vol = 68; }},
    {InstrType::Synth, "PLUCK", "DUBPLK", [](I& m) {  // dub echo pluck
       two(m, SynOsc::Tri, SynOsc::Saw, 25, 64); lp(m, 66, 20, 20, 50); env(m, 0, 65, 0, 55); fx(m, 100, 70); m.vol = 80; }},
    {InstrType::Synth, "FX", "RISER", [](I& m) {  // noise + pitch rising over ~4 s
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.synNoise = 90; m.macro[kMacMix] = 64; m.macro[kMacDet] = 80;
       env(m, 60, 100, 120, 80); lp(m, 50, 40, 60, 115);
       lfo1(m, LfoWave::Saw, LfoDest::Pitch, 4, 30); fx(m, 30, 70); m.vol = 60; }},
    {InstrType::Synth, "FX", "NOISESWP", [](I& m) {
       sy(m, SynOsc::Tri, nullptr, SynOsc::Tri, nullptr); m.macro[kMacMix] = 0; m.synNoise = 127;
       env(m, 50, 90, 80, 90); bp(m, 70, 80); lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 35, 45); m.vol = 70; fx(m, 30, 80); }},
    {InstrType::Synth, "FX", "ROBOT", [](I& m) {
       sy(m, SynOsc::Wt, "*FORMANT", SynOsc::Square, nullptr); m.macro[kMacMix] = 30; m.crushRate = 60; m.crushBits = 60;
       lfo1(m, LfoWave::Square, LfoDest::Dec, 90, 50); m.mono = true; }},
    {InstrType::Synth, "FX", "SCIFI", [](I& m) {
       sy(m, SynOsc::Wt, "*SYNC", SynOsc::Saw, nullptr); m.macro[kMacMix] = 40;
       lfo1(m, LfoWave::Random, LfoDest::Pitch, 90, 12); lfoN(m, 1, LfoWave::Sine, LfoDest::Dec, 60, 50);
       fx(m, 60, 60); }},
    {InstrType::Synth, "FX", "UPLIFTER", [](I& m) {  // 2-bar rising sweep
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 80); m.synNoise = 70; hp(m, 40, 20); env(m, 80, 100, 120, 90); lfoT(m, LfoWave::Saw, LfoDest::Cutoff, 9, 40); fx(m, 30, 90); m.vol = 52; }},
    {InstrType::Synth, "FX", "DOWNLIFT", [](I& m) {  // falling sweep
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 78); m.synNoise = 40; lp(m, 90, 20, 0, 40); env(m, 0, 105, 0, 95); lfo1(m, LfoWave::Saw, LfoDest::Pitch, 6, -30); fx(m, 30, 90); m.vol = 52; }},
    {InstrType::Synth, "FX", "WHOOSH", [](I& m) {  // noise whoosh
       two(m, SynOsc::Tri, SynOsc::Tri, 0, 64); m.synNoise = 127; bp(m, 60, 40); env(m, 85, 100, 0, 95); lfo1(m, LfoWave::Tri, LfoDest::Cutoff, 20, 35); fx(m, 20, 100); m.vol = 60; }},
    {InstrType::Synth, "FX", "WIND", [](I& m) {  // ambient wind
       two(m, SynOsc::Tri, SynOsc::Tri, 0, 64); m.synNoise = 127; bp(m, 62, 55); env(m, 95, 100, 120, 105); lfo1(m, LfoWave::Random, LfoDest::Cutoff, 15, 20); fx(m, 0, 100); m.vol = 58; }},
    {InstrType::Synth, "FX", "ZAP", [](I& m) {  // laser zap down
       two(m, SynOsc::Tri, SynOsc::Square, 30, 64); env(m, 0, 55, 0, 40); lfo1(m, LfoWave::Saw, LfoDest::Pitch, 70, -40); fx(m, 40, 30); m.vol = 70; }},
    {InstrType::Synth, "FX", "SWEEPUP", [](I& m) {  // filter opens slowly
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 76); lp(m, 40, 30, 60, 0); m.fAtk = 110; env(m, 0, 100, 127, 80); fx(m, 30, 80); m.vol = 52; }},
    {InstrType::Synth, "FX", "SIREN", [](I& m) {  // dub / jungle siren
       two(m, SynOsc::Square, SynOsc::Tri, 40, 64); lp(m, 85, 10, 0, 40); lfo1(m, LfoWave::Tri, LfoDest::Pitch, 50, 20); fx(m, 50, 60); m.vol = 52; }},
    {InstrType::Synth, "FX", "LASER", [](I& m) {  // sync laser
       two(m, SynOsc::Saw, SynOsc::Saw, 64, 64); m.synSync = true; m.synSemi = 24; m.macro[kMacSenv] = 20; m.synEDec = 55; lp(m, 90, 20, 0, 40); env(m, 0, 65, 0, 50); fx(m, 60, 40); m.vol = 55; }},
    {InstrType::Synth, "FX", "SPACEDRN", [](I& m) {  // space texture
       sy(m, SynOsc::Wt, "*FORMANT", SynOsc::Tri, nullptr); m.synSemi = 7; m.synRing = 60; m.macro[kMacMix] = 50; env(m, 94, 100, 120, 110); lfo1(m, LfoWave::Sine, LfoDest::Dec, 12, 25); lfoN(m, 1, LfoWave::Tri, LfoDest::Ring, 9, 20); fx(m, 40, 120); m.vol = 60; }},
    {InstrType::Synth, "FX", "IMPACT", [](I& m) {  // boom impact
       two(m, SynOsc::Tri, SynOsc::Tri, 0, 64); m.synSub = 80; m.synNoise = 90; lp(m, 80, 10, -20, 80); env(m, 0, 100, 0, 95); lfo1(m, LfoWave::Saw, LfoDest::Pitch, 8, -20); fx(m, 0, 110); m.vol = 66; }},
};

}  // namespace

const FactoryTable kFactorySynth = {kTable, static_cast<int>(sizeof(kTable) / sizeof(kTable[0]))};

}  // namespace mt
