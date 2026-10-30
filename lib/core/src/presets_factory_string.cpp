// Factory STRING presets (see presets_factory.h: how to add one).
#include "presets_factory_fill.h"

namespace mt {
namespace {
using namespace pf;

constexpr FactoryPreset kTable[] = {
    // 3 STRING (plucked line), 3 MODAL (struck modes). Sustain 127: the resonator's own decay shapes the note.
    {InstrType::String, "PLUCK", "NYLON", [](I& m) {  // soft pluck near the middle, ~2 s
       st(m, StringModel::String, 88, 50, 0, 40, 0); env(m, 0, 40, 127, 50); fx(m, 20, 40); }},
    {InstrType::String, "PLUCK", "HARP", [](I& m) {  // bright, long ring, short noise burst
       st(m, StringModel::String, 105, 80, 10, 25, 35); env(m, 0, 40, 127, 70); fx(m, 30, 70); }},
    {InstrType::String, "PLUCK", "PIZZ", [](I& m) {  // dark, short
       st(m, StringModel::String, 43, 30, 0, 50, 0); env(m, 0, 40, 127, 20); fx(m, 10, 30); }},
    {InstrType::String, "PLUCK", "SOFTPLK", [](I& m) {  // dark, short noise burst: liquid / lofi
       st(m, StringModel::String, 80, 40, 0, 35, 20); env(m, 0, 40, 127, 45); lp(m, 100, 0, 0, 40); fx(m, 30, 40); }},
    {InstrType::String, "PLUCK", "KOTO", [](I& m) {  // bright, slightly stiff, struck near the bridge
       st(m, StringModel::String, 92, 70, 15, 15, 0); env(m, 0, 40, 127, 55); m.vol = 90; fx(m, 20, 50); }},
    {InstrType::String, "PLUCK", "LOFIGTR", [](I& m) {  // muted guitar with tape wow
       st(m, StringModel::String, 75, 32, 0, 45, 10); env(m, 0, 40, 127, 40); lp(m, 85, 5, 0, 40);
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 25, 1); fx(m, 15, 40); }},
    {InstrType::String, "PLUCK", "DUB PLK", [](I& m) {  // short pluck made for a long delay: dub techno
       st(m, StringModel::String, 70, 45, 0, 30, 0); env(m, 0, 40, 127, 35); lp(m, 95, 10, 0, 40); fx(m, 70, 40); }},
    {InstrType::String, "PLUCK", "MUTED", [](I& m) {  // palm-muted, tight: house / UKG
       st(m, StringModel::String, 35, 55, 0, 20, 0); env(m, 0, 40, 127, 20); fx(m, 20, 25); }},
    {InstrType::String, "PLUCK", "AIRHARP", [](I& m) {  // soft burst, long ring, big room: ambient
       st(m, StringModel::String, 110, 60, 0, 30, 25); env(m, 0, 40, 127, 80); m.vol = 90; fx(m, 35, 80); }},
    {InstrType::String, "PLUCK", "SITAR", [](I& m) {  // stiff, bright, buzzy ring: goa / psy
       st(m, StringModel::String, 100, 85, 40, 10, 0); env(m, 0, 40, 127, 60); m.vol = 85; fx(m, 25, 50); }},
    {InstrType::String, "PLUCK", "STR BASS", [](I& m) {  // plucked upright-ish bass: liquid / deep house
       st(m, StringModel::String, 70, 35, 0, 45, 0); m.transpose = -12; env(m, 0, 40, 127, 30); m.mono = true; }},
    {InstrType::String, "KEYS", "MARIMBA", [](I& m) {  // bar-like partials, mellow mallet
       st(m, StringModel::Modal, 72, 35, 50, 20, 0); env(m, 0, 40, 127, 40); fx(m, 10, 40); }},
    {InstrType::String, "KEYS", "BELL", [](I& m) {  // inharmonic, long
       st(m, StringModel::Modal, 115, 90, 110, 15, 0); env(m, 0, 40, 127, 90); fx(m, 30, 80); }},
    {InstrType::String, "KEYS", "KALIMBA", [](I& m) {  // tine: short click, medium ring
       st(m, StringModel::Modal, 81, 60, 75, 30, 0); env(m, 0, 40, 127, 50); fx(m, 20, 50); }},
    {InstrType::String, "KEYS", "VIBRA", [](I& m) {  // vibraphone: bar modes, motor tremolo
       st(m, StringModel::Modal, 95, 55, 40, 25, 0); env(m, 0, 40, 127, 60);
       lfo1(m, LfoWave::Sine, LfoDest::Vol, 72, 8); fx(m, 20, 60); }},
    {InstrType::String, "KEYS", "GLOCK", [](I& m) {  // glockenspiel, an octave up
       st(m, StringModel::Modal, 100, 95, 90, 10, 0); m.transpose = 12; env(m, 0, 40, 127, 60); m.vol = 85; fx(m, 30, 60); }},
    {InstrType::String, "KEYS", "STEELPAN", [](I& m) {  // soft mallet burst on a pan
       st(m, StringModel::Modal, 90, 65, 60, 35, 15); env(m, 0, 40, 127, 55); fx(m, 20, 50); }},
    {InstrType::String, "KEYS", "WOOD", [](I& m) {  // short woody tick: IDM / glitch percussion
       st(m, StringModel::Modal, 40, 50, 30, 30, 0); env(m, 0, 40, 127, 20); fx(m, 15, 25); }},
    {InstrType::String, "KEYS", "TINE EP", [](I& m) {  // tine piano, rounded, tremolo: lofi / liquid
       st(m, StringModel::Modal, 92, 45, 70, 35, 0); env(m, 0, 40, 127, 60); lp(m, 95, 0, 0, 40);
       lfo1(m, LfoWave::Sine, LfoDest::Vol, 65, 6); fx(m, 20, 55); }},
    {InstrType::String, "KEYS", "GAMELAN", [](I& m) {  // inharmonic gong-bar, long
       st(m, StringModel::Modal, 108, 70, 100, 20, 0); env(m, 0, 40, 127, 80); m.vol = 85; fx(m, 30, 80); }},
    // PAD: long DAMP; the attack must open before the finite exciter has lost its energy.
    // These are struck swells, not continuously bowed strings.
    {InstrType::String, "PAD", "BOWED", [](I& m) {  // bowed-like swell, dark, slow vibrato
       st(m, StringModel::String, 127, 70, 0, 20, 20); env(m, 35, 40, 127, 90);
       lfo1(m, LfoWave::Sine, LfoDest::Pitch, 55, 1); m.vol = 100; fx(m, 25, 90); }},
    {InstrType::String, "PAD", "GLASDRON", [](I& m) {  // glass plate swell: ambient drone
       st(m, StringModel::Modal, 127, 60, 110, 15, 127); env(m, 60, 40, 127, 100);
       lfo1(m, LfoWave::Sine, LfoDest::Col, 15, 12); m.vol = 80; fx(m, 40, 100); }},
    {InstrType::String, "PAD", "DARKDRON", [](I& m) {  // low string, closed filter, slow breath: dark ambient
       st(m, StringModel::String, 127, 20, 20, 64, 0); m.transpose = -12; env(m, 40, 40, 127, 100);
       lp(m, 70, 10, 0, 40); lfo1(m, LfoWave::Sine, LfoDest::Cutoff, 10, 10); m.vol = 110; fx(m, 20, 90); }},
    {InstrType::String, "PAD", "CHIMES", [](I& m) {  // wind chimes, strike position wanders
       st(m, StringModel::Modal, 120, 80, 105, 10, 0); env(m, 0, 40, 127, 90);
       lfo1(m, LfoWave::Random, LfoDest::Swp, 40, 15); m.vol = 80; fx(m, 40, 100); }},
    {InstrType::String, "PAD", "SHIMMER", [](I& m) {  // airy high string swell: dream / chillwave
       st(m, StringModel::String, 120, 70, 0, 15, 20); m.transpose = 12; env(m, 35, 40, 127, 100);
       m.vol = 100; fx(m, 50, 110); }},
};

}  // namespace

const FactoryTable kFactoryString = {kTable, static_cast<int>(sizeof(kTable) / sizeof(kTable[0]))};

}  // namespace mt
