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
constexpr FactoryDrum kFmKit[] = {{kF, "KICK", 115}, {kF, "SNARE", 100}, {kF, "HAT C", 85}, {kF, "HAT O", 80},
                                  {kF, "CLAP", 95},  {kF, "TOM", 95},    {kF, "COWBELL", 80}, {kF, "RIDE", 75}};
constexpr FactoryDrum kFmPunch[] = {{kF, "KICK PNC", 120}, {kF, "SNR FAT", 105}, {kF, "HAT C", 85},
                                    {kF, "HAT O", 80},     {kF, "CLAP BIG", 100}, {kF, "CONGA", 90},
                                    {kF, "RIDE", 75},      {kF, "ZAP", 85}};
constexpr FactoryDrum kFmPerc[] = {{kF, "KICK SUB", 115}, {kF, "SNR TGHT", 100}, {kF, "WOODBLK", 90},
                                   {kF, "CONGA", 95},     {kF, "TOM", 95},      {kF, "COWBELL", 85},
                                   {kF, "ZAP", 85},       {kF, "GONG", 75}};
constexpr FactoryDrum kChip[] = {{kC, "CHIPKICK", 115}, {kC, "NOIS SN", 100}, {kC, "NOIS HH", 85},
                                 {kC, "NOIS OH", 80},   {kC, "METALBL", 85},  {kC, "BLIP", 90}};
constexpr FactoryDrum kHybrid[] = {{kD, "BD909", 115}, {kD, "SD808", 100}, {kF, "HAT C", 85},  {kD, "OH909", 80},
                                   {kF, "CLAP", 95},   {kD, "CB808", 80},  {kF, "CONGA", 90}, {kD, "RIM BIT", 90}};
constexpr FactoryDrum kDub[] = {{kD, "BD909 S", 115}, {kD, "RS909", 95}, {kD, "CH909", 85},    {kD, "OH808", 80},
                                {kD, "CP808", 90},    {kD, "TOM808 L", 95}, {kD, "CY909", 75}, {kD, "CB808", 80}};
constexpr FactoryDrum kAnalog[] = {{kD, "BDA", 115}, {kD, "SDA", 100},  {kD, "HHR C", 85}, {kD, "HHR O", 80},
                                   {kD, "CPX", 95},  {kD, "RSX", 90},  {kD, "BDS", 110},   {kD, "BDS DIRT", 95}};
// Style kits (DICE genres): kick ~116-120, snare / clap ~86-100, hats ~7-8 dB under the kick,
// perc under the snare; the harsh drums (HARD, NEURO, DARKSYN) a little lower.
constexpr FactoryDrum kHsDeep[] = {{kD, "BD909 S", 120}, {kD, "SD808 S", 90}, {kD, "CH909", 50}, {kD, "OH909", 54},
                                   {kD, "CP808", 94}, {kD, "RS909", 78}, {kF, "CONGA", 74}, {kD, "CB808", 66}};
constexpr FactoryDrum kHsTech[] = {{kD, "BD909", 118}, {kF, "SNR TGHT", 92}, {kF, "HAT C", 50}, {kF, "HAT O", 54},
                                   {kD, "CP909", 96}, {kD, "RS909", 78}, {kF, "RIDE", 52}, {kD, "TOM909", 76}};
constexpr FactoryDrum kHsAcid[] = {{kD, "BD909", 118}, {kD, "SD909", 94}, {kD, "CH808", 50}, {kD, "OH808", 54},
                                   {kD, "CP808", 96}, {kD, "CB808", 66}, {kD, "RS808", 76}, {kD, "TOM808", 76}};
constexpr FactoryDrum kHsDisco[] = {{kD, "BD909 S", 118}, {kF, "SNR FAT", 92}, {kF, "HAT C", 50}, {kF, "HAT O", 56},
                                    {kF, "CLAP BIG", 94}, {kF, "CONGA", 76}, {kF, "COWBELL", 64}, {kF, "TOM", 78}};
constexpr FactoryDrum kHsProg[] = {{kF, "KICK", 120}, {kF, "SNR TGHT", 90}, {kD, "CH909", 50}, {kD, "OH909", 54},
                                   {kD, "CPX WIDE", 92}, {kF, "RIDE", 52}, {kD, "RSX", 74}, {kF, "CONGA", 72}};
constexpr FactoryDrum kTcMin[] = {{kF, "KICK SUB", 118}, {kD, "RSX", 86}, {kF, "HAT C", 48}, {kF, "HAT O", 52},
                                  {kD, "CPX", 88}, {kF, "WOODBLK", 72}, {kD, "RS909", 72}, {kF, "CONGA", 70}};
constexpr FactoryDrum kTcDub[] = {{kD, "BD909 S", 120}, {kD, "SD808 S", 86}, {kD, "HHR C", 48}, {kD, "HHR O", 52},
                                  {kD, "CP808", 88}, {kD, "RS909", 74}, {kD, "TOM808 L", 76}, {kD, "CY909", 46}};
constexpr FactoryDrum kTcPeak[] = {{kD, "BD909 H", 120}, {kD, "SD909", 94}, {kD, "CH909", 52}, {kD, "OH909", 56},
                                   {kD, "CP909", 98}, {kF, "RIDE", 54}, {kD, "TOM909 H", 76}, {kD, "RS909", 76}};
constexpr FactoryDrum kTcHard[] = {{kD, "BD DIST", 112}, {kD, "SD909 T", 94}, {kD, "CH909", 50}, {kD, "OH909", 54},
                                   {kD, "CLAP VRB", 90}, {kD, "METAL HT", 60}, {kD, "BD RUMBL", 100},
                                   {kD, "RS909", 72}};
constexpr FactoryDrum kTcAcid[] = {{kF, "KICK PNC", 118}, {kF, "SNR TGHT", 90}, {kD, "CH808", 50}, {kD, "OH808", 54},
                                   {kD, "CP909", 94}, {kD, "CB808", 64}, {kD, "RS808", 74}, {kD, "TOM808", 74}};
constexpr FactoryDrum kTcMelo[] = {{kF, "KICK", 120}, {kF, "SNR TGHT", 90}, {kF, "HAT C", 50}, {kF, "HAT O", 54},
                                   {kD, "CPX WIDE", 92}, {kF, "RIDE", 50}, {kF, "CONGA", 72}, {kF, "TOM", 74}};
constexpr FactoryDrum kTrUp[] = {{kD, "BD909", 120}, {kD, "SD909", 92}, {kD, "CH909", 52}, {kD, "OH909", 58},
                                 {kD, "CP909", 98}, {kD, "CY909", 50}, {kD, "TOM909", 76}, {kF, "RIDE", 50}};
constexpr FactoryDrum kTrFull[] = {{kF, "KICK PNC", 120}, {kF, "SNR FAT", 92}, {kF, "HAT C", 52}, {kF, "HAT O", 56},
                                   {kF, "CLAP BIG", 96}, {kF, "ZAP", 70}, {kF, "TOM", 76}, {kF, "RIDE", 50}};
constexpr FactoryDrum kTrDark[] = {{kF, "KICK SUB", 120}, {kF, "SNR TGHT", 88}, {kF, "HAT C", 50}, {kF, "HAT O", 54},
                                   {kF, "CLAP", 90}, {kF, "ZAP", 68}, {kC, "METALBL", 58}, {kF, "WOODBLK", 68}};
constexpr FactoryDrum kTrGoa[] = {{kD, "BD909 H", 118}, {kD, "SDS FM", 90}, {kF, "HAT C", 50}, {kF, "HAT O", 56},
                                  {kD, "CPX", 92}, {kF, "ZAP", 70}, {kF, "TOM", 76}, {kF, "GONG", 54}};
constexpr FactoryDrum kTrProg[] = {{kD, "BD909 S", 120}, {kD, "SD909", 90}, {kD, "CH909", 50}, {kD, "OH909", 54},
                                   {kD, "CPX WIDE", 92}, {kF, "RIDE", 50}, {kD, "RS909", 74}, {kD, "TOM909", 74}};
constexpr FactoryDrum kDnLiq[] = {{kF, "KICK", 118}, {kF, "SNR TGHT", 96}, {kF, "HAT C", 52}, {kF, "HAT O", 54},
                                  {kF, "CLAP", 88}, {kF, "RIDE", 50}, {kF, "CONGA", 72}, {kD, "RS808", 72}};
constexpr FactoryDrum kDnJump[] = {{kF, "KICK PNC", 120}, {kF, "SNR FAT", 100}, {kF, "HAT C", 52}, {kF, "HAT O", 56},
                                   {kF, "CLAP BIG", 94}, {kF, "ZAP", 70}, {kD, "BD808 D", 100}, {kF, "TOM", 74}};
constexpr FactoryDrum kDnNeuro[] = {{kD, "BDS DIRT", 116}, {kD, "SD NOISE", 92}, {kD, "HHR C", 50}, {kD, "HHR O", 54},
                                    {kD, "CLAP VRB", 88}, {kD, "METAL HT", 58}, {kF, "ZAP", 66}, {kD, "SDS FM", 80}};
constexpr FactoryDrum kDnJungle[] = {{kD, "BD DUSTY", 118}, {kD, "SD DUSTY", 100}, {kD, "HH DUSTY", 52},
                                     {kD, "OH909", 52}, {kD, "CP DUSTY", 90}, {kF, "RIDE", 50}, {kD, "BD808 L", 96},
                                     {kD, "RIM BIT", 74}};
constexpr FactoryDrum kDnHalf[] = {{kD, "BD808 D", 120}, {kF, "SNR FAT", 98}, {kF, "HAT C", 50}, {kF, "HAT O", 54},
                                   {kF, "CLAP BIG", 92}, {kD, "RSX", 74}, {kD, "TOM808 L", 78}, {kF, "ZAP", 64}};
constexpr FactoryDrum kBrBeat[] = {{kD, "BDA", 118}, {kD, "SDA SNAP", 98}, {kD, "HHR C", 52}, {kD, "HHR O", 56},
                                   {kD, "CPX", 90}, {kD, "RSX", 74}, {kF, "TOM", 74}, {kF, "RIDE", 50}};
constexpr FactoryDrum kBr2Step[] = {{kD, "BD909 S", 118}, {kF, "SNR TGHT", 94}, {kF, "HAT C", 50}, {kF, "HAT O", 54},
                                    {kD, "CP808", 92}, {kD, "RS808", 76}, {kF, "CONGA", 72}, {kF, "WOODBLK", 70}};
constexpr FactoryDrum kBrElec[] = {{kD, "BD808", 118}, {kD, "SD808", 96}, {kD, "CH808", 52}, {kD, "OH808", 56},
                                   {kD, "CP808", 96}, {kD, "CB808", 66}, {kD, "TOM808", 76}, {kF, "ZAP", 66}};
constexpr FactoryDrum kHhBoom[] = {{kD, "BD DUSTY", 120}, {kD, "SD DUSTY", 102}, {kD, "HH DUSTY", 52},
                                   {kD, "OH808", 50}, {kD, "CP DUSTY", 90}, {kD, "RIM BIT", 74}, {kD, "TOM808 L", 74},
                                   {kD, "CB808", 60}};
constexpr FactoryDrum kHhLofi[] = {{kD, "BDA BOOM", 118}, {kD, "SD DUSTY", 96}, {kD, "HH DUSTY", 50}, {kD, "HHR O", 50},
                                   {kD, "CP DUSTY", 88}, {kD, "RIM BIT", 72}, {kF, "WOODBLK", 66}, {kF, "CONGA", 68}};
constexpr FactoryDrum kHhTrap[] = {{kD, "BD808 L", 120}, {kD, "SD808 S", 94}, {kD, "CH808", 54}, {kD, "OH808", 52},
                                   {kD, "CP808", 100}, {kD, "RS808", 74}, {kD, "TOM808 L", 74}, {kD, "CB808", 62}};
constexpr FactoryDrum kIdBrain[] = {{kD, "BDS", 116}, {kD, "SDS FM", 92}, {kF, "HAT C", 50}, {kC, "NOIS OH", 52},
                                    {kD, "CPX", 88}, {kC, "BLIP", 70}, {kF, "ZAP", 66}, {kF, "WOODBLK", 68}};
constexpr FactoryDrum kIdGlitch[] = {{kF, "KICK SUB", 116}, {kD, "RSX NOIS", 86}, {kC, "NOIS HH", 48},
                                     {kC, "AY HAT", 50}, {kF, "CLAP", 86}, {kC, "BLIP", 68}, {kC, "SID NZ", 58},
                                     {kC, "METALBL", 58}};
constexpr FactoryDrum kIdElec[] = {{kD, "BDA", 116}, {kF, "SNR TGHT", 92}, {kF, "HAT C", 50}, {kD, "HHR O", 54},
                                   {kD, "CPX WIDE", 88}, {kF, "WOODBLK", 70}, {kF, "CONGA", 72}, {kF, "GONG", 50}};
constexpr FactoryDrum kAmDream[] = {{kF, "KICK SUB", 108}, {kF, "SNR TGHT", 80}, {kF, "HAT C", 44}, {kF, "HAT O", 48},
                                    {kF, "CLAP", 78}, {kF, "WOODBLK", 62}, {kF, "CONGA", 64}, {kF, "GONG", 50}};
constexpr FactoryDrum kAmDark[] = {{kD, "BDA BOOM", 110}, {kD, "RSX", 78}, {kD, "HHR C", 44}, {kD, "CY808", 46},
                                   {kD, "CPX WIDE", 76}, {kD, "TOM808 L", 70}, {kF, "GONG", 52}, {kC, "METALBL", 50}};
constexpr FactoryDrum kAmDrone[] = {{kF, "KICK SUB", 104}, {kD, "RS808", 74}, {kF, "HAT C", 42}, {kF, "RIDE", 46},
                                    {kF, "CLAP", 72}, {kF, "GONG", 56}, {kF, "TOM", 66}};
constexpr FactoryDrum kSwOut[] = {{kD, "BDA", 118}, {kD, "SDS", 100}, {kD, "CH808", 50}, {kD, "OH808", 54},
                                  {kD, "CPX WIDE", 96}, {kD, "TOM808", 78}, {kD, "CY808", 48}, {kD, "CB808", 58}};
constexpr FactoryDrum kSwDark[] = {{kD, "BDS DIRT", 116}, {kD, "SDS FM", 96}, {kD, "HHR C", 50}, {kD, "HHR O", 54},
                                   {kF, "CLAP BIG", 92}, {kD, "TOM909 H", 76}, {kD, "METAL HT", 56}, {kD, "CY909", 48}};
constexpr FactoryDrum kSwChill[] = {{kD, "BD808 S", 116}, {kD, "SD808 S", 92}, {kD, "CH808", 48}, {kD, "OH808", 52},
                                    {kD, "CP808", 92}, {kD, "RS808", 72}, {kF, "CONGA", 70}, {kD, "CY808", 44}};
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
