// Factory FM presets (see presets_factory.h: how to add one).
#include "presets_factory_fill.h"

namespace mt {
namespace {
using namespace pf;

// FM SAT and the volume that keeps the preset as loud as the rest with it.
inline void sv(I& m, uint8_t sat, uint8_t vol) { m.fmSat = sat; m.vol = vol; }

constexpr FactoryPreset kTable[] = {
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
    // Kicks by style. SAT (not the track insert) so they keep their weight behind a KIT lane.
    {InstrType::Fm, "DRUMS", "KICK DEP", [](I& m) { fm(m, FmMachine::Kick); mac(m, 90, 25, 30, 55, 55); sv(m, 20, 95); }},
    {InstrType::Fm, "DRUMS", "KICK TCH", [](I& m) { fm(m, FmMachine::Kick); mac(m, 80, 50, 40, 70, 40); sv(m, 35, 88); }},
    {InstrType::Fm, "DRUMS", "KICK 909", [](I& m) { fm(m, FmMachine::Kick); mac(m, 75, 60, 45, 80, 35); sv(m, 30, 88); }},
    {InstrType::Fm, "DRUMS", "KICK BOM", [](I& m) { fm(m, FmMachine::Kick); mac(m, 105, 22, 25, 60, 70); sv(m, 15, 95); m.transpose = -2; }},
    {InstrType::Fm, "DRUMS", "KICK TRP", [](I& m) {  // long 808-style sine boom
       fm(m, FmMachine::Kick); mac(m, 118, 8, 20, 45, 80); sv(m, 30, 90); }},
    {InstrType::Fm, "DRUMS", "KICK DNB", [](I& m) { fm(m, FmMachine::Kick); mac(m, 62, 62, 50, 85, 30); sv(m, 25, 90); }},
    {InstrType::Fm, "DRUMS", "KICK PSY", [](I& m) {  // short, clicky, leaves room for the rolling bass
       fm(m, FmMachine::Kick); mac(m, 56, 70, 40, 95, 25); sv(m, 30, 88); }},
    {InstrType::Fm, "DRUMS", "KICK HRD", [](I& m) {  // hard techno: feedback sub, dense
       fm(m, FmMachine::Kick); mac(m, 95, 78, 90, 85, 40); sv(m, 60, 74); }},
    {InstrType::Fm, "DRUMS", "KICK LOF", [](I& m) { fm(m, FmMachine::Kick); mac(m, 70, 15, 20, 45, 60); sv(m, 10, 100); }},
    {InstrType::Fm, "DRUMS", "KICK ELC", [](I& m) { fm(m, FmMachine::Kick); mac(m, 85, 35, 30, 100, 65); sv(m, 20, 92); }},
    {InstrType::Fm, "DRUMS", "KICK MIN", [](I& m) { fm(m, FmMachine::Kick); mac(m, 55, 40, 35, 60, 35); sv(m, 20, 95); }},
    {InstrType::Fm, "DRUMS", "KICK DUB", [](I& m) { fm(m, FmMachine::Kick); mac(m, 100, 18, 30, 50, 60); sv(m, 15, 95); }},
    // Snares.
    {InstrType::Fm, "DRUMS", "SNR HOUS", [](I& m) { fm(m, FmMachine::Snare); mac(m, 60, 60, 70, 25, 55); }},
    {InstrType::Fm, "DRUMS", "SNR DNB", [](I& m) { fm(m, FmMachine::Snare); mac(m, 55, 70, 85, 35, 45); sv(m, 25, 90); }},
    {InstrType::Fm, "DRUMS", "SNR BRK", [](I& m) { fm(m, FmMachine::Snare); mac(m, 65, 55, 60, 30, 60); sv(m, 20, 92); }},
    {InstrType::Fm, "DRUMS", "SNR BOOM", [](I& m) {  // boom bap: low, fat, dark noise
       fm(m, FmMachine::Snare); mac(m, 80, 45, 30, 25, 70); sv(m, 30, 90); fx(m, 0, 20); }},
    {InstrType::Fm, "DRUMS", "SNR LOFI", [](I& m) { fm(m, FmMachine::Snare); mac(m, 60, 50, 20, 20, 55); sv(m, 15, 95); }},
    {InstrType::Fm, "DRUMS", "SNR TRAP", [](I& m) { fm(m, FmMachine::Snare); mac(m, 60, 75, 100, 20, 65); fx(m, 0, 25); }},
    {InstrType::Fm, "DRUMS", "SNR ELEC", [](I& m) { fm(m, FmMachine::Snare); mac(m, 65, 30, 50, 60, 40); }},
    {InstrType::Fm, "DRUMS", "SNR NEUR", [](I& m) {  // neuro / halftime: hard, dense
       fm(m, FmMachine::Snare); mac(m, 70, 60, 90, 45, 55); sv(m, 55, 80); fx(m, 0, 20); }},
    {InstrType::Fm, "DRUMS", "SNR TRNC", [](I& m) { fm(m, FmMachine::Snare); mac(m, 75, 70, 80, 25, 85); fx(m, 0, 40); m.vol = 92; }},
    {InstrType::Fm, "DRUMS", "SNR GHST", [](I& m) { fm(m, FmMachine::Snare); mac(m, 40, 70, 70, 20, 35); m.vol = 70; }},
    // Claps.
    {InstrType::Fm, "DRUMS", "CLAP HSE", [](I& m) { fm(m, FmMachine::Clap); mac(m, 70, 60, 50, 30, 60); fx(m, 0, 30); }},
    {InstrType::Fm, "DRUMS", "CLAP TGT", [](I& m) { fm(m, FmMachine::Clap); mac(m, 50, 70, 30, 30, 40); }},
    {InstrType::Fm, "DRUMS", "CLAP 909", [](I& m) { fm(m, FmMachine::Clap); mac(m, 65, 64, 60, 40, 55); }},
    {InstrType::Fm, "DRUMS", "CLAP WRM", [](I& m) { fm(m, FmMachine::Clap); mac(m, 70, 40, 50, 25, 60); }},
    {InstrType::Fm, "DRUMS", "CLAP TRP", [](I& m) { fm(m, FmMachine::Clap); mac(m, 75, 70, 90, 30, 70); fx(m, 0, 40); m.vol = 92; }},
    {InstrType::Fm, "DRUMS", "CLAP DUB", [](I& m) { fm(m, FmMachine::Clap); mac(m, 90, 50, 60, 30, 80); fx(m, 35, 60); m.vol = 88; }},
    {InstrType::Fm, "DRUMS", "SNAP", [](I& m) { fm(m, FmMachine::Clap); mac(m, 35, 95, 0, 50, 20); m.vol = 90; }},
    // Hats and cymbals.
    {InstrType::Fm, "DRUMS", "HAT TGHT", [](I& m) { fm(m, FmMachine::Hat); mac(m, 25, 80, 90, 64, 50); m.vol = 90; }},
    {InstrType::Fm, "DRUMS", "HAT SOFT", [](I& m) { fm(m, FmMachine::Hat); mac(m, 35, 50, 100, 50, 64); m.vol = 82; }},
    {InstrType::Fm, "DRUMS", "HAT MTL", [](I& m) { fm(m, FmMachine::Hat); mac(m, 40, 70, 40, 90, 64); m.vol = 80; }},
    {InstrType::Fm, "DRUMS", "HAT TRAP", [](I& m) { fm(m, FmMachine::Hat); mac(m, 30, 90, 85, 64, 50); m.vol = 85; }},
    {InstrType::Fm, "DRUMS", "HAT O LG", [](I& m) { fm(m, FmMachine::Hat); mac(m, 100, 70, 95, 64, 70); m.vol = 80; }},
    {InstrType::Fm, "DRUMS", "HAT 909O", [](I& m) { fm(m, FmMachine::Hat); mac(m, 85, 75, 80, 60, 64); m.vol = 82; }},
    {InstrType::Fm, "DRUMS", "HAT DUST", [](I& m) { fm(m, FmMachine::Hat); mac(m, 45, 40, 110, 40, 64); m.vol = 82; }},
    {InstrType::Fm, "DRUMS", "CRASH", [](I& m) { fm(m, FmMachine::Metal); mac(m, 120, 70, 125, 15, 50); m.vol = 68; fx(m, 0, 30); }},
    {InstrType::Fm, "PERC", "TOM", [](I& m) { fm(m, FmMachine::Perc); mac(m, 75, 30, 40, 60, 50); m.transpose = -5; }},
    {InstrType::Fm, "PERC", "CONGA", [](I& m) { fm(m, FmMachine::Perc); mac(m, 60, 50, 60, 35, 30); }},
    {InstrType::Fm, "PERC", "COWBELL", [](I& m) { fm(m, FmMachine::Metal); mac(m, 70, 64, 45, 0, 64); }},
    {InstrType::Fm, "PERC", "GONG", [](I& m) { fm(m, FmMachine::Metal); mac(m, 120, 60, 75, 10, 64); fx(m, 0, 70); }},
    {InstrType::Fm, "PERC", "ZAP", [](I& m) { fm(m, FmMachine::Kick); mac(m, 50, 90, 60, 127, 20); m.transpose = 12; }},
    {InstrType::Fm, "PERC", "TOM LO", [](I& m) { fm(m, FmMachine::Perc); mac(m, 82, 20, 30, 55, 60); m.transpose = -10; }},
    {InstrType::Fm, "PERC", "TOM HI", [](I& m) { fm(m, FmMachine::Perc); mac(m, 70, 25, 30, 55, 50); m.transpose = 3; }},
    {InstrType::Fm, "PERC", "TOM ELEC", [](I& m) {  // synth tom, long sweep (electro, synthwave)
       fm(m, FmMachine::Perc); mac(m, 85, 15, 20, 110, 90); m.transpose = -7; fx(m, 0, 30); }},
    {InstrType::Fm, "PERC", "RIM", [](I& m) { fm(m, FmMachine::Perc); mac(m, 35, 100, 70, 15, 20); m.transpose = 7; m.vol = 85; }},
    {InstrType::Fm, "PERC", "RIM SOFT", [](I& m) { fm(m, FmMachine::Perc); mac(m, 40, 80, 50, 10, 25); m.transpose = 5; m.vol = 80; fx(m, 0, 25); }},
    {InstrType::Fm, "PERC", "SHAKER", [](I& m) { fm(m, FmMachine::Hat); mac(m, 50, 70, 127, 30, 80); m.vol = 72; }},
    {InstrType::Fm, "PERC", "SHKR SFT", [](I& m) { fm(m, FmMachine::Hat); mac(m, 55, 55, 120, 30, 90); m.vol = 66; }},
    {InstrType::Fm, "PERC", "CLAVE", [](I& m) { fm(m, FmMachine::Perc); mac(m, 30, 110, 50, 5, 20); m.transpose = 10; m.vol = 78; }},
    {InstrType::Fm, "PERC", "BONGO", [](I& m) { fm(m, FmMachine::Perc); mac(m, 55, 55, 60, 30, 25); m.transpose = 7; m.vol = 90; }},
    {InstrType::Fm, "PERC", "TABLA", [](I& m) { fm(m, FmMachine::Perc); mac(m, 62, 45, 70, 60, 40); m.vol = 90; fx(m, 0, 20); }},
    {InstrType::Fm, "PERC", "BLIP", [](I& m) {  // glitch / IDM tick
       fm(m, FmMachine::Perc); mac(m, 20, 90, 100, 80, 10); m.transpose = 12; m.vol = 75; fx(m, 30, 0); }},
    {InstrType::Fm, "PERC", "CLICK", [](I& m) { fm(m, FmMachine::Perc); mac(m, 12, 127, 60, 0, 10); m.vol = 70; }},
    {InstrType::Fm, "PERC", "AGOGO", [](I& m) { fm(m, FmMachine::Metal); mac(m, 60, 64, 45, 10, 64); m.transpose = 7; m.vol = 72; }},
    {InstrType::Fm, "PERC", "TRIANGL", [](I& m) { fm(m, FmMachine::Metal); mac(m, 100, 45, 5, 0, 70); m.transpose = 19; m.vol = 60; fx(m, 0, 30); }},
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
    // TONE SHAPE zones: sine 0-25, saw 26-50, square 51-76, stack 77-101, bell 102-127. CHORD SHAPE: 12
    // zones of ~10.6 (MAJ 5 MIN 16 SUS2 26 SUS4 37 7 48 MI7 58 MAJ7 69 DIM 79 AUG 90 5 100 OCT 111 MI9 122).
    // DECAY (5 ms .. 4 s) replaces the ADSR decay: 64 ~ 0.15 s, 80 ~ 0.35 s, 90 ~ 0.6 s, 100 ~ 1 s.
    {InstrType::Fm, "KEYS", "EP DEEP", [](I& m) {  // deep house Rhodes: round, slow tremolo
       fm(m, FmMachine::Tone); mac(m, 92, 28, 10, 50, 45); env(m, 0, 90, 45, 65);
       lfo1(m, LfoWave::Sine, LfoDest::Vol, 60, 8); fx(m, 20, 45); m.vol = 95; }},
    {InstrType::Fm, "KEYS", "EP BELL", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 90, 32, 108, 45, 50); env(m, 0, 90, 20, 70); fx(m, 30, 50); m.vol = 85; }},
    {InstrType::Fm, "KEYS", "EP DX", [](I& m) {  // classic tine EP, a little brighter
       fm(m, FmMachine::Tone); mac(m, 88, 42, 12, 72, 35); env(m, 0, 90, 35, 60); fx(m, 15, 35); m.vol = 92; }},
    {InstrType::Fm, "KEYS", "EP LOFI", [](I& m) {  // dull, tape wow
       fm(m, FmMachine::Tone); mac(m, 90, 22, 8, 40, 45); env(m, 0, 90, 40, 60); lp(m, 82, 0, 0, 60);
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 30, 3); fx(m, 10, 40); }},
    {InstrType::Fm, "KEYS", "VIBES", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 98, 15, 110, 50, 60); env(m, 0, 98, 0, 85);
       lfo1(m, LfoWave::Sine, LfoDest::Vol, 75, 10); fx(m, 20, 60); m.vol = 88; }},
    {InstrType::Fm, "KEYS", "KALIMBA", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 78, 10, 105, 60, 25); env(m, 0, 82, 0, 65); m.transpose = 12; fx(m, 20, 50); m.vol = 88; }},
    {InstrType::Fm, "KEYS", "MALLET", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 74, 10, 15, 70, 25); env(m, 0, 80, 0, 60); fx(m, 15, 40); }},
    {InstrType::Fm, "KEYS", "CELESTA", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 90, 20, 120, 50, 40); env(m, 0, 90, 0, 75); m.transpose = 12; fx(m, 25, 60); m.vol = 80; }},
    {InstrType::Fm, "KEYS", "TUBEBELL", [](I& m) { fm(m, FmMachine::Metal); mac(m, 110, 50, 5, 0, 70); fx(m, 0, 60); m.vol = 78; }},
    {InstrType::Fm, "KEYS", "PLUCK FM", [](I& m) {  // trance / prog pluck
       fm(m, FmMachine::Tone); mac(m, 70, 45, 38, 80, 25); env(m, 0, 75, 0, 55); fx(m, 45, 35); m.vol = 85; }},
    {InstrType::Fm, "KEYS", "PLK SOFT", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 74, 25, 15, 65, 30); env(m, 0, 78, 0, 60); fx(m, 35, 45); m.vol = 95; }},
    {InstrType::Fm, "KEYS", "PLK PSY", [](I& m) {  // goa / psy arp pluck
       fm(m, FmMachine::Tone); mac(m, 62, 50, 62, 85, 20); env(m, 0, 68, 0, 45); fx(m, 45, 20); m.vol = 82; }},
    {InstrType::Fm, "KEYS", "CHRD MI7", [](I& m) {  // dub techno stab, filtered
       fm(m, FmMachine::Chord); mac(m, 72, 35, 58, 25, 35); env(m, 0, 74, 0, 65); lp(m, 80, 10, 20, 65);
       fx(m, 60, 60); m.vol = 88; }},
    {InstrType::Fm, "KEYS", "CHRD DUB", [](I& m) {
       fm(m, FmMachine::Chord); mac(m, 68, 30, 16, 20, 30); env(m, 0, 70, 0, 70); lp(m, 75, 15, 25, 60);
       fx(m, 80, 60); m.vol = 88; }},
    {InstrType::Fm, "KEYS", "CHRD SU4", [](I& m) {
       fm(m, FmMachine::Chord); mac(m, 80, 40, 37, 30, 50); env(m, 0, 80, 50, 70); fx(m, 40, 50); m.vol = 80; }},
    {InstrType::Fm, "KEYS", "CHRD HSE", [](I& m) {  // dominant 7 stab: disco / house
       fm(m, FmMachine::Chord); mac(m, 66, 50, 48, 40, 35); env(m, 0, 70, 0, 55); fx(m, 30, 30); m.vol = 80; }},
    {InstrType::Fm, "KEYS", "ORGAN HS", [](I& m) {  // 90s house organ, short
       fm(m, FmMachine::Tone); mac(m, 72, 55, 88, 40, 30); env(m, 0, 70, 50, 45); fx(m, 25, 30); m.vol = 82; }},
    {InstrType::Fm, "KEYS", "CHRD STB", [](I& m) {  // trance minor stab
       fm(m, FmMachine::Chord); mac(m, 66, 68, 16, 60, 30); env(m, 0, 70, 0, 55); fx(m, 50, 50); m.vol = 75; }},
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
    {InstrType::Fm, "BASS", "DEEP SUB", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 90, 15, 10, 35, 30); m.transpose = -12; env(m, 0, 70, 110, 35);
       lp(m, 70, 0, 10, 60); m.mono = true; }},
    {InstrType::Fm, "BASS", "BASS DEP", [](I& m) {  // deep house: soft pluck on a round body
       fm(m, FmMachine::Tone); mac(m, 82, 30, 12, 60, 35); m.transpose = -12; env(m, 0, 75, 50, 40);
       lp(m, 80, 10, 20, 60); m.mono = true; }},
    {InstrType::Fm, "BASS", "ACID FM", [](I& m) {  // 303-ish: saw zone into a resonant LP, slides
       fm(m, FmMachine::Tone); mac(m, 64, 60, 40, 50, 25); m.transpose = -12; env(m, 0, 70, 90, 30);
       lp(m, 70, 75, 50, 55); m.mono = true; m.glide = 8; sv(m, 20, 75); }},
    {InstrType::Fm, "BASS", "TECH BAS", [](I& m) {  // rolling techno bass, short
       fm(m, FmMachine::Tone); mac(m, 62, 40, 60, 70, 20); m.transpose = -12; env(m, 0, 62, 0, 35);
       lp(m, 85, 15, 25, 50); m.mono = true; m.vol = 90; }},
    {InstrType::Fm, "BASS", "DISCO BS", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 74, 45, 38, 60, 30); m.transpose = -12; env(m, 0, 72, 40, 40);
       lp(m, 88, 10, 20, 55); m.mono = true; m.vol = 88; }},
    {InstrType::Fm, "BASS", "PSY BASS", [](I& m) {  // full-on rolling 16ths: tight, plucked
       fm(m, FmMachine::Tone); mac(m, 54, 50, 40, 75, 15); m.transpose = -12; env(m, 0, 58, 0, 30);
       lp(m, 80, 20, 35, 45); m.mono = true; m.vol = 85; }},
    {InstrType::Fm, "BASS", "GOA BASS", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 58, 55, 62, 70, 20); m.transpose = -12; env(m, 0, 62, 0, 32);
       lp(m, 82, 20, 30, 50); m.mono = true; m.vol = 85; }},
    {InstrType::Fm, "BASS", "REESE FM", [](I& m) {  // liquid dnb: wide detune, slow drift
       fm(m, FmMachine::Tone); mac(m, 64, 85, 38, 0, 64); m.transpose = -12; env(m, 0, 70, 120, 35);
       lp(m, 85, 15, 0, 0); lfo1(m, LfoWave::Tri, LfoDest::Col, 25, 15); m.mono = true; m.vol = 80; }},
    {InstrType::Fm, "BASS", "NEURO", [](I& m) {  // aggressive: moving index, saturated
       fm(m, FmMachine::Tone); mac(m, 64, 90, 45, 30, 40); m.transpose = -12; env(m, 0, 70, 120, 30);
       lp(m, 90, 40, 0, 0); lfo1(m, LfoWave::Sine, LfoDest::Col, 70, 40); m.mono = true;
       sv(m, 60, 60); m.drive = 30; }},
    {InstrType::Fm, "BASS", "GROWL FM", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 80, 62, 40, 40); m.transpose = -12; env(m, 0, 70, 120, 30);
       lp(m, 85, 50, 0, 0); lfo1(m, LfoWave::Tri, LfoDest::Cutoff, 75, 45);
       lfoN(m, 2, LfoWave::Sine, LfoDest::Col, 60, 30); m.mono = true; sv(m, 50, 62); }},
    {InstrType::Fm, "BASS", "JUMPUP", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 70, 60, 0, 64); m.transpose = -12; env(m, 0, 60, 120, 30);
       lfo1(m, LfoWave::Sine, LfoDest::Col, 85, 50); m.mono = true; m.glide = 4; sv(m, 40, 68); }},
    {InstrType::Fm, "BASS", "HALF SUB", [](I& m) {  // halftime / dnb sub with weight
       fm(m, FmMachine::Tone); mac(m, 64, 20, 5, 20, 50); m.transpose = -12; env(m, 0, 75, 120, 40);
       m.mono = true; sv(m, 40, 85); }},
    {InstrType::Fm, "BASS", "808 FM", [](I& m) {  // trap 808: long sine decay, glide
       fm(m, FmMachine::Tone); mac(m, 110, 15, 5, 30, 50); m.transpose = -12; env(m, 0, 100, 0, 70);
       m.mono = true; m.glide = 15; sv(m, 35, 85); }},
    {InstrType::Fm, "BASS", "BS ROUND", [](I& m) {  // boom bap / lofi upright-ish
       fm(m, FmMachine::Tone); mac(m, 85, 25, 30, 40, 50); m.transpose = -12; env(m, 0, 80, 60, 40);
       lp(m, 75, 0, 15, 60); m.mono = true; }},
    {InstrType::Fm, "BASS", "DUB BASS", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 90, 10, 5, 20, 40); m.transpose = -12; env(m, 5, 80, 100, 45);
       lp(m, 65, 0, 0, 0); m.mono = true; }},
    {InstrType::Fm, "BASS", "OUTRUN B", [](I& m) {  // synthwave 8ths
       fm(m, FmMachine::Tone); mac(m, 70, 50, 40, 60, 30); m.transpose = -12; env(m, 0, 70, 30, 35);
       lp(m, 85, 15, 30, 55); m.mono = true; m.vol = 90; }},
    {InstrType::Fm, "BASS", "DARK BS", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 76, 60, 62, 50, 40); m.transpose = -12; env(m, 0, 70, 80, 35);
       lp(m, 80, 25, 25, 55); m.mono = true; sv(m, 40, 75); }},
    {InstrType::Fm, "BASS", "ELEC BS", [](I& m) {  // electro: short square, index pluck
       fm(m, FmMachine::Tone); mac(m, 60, 55, 62, 90, 25); m.transpose = -12; env(m, 0, 60, 20, 30);
       lp(m, 90, 20, 30, 45); m.mono = true; m.vol = 85; }},
    {InstrType::Fm, "LEAD", "FM LEAD", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 80, 40, 30, 50); env(m, 0, 70, 110, 45); m.mono = true; m.glide = 6;
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 80, 2); fx(m, 40, 30); }},
    {InstrType::Fm, "LEAD", "SQ LEAD", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 70, 60, 0, 64); env(m, 0, 70, 100, 40); m.mono = true; fx(m, 30, 20); }},
    {InstrType::Fm, "LEAD", "BRASS", [](I& m) {  // saw zone, index swells in
       fm(m, FmMachine::Tone); mac(m, 64, 50, 35, 60, 90); env(m, 50, 70, 100, 45); lp(m, 95, 10, 15, 70); }},
    {InstrType::Fm, "LEAD", "LEAD SFT", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 90, 30, 15, 30, 60); env(m, 5, 70, 100, 50); m.mono = true; m.glide = 5;
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 78, 2); fx(m, 40, 40); m.vol = 90; }},
    {InstrType::Fm, "LEAD", "TRNC LD", [](I& m) {  // uplifting: detuned saw zone, big send
       fm(m, FmMachine::Tone); mac(m, 80, 75, 38, 20, 50); env(m, 0, 70, 110, 55); m.mono = true; m.glide = 4;
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 80, 2); fx(m, 50, 60); m.vol = 78; }},
    {InstrType::Fm, "LEAD", "PSY LEAD", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 70, 65, 62, 40, 40); env(m, 0, 70, 100, 40); m.mono = true; m.glide = 4;
       fx(m, 45, 30); m.vol = 78; }},
    {InstrType::Fm, "LEAD", "FLUTE FM", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 80, 20, 10, 40, 70); env(m, 35, 70, 110, 55); m.mono = true;
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 75, 3); fx(m, 30, 50); m.vol = 90; }},
    {InstrType::Fm, "LEAD", "RUN LEAD", [](I& m) {  // outrun brass-saw lead
       fm(m, FmMachine::Tone); mac(m, 85, 60, 40, 50, 80); env(m, 20, 70, 105, 55); lp(m, 95, 15, 15, 70);
       m.mono = true; m.glide = 6; fx(m, 45, 50); m.vol = 82; }},
    {InstrType::Fm, "LEAD", "CHILL LD", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 85, 35, 60, 30, 60); env(m, 10, 70, 100, 60); lp(m, 85, 10, 0, 0);
       m.mono = true; m.glide = 5; lfo1(m, LfoWave::Sine, LfoDest::Pitch, 70, 2); fx(m, 50, 60); m.vol = 85; }},
    {InstrType::Fm, "LEAD", "GLASS LD", [](I& m) {  // IDM / electronica bell lead
       fm(m, FmMachine::Tone); mac(m, 90, 30, 110, 50, 50); env(m, 0, 70, 80, 60); fx(m, 50, 60); m.vol = 82; }},
    {InstrType::Fm, "PAD", "FM PAD", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 35, 90, 0, 64); env(m, 75, 90, 110, 90);
       lfo1(m, LfoWave::Tri, LfoDest::Col, 30, 25); fx(m, 30, 70); }},
    {InstrType::Fm, "PAD", "CHRD PAD", [](I& m) {  // MAJ, slow
       fm(m, FmMachine::Chord); mac(m, 64, 25, 5, 0, 64); env(m, 75, 90, 110, 95); fx(m, 30, 80); }},
    {InstrType::Fm, "PAD", "PAD WARM", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 30, 12, 0, 64); env(m, 80, 90, 120, 95);
       lfo1(m, LfoWave::Tri, LfoDest::Col, 25, 15); fx(m, 30, 70); m.vol = 90; }},
    {InstrType::Fm, "PAD", "PAD GLAS", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 100, 25, 112, 40, 90); env(m, 70, 90, 90, 95); fx(m, 40, 80); m.vol = 80; }},
    {InstrType::Fm, "PAD", "PAD DARK", [](I& m) {
       fm(m, FmMachine::Tone); mac(m, 64, 45, 40, 0, 64); env(m, 85, 90, 115, 100); lp(m, 65, 10, 0, 0);
       lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 12, 15); fx(m, 20, 80); m.vol = 90; }},
    {InstrType::Fm, "PAD", "DRONE FM", [](I& m) {  // stack zone, slow index and filter drift
       fm(m, FmMachine::Tone); mac(m, 64, 50, 90, 0, 64); env(m, 100, 90, 120, 115); lp(m, 80, 10, 0, 0);
       lfo1(m, LfoWave::Tri, LfoDest::Col, 10, 30); lfoN(m, 2, LfoWave::Sine, LfoDest::Cutoff, 8, 20);
       fx(m, 20, 90); m.vol = 85; }},
    {InstrType::Fm, "PAD", "DREAM PD", [](I& m) {  // SUS2, bright swell fading out
       fm(m, FmMachine::Chord); mac(m, 64, 25, 26, 30, 100); env(m, 80, 90, 110, 100); fx(m, 40, 90); m.vol = 82; }},
    {InstrType::Fm, "PAD", "PAD MI7", [](I& m) {
       fm(m, FmMachine::Chord); mac(m, 64, 30, 58, 0, 64); env(m, 75, 90, 110, 95); lp(m, 80, 5, 0, 0);
       fx(m, 30, 80); m.vol = 85; }},
    {InstrType::Fm, "PAD", "PAD MI9", [](I& m) {
       fm(m, FmMachine::Chord); mac(m, 64, 25, 122, 0, 64); env(m, 70, 90, 110, 95); fx(m, 30, 80); m.vol = 82; }},
    {InstrType::Fm, "PAD", "STRNG FM", [](I& m) {  // string ensemble: saw zone, slow attack
       fm(m, FmMachine::Tone); mac(m, 64, 55, 38, 0, 64); env(m, 70, 90, 115, 85); lp(m, 90, 5, 0, 0);
       lfo1(m, LfoWave::Tri, LfoDest::Pitch, 60, 2); fx(m, 20, 70); m.vol = 82; }},
    {InstrType::Fm, "FX", "RISER", [](I& m) {  // pitch and index climb over the held note
       fm(m, FmMachine::Tone); mac(m, 64, 50, 40, 0, 64); env(m, 100, 90, 127, 80);
       lfo1(m, LfoWave::Saw, LfoDest::Pitch, 10, 40); lfoN(m, 2, LfoWave::Saw, LfoDest::Col, 10, 40);
       fx(m, 40, 70); m.vol = 72; }},
    {InstrType::Fm, "FX", "LASER", [](I& m) { fm(m, FmMachine::Perc); mac(m, 70, 90, 90, 127, 90); m.transpose = 12; m.vol = 72; fx(m, 40, 30); }},
};

}  // namespace

const FactoryTable kFactoryFm = {kTable, static_cast<int>(sizeof(kTable) / sizeof(kTable[0]))};

}  // namespace mt
