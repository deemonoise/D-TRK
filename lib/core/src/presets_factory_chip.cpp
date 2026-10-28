// Factory CHIP presets (see presets_factory.h: how to add one).
#include "presets_factory_fill.h"

namespace mt {
namespace {
using namespace pf;

constexpr FactoryPreset kTable[] = {
    {InstrType::Chip, "LEAD", "SQ LEAD", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 25; m.decay = 60; m.sustain = 100; m.release = 50;
       m.mono = true; m.glide = 8; m.lfoDest = D(LfoDest::Pitch); m.lfoRate = 75; m.lfoDepth = 2; }},
    {InstrType::Chip, "LEAD", "ARP PLK", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 12; m.decay = 70; m.sustain = 0; m.release = 50;
       m.fltMode = F(FltMode::Lp); m.cutoff = 75; m.reso = 30; m.fenv = 35; m.fDec = 65; m.keytrack = 64; }},
    {InstrType::Chip, "LEAD", "NES LEAD", [](I& m) {  // 50 % square, delayed vibrato feel
       m.wave = W(Wave::Pulse); m.duty = 50; env(m, 0, 70, 90, 45); m.mono = true;
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 80, 3); fx(m, 30, 0); }},
    {InstrType::Chip, "LEAD", "THIN LD", [](I& m) {  // 12.5 % pulse, nasal
       m.wave = W(Wave::Pulse); m.duty = 12; env(m, 0, 60, 100, 40); m.glide = 5; m.mono = true; }},
    {InstrType::Chip, "LEAD", "SAW LD", [](I& m) {
       m.wave = W(Wave::Saw); env(m, 0, 70, 100, 50); lp(m, 95, 25, 15, 60); m.keytrack = 64;
       m.mono = true; m.glide = 6; fx(m, 40, 20); }},
    {InstrType::Chip, "LEAD", "FLUTE", [](I& m) {  // sine table, breathy attack
       m.wave = Wt(0); env(m, 55, 70, 110, 55); lfo1(m, LfoWave::Sine, LfoDest::Pitch, 78, 2); fx(m, 0, 40); }},
    {InstrType::Chip, "LEAD", "GBLEAD", [](I& m) {  // stepped sine: Game Boy wave channel
       m.wave = Wt(7); env(m, 0, 60, 100, 40); m.mono = true; m.glide = 4; }},
    {InstrType::Chip, "LEAD", "C64 LEAD", [](I& m) {  // SID pulse & saw, slow PWM
       m.wave = W(Wave::SidPs); m.duty = 50; m.pwmRate = 20; m.pwmDepth = 15; env(m, 0, 60, 90, 40);
       m.mono = true; }},
    {InstrType::Chip, "LEAD", "ZX ARP", [](I& m) {  // square with AY 16-step volume
       m.wave = W(Wave::Pulse); m.duty = 50; m.chipStep = true; env(m, 0, 60, 40, 40); }},
    {InstrType::Chip, "LEAD", "AY TN", [](I& m) {  // AY tone AND noise on one channel
       m.wave = W(Wave::AyToneNoise); env(m, 0, 80, 80, 40); }},
    // Softer leads: rounded tops (LP), gentle vibrato, delay / reverb space.
    {InstrType::Chip, "LEAD", "SOFT SQ", [](I& m) {  // 50 % square through a gentle LP, late-ish vibrato
       m.wave = W(Wave::Pulse); m.duty = 50; env(m, 10, 70, 100, 60); lp(m, 88, 10, 10, 70); m.keytrack = 64;
       m.mono = true; m.glide = 6; lfo1(m, LfoWave::Sine, LfoDest::Pitch, 70, 2); m.vol = 90; fx(m, 35, 30); }},
    {InstrType::Chip, "LEAD", "OUTRUN", [](I& m) {  // SID pulse & saw, slow PWM, glide, wide delay
       m.wave = W(Wave::SidPs); m.duty = 45; m.pwmRate = 18; m.pwmDepth = 12; env(m, 5, 70, 100, 60);
       lp(m, 92, 20, 12, 70); m.keytrack = 64; m.mono = true; m.glide = 10; m.vol = 85; fx(m, 45, 35); }},
    {InstrType::Chip, "LEAD", "WARM PWM", [](I& m) {  // PWM pulse, small filter blip on attack
       m.wave = W(Wave::Pulse); m.duty = 35; m.pwmRate = 15; m.pwmDepth = 20; env(m, 20, 70, 100, 60);
       lp(m, 84, 15, 15, 70); m.keytrack = 64; m.mono = true; m.glide = 5; m.vol = 90; fx(m, 30, 30); }},
    {InstrType::Chip, "LEAD", "SOFT SIN", [](I& m) {  // sine + 2nd, slow swell, ocarina-like
       m.wave = Wt(1); env(m, 35, 70, 110, 60); m.mono = true; m.glide = 4;
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 72, 2); fx(m, 25, 50); }},
    {InstrType::Chip, "LEAD", "HOLLOW", [](I& m) {  // soft square table (1-3-5), woody and round
       m.wave = Wt(14); env(m, 5, 70, 105, 55); lp(m, 95, 5, 0, 40); m.mono = true; m.glide = 4; fx(m, 30, 35); }},
    {InstrType::Chip, "LEAD", "DARKLEAD", [](I& m) {  // low-passed saw, filter swell, darksynth
       m.wave = W(Wave::Saw); env(m, 5, 75, 100, 60); lp(m, 70, 30, 25, 75); m.keytrack = 64;
       m.mono = true; m.glide = 8; m.vol = 80; fx(m, 35, 45); }},
    {InstrType::Chip, "BASS", "TRI BASS", [](I& m) {
       m.wave = W(Wave::Triangle); m.transpose = -12; m.decay = 60; m.sustain = 110; m.release = 30;
       m.mono = true; }},
    {InstrType::Chip, "BASS", "ACID", [](I& m) {
       m.wave = W(Wave::Saw); m.transpose = -12; m.decay = 70; m.sustain = 90; m.release = 30;
       m.mono = true; m.glide = 15; m.fltMode = F(FltMode::Lp); m.cutoff = 38; m.reso = 105;
       m.fenv = 48; m.fDec = 68; m.keytrack = 64; m.vol = 55; }},  // resonance is loud
    {InstrType::Chip, "BASS", "SQ BASS", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 40; m.transpose = -12; env(m, 0, 65, 80, 30); m.mono = true;
       lp(m, 70, 20, 25, 55); }},
    {InstrType::Chip, "BASS", "C64 BASS", [](I& m) {  // SID pulse & tri through the SID filter
       m.wave = W(Wave::SidPt); m.duty = 50; env(m, 0, 50, 60, 30); m.mono = true;
       m.fltMode = F(FltMode::Sid); m.cutoff = 60; m.reso = 90; }},
    {InstrType::Chip, "BASS", "ZX BUZZ", [](I& m) {  // AY envelope as a saw oscillator
       m.wave = W(Wave::AySaw); m.transpose = -12; env(m, 0, 70, 100, 30); m.mono = true; }},
    {InstrType::Chip, "BASS", "PLK BASS", [](I& m) {  // short and round
       m.wave = W(Wave::Saw); m.transpose = -12; env(m, 0, 70, 0, 50); lp(m, 45, 40, 50, 55); m.mono = true; }},
    {InstrType::Chip, "BASS", "SUB SINE", [](I& m) {
       m.wave = Wt(0); m.transpose = -12; env(m, 0, 60, 120, 35); m.mono = true; m.glide = 6; }},
    {InstrType::Chip, "BASS", "GRIT BAS", [](I& m) {  // driven, crushed saw
       m.wave = W(Wave::Saw); m.transpose = -12; env(m, 0, 65, 100, 30); m.mono = true;
       m.drive = 40; m.crushBits = 70; lp(m, 75, 15, 0, 40); m.vol = 45; }},
    {InstrType::Chip, "BASS", "ROUND SB", [](I& m) {  // sine + 2nd: sub with a little body
       m.wave = Wt(1); m.transpose = -12; env(m, 0, 60, 115, 35); m.mono = true; m.glide = 4; }},
    {InstrType::Chip, "BASS", "OUTBASS", [](I& m) {  // synthwave 16ths: plucky LP saw
       m.wave = W(Wave::Saw); m.transpose = -12; env(m, 0, 65, 85, 30); lp(m, 60, 25, 30, 62);
       m.keytrack = 64; m.mono = true; m.glide = 4; m.vol = 90; }},
    {InstrType::Chip, "BASS", "SOFTACID", [](I& m) {  // tamer 303: moderate resonance, rounded
       m.wave = W(Wave::Saw); m.transpose = -12; env(m, 0, 70, 90, 30); lp(m, 50, 80, 40, 66);
       m.keytrack = 64; m.mono = true; m.glide = 12; m.vol = 65; }},
    {InstrType::Chip, "BASS", "PWM BASS", [](I& m) {  // slow PWM, warm low pass
       m.wave = W(Wave::Pulse); m.duty = 30; m.pwmRate = 25; m.pwmDepth = 15; m.transpose = -12;
       env(m, 0, 65, 95, 30); lp(m, 65, 15, 20, 60); m.keytrack = 64; m.mono = true; m.vol = 90; }},
    {InstrType::Chip, "BASS", "GB BASS", [](I& m) {  // 4-bit sine, Game Boy wave channel
       m.wave = Wt(7); m.transpose = -12; env(m, 0, 62, 100, 30); m.mono = true; m.glide = 3; }},
    {InstrType::Chip, "PAD", "PWM PAD", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 50; m.pwmRate = 20; m.pwmDepth = 35;
       m.attack = 75; m.decay = 80; m.sustain = 100; m.release = 85;
       m.fltMode = F(FltMode::Lp); m.cutoff = 85; m.reso = 20; }},
    {InstrType::Chip, "PAD", "WT PAD", [](I& m) {  // vocal A/O
       m.wave = Wt(6); m.attack = 80; m.decay = 90; m.sustain = 100; m.release = 90;
       m.lfoDest = D(LfoDest::Cutoff); m.lfoRate = 25; m.lfoDepth = 20;
       m.fltMode = F(FltMode::Lp); m.cutoff = 80; }},
    {InstrType::Chip, "PAD", "GLASSPAD", [](I& m) {  // FM-ish table, slow filter drift
       m.wave = Wt(10); env(m, 70, 90, 100, 95); lp(m, 85, 30, 0, 40);
       lfo1(m, LfoWave::Tri, LfoDest::Cutoff, 35, 18); fx(m, 30, 70); }},
    {InstrType::Chip, "PAD", "ORGAN", [](I& m) {  // harmonics 1 + 3 + 5
       m.wave = Wt(14); env(m, 20, 60, 120, 50); lfo1(m, LfoWave::Sine, LfoDest::Vol, 85, 6); fx(m, 0, 40); }},
    {InstrType::Chip, "PAD", "STRINGS", [](I& m) {  // saw-like table, slow vibrato
       m.wave = Wt(8); env(m, 75, 80, 110, 85); lp(m, 90, 10, 0, 40);
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 60, 1); fx(m, 20, 60); }},
    {InstrType::Chip, "PAD", "SOFT PAD", [](I& m) {  // soft saw table, dark, slow filter breath
       m.wave = Wt(8); env(m, 80, 90, 100, 95); lp(m, 78, 10, 0, 40);
       lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 20, 12); m.vol = 85; fx(m, 30, 80); }},
    {InstrType::Chip, "PAD", "DRIFTPAD", [](I& m) {  // chillwave: PWM with a lazy pitch wobble (tape)
       m.wave = W(Wave::Pulse); m.duty = 50; m.pwmRate = 12; m.pwmDepth = 30; env(m, 85, 90, 105, 100);
       lp(m, 75, 10, 0, 40); lfo1(m, LfoWave::Sine, LfoDest::Pitch, 20, 1); m.vol = 85; fx(m, 30, 90); }},
    {InstrType::Chip, "PAD", "CHOIR", [](I& m) {  // vocal table, slow swell, gentle tremolo
       m.wave = Wt(6); env(m, 85, 90, 110, 95); lp(m, 88, 5, 0, 40);
       lfo1(m, LfoWave::Sine, LfoDest::Vol, 40, 5); m.vol = 90; fx(m, 20, 90); }},
    {InstrType::Chip, "PAD", "DARKPAD", [](I& m) {  // low-passed saw, slow sweep: darksynth / dark ambient
       m.wave = W(Wave::Saw); env(m, 90, 95, 100, 100); lp(m, 62, 25, 0, 40);
       lfo1(m, LfoWave::Tri, LfoDest::Cutoff, 15, 15); m.vol = 80; fx(m, 25, 90); }},
    {InstrType::Chip, "KEYS", "WT BELL", [](I& m) {  // bell 1 + 2.76
       m.wave = Wt(13); m.decay = 95; m.sustain = 0; m.release = 80; }},
    {InstrType::Chip, "KEYS", "CHIPKEYS", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 30; env(m, 0, 80, 40, 60); lp(m, 90, 10, 20, 70); fx(m, 30, 30); }},
    {InstrType::Chip, "KEYS", "MARIMBA", [](I& m) {
       m.wave = Wt(2); env(m, 0, 78, 0, 70); fx(m, 0, 40); }},
    {InstrType::Chip, "KEYS", "MUSICBOX", [](I& m) {
       m.wave = Wt(0); m.transpose = 12; env(m, 0, 90, 0, 85); fx(m, 40, 60); }},
    {InstrType::Chip, "KEYS", "CLAV", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 15; env(m, 0, 65, 0, 40); bp(m, 92, 40); m.vol = 90; }},
    {InstrType::Chip, "KEYS", "EP SOFT", [](I& m) {  // sine + 2nd, tremolo: lofi / chillwave keys
       m.wave = Wt(1); env(m, 0, 85, 30, 70); lfo1(m, LfoWave::Sine, LfoDest::Vol, 70, 5); m.vol = 95; fx(m, 20, 50); }},
    {InstrType::Chip, "KEYS", "VIBES", [](I& m) {  // sine + 3rd, motor tremolo, long decay
       m.wave = Wt(2); env(m, 0, 88, 0, 80); lfo1(m, LfoWave::Sine, LfoDest::Vol, 75, 8); fx(m, 25, 60); }},
    {InstrType::Chip, "KEYS", "CHIPSTAB", [](I& m) {  // short square chord stab, house / garage
       m.wave = W(Wave::Pulse); m.duty = 50; env(m, 0, 68, 0, 50); lp(m, 88, 15, 20, 60); m.keytrack = 64;
       m.vol = 85; fx(m, 40, 40); }},
    {InstrType::Chip, "PERC", "NOIS HH", [](I& m) {
       m.wave = W(Wave::Noise); m.transpose = 24; m.decay = 50; m.sustain = 0; m.release = 40;
       m.fltMode = F(FltMode::Hp); m.cutoff = 108; }},
    {InstrType::Chip, "PERC", "NOIS SN", [](I& m) {
       m.wave = W(Wave::Noise); m.transpose = 12; m.decay = 65; m.sustain = 0; m.release = 40;
       m.fltMode = F(FltMode::Bp); m.cutoff = 90; m.reso = 30; m.fenv = 20; m.fDec = 50; }},
    {InstrType::Chip, "PERC", "METALBL", [](I& m) {
       m.wave = W(Wave::Metal); m.decay = 85; m.sustain = 0; m.release = 60;
       m.fltMode = F(FltMode::Bp); m.cutoff = 95; m.reso = 60; m.vol = 55; }},
    {InstrType::Chip, "PERC", "NOIS OH", [](I& m) {
       m.wave = W(Wave::Noise); m.transpose = 24; env(m, 0, 80, 0, 70); hp(m, 110, 10); }},
    {InstrType::Chip, "PERC", "SID NZ", [](I& m) {
       m.wave = W(Wave::SidNoise); env(m, 0, 30, 0, 20); }},
    {InstrType::Chip, "PERC", "AY HAT", [](I& m) {  // AY noise, stepped decay
       m.wave = W(Wave::AyNoise); m.chipStep = true; env(m, 0, 25, 0, 15); }},
    {InstrType::Chip, "PERC", "CHIPKICK", [](I& m) {  // triangle with a fast pitch drop (LFO saw down)
       m.wave = W(Wave::Triangle); m.transpose = -24; env(m, 0, 62, 0, 45);
       lfo1(m, LfoWave::Saw, LfoDest::Pitch, 100, -60); }},  // ~8 Hz: one fast drop per hit
    {InstrType::Chip, "PERC", "BLIP", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 25; m.transpose = 12; env(m, 0, 48, 0, 30); }},
    {InstrType::Chip, "FX", "LASER", [](I& m) {  // pitch dives
       m.wave = W(Wave::Saw); m.transpose = 12; env(m, 0, 75, 0, 60);
       lfo1(m, LfoWave::Saw, LfoDest::Pitch, 82, -60); fx(m, 60, 20); }},
    {InstrType::Chip, "FX", "SIREN", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 50; env(m, 0, 60, 110, 60); lfo1(m, LfoWave::Tri, LfoDest::Pitch, 60, 30); }},
    {InstrType::Chip, "FX", "WIND", [](I& m) {
       m.wave = W(Wave::Noise); env(m, 70, 80, 110, 90); bp(m, 75, 70);
       lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 30, 35); m.vol = 70; fx(m, 0, 60); }},
    {InstrType::Chip, "FX", "COIN", [](I& m) {
       m.wave = W(Wave::Pulse); m.duty = 50; m.transpose = 24; env(m, 0, 70, 0, 50);
       lfo1(m, LfoWave::Square, LfoDest::Pitch, 95, 7); }},
    // ARP: short plucks for 16th arpeggios; delay does the rest.
    {InstrType::Chip, "ARP", "SOFT ARP", [](I& m) {  // 25 % pulse, filter pluck
       m.wave = W(Wave::Pulse); m.duty = 25; env(m, 0, 72, 0, 60); lp(m, 85, 20, 25, 65); m.keytrack = 64;
       m.vol = 90; fx(m, 50, 30); }},
    {InstrType::Chip, "ARP", "GLASSARP", [](I& m) {  // FM k=2 table, glassy
       m.wave = Wt(10); env(m, 0, 75, 0, 70); lp(m, 100, 5, 0, 40); m.vol = 85; fx(m, 45, 40); }},
    {InstrType::Chip, "ARP", "BELL ARP", [](I& m) {  // bell table, ringing
       m.wave = Wt(13); env(m, 0, 82, 0, 75); lp(m, 100, 0, 0, 40); m.vol = 90; fx(m, 40, 50); }},
    {InstrType::Chip, "ARP", "4BIT ARP", [](I& m) {  // stepped sine with AY volume steps: glitchy, soft
       m.wave = Wt(7); m.chipStep = true; env(m, 0, 68, 0, 55); fx(m, 40, 30); }},
    {InstrType::Chip, "ARP", "SID ARP", [](I& m) {  // SID pulse & tri, short sustain
       m.wave = W(Wave::SidPt); m.duty = 40; env(m, 0, 70, 30, 50); lp(m, 90, 20, 15, 60); m.vol = 90; fx(m, 40, 30); }},
    {InstrType::Chip, "ARP", "PLINK", [](I& m) {  // high triangle blip: braindance / IDM
       m.wave = W(Wave::Triangle); m.transpose = 12; env(m, 0, 66, 0, 50); fx(m, 40, 40); }},
    {InstrType::Chip, "ARP", "RND BLIP", [](I& m) {  // random pitch steps per hit: glitch
       m.wave = W(Wave::Pulse); m.duty = 25; env(m, 0, 55, 0, 40); lp(m, 92, 10, 0, 40);
       lfo1(m, LfoWave::Random, LfoDest::Pitch, 95, 10); m.vol = 85; fx(m, 35, 30); }},
};

}  // namespace

const FactoryTable kFactoryChip = {kTable, static_cast<int>(sizeof(kTable) / sizeof(kTable[0]))};

}  // namespace mt
