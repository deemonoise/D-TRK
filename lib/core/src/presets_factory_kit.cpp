// Factory KIT presets (see presets_factory.h: how to add one).
#include "presets_factory_fill.h"

namespace mt {
namespace {
using namespace pf;

// KIT lanes: kick, snare, closed / open hat, clap, then the extras.
constexpr InstrType kD = InstrType::Drum, kF = InstrType::Fm, kC = InstrType::Chip;
constexpr FactoryDrum k808[] = {{kD, "BD808", 115}, {kD, "SD808", 100}, {kD, "CH808", 85}, {kD, "OH808", 80},
                                {kD, "CP808", 95},  {kD, "TOM808", 95}, {kD, "RS808", 90}, {kD, "CB808", 80}};
constexpr FactoryDrum k808Deep[] = {{kD, "BD808 L", 120}, {kD, "SD808 S", 100}, {kD, "CH808", 85},
                                    {kD, "OH808", 80},    {kD, "CP808", 95},    {kD, "TOM808 L", 95},
                                    {kD, "CY808", 75},    {kD, "CL808", 90}};
constexpr FactoryDrum k909[] = {{kD, "BD909", 115}, {kD, "SD909", 100}, {kD, "CH909", 85}, {kD, "OH909", 80},
                                {kD, "CP909", 95},  {kD, "TOM909", 95}, {kD, "RS909", 90}, {kD, "CY909", 75}};
constexpr FactoryDrum k909Hard[] = {{kD, "BD909 H", 120}, {kD, "SD909 T", 105}, {kD, "CH909", 90},
                                    {kD, "OH909", 85},    {kD, "CP909", 100},   {kD, "TOM909 H", 95},
                                    {kD, "RS909", 90},    {kD, "CY909", 80}};
constexpr FactoryDrum kLofi[] = {{kD, "BD DUSTY", 115}, {kD, "SD DUSTY", 100}, {kD, "HH DUSTY", 85},
                                 {kD, "CP DUSTY", 95},  {kD, "RIM BIT", 90}};
constexpr FactoryDrum kHard[] = {{kD, "BD DIST", 115}, {kD, "SD NOISE", 105}, {kD, "HH HARSH", 85},
                                 {kD, "CLAP VRB", 95}, {kD, "METAL HT", 85},  {kD, "BD RUMBL", 110}};
constexpr FactoryDrum kFmKit[] = {{kF, "BDMODERN", 115}, {kF, "SDBASIC", 100}, {kF, "HHSHORT", 85}, {kF, "HHLONG", 80},
                                  {kF, "CPVINT", 95},  {kF, "PCCARBON", 95},    {kF, "PCMETAL", 80}, {kF, "CYALLOY", 75}};
constexpr FactoryDrum kFmPunch[] = {{kF, "BDPUNCH", 120}, {kF, "SDVBODY", 105}, {kF, "HHSHORT", 85},
                                    {kF, "HHLONG", 80},     {kF, "CPBODY", 100}, {kF, "PCCARBON", 90},
                                    {kF, "CYALLOY", 75},      {kF, "PCMETAL", 85}};
constexpr FactoryDrum kFmPerc[] = {{kF, "BDLONG", 115}, {kF, "SDBTIGHT", 100}, {kF, "PCSHORT", 90},
                                   {kF, "PCCARBON", 95},     {kF, "PCCARBON", 95},      {kF, "PCMETAL", 85},
                                   {kF, "PCMETAL", 85},       {kF, "CYDARK", 75}};
constexpr FactoryDrum kChip[] = {{kC, "CHIPKICK", 115}, {kC, "NOIS SN", 100}, {kC, "NOIS HH", 85},
                                 {kC, "NOIS OH", 80},   {kC, "METALBL", 85},  {kC, "BLIP", 90}};
constexpr FactoryDrum kHybrid[] = {{kD, "BD909", 115}, {kD, "SD808", 100}, {kF, "HHSHORT", 85},  {kD, "OH909", 80},
                                   {kF, "CPVINT", 95},   {kD, "CB808", 80},  {kF, "PCCARBON", 90}, {kD, "RIM BIT", 90}};
constexpr FactoryDrum kDub[] = {{kD, "BD909 S", 115}, {kD, "RS909", 95}, {kD, "CH909", 85},    {kD, "OH808", 80},
                                {kD, "CP808", 90},    {kD, "TOM808 L", 95}, {kD, "CY909", 75}, {kD, "CB808", 80}};
constexpr FactoryDrum kAnalog[] = {{kD, "BDA", 115}, {kD, "SDA", 100},  {kD, "HHR C", 85}, {kD, "HHR O", 80},
                                   {kD, "CPX", 95},  {kD, "RSX", 90},  {kD, "BDS", 110},   {kD, "BDS DIRT", 95}};
// Style kits (DICE genres): kick ~116-120, snare / clap ~86-100, hats ~7-8 dB under the kick,
// perc under the snare; the harsh drums (HARD, NEURO, DARKSYN) a little lower.
constexpr FactoryDrum kHsDeep[] = {{kD, "BD909 S", 120}, {kD, "SD808 S", 90}, {kD, "CH909", 50}, {kD, "OH909", 54},
                                   {kD, "CP808", 94}, {kD, "RS909", 78}, {kF, "PCCARBON", 74}, {kD, "CB808", 66}};
constexpr FactoryDrum kHsTech[] = {{kD, "BD909", 118}, {kF, "SDBTIGHT", 92}, {kF, "HHSHORT", 50}, {kF, "HHLONG", 54},
                                   {kD, "CP909", 96}, {kD, "RS909", 78}, {kF, "CYALLOY", 52}, {kD, "TOM909", 76}};
constexpr FactoryDrum kHsAcid[] = {{kD, "BD909", 118}, {kD, "SD909", 94}, {kD, "CH808", 50}, {kD, "OH808", 54},
                                   {kD, "CP808", 96}, {kD, "CB808", 66}, {kD, "RS808", 76}, {kD, "TOM808", 76}};
constexpr FactoryDrum kHsDisco[] = {{kD, "BD909 S", 118}, {kF, "SDVBODY", 92}, {kF, "HHSHORT", 50}, {kF, "HHLONG", 56},
                                    {kF, "CPBODY", 94}, {kF, "PCCARBON", 76}, {kF, "PCMETAL", 64}, {kF, "PCCARBON", 78}};
constexpr FactoryDrum kHsProg[] = {{kF, "BDMODERN", 120}, {kF, "SDBTIGHT", 90}, {kD, "CH909", 50}, {kD, "OH909", 54},
                                   {kD, "CPX WIDE", 92}, {kF, "CYALLOY", 52}, {kD, "RSX", 74}, {kF, "PCCARBON", 72}};
constexpr FactoryDrum kTcMin[] = {{kF, "BDLONG", 118}, {kD, "RSX", 86}, {kF, "HHSHORT", 48}, {kF, "HHLONG", 52},
                                  {kD, "CPX", 88}, {kF, "PCSHORT", 72}, {kD, "RS909", 72}, {kF, "PCCARBON", 70}};
constexpr FactoryDrum kTcDub[] = {{kD, "BD909 S", 120}, {kD, "SD808 S", 86}, {kD, "HHR C", 48}, {kD, "HHR O", 52},
                                  {kD, "CP808", 88}, {kD, "RS909", 74}, {kD, "TOM808 L", 76}, {kD, "CY909", 46}};
constexpr FactoryDrum kTcPeak[] = {{kD, "BD909 H", 120}, {kD, "SD909", 94}, {kD, "CH909", 52}, {kD, "OH909", 56},
                                   {kD, "CP909", 98}, {kF, "CYALLOY", 54}, {kD, "TOM909 H", 76}, {kD, "RS909", 76}};
constexpr FactoryDrum kTcHard[] = {{kD, "BD DIST", 112}, {kD, "SD909 T", 94}, {kD, "CH909", 50}, {kD, "OH909", 54},
                                   {kD, "CLAP VRB", 90}, {kD, "METAL HT", 60}, {kD, "BD RUMBL", 100},
                                   {kD, "RS909", 72}};
constexpr FactoryDrum kTcAcid[] = {{kF, "BDPUNCH", 118}, {kF, "SDBTIGHT", 90}, {kD, "CH808", 50}, {kD, "OH808", 54},
                                   {kD, "CP909", 94}, {kD, "CB808", 64}, {kD, "RS808", 74}, {kD, "TOM808", 74}};
constexpr FactoryDrum kTcMelo[] = {{kF, "BDMODERN", 120}, {kF, "SDBTIGHT", 90}, {kF, "HHSHORT", 50}, {kF, "HHLONG", 54},
                                   {kD, "CPX WIDE", 92}, {kF, "CYALLOY", 50}, {kF, "PCCARBON", 72}, {kF, "PCCARBON", 74}};
constexpr FactoryDrum kTrUp[] = {{kD, "BD909", 120}, {kD, "SD909", 92}, {kD, "CH909", 52}, {kD, "OH909", 58},
                                 {kD, "CP909", 98}, {kD, "CY909", 50}, {kD, "TOM909", 76}, {kF, "CYALLOY", 50}};
constexpr FactoryDrum kTrFull[] = {{kF, "BDPUNCH", 120}, {kF, "SDVBODY", 92}, {kF, "HHSHORT", 52}, {kF, "HHLONG", 56},
                                   {kF, "CPBODY", 96}, {kF, "PCMETAL", 70}, {kF, "PCCARBON", 76}, {kF, "CYALLOY", 50}};
constexpr FactoryDrum kTrDark[] = {{kF, "BDLONG", 120}, {kF, "SDBTIGHT", 88}, {kF, "HHSHORT", 50}, {kF, "HHLONG", 54},
                                   {kF, "CPVINT", 90}, {kF, "PCMETAL", 68}, {kC, "METALBL", 58}, {kF, "PCSHORT", 68}};
constexpr FactoryDrum kTrGoa[] = {{kD, "BD909 H", 118}, {kD, "SDS FM", 90}, {kF, "HHSHORT", 50}, {kF, "HHLONG", 56},
                                  {kD, "CPX", 92}, {kF, "PCMETAL", 70}, {kF, "PCCARBON", 76}, {kF, "CYDARK", 54}};
constexpr FactoryDrum kTrProg[] = {{kD, "BD909 S", 120}, {kD, "SD909", 90}, {kD, "CH909", 50}, {kD, "OH909", 54},
                                   {kD, "CPX WIDE", 92}, {kF, "CYALLOY", 50}, {kD, "RS909", 74}, {kD, "TOM909", 74}};
constexpr FactoryDrum kDnLiq[] = {{kF, "BDMODERN", 118}, {kF, "SDBTIGHT", 96}, {kF, "HHSHORT", 52}, {kF, "HHLONG", 54},
                                  {kF, "CPVINT", 88}, {kF, "CYALLOY", 50}, {kF, "PCCARBON", 72}, {kD, "RS808", 72}};
constexpr FactoryDrum kDnJump[] = {{kF, "BDPUNCH", 120}, {kF, "SDVBODY", 100}, {kF, "HHSHORT", 52}, {kF, "HHLONG", 56},
                                   {kF, "CPBODY", 94}, {kF, "PCMETAL", 70}, {kD, "BD808 D", 100}, {kF, "PCCARBON", 74}};
constexpr FactoryDrum kDnNeuro[] = {{kD, "BDS DIRT", 116}, {kD, "SD NOISE", 92}, {kD, "HHR C", 50}, {kD, "HHR O", 54},
                                    {kD, "CLAP VRB", 88}, {kD, "METAL HT", 58}, {kF, "PCMETAL", 66}, {kD, "SDS FM", 80}};
constexpr FactoryDrum kDnJungle[] = {{kD, "BD DUSTY", 118}, {kD, "SD DUSTY", 100}, {kD, "HH DUSTY", 52},
                                     {kD, "OH909", 52}, {kD, "CP DUSTY", 90}, {kF, "CYALLOY", 50}, {kD, "BD808 L", 96},
                                     {kD, "RIM BIT", 74}};
constexpr FactoryDrum kDnHalf[] = {{kD, "BD808 D", 120}, {kF, "SDVBODY", 98}, {kF, "HHSHORT", 50}, {kF, "HHLONG", 54},
                                   {kF, "CPBODY", 92}, {kD, "RSX", 74}, {kD, "TOM808 L", 78}, {kF, "PCMETAL", 64}};
constexpr FactoryDrum kBrBeat[] = {{kD, "BDA", 118}, {kD, "SDA SNAP", 98}, {kD, "HHR C", 52}, {kD, "HHR O", 56},
                                   {kD, "CPX", 90}, {kD, "RSX", 74}, {kF, "PCCARBON", 74}, {kF, "CYALLOY", 50}};
constexpr FactoryDrum kBr2Step[] = {{kD, "BD909 S", 118}, {kF, "SDBTIGHT", 94}, {kF, "HHSHORT", 50}, {kF, "HHLONG", 54},
                                    {kD, "CP808", 92}, {kD, "RS808", 76}, {kF, "PCCARBON", 72}, {kF, "PCSHORT", 70}};
constexpr FactoryDrum kBrElec[] = {{kD, "BD808", 118}, {kD, "SD808", 96}, {kD, "CH808", 52}, {kD, "OH808", 56},
                                   {kD, "CP808", 96}, {kD, "CB808", 66}, {kD, "TOM808", 76}, {kF, "PCMETAL", 66}};
constexpr FactoryDrum kHhBoom[] = {{kD, "BD DUSTY", 120}, {kD, "SD DUSTY", 102}, {kD, "HH DUSTY", 52},
                                   {kD, "OH808", 50}, {kD, "CP DUSTY", 90}, {kD, "RIM BIT", 74}, {kD, "TOM808 L", 74},
                                   {kD, "CB808", 60}};
constexpr FactoryDrum kHhLofi[] = {{kD, "BDA BOOM", 118}, {kD, "SD DUSTY", 96}, {kD, "HH DUSTY", 50}, {kD, "HHR O", 50},
                                   {kD, "CP DUSTY", 88}, {kD, "RIM BIT", 72}, {kF, "PCSHORT", 66}, {kF, "PCCARBON", 68}};
constexpr FactoryDrum kHhTrap[] = {{kD, "BD808 L", 120}, {kD, "SD808 S", 94}, {kD, "CH808", 54}, {kD, "OH808", 52},
                                   {kD, "CP808", 100}, {kD, "RS808", 74}, {kD, "TOM808 L", 74}, {kD, "CB808", 62}};
constexpr FactoryDrum kIdBrain[] = {{kD, "BDS", 116}, {kD, "SDS FM", 92}, {kF, "HHSHORT", 50}, {kC, "NOIS OH", 52},
                                    {kD, "CPX", 88}, {kC, "BLIP", 70}, {kF, "PCMETAL", 66}, {kF, "PCSHORT", 68}};
constexpr FactoryDrum kIdGlitch[] = {{kF, "BDLONG", 116}, {kD, "RSX NOIS", 86}, {kC, "NOIS HH", 48},
                                     {kC, "AY HAT", 50}, {kF, "CPVINT", 86}, {kC, "BLIP", 68}, {kC, "SID NZ", 58},
                                     {kC, "METALBL", 58}};
constexpr FactoryDrum kIdElec[] = {{kD, "BDA", 116}, {kF, "SDBTIGHT", 92}, {kF, "HHSHORT", 50}, {kD, "HHR O", 54},
                                   {kD, "CPX WIDE", 88}, {kF, "PCSHORT", 70}, {kF, "PCCARBON", 72}, {kF, "CYDARK", 50}};
constexpr FactoryDrum kAmDream[] = {{kF, "BDLONG", 108}, {kF, "SDBTIGHT", 80}, {kF, "HHSHORT", 44}, {kF, "HHLONG", 48},
                                    {kF, "CPVINT", 78}, {kF, "PCSHORT", 62}, {kF, "PCCARBON", 64}, {kF, "CYDARK", 50}};
constexpr FactoryDrum kAmDark[] = {{kD, "BDA BOOM", 110}, {kD, "RSX", 78}, {kD, "HHR C", 44}, {kD, "CY808", 46},
                                   {kD, "CPX WIDE", 76}, {kD, "TOM808 L", 70}, {kF, "CYDARK", 52}, {kC, "METALBL", 50}};
constexpr FactoryDrum kAmDrone[] = {{kF, "BDLONG", 104}, {kD, "RS808", 74}, {kF, "HHSHORT", 42}, {kF, "CYALLOY", 46},
                                    {kF, "CPVINT", 72}, {kF, "CYDARK", 56}, {kF, "PCCARBON", 66}};
constexpr FactoryDrum kSwOut[] = {{kD, "BDA", 118}, {kD, "SDS", 100}, {kD, "CH808", 50}, {kD, "OH808", 54},
                                  {kD, "CPX WIDE", 96}, {kD, "TOM808", 78}, {kD, "CY808", 48}, {kD, "CB808", 58}};
constexpr FactoryDrum kSwDark[] = {{kD, "BDS DIRT", 116}, {kD, "SDS FM", 96}, {kD, "HHR C", 50}, {kD, "HHR O", 54},
                                   {kF, "CPBODY", 92}, {kD, "TOM909 H", 76}, {kD, "METAL HT", 56}, {kD, "CY909", 48}};
constexpr FactoryDrum kSwChill[] = {{kD, "BD808 S", 116}, {kD, "SD808 S", 92}, {kD, "CH808", 48}, {kD, "OH808", 52},
                                    {kD, "CP808", 92}, {kD, "RS808", 72}, {kF, "PCCARBON", 70}, {kD, "CY808", 44}};
void kitFill(I&) {}
#define KIT(cat, name, d) {InstrType::Kit, cat, name, kitFill, d, sizeof(d) / sizeof(d[0])}

constexpr FactoryPreset kTable[] = {
    KIT("DRUM", "808 KIT", k808),
    KIT("DRUM", "808 DEEP", k808Deep),
    KIT("DRUM", "909 KIT", k909),
    KIT("DRUM", "909 HARD", k909Hard),
    KIT("DRUM", "LOFI KIT", kLofi),
    KIT("DRUM", "HARD KIT", kHard),
    KIT("DRUM", "ANALOG", kAnalog),
    KIT("FM", "FM KIT", kFmKit),
    KIT("FM", "FM PUNCH", kFmPunch),
    KIT("FM", "FM PERC", kFmPerc),
    KIT("CHIP", "CHIP KIT", kChip),
    KIT("MIX", "HYBRID", kHybrid),
    KIT("MIX", "DUB KIT", kDub),
    KIT("HOUSE", "DEEP HSE", kHsDeep),
    KIT("HOUSE", "TECH HSE", kHsTech),
    KIT("HOUSE", "ACID HSE", kHsAcid),
    KIT("HOUSE", "DISCO", kHsDisco),
    KIT("HOUSE", "PROG HSE", kHsProg),
    KIT("TECHNO", "MINIMAL", kTcMin),
    KIT("TECHNO", "DUB TECH", kTcDub),
    KIT("TECHNO", "PEAK", kTcPeak),
    KIT("TECHNO", "HARD TEC", kTcHard),
    KIT("TECHNO", "ACID TEC", kTcAcid),
    KIT("TECHNO", "MELO TEC", kTcMelo),
    KIT("TRANCE", "UPLIFT", kTrUp),
    KIT("TRANCE", "FULLON", kTrFull),
    KIT("TRANCE", "DARK PSY", kTrDark),
    KIT("TRANCE", "GOA", kTrGoa),
    KIT("TRANCE", "PROG TRN", kTrProg),
    KIT("DNB", "LIQUID", kDnLiq),
    KIT("DNB", "JUMPUP", kDnJump),
    KIT("DNB", "NEURO", kDnNeuro),
    KIT("DNB", "JUNGLE", kDnJungle),
    KIT("DNB", "HALFTIME", kDnHalf),
    KIT("BREAKS", "BREAKBT", kBrBeat),
    KIT("BREAKS", "2STEP", kBr2Step),
    KIT("BREAKS", "ELECTRO", kBrElec),
    KIT("HIPHOP", "BOOMBAP", kHhBoom),
    KIT("HIPHOP", "LOFI HOP", kHhLofi),
    KIT("HIPHOP", "TRAP", kHhTrap),
    KIT("IDM", "BRAINDNC", kIdBrain),
    KIT("IDM", "GLITCH", kIdGlitch),
    KIT("IDM", "ELECTRON", kIdElec),
    KIT("AMBIENT", "DREAM", kAmDream),
    KIT("AMBIENT", "DARK AMB", kAmDark),
    KIT("AMBIENT", "DRONE", kAmDrone),
    KIT("SYNTHWAVE", "OUTRUN", kSwOut),
    KIT("SYNTHWAVE", "DARKSYN", kSwDark),
    KIT("SYNTHWAVE", "CHILLWV", kSwChill),
};

}  // namespace

const FactoryTable kFactoryKit = {kTable, static_cast<int>(sizeof(kTable) / sizeof(kTable[0]))};

}  // namespace mt
