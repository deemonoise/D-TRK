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

// Helpers for the library below.
void env(I& m, uint8_t a, uint8_t d, uint8_t s, uint8_t r) { m.attack = a; m.decay = d; m.sustain = s; m.release = r; }
void hp(I& m, uint8_t cut, uint8_t res) { m.fltMode = F(FltMode::Hp); m.cutoff = cut; m.reso = res; }
void bp(I& m, uint8_t cut, uint8_t res) { m.fltMode = F(FltMode::Bp); m.cutoff = cut; m.reso = res; }
void lfo1(I& m, LfoWave w, LfoDest d, uint8_t rate, int8_t depth) {
  m.lfoWave = LW(w); m.lfoDest = D(d); m.lfoRate = rate; m.lfoDepth = depth;
}
void lfoN(I& m, int i, LfoWave w, LfoDest d, uint8_t rate, int8_t depth) {  // LFO 2..4 (i = 1..3)
  LfoCfg& l = m.lfo[i - 1];
  l.wave = LW(w); l.dest = D(d); l.rate = rate; l.depth = depth; l.sync = 0;
}
void fx(I& m, uint8_t send, uint8_t rsend) { m.send = send; m.rsend = rsend; }

constexpr FactoryPreset kAll[] = {
    // ================= CHIP =================
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
    {InstrType::Chip, "BASS", "PLK BASS", [](I& m) {  // short and round
       m.wave = W(Wave::Saw); m.transpose = -12; env(m, 0, 70, 0, 50); lp(m, 45, 40, 50, 55); m.mono = true; }},
    {InstrType::Chip, "BASS", "SUB SINE", [](I& m) {
       m.wave = Wt(0); m.transpose = -12; env(m, 0, 60, 120, 35); m.mono = true; m.glide = 6; }},
    {InstrType::Chip, "BASS", "GRIT BAS", [](I& m) {  // driven, crushed saw
       m.wave = W(Wave::Saw); m.transpose = -12; env(m, 0, 65, 100, 30); m.mono = true;
       m.drive = 40; m.crushBits = 70; lp(m, 75, 15, 0, 40); m.vol = 45; }},
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

    // ================= FM =================
    {InstrType::Fm, "DRUMS", "KICK", [](I& m) { fm(m, FmMachine::Kick); }},
    {InstrType::Fm, "DRUMS", "KICK SUB", [](I& m) { fm(m, FmMachine::Kick); mac(m, 105, 20, 20, 50, 70); }},
    {InstrType::Fm, "DRUMS", "KICK PNC", [](I& m) { fm(m, FmMachine::Kick); mac(m, 70, 70, 90, 90, 25); m.drive = 40; m.vol = 60; }},
    {InstrType::Fm, "DRUMS", "SNARE", [](I& m) { fm(m, FmMachine::Snare); }},
    {InstrType::Fm, "DRUMS", "SNR TGHT", [](I& m) { fm(m, FmMachine::Snare); mac(m, 50, 80, 60, 40, 50); }},
    {InstrType::Fm, "DRUMS", "SNR FAT", [](I& m) { fm(m, FmMachine::Snare); mac(m, 85, 50, 30, 20, 70); fx(m, 0, 40); }},
    {InstrType::Fm, "DRUMS", "CLAP", [](I& m) { fm(m, FmMachine::Clap); }},
    {InstrType::Fm, "DRUMS", "CLAP BIG", [](I& m) { fm(m, FmMachine::Clap); mac(m, 85, 60, 90, 50, 64); fx(m, 0, 60); }},
    {InstrType::Fm, "DRUMS", "HAT C", [](I& m) { fm(m, FmMachine::Hat); }},
    {InstrType::Fm, "DRUMS", "HAT O", [](I& m) { fm(m, FmMachine::Hat); m.macro[kMacDec] = 90; }},
    {InstrType::Fm, "DRUMS", "RIDE", [](I& m) { fm(m, FmMachine::Metal); mac(m, 105, 50, 110, 20, 64); m.vol = 80; }},
    {InstrType::Fm, "DRUMS", "WOODBLK", [](I& m) { fm(m, FmMachine::Perc); mac(m, 45, 90, 80, 20, 30); }},
    {InstrType::Fm, "PERC", "TOM", [](I& m) { fm(m, FmMachine::Perc); mac(m, 75, 30, 40, 60, 50); m.transpose = -5; }},
    {InstrType::Fm, "PERC", "CONGA", [](I& m) { fm(m, FmMachine::Perc); mac(m, 60, 50, 60, 35, 30); }},
    {InstrType::Fm, "PERC", "COWBELL", [](I& m) { fm(m, FmMachine::Metal); mac(m, 70, 64, 45, 0, 64); }},
    {InstrType::Fm, "PERC", "GONG", [](I& m) { fm(m, FmMachine::Metal); mac(m, 120, 60, 75, 10, 64); fx(m, 0, 70); }},
    {InstrType::Fm, "PERC", "ZAP", [](I& m) { fm(m, FmMachine::Kick); mac(m, 50, 90, 60, 127, 20); m.transpose = 12; }},
    {InstrType::Fm, "KEYS", "BELL", [](I& m) { fm(m, FmMachine::Metal); mac(m, 100, 60, 0, 0, 64); }},
    {InstrType::Fm, "KEYS", "E.PIANO", [](I& m) {  // sine + decaying index: tine attack
       fm(m, FmMachine::Tone); mac(m, 70, 50, 0, 60, 40);
       m.decay = 95; m.sustain = 40; m.release = 60; }},
    {InstrType::Fm, "KEYS", "EP SOFT", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 25, 0, 35, 50); env(m, 0, 95, 30, 65);
       lfo1(m, LfoWave::Sine, LfoDest::Vol, 70, 6); fx(m, 20, 40); }},
    {InstrType::Fm, "KEYS", "DX BELL", [](I& m) {  // bell zone, pluck index
       fm(m, FmMachine::Tone); mac(m, 64, 30, 115, 70, 70); env(m, 0, 100, 0, 85); fx(m, 30, 60); }},
    {InstrType::Fm, "KEYS", "GLOCK", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 20, 115, 40, 30); m.transpose = 12; env(m, 0, 88, 0, 75); fx(m, 0, 50); }},
    {InstrType::Fm, "KEYS", "MARIMBA", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 10, 0, 80, 15); env(m, 0, 78, 0, 60); }},
    {InstrType::Fm, "KEYS", "ORGAN", [](I& m) {  // stack zone, held
       fm(m, FmMachine::Tone); mac(m, 64, 60, 90, 0, 64); env(m, 10, 60, 120, 45);
       lfo1(m, LfoWave::Sine, LfoDest::Vol, 88, 8); }},
    {InstrType::Fm, "KEYS", "CHORD M7", [](I& m) {  // SHAPE 68: MAJ7 (fmChordName)
       fm(m, FmMachine::Chord); m.macro[kMacShp] = 68; m.attack = 10; m.release = 70; }},
    {InstrType::Fm, "KEYS", "CHRD MIN", [](I& m) {  // MIN
       fm(m, FmMachine::Chord); mac(m, 64, 40, 16, 20, 50); env(m, 10, 80, 60, 70); fx(m, 30, 50); }},
    {InstrType::Fm, "KEYS", "CHRD MI9", [](I& m) {  // MI9: deep house stab
       fm(m, FmMachine::Chord); mac(m, 64, 45, 122, 30, 40); env(m, 0, 72, 0, 60); fx(m, 50, 50); }},
    {InstrType::Fm, "KEYS", "CHRD 5TH", [](I& m) {  // power fifths
       fm(m, FmMachine::Chord); mac(m, 64, 70, 100, 10, 64); env(m, 0, 80, 90, 50); m.drive = 25; m.vol = 55; }},
    {InstrType::Fm, "BASS", "FM BASS", [](I& m) {  // SHAPE 40: feedback saw
       fm(m, FmMachine::Tone); mac(m, 60, 70, 40, 50, 30); m.transpose = -12; m.mono = true;
       m.fltMode = F(FltMode::Lp); m.cutoff = 70; m.fenv = 20; }},
    {InstrType::Fm, "BASS", "DX BASS", [](I& m) {  // square zone, plucked index
       fm(m, FmMachine::Tone); mac(m, 64, 30, 60, 90, 25); m.transpose = -12; env(m, 0, 70, 60, 35); m.mono = true; }},
    {InstrType::Fm, "BASS", "SUB FM", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 10, 0, 30, 30); m.transpose = -24; env(m, 0, 70, 110, 30); m.mono = true; }},
    {InstrType::Fm, "BASS", "WOBBLE", [](I& m) {  // LFO on the index (COLOR)
       fm(m, FmMachine::Tone); mac(m, 64, 60, 40, 0, 64); m.transpose = -12; env(m, 0, 60, 120, 30); m.mono = true;
       lfo1(m, LfoWave::Sine, LfoDest::Col, 80, 45); m.lfoSync = 0; }},
    {InstrType::Fm, "LEAD", "FM LEAD", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 80, 40, 30, 50); env(m, 0, 70, 110, 45); m.mono = true; m.glide = 6;
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 80, 2); fx(m, 40, 30); }},
    {InstrType::Fm, "LEAD", "SQ LEAD", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 70, 60, 0, 64); env(m, 0, 70, 100, 40); m.mono = true; fx(m, 30, 20); }},
    {InstrType::Fm, "LEAD", "BRASS", [](I& m) {  // saw zone, index swells in
       fm(m, FmMachine::Tone); mac(m, 64, 50, 35, 60, 90); env(m, 50, 70, 100, 45); lp(m, 95, 10, 15, 70); }},
    {InstrType::Fm, "PAD", "FM PAD", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 35, 90, 0, 64); env(m, 75, 90, 110, 90);
       lfo1(m, LfoWave::Tri, LfoDest::Col, 30, 25); fx(m, 30, 70); }},
    {InstrType::Fm, "PAD", "CHRD PAD", [](I& m) {  // MAJ, slow
       fm(m, FmMachine::Chord); mac(m, 64, 25, 5, 0, 64); env(m, 75, 90, 110, 95); fx(m, 30, 80); }},

    // ================= DRUM =================
    {InstrType::Drum, "808", "BD808", [](I& m) { dr(m, DrumMachine::Bd8); }},
    {InstrType::Drum, "808", "BD808 L", [](I& m) {  // long boom, driven, low-passed
       dr(m, DrumMachine::Bd8); mac(m, 115, 10, 40, 30, 60);
       m.fltMode = F(FltMode::Lp); m.cutoff = 95; }},
    {InstrType::Drum, "808", "BD808 S", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 60, 30, 10, 50, 40); }},
    {InstrType::Drum, "808", "BD808 D", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 100, 40, 30, 50, 50); m.drive = 45; m.vol = 50; }},
    {InstrType::Drum, "808", "SD808", [](I& m) { dr(m, DrumMachine::Sd8); }},
    {InstrType::Drum, "808", "SD808 S", [](I& m) { dr(m, DrumMachine::Sd8); mac(m, 50, 70, 110, 30, 64); }},
    {InstrType::Drum, "808", "TOM808", [](I& m) { dr(m, DrumMachine::Tom8); }},
    {InstrType::Drum, "808", "TOM808 L", [](I& m) { dr(m, DrumMachine::Tom8); m.transpose = -7; }},
    {InstrType::Drum, "808", "CH808", [](I& m) { dr(m, DrumMachine::Hh8); }},
    {InstrType::Drum, "808", "OH808", [](I& m) { dr(m, DrumMachine::Hh8); m.macro[kMacDec] = 85; }},
    {InstrType::Drum, "808", "CY808", [](I& m) { dr(m, DrumMachine::Cy8); }},
    {InstrType::Drum, "808", "CP808", [](I& m) { dr(m, DrumMachine::Cp8); }},
    {InstrType::Drum, "808", "RS808", [](I& m) { dr(m, DrumMachine::Rs8); }},
    {InstrType::Drum, "808", "CL808", [](I& m) { dr(m, DrumMachine::Cl8); }},
    {InstrType::Drum, "808", "CB808", [](I& m) { dr(m, DrumMachine::Cb8); }},
    {InstrType::Drum, "909", "BD909", [](I& m) {
       dr(m, DrumMachine::Bd9); m.fltMode = F(FltMode::Lp); m.cutoff = 112; m.reso = 20; }},
    {InstrType::Drum, "909", "BD909 H", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 85, 90, 60, 80, 30); m.drive = 30; m.vol = 55; }},
    {InstrType::Drum, "909", "BD909 S", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 50, 60, 40, 60, 40); }},
    {InstrType::Drum, "909", "SD909", [](I& m) { dr(m, DrumMachine::Sd9); }},
    {InstrType::Drum, "909", "SD909 T", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 40, 80, 100, 40, 64); }},
    {InstrType::Drum, "909", "TOM909", [](I& m) { dr(m, DrumMachine::Tom9); }},
    {InstrType::Drum, "909", "TOM909 H", [](I& m) { dr(m, DrumMachine::Tom9); m.transpose = 7; }},
    {InstrType::Drum, "909", "CH909", [](I& m) { dr(m, DrumMachine::Hh9); }},
    {InstrType::Drum, "909", "OH909", [](I& m) {
       dr(m, DrumMachine::Hh9); m.macro[kMacDec] = 85; m.fltMode = F(FltMode::Hp); m.cutoff = 90; }},
    {InstrType::Drum, "909", "CY909", [](I& m) { dr(m, DrumMachine::Cy9); }},
    {InstrType::Drum, "909", "CP909", [](I& m) { dr(m, DrumMachine::Cp9); }},
    {InstrType::Drum, "909", "RS909", [](I& m) { dr(m, DrumMachine::Rs9); }},
    {InstrType::Drum, "LOFI", "BD DUSTY", [](I& m) {
       dr(m, DrumMachine::Bd9); mac(m, 70, 40, 30, 50, 40); m.crushBits = 85; m.crushRate = 30; lp(m, 90, 0, 0, 40); }},
    {InstrType::Drum, "LOFI", "SD DUSTY", [](I& m) {
       dr(m, DrumMachine::Sd8); mac(m, 50, 60, 80, 30, 64); m.crushBits = 90; m.crushRate = 40; lp(m, 95, 10, 0, 40); }},
    {InstrType::Drum, "LOFI", "HH DUSTY", [](I& m) { dr(m, DrumMachine::Hh8); m.crushBits = 80; m.crushRate = 50; }},
    {InstrType::Drum, "LOFI", "CP DUSTY", [](I& m) { dr(m, DrumMachine::Cp8); m.crushRate = 60; lp(m, 100, 0, 0, 40); }},
    {InstrType::Drum, "LOFI", "RIM BIT", [](I& m) { dr(m, DrumMachine::Rs9); m.crushBits = 110; }},
    {InstrType::Drum, "HARD", "BD DIST", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 80, 100, 90, 90, 30); m.drive = 60; m.vol = 45; }},
    {InstrType::Drum, "HARD", "BD RUMBL", [](I& m) {  // long tail through a low-pass, for techno
       dr(m, DrumMachine::Bd8); mac(m, 120, 30, 50, 40, 70); m.drive = 35; lp(m, 60, 30, 0, 40); fx(m, 0, 80); m.vol = 40; }},
    {InstrType::Drum, "HARD", "SD NOISE", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 70, 100, 127, 20, 64); m.drive = 60; }},
    {InstrType::Drum, "HARD", "CLAP VRB", [](I& m) { dr(m, DrumMachine::Cp9); mac(m, 80, 70, 64, 50, 50); fx(m, 0, 100); }},
    {InstrType::Drum, "HARD", "HH HARSH", [](I& m) { dr(m, DrumMachine::Hh9); mac(m, 25, 110, 70, 64, 64); m.drive = 60; }},
    {InstrType::Drum, "HARD", "METAL HT", [](I& m) { dr(m, DrumMachine::Cb8); mac(m, 60, 90, 100, 60, 0); bp(m, 100, 50); }},

    // ================= SYNTH =================
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
       lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 4, 40); m.lfoSync = 1; }},
    {InstrType::Synth, "BASS", "FM GROWL", [](I& m) {  // sync sweep + drive
       sy(m, SynOsc::Saw, nullptr, SynOsc::Saw, nullptr); m.synSync = true; m.synSemi = 12; m.macro[kMacMix] = 80;
       m.transpose = -12; m.macro[kMacSenv] = 100; m.synEDec = 55; m.drive = 45; lp(m, 80, 30, 0, 40); m.mono = true; m.vol = 30; }},
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
