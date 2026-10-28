// Factory DRUM presets (see presets_factory.h: how to add one).
#include "presets_factory_fill.h"

namespace mt {
namespace {
using namespace pf;

constexpr FactoryPreset kTable[] = {
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
    // Second engine (BDA .. HHR): macros as drumMacroName.
    {InstrType::Drum, "ANALOG", "BDA", [](I& m) { dr(m, DrumMachine::Bda); }},
    {InstrType::Drum, "ANALOG", "BDA BOOM", [](I& m) { dr(m, DrumMachine::Bda); m.macro[kMacDec] = 115; m.macro[kMacCol] = 30; }},
    {InstrType::Drum, "ANALOG", "SDA", [](I& m) { dr(m, DrumMachine::Sda); }},
    {InstrType::Drum, "ANALOG", "SDA SNAP", [](I& m) { dr(m, DrumMachine::Sda); m.macro[kMacShp] = 110; }},
    {InstrType::Drum, "ANALOG", "BDS", [](I& m) { dr(m, DrumMachine::Bds); }},
    {InstrType::Drum, "ANALOG", "BDS DIRT", [](I& m) { dr(m, DrumMachine::Bds); m.macro[kMacShp] = 100; m.drive = 30; }},
    {InstrType::Drum, "ANALOG", "SDS", [](I& m) { dr(m, DrumMachine::Sds); }},
    {InstrType::Drum, "ANALOG", "SDS FM", [](I& m) { dr(m, DrumMachine::Sds); m.macro[kMacSwp] = 100; }},
    {InstrType::Drum, "ANALOG", "CPX", [](I& m) { dr(m, DrumMachine::Cpx); }},
    {InstrType::Drum, "ANALOG", "CPX WIDE", [](I& m) { dr(m, DrumMachine::Cpx); m.macro[kMacSwp] = 120; m.macro[kMacCon] = 90; }},
    {InstrType::Drum, "ANALOG", "RSX", [](I& m) { dr(m, DrumMachine::Rsx); }},
    {InstrType::Drum, "ANALOG", "RSX NOIS", [](I& m) { dr(m, DrumMachine::Rsx); m.macro[kMacShp] = 110; }},
    {InstrType::Drum, "ANALOG", "HHR C", [](I& m) { dr(m, DrumMachine::Hhr); m.macro[kMacDec] = 25; }},
    {InstrType::Drum, "ANALOG", "HHR O", [](I& m) { dr(m, DrumMachine::Hhr); m.macro[kMacDec] = 95; }},
    // Genre sets for DICE (first engine only: the second has exactly two presets per machine).
    // Levels: kicks ~100, snares / claps ~85, hats ~70, cymbals ~60; drive or reverb lowers vol.
    {InstrType::Drum, "HOUSE", "BD DEEP", [](I& m) {  // round, warm, a little low
       dr(m, DrumMachine::Bd9); mac(m, 80, 30, 20, 45, 45); lp(m, 100, 0, 0, 40); m.transpose = -2; }},
    {InstrType::Drum, "HOUSE", "BD HOUSE", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 75, 55, 30, 55, 40); lp(m, 112, 0, 0, 40); }},
    {InstrType::Drum, "HOUSE", "BD DISCO", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 65, 40, 15, 45, 35); lp(m, 105, 0, 0, 40); }},
    {InstrType::Drum, "HOUSE", "SD HOUSE", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 50, 60, 70, 30, 60); m.vol = 88; }},
    {InstrType::Drum, "HOUSE", "CP HOUSE", [](I& m) { dr(m, DrumMachine::Cp9); mac(m, 70, 60, 64, 60, 64); fx(m, 0, 40); m.vol = 88; }},
    {InstrType::Drum, "HOUSE", "CP DEEP", [](I& m) {
       dr(m, DrumMachine::Cp8); mac(m, 80, 50, 70, 55, 70); lp(m, 100, 0, 0, 40); fx(m, 0, 50); m.vol = 85; }},
    {InstrType::Drum, "HOUSE", "CH HOUSE", [](I& m) { dr(m, DrumMachine::Hh9); mac(m, 28, 60, 55, 64, 60); lp(m, 115, 0, 0, 40); m.vol = 78; }},
    {InstrType::Drum, "HOUSE", "OH HOUSE", [](I& m) { dr(m, DrumMachine::Hh9); mac(m, 90, 60, 55, 64, 64); lp(m, 112, 0, 0, 40); m.vol = 72; }},
    {InstrType::Drum, "HOUSE", "SHAKER", [](I& m) { dr(m, DrumMachine::Hh8); mac(m, 20, 70, 110, 64, 40); lp(m, 112, 0, 0, 40); m.vol = 68; }},
    {InstrType::Drum, "HOUSE", "TAMB", [](I& m) { dr(m, DrumMachine::Hh8); mac(m, 45, 80, 90, 72, 50); lp(m, 115, 0, 0, 40); m.vol = 66; }},
    {InstrType::Drum, "HOUSE", "RIM DEEP", [](I& m) { dr(m, DrumMachine::Rs8); mac(m, 60, 50, 50, 20, 0); fx(m, 0, 40); m.vol = 80; }},
    {InstrType::Drum, "HOUSE", "CONGA M", [](I& m) { dr(m, DrumMachine::Tom8); mac(m, 55, 0, 0, 30, 40); m.transpose = 7; m.vol = 82; }},
    {InstrType::Drum, "HOUSE", "CONGA H", [](I& m) { dr(m, DrumMachine::Tom8); mac(m, 50, 0, 0, 30, 40); m.transpose = 12; m.vol = 80; }},
    {InstrType::Drum, "HOUSE", "CB DISCO", [](I& m) { dr(m, DrumMachine::Cb8); mac(m, 55, 64, 64, 50, 0); lp(m, 110, 0, 0, 40); m.vol = 68; }},
    {InstrType::Drum, "TECHNO", "BD TECH", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 70, 60, 30, 50, 35); }},
    {InstrType::Drum, "TECHNO", "BD MINI", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 55, 45, 10, 35, 35); }},
    {InstrType::Drum, "TECHNO", "BD DUB", [](I& m) {  // soft, low-passed, into the reverb
       dr(m, DrumMachine::Bd8); mac(m, 95, 20, 15, 40, 50); lp(m, 85, 10, 0, 40); fx(m, 0, 45); m.vol = 95; }},
    {InstrType::Drum, "TECHNO", "BD PEAK", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 80, 70, 50, 60, 40); m.drive = 25; m.vol = 80; }},
    {InstrType::Drum, "TECHNO", "BD HTEK", [](I& m) {  // hard techno, the one loud one here
       dr(m, DrumMachine::Bd9); mac(m, 85, 85, 80, 70, 35); m.drive = 40; m.vol = 62; }},
    {InstrType::Drum, "TECHNO", "BD TAIL", [](I& m) {
       dr(m, DrumMachine::Bd8); mac(m, 118, 25, 30, 35, 60); lp(m, 70, 20, 0, 40); fx(m, 0, 70); m.drive = 20; m.vol = 70; }},
    {InstrType::Drum, "TECHNO", "SD HTEK", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 55, 70, 90, 40, 50); m.drive = 25; m.vol = 75; }},
    {InstrType::Drum, "TECHNO", "CP TECH", [](I& m) { dr(m, DrumMachine::Cp9); mac(m, 60, 70, 60, 70, 50); m.vol = 85; }},
    {InstrType::Drum, "TECHNO", "CP DUB", [](I& m) {
       dr(m, DrumMachine::Cp8); mac(m, 90, 45, 70, 50, 80); lp(m, 98, 0, 0, 40); fx(m, 60, 70); m.vol = 76; }},
    {InstrType::Drum, "TECHNO", "CH TECH", [](I& m) { dr(m, DrumMachine::Hh9); mac(m, 22, 70, 45, 64, 55); lp(m, 118, 0, 0, 40); m.vol = 74; }},
    {InstrType::Drum, "TECHNO", "OH TECH", [](I& m) { dr(m, DrumMachine::Hh9); mac(m, 80, 64, 50, 64, 55); lp(m, 116, 0, 0, 40); m.vol = 70; }},
    {InstrType::Drum, "TECHNO", "RIDE TEC", [](I& m) { dr(m, DrumMachine::Cy9); mac(m, 85, 64, 0, 64, 64); lp(m, 115, 0, 0, 40); m.vol = 62; }},
    {InstrType::Drum, "TECHNO", "RIM DUB", [](I& m) { dr(m, DrumMachine::Rs9); mac(m, 70, 40, 40, 20, 0); fx(m, 70, 60); m.vol = 72; }},
    {InstrType::Drum, "TECHNO", "PERC TEC", [](I& m) { dr(m, DrumMachine::Cb8); mac(m, 45, 90, 30, 90, 0); bp(m, 95, 30); m.vol = 66; }},
    {InstrType::Drum, "TRANCE", "BD TRNC", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 70, 75, 35, 65, 38); lp(m, 112, 10, 0, 40); }},
    {InstrType::Drum, "TRANCE", "BD PSY", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 45, 80, 30, 70, 30); }},  // short: room for the bass
    {InstrType::Drum, "TRANCE", "BD GOA", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 60, 50, 20, 55, 35); }},
    {InstrType::Drum, "TRANCE", "CP TRNC", [](I& m) { dr(m, DrumMachine::Cp9); mac(m, 75, 70, 60, 60, 70); fx(m, 0, 60); m.vol = 80; }},
    {InstrType::Drum, "TRANCE", "SD TRNC", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 60, 70, 85, 35, 70); fx(m, 0, 45); m.vol = 84; }},
    {InstrType::Drum, "TRANCE", "OH TRNC", [](I& m) { dr(m, DrumMachine::Hh9); mac(m, 85, 70, 60, 70, 50); lp(m, 116, 0, 0, 40); m.vol = 70; }},
    {InstrType::Drum, "TRANCE", "CH PSY", [](I& m) { dr(m, DrumMachine::Hh8); mac(m, 18, 75, 40, 70, 40); m.vol = 70; }},
    {InstrType::Drum, "TRANCE", "CRASH SF", [](I& m) { dr(m, DrumMachine::Cy9); mac(m, 100, 64, 127, 64, 64); lp(m, 112, 0, 0, 40); fx(m, 0, 40); m.vol = 58; }},
    {InstrType::Drum, "DNB", "BD DNB", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 55, 70, 30, 55, 30); }},
    {InstrType::Drum, "DNB", "BD LIQ", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 60, 40, 15, 40, 35); lp(m, 105, 0, 0, 40); }},
    {InstrType::Drum, "DNB", "BD NEURO", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 55, 85, 70, 65, 30); m.drive = 35; m.vol = 68; }},
    {InstrType::Drum, "DNB", "BD JUNG", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 65, 40, 25, 45, 40); lp(m, 100, 0, 0, 40); }},
    {InstrType::Drum, "DNB", "SD DNB", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 50, 80, 95, 40, 55); m.vol = 88; }},
    {InstrType::Drum, "DNB", "SD LIQ", [](I& m) { dr(m, DrumMachine::Sd8); mac(m, 55, 60, 85, 30, 70); fx(m, 0, 50); m.vol = 84; }},
    {InstrType::Drum, "DNB", "SD NEURO", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 60, 70, 110, 45, 60); m.drive = 35; m.vol = 66; }},
    {InstrType::Drum, "DNB", "SD JUMP", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 45, 90, 90, 55, 45); m.drive = 20; m.vol = 78; }},
    {InstrType::Drum, "DNB", "SD JUNG", [](I& m) { dr(m, DrumMachine::Sd8); mac(m, 45, 70, 80, 40, 50); lp(m, 110, 0, 0, 40); m.transpose = 3; m.vol = 85; }},
    {InstrType::Drum, "DNB", "SD GHOST", [](I& m) { dr(m, DrumMachine::Sd8); mac(m, 30, 50, 60, 30, 40); lp(m, 108, 0, 0, 40); m.vol = 55; }},
    {InstrType::Drum, "DNB", "SD HALF", [](I& m) { dr(m, DrumMachine::Sd8); mac(m, 80, 50, 90, 30, 85); fx(m, 0, 70); m.vol = 80; }},
    {InstrType::Drum, "DNB", "CH DNB", [](I& m) { dr(m, DrumMachine::Hh9); mac(m, 20, 75, 50, 70, 45); lp(m, 118, 0, 0, 40); m.vol = 70; }},
    {InstrType::Drum, "DNB", "RIDE DNB", [](I& m) { dr(m, DrumMachine::Cy9); mac(m, 80, 70, 0, 64, 60); lp(m, 116, 0, 0, 40); m.vol = 58; }},
    {InstrType::Drum, "BREAKS", "BD BREAK", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 65, 60, 35, 50, 40); lp(m, 108, 0, 0, 40); }},
    {InstrType::Drum, "BREAKS", "SD BREAK", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 55, 64, 85, 35, 60); lp(m, 112, 0, 0, 40); m.vol = 88; }},
    {InstrType::Drum, "BREAKS", "BD 2STEP", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 70, 35, 15, 40, 40); }},
    {InstrType::Drum, "BREAKS", "SD 2STEP", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 40, 75, 80, 40, 45); m.vol = 85; }},
    {InstrType::Drum, "BREAKS", "RIM UKG", [](I& m) { dr(m, DrumMachine::Rs9); mac(m, 55, 60, 60, 30, 0); m.vol = 76; }},
    {InstrType::Drum, "BREAKS", "BD ELECT", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 85, 30, 20, 55, 45); }},
    {InstrType::Drum, "BREAKS", "SD ELECT", [](I& m) { dr(m, DrumMachine::Sd8); mac(m, 50, 70, 100, 40, 50); m.vol = 84; }},
    {InstrType::Drum, "BREAKS", "CP ELECT", [](I& m) { dr(m, DrumMachine::Cp8); mac(m, 55, 64, 64, 64, 40); m.vol = 85; }},
    {InstrType::Drum, "BREAKS", "CB ELECT", [](I& m) { dr(m, DrumMachine::Cb8); mac(m, 55, 64, 64, 64, 0); m.vol = 68; }},
    {InstrType::Drum, "BREAKS", "CL ELECT", [](I& m) { dr(m, DrumMachine::Cl8); mac(m, 55, 64, 0, 20, 0); m.vol = 68; }},
    {InstrType::Drum, "BREAKS", "TOM ELEC", [](I& m) { dr(m, DrumMachine::Tom8); mac(m, 80, 0, 0, 60, 60); m.vol = 80; }},
    {InstrType::Drum, "HIPHOP", "BD BOOM", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 70, 40, 30, 45, 40); lp(m, 95, 0, 0, 40); m.drive = 15; m.vol = 90; }},
    {InstrType::Drum, "HIPHOP", "SD BOOM", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 55, 50, 85, 30, 60); lp(m, 105, 0, 0, 40); m.crushRate = 25; m.vol = 85; }},
    {InstrType::Drum, "HIPHOP", "BD LOFI", [](I& m) {
       dr(m, DrumMachine::Bd8); mac(m, 65, 30, 15, 35, 40); lp(m, 85, 0, 0, 40); m.crushBits = 95; m.crushRate = 35; m.vol = 95; }},
    {InstrType::Drum, "HIPHOP", "SD LOFI", [](I& m) {
       dr(m, DrumMachine::Sd8); mac(m, 50, 50, 75, 30, 60); lp(m, 92, 0, 0, 40); m.crushBits = 95; m.crushRate = 40; m.vol = 80; }},
    {InstrType::Drum, "HIPHOP", "HH LOFI", [](I& m) { dr(m, DrumMachine::Hh8); mac(m, 30, 55, 40, 64, 60); lp(m, 100, 0, 0, 40); m.crushRate = 40; m.vol = 64; }},
    {InstrType::Drum, "HIPHOP", "RIM LOFI", [](I& m) { dr(m, DrumMachine::Rs8); mac(m, 55, 40, 40, 20, 0); lp(m, 100, 0, 0, 40); m.crushRate = 30; m.vol = 72; }},
    {InstrType::Drum, "HIPHOP", "808 SUB", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 120, 15, 20, 25, 60); }},  // long, plays the bass line
    {InstrType::Drum, "HIPHOP", "808 DIST", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 115, 25, 50, 30, 55); m.drive = 30; m.vol = 72; }},
    {InstrType::Drum, "HIPHOP", "808 SHRT", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 85, 30, 15, 35, 45); }},
    {InstrType::Drum, "HIPHOP", "SD TRAP", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 45, 80, 95, 40, 45); m.vol = 84; }},
    {InstrType::Drum, "HIPHOP", "CP TRAP", [](I& m) { dr(m, DrumMachine::Cp8); mac(m, 60, 70, 70, 60, 60); fx(m, 0, 30); m.vol = 84; }},
    {InstrType::Drum, "HIPHOP", "HH TRAP", [](I& m) { dr(m, DrumMachine::Hh8); mac(m, 15, 75, 30, 60, 40); m.vol = 70; }},  // rolls
    {InstrType::Drum, "HIPHOP", "OH TRAP", [](I& m) { dr(m, DrumMachine::Hh8); mac(m, 80, 70, 35, 64, 60); m.vol = 64; }},
    {InstrType::Drum, "IDM", "BD GLTCH", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 30, 90, 20, 90, 10); m.crushRate = 40; m.vol = 90; }},
    {InstrType::Drum, "IDM", "BD IDM", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 55, 50, 40, 90, 20); m.vol = 95; }},  // elastic sweep
    {InstrType::Drum, "IDM", "SD GLTCH", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 25, 100, 100, 70, 20); m.crushBits = 100; m.vol = 74; }},
    {InstrType::Drum, "IDM", "SD BRAIN", [](I& m) { dr(m, DrumMachine::Sd8); mac(m, 35, 80, 95, 70, 40); m.vol = 80; }},
    {InstrType::Drum, "IDM", "HH GLTCH", [](I& m) { dr(m, DrumMachine::Hh9); mac(m, 5, 80, 100, 90, 20); m.vol = 64; }},
    {InstrType::Drum, "IDM", "TICK", [](I& m) { dr(m, DrumMachine::Hh8); mac(m, 3, 90, 0, 90, 10); m.vol = 60; }},
    {InstrType::Drum, "IDM", "CLICK", [](I& m) { dr(m, DrumMachine::Cl8); mac(m, 20, 90, 0, 40, 0); m.vol = 68; }},
    {InstrType::Drum, "IDM", "BLIP", [](I& m) { dr(m, DrumMachine::Rs9); mac(m, 30, 90, 30, 100, 0); m.vol = 68; }},
    {InstrType::Drum, "IDM", "TOM ZAP", [](I& m) { dr(m, DrumMachine::Tom9); mac(m, 40, 90, 0, 100, 20); m.transpose = 12; m.vol = 74; }},
    {InstrType::Drum, "AMBIENT", "BD SOFT", [](I& m) { dr(m, DrumMachine::Bd8); mac(m, 80, 5, 0, 30, 50); lp(m, 80, 0, 0, 40); fx(m, 0, 50); m.vol = 92; }},
    {InstrType::Drum, "AMBIENT", "BD PULSE", [](I& m) {  // dark heartbeat
       dr(m, DrumMachine::Bd8); mac(m, 100, 0, 0, 20, 60); lp(m, 70, 0, 0, 40); fx(m, 0, 60); m.vol = 95; }},
    {InstrType::Drum, "AMBIENT", "SD AIR", [](I& m) { dr(m, DrumMachine::Sd8); mac(m, 70, 40, 90, 20, 90); lp(m, 100, 0, 0, 40); fx(m, 0, 90); m.vol = 68; }},
    {InstrType::Drum, "AMBIENT", "HH SOFT", [](I& m) { dr(m, DrumMachine::Hh8); mac(m, 35, 40, 60, 50, 70); lp(m, 95, 0, 0, 40); fx(m, 0, 70); m.vol = 55; }},
    {InstrType::Drum, "AMBIENT", "RIM AIR", [](I& m) { dr(m, DrumMachine::Rs8); mac(m, 70, 30, 30, 10, 0); fx(m, 70, 80); m.vol = 62; }},
    {InstrType::Drum, "AMBIENT", "CB GLASS", [](I& m) { dr(m, DrumMachine::Cb8); mac(m, 100, 30, 30, 20, 0); lp(m, 100, 0, 0, 40); fx(m, 40, 90); m.vol = 52; }},
    {InstrType::Drum, "AMBIENT", "CY WASH", [](I& m) { dr(m, DrumMachine::Cy8); mac(m, 127, 40, 40, 50, 90); lp(m, 95, 0, 0, 40); fx(m, 0, 80); m.vol = 50; }},
    {InstrType::Drum, "AMBIENT", "TOM AMB", [](I& m) { dr(m, DrumMachine::Tom9); mac(m, 100, 40, 0, 30, 60); lp(m, 90, 0, 0, 40); fx(m, 0, 90); m.vol = 70; }},
    {InstrType::Drum, "SYNTHWAVE", "BD OUTRN", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 75, 55, 35, 55, 40); lp(m, 108, 0, 0, 40); }},
    {InstrType::Drum, "SYNTHWAVE", "BD DARKS", [](I& m) { dr(m, DrumMachine::Bd9); mac(m, 70, 80, 55, 65, 35); m.drive = 25; m.vol = 80; }},
    {InstrType::Drum, "SYNTHWAVE", "SD GATE", [](I& m) { dr(m, DrumMachine::Sd9); mac(m, 70, 55, 90, 30, 80); fx(m, 0, 100); m.vol = 76; }},  // 80s big room
    {InstrType::Drum, "SYNTHWAVE", "CP 80S", [](I& m) { dr(m, DrumMachine::Cp9); mac(m, 75, 64, 64, 64, 70); fx(m, 0, 90); m.vol = 74; }},
    {InstrType::Drum, "SYNTHWAVE", "TOM 80S", [](I& m) { dr(m, DrumMachine::Tom9); mac(m, 85, 55, 10, 70, 60); fx(m, 0, 70); m.vol = 80; }},
    {InstrType::Drum, "SYNTHWAVE", "SD CHILL", [](I& m) {
       dr(m, DrumMachine::Sd8); mac(m, 60, 45, 70, 30, 70); lp(m, 95, 0, 0, 40); fx(m, 0, 60); m.crushRate = 25; m.vol = 78; }},
    {InstrType::Drum, "SYNTHWAVE", "HH 80S", [](I& m) { dr(m, DrumMachine::Hh9); mac(m, 35, 64, 50, 64, 64); lp(m, 112, 0, 0, 40); m.vol = 68; }},
};

}  // namespace

const FactoryTable kFactoryDrum = {kTable, static_cast<int>(sizeof(kTable) / sizeof(kTable[0]))};

}  // namespace mt
