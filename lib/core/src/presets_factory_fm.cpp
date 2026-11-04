// Factory starting points for the measured reference-inspired family; not exact hardware clones.
#include "presets_factory_fill.h"

namespace mt {
namespace {
using namespace pf;
constexpr FactoryPreset kTable[] = {
    {InstrType::Fm, "DRUMS", "BDMODERN", [](I& m) { fm(m, FmMachine::BdModern); }},
    {InstrType::Fm, "DRUMS", "BDPUNCH", [](I& m) { fm(m, FmMachine::BdModern); m.fmParam[2] = 127; m.fmParam[3] = 64; }},
    {InstrType::Fm, "DRUMS", "BDLONG", [](I& m) { fm(m, FmMachine::BdModern); m.fmParam[3] = 110; m.fmParam[1] = 32; }},
    {InstrType::Fm, "DRUMS", "SDBASIC", [](I& m) { fm(m, FmMachine::SdBasic); }},
    {InstrType::Fm, "DRUMS", "SDBTIGHT", [](I& m) { fm(m, FmMachine::SdBasic); m.fmParam[3] = 48; }},
    {InstrType::Fm, "DRUMS", "SDBBRITE", [](I& m) { fm(m, FmMachine::SdBasic); m.fmParam[5] = 110; m.fmParam[6] = 80; }},
    {InstrType::Fm, "DRUMS", "SDVINT", [](I& m) { fm(m, FmMachine::SdVintage); }},
    {InstrType::Fm, "DRUMS", "SDVSHORT", [](I& m) { fm(m, FmMachine::SdVintage); m.fmParam[3] = 48; m.fmParam[4] = 110; }},
    {InstrType::Fm, "DRUMS", "SDVBODY", [](I& m) { fm(m, FmMachine::SdVintage); m.fmParam[4] = 16; m.fmParam[5] = 96; }},
    {InstrType::Fm, "DRUMS", "CPVINT", [](I& m) { fm(m, FmMachine::CpVintage); }},
    {InstrType::Fm, "DRUMS", "CPSHORT", [](I& m) { fm(m, FmMachine::CpVintage); m.fmParam[3] = 48; }},
    {InstrType::Fm, "DRUMS", "CPBODY", [](I& m) { fm(m, FmMachine::CpVintage); m.fmParam[4] = 127; m.fmParam[5] = 80; }},
    {InstrType::Fm, "DRUMS", "HHBASIC", [](I& m) { fm(m, FmMachine::HhBasic); }},
    {InstrType::Fm, "DRUMS", "HHSHORT", [](I& m) { fm(m, FmMachine::HhBasic); m.fmParam[3] = 32; }},
    {InstrType::Fm, "DRUMS", "HHLONG", [](I& m) { fm(m, FmMachine::HhBasic); m.fmParam[3] = 110; }},
    {InstrType::Fm, "METAL", "CYALLOY", [](I& m) { fm(m, FmMachine::CyAlloy); }},
    {InstrType::Fm, "METAL", "CYDARK", [](I& m) { fm(m, FmMachine::CyAlloy); m.fmParam[5] = 16; m.fmParam[3] = 100; }},
    {InstrType::Fm, "METAL", "CYBRITE", [](I& m) { fm(m, FmMachine::CyAlloy); m.fmParam[4] = 127; m.fmParam[5] = 110; }},
    {InstrType::Fm, "PERC", "PCCARBON", [](I& m) { fm(m, FmMachine::PcCarbon); }},
    {InstrType::Fm, "PERC", "PCSHORT", [](I& m) { fm(m, FmMachine::PcCarbon); m.fmParam[3] = 40; }},
    {InstrType::Fm, "PERC", "PCMETAL", [](I& m) { fm(m, FmMachine::PcCarbon); m.fmParam[4] = 127; m.fmParam[5] = 110; }},
    {InstrType::Fm, "PERC", "METALTCK", [](I& m) { fm(m, FmMachine::HhBasic); m.fmParam[1] = 80; m.fmParam[2] = 16; m.fmParam[3] = 48; }},
    {InstrType::Fm, "TONAL", "TONE", [](I& m) { fm(m, FmMachine::Tone); env(m, 0, 127, 127, 20); }},
    {InstrType::Fm, "TONAL", "TONESINE", [](I& m) { fm(m, FmMachine::Tone); m.fmParam[5] = 0; env(m, 0, 127, 127, 20); }},
    {InstrType::Fm, "TONAL", "TONEBASS", [](I& m) { fm(m, FmMachine::Tone); m.fmParam[5] = 0; m.transpose = -12; env(m, 0, 127, 127, 20); }},
    {InstrType::Fm, "TONAL", "TONEPLUK", [](I& m) { fm(m, FmMachine::Tone); m.fmParam[3] = 64; m.fmParam[6] = 90; m.fmParam[5] = 80; env(m, 0, 127, 127, 20); }},
    {InstrType::Fm, "TONAL", "TOY", [](I& m) { fm(m, FmMachine::Toy); env(m, 0, 127, 127, 20); }},
    {InstrType::Fm, "TONAL", "TOYBRITE", [](I& m) { fm(m, FmMachine::Toy); m.fmParam[5] = 127; m.fmParam[6] = 127; env(m, 0, 127, 127, 20); }},
    {InstrType::Fm, "TONAL", "TOYSHORT", [](I& m) { fm(m, FmMachine::Toy); m.fmParam[3] = 48; m.fmParam[4] = 127; env(m, 0, 127, 127, 20); }},
    {InstrType::Fm, "CHORD", "CHORD", [](I& m) { fm(m, FmMachine::Chord); env(m, 0, 127, 127, 20); }},
    {InstrType::Fm, "CHORD", "CHRDUNI", [](I& m) { fm(m, FmMachine::Chord); m.fmParam[4] = 1; env(m, 0, 127, 127, 20); }},
    {InstrType::Fm, "CHORD", "CHRDTYPE", [](I& m) { fm(m, FmMachine::Chord); m.fmParam[4] = 9; m.fmParam[5] = 96; env(m, 0, 127, 127, 20); }},
};
}  // namespace
const FactoryTable kFactoryFm = {kTable, static_cast<int>(sizeof(kTable) / sizeof(kTable[0]))};
}  // namespace mt
