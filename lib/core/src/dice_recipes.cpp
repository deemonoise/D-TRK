#include "dice_recipes.h"
#include <string.h>
#include "arp_gen.h"

namespace mt {
namespace {

// ---- Shorthands ---------------------------------------------------------------------------------
#define END {nullptr, 0}
#define VEND {0, 0}
#define SEND {InstrType::Chip, nullptr, 0}
constexpr InstrType kSy = InstrType::Synth, kFm = InstrType::Fm, kCh = InstrType::Chip, kSt = InstrType::String;
constexpr uint8_t sc(ScaleType t) { return static_cast<uint8_t>(t); }
constexpr uint8_t am(ArpMode m) { return static_cast<uint8_t>(m); }
constexpr uint8_t ld(DiceLead l) { return static_cast<uint8_t>(l); }
constexpr uint8_t D = 1, B = 2, C = 4, L = 8, A = 16, F = 32;  // role bits (DiceRole order)
constexpr Resolution k16 = Resolution::Sixteenth, k8 = Resolution::Eighth;
constexpr ChordFlavor kTri = ChordFlavor::Triad, k7th = ChordFlavor::Seventh, k9th = ChordFlavor::Ninth,
                      kSus = ChordFlavor::Sus;

// ---- Scales -------------------------------------------------------------------------------------
constexpr DiceVal SC_MIN[] = {{sc(ScaleType::Minor), 8}, {sc(ScaleType::Dorian), 4}, {sc(ScaleType::Phrygian), 2}, VEND};
constexpr DiceVal SC_DOR[] = {{sc(ScaleType::Dorian), 7}, {sc(ScaleType::Minor), 5}, {sc(ScaleType::Mixolydian), 2}, VEND};
constexpr DiceVal SC_PHR[] = {{sc(ScaleType::Phrygian), 6}, {sc(ScaleType::Minor), 5}, {sc(ScaleType::HarmonicMinor), 3}, VEND};
constexpr DiceVal SC_HMI[] = {{sc(ScaleType::HarmonicMinor), 6}, {sc(ScaleType::Phrygian), 5}, {sc(ScaleType::Minor), 3}, VEND};
constexpr DiceVal SC_MAJ[] = {{sc(ScaleType::Major), 6}, {sc(ScaleType::Lydian), 4}, {sc(ScaleType::Mixolydian), 3}, VEND};
constexpr DiceVal SC_LYD[] = {{sc(ScaleType::Lydian), 6}, {sc(ScaleType::Major), 5}, {sc(ScaleType::Dorian), 3}, VEND};
constexpr DiceVal SC_DISCO[] = {{sc(ScaleType::Dorian), 6}, {sc(ScaleType::Minor), 5}, {sc(ScaleType::Major), 3}, VEND};
constexpr DiceVal SC_EPIC[] = {{sc(ScaleType::Minor), 8}, {sc(ScaleType::HarmonicMinor), 3}, {sc(ScaleType::Major), 2}, VEND};

// ---- Kits: drums per lane (kick, snare, clap, hat, open hat, perc) ----------------------------------
// Levels: kick and snare / clap carry the groove; hats 6..8 dB under the kick, perc below the snare.
// FM drums lead most lists; 808 / 909 where the style is built on them.
#define DEND {InstrType::Chip, nullptr, 0, 0, 0, 0}
constexpr InstrType kDr = InstrType::Drum;
// Deep house: round, soft click, dark hats.
constexpr DiceDrum DK_DEEP[] = {{kDr, "BD909 S", 120, 0, -10, 6}, {kFm, "KICK", 120, 0, -10, 5},
                                {kDr, "BD808", 118, -20, 0, 3}, {kFm, "KICK DEP", 120, 0, 0, 5},
                                {kDr, "BD DEEP", 120, 0, 0, 5}, {kFm, "KICK DUB", 118, 0, 0, 3},
                                {kDr, "BD 2STEP", 118, 0, 0, 2}, DEND};
constexpr DiceDrum DS_DEEP[] = {{kFm, "SNR TGHT", 94, 0, -10, 5}, {kDr, "SD909", 92, -10, -10, 4},
                                {kDr, "SD808 S", 92, 0, 0, 3}, {kDr, "SD HOUSE", 92, 0, 0, 3},
                                {kFm, "SNR HOUS", 92, 0, 0, 3}, {kFm, "SNR GHST", 90, 0, 0, 2}, DEND};
constexpr DiceDrum DC_DEEP[] = {{kDr, "CP808", 94, 0, 0, 6}, {kFm, "CLAP", 94, 0, 0, 5}, {kDr, "CP909", 90, 0, 0, 3},
                                {kDr, "CP DEEP", 94, 0, 0, 5}, {kFm, "CLAP WRM", 94, 0, 0, 4},
                                {kFm, "CLAP HSE", 92, 0, 0, 3}, {kFm, "CLAP DUB", 90, 0, 0, 2}, DEND};
constexpr DiceDrum DH_DEEP[] = {{kDr, "CH909", 50, -8, -10, 6}, {kFm, "HAT C", 50, 0, -10, 5},
                                {kDr, "CH808", 50, 0, 0, 3}, {kDr, "CH HOUSE", 50, 0, 0, 5},
                                {kFm, "HAT SOFT", 50, 0, 0, 4}, {kDr, "HH SOFT", 48, 0, 0, 2}, DEND};
constexpr DiceDrum DO_DEEP[] = {{kDr, "OH909", 54, -12, -10, 6}, {kFm, "HAT O", 54, -15, -10, 4},
                                {kDr, "OH HOUSE", 54, 0, 0, 5}, DEND};
constexpr DiceDrum DP_DEEP[] = {{kFm, "CONGA", 62, 0, 0, 6}, {kDr, "RS808", 58, 0, 0, 5},
                                {kFm, "WOODBLK", 56, 0, 0, 3}, {kDr, "RIM DEEP", 58, 0, 0, 4},
                                {kDr, "SHAKER", 56, 0, 0, 4}, {kFm, "SHKR SFT", 56, 0, 0, 3},
                                {kDr, "CONGA M", 60, 0, 0, 3}, {kDr, "CONGA H", 58, 0, 0, 3},
                                {kFm, "BONGO", 58, 0, 0, 3}, {kFm, "RIM SOFT", 56, 0, 0, 3},
                                {kFm, "CLAVE", 54, 0, 0, 2}, DEND};
// House / tech house: 909 and FM punch.
constexpr DiceDrum DK_HOUSE[] = {{kDr, "BD909", 120, 0, 0, 6}, {kFm, "KICK PNC", 120, 0, -10, 5},
                                 {kDr, "BDA", 118, 0, 0, 3}, {kFm, "KICK 909", 120, 0, 0, 5},
                                 {kDr, "BD HOUSE", 120, 0, 0, 5}, {kDr, "BD TECH", 120, 0, 0, 4},
                                 {kFm, "KICK TCH", 120, 0, 0, 4}, {kFm, "KICK MIN", 118, 0, 0, 2}, DEND};
constexpr DiceDrum DS_HOUSE[] = {{kDr, "SD909", 100, 0, 0, 5}, {kFm, "SNARE", 100, 0, 0, 4},
                                 {kDr, "SD HOUSE", 100, 0, 0, 4}, {kFm, "SNR HOUS", 100, 0, 0, 4}, DEND};
constexpr DiceDrum DC_HOUSE[] = {{kDr, "CP909", 98, 0, 0, 6}, {kFm, "CLAP BIG", 96, 0, 0, 5},
                                 {kDr, "CPX", 96, 0, 0, 3}, {kDr, "CP HOUSE", 98, 0, 0, 5},
                                 {kFm, "CLAP HSE", 96, 0, 0, 4}, {kFm, "CLAP 909", 96, 0, 0, 4},
                                 {kDr, "CP TECH", 96, 0, 0, 3}, {kFm, "CLAP TGT", 94, 0, 0, 2}, DEND};
constexpr DiceDrum DH_HOUSE[] = {{kDr, "CH909", 54, 0, 0, 6}, {kFm, "HAT C", 54, 0, 0, 5},
                                 {kDr, "HHR C", 52, 0, 0, 3}, {kDr, "CH HOUSE", 54, 0, 0, 5},
                                 {kDr, "CH TECH", 52, 0, 0, 4}, {kFm, "HAT TGHT", 52, 0, 0, 3}, DEND};
constexpr DiceDrum DO_HOUSE[] = {{kDr, "OH909", 56, -10, 0, 6}, {kFm, "HAT O", 56, -15, 0, 4},
                                 {kDr, "OH HOUSE", 56, 0, 0, 5}, {kFm, "HAT O LG", 54, 0, 0, 3}, DEND};
constexpr DiceDrum DP_HOUSE[] = {{kDr, "RS909", 58, 0, 0, 5}, {kFm, "CONGA", 62, 0, 0, 4},
                                 {kFm, "COWBELL", 50, 0, 0, 2}, {kFm, "SHAKER", 56, 0, 0, 4},
                                 {kDr, "SHAKER", 56, 0, 0, 4}, {kFm, "RIM", 56, 0, 0, 3},
                                 {kDr, "RIM UKG", 56, 0, 0, 3}, {kFm, "CLAVE", 54, 0, 0, 3},
                                 {kDr, "CONGA M", 60, 0, 0, 2}, DEND};
// Acid: 909 kick, 808 snare / clap, 909 hats.
constexpr DiceDrum DK_ACID[] = {{kDr, "BD909", 120, 0, 0, 6}, {kDr, "BD808", 120, -10, 0, 5},
                                {kFm, "KICK", 120, -5, 0, 4}, {kFm, "KICK 909", 120, 0, 0, 4},
                                {kDr, "BD PEAK", 118, 0, 0, 3}, {kDr, "BD HOUSE", 120, 0, 0, 3}, DEND};
constexpr DiceDrum DS_ACID[] = {{kDr, "SD808", 98, 0, 0, 5}, {kDr, "SD909", 98, 0, 0, 4},
                                {kDr, "SD HOUSE", 98, 0, 0, 3}, DEND};
constexpr DiceDrum DC_ACID[] = {{kDr, "CP808", 96, 0, 0, 6}, {kDr, "CP909", 96, 0, 0, 4},
                                {kFm, "CLAP 909", 96, 0, 0, 4}, {kDr, "CP TECH", 94, 0, 0, 3}, DEND};
constexpr DiceDrum DH_ACID[] = {{kDr, "CH909", 52, 0, 0, 6}, {kDr, "CH808", 52, 0, 0, 4},
                                {kDr, "CH TECH", 52, 0, 0, 4}, {kDr, "CH PSY", 52, 0, 0, 3}, DEND};
constexpr DiceDrum DO_ACID[] = {{kDr, "OH909", 56, -10, 0, 6}, {kDr, "OH808", 54, 0, 0, 4},
                                {kDr, "OH TECH", 54, 0, 0, 3}, DEND};
constexpr DiceDrum DP_ACID[] = {{kDr, "RS808", 56, 0, 0, 5}, {kDr, "CL808", 54, 0, 0, 3}, {kDr, "CB808", 50, 0, 0, 3},
                                {kDr, "PERC TEC", 54, 0, 0, 3}, {kDr, "RIM UKG", 54, 0, 0, 2}, DEND};
// Disco / nu disco: warm kick, 808 snare, congas.
constexpr DiceDrum DK_DISCO[] = {{kDr, "BD909 S", 118, 0, 0, 6}, {kFm, "KICK", 118, 0, 0, 5},
                                 {kDr, "BD DISCO", 118, 0, 0, 5}, {kFm, "KICK DEP", 118, 0, 0, 4},
                                 {kDr, "BD HOUSE", 118, 0, 0, 3}, DEND};
constexpr DiceDrum DS_DISCO[] = {{kDr, "SD808", 100, 0, 0, 5}, {kFm, "SNR FAT", 98, 0, 0, 4},
                                 {kDr, "SD HOUSE", 100, 0, 0, 4}, {kFm, "SNR HOUS", 98, 0, 0, 3}, DEND};
constexpr DiceDrum DC_DISCO[] = {{kDr, "CP808", 96, 0, 0, 6}, {kFm, "CLAP", 94, 0, 0, 5},
                                 {kFm, "CLAP WRM", 96, 0, 0, 4}, {kDr, "CP HOUSE", 96, 0, 0, 4}, DEND};
constexpr DiceDrum DH_DISCO[] = {{kDr, "CH808", 52, 0, 0, 6}, {kFm, "HAT C", 52, 0, 0, 5},
                                 {kDr, "CH HOUSE", 52, 0, 0, 4}, DEND};
constexpr DiceDrum DO_DISCO[] = {{kDr, "OH808", 54, -5, 0, 6}, {kDr, "OH909", 54, -10, 0, 4},
                                 {kDr, "OH HOUSE", 54, 0, 0, 4}, DEND};
constexpr DiceDrum DP_DISCO[] = {{kFm, "CONGA", 64, 0, 0, 6}, {kDr, "CB808", 50, 0, 0, 3},
                                 {kFm, "COWBELL", 50, 0, 0, 3}, {kDr, "CONGA M", 64, 0, 0, 5},
                                 {kDr, "TAMB", 50, 0, 0, 3}, {kDr, "CONGA H", 62, 0, 0, 4},
                                 {kFm, "BONGO", 62, 0, 0, 4}, {kDr, "CB DISCO", 50, 0, 0, 3},
                                 {kFm, "AGOGO", 52, 0, 0, 3}, {kFm, "TOM HI", 58, 0, 0, 2},
                                 {kFm, "TOM ELEC", 56, 0, 0, 2}, {kFm, "TRIANGL", 46, 0, 0, 2}, DEND};
// Techno (peak / melodic): tight FM punch, 909 top.
constexpr DiceDrum DK_TECHNO[] = {{kFm, "KICK PNC", 122, -10, 0, 6}, {kDr, "BD909", 120, -10, 10, 5},
                                  {kDr, "BDA", 120, 0, 0, 4}, {kFm, "KICK TCH", 122, 0, 0, 5},
                                  {kDr, "BD TECH", 120, 0, 0, 4}, {kDr, "BD PEAK", 120, 0, 0, 4},
                                  {kDr, "BD TRNC", 120, 0, 0, 2}, {kDr, "BD TAIL", 118, 0, 0, 2}, DEND};
constexpr DiceDrum DS_TECHNO[] = {{kDr, "SD909 T", 100, 0, 0, 5}, {kFm, "SNR TGHT", 100, 0, 0, 4},
                                  {kDr, "SD HTEK", 100, 0, 0, 3}, {kFm, "SNR HOUS", 98, 0, 0, 2}, DEND};
constexpr DiceDrum DC_TECHNO[] = {{kDr, "CP909", 96, 0, 0, 6}, {kFm, "CLAP BIG", 96, 0, 0, 5},
                                  {kDr, "CP TECH", 96, 0, 0, 5}, {kFm, "CLAP 909", 96, 0, 0, 3},
                                  {kFm, "CLAP HSE", 94, 0, 0, 2}, DEND};
constexpr DiceDrum DH_TECHNO[] = {{kFm, "HAT C", 52, 0, 0, 6}, {kDr, "CH909", 52, 0, 0, 5},
                                  {kDr, "HHR C", 50, 0, 0, 4}, {kDr, "CH TECH", 52, 0, 0, 5},
                                  {kFm, "HAT TGHT", 52, 0, 0, 4}, {kFm, "HAT MTL", 50, 0, 0, 2}, DEND};
constexpr DiceDrum DO_TECHNO[] = {{kDr, "OH909", 54, -12, 0, 5}, {kFm, "HAT O", 54, -15, 0, 4},
                                  {kDr, "OH TECH", 54, 0, 0, 4}, DEND};
constexpr DiceDrum DP_TECHNO[] = {{kDr, "RS909", 56, 0, 0, 5}, {kFm, "WOODBLK", 54, 0, 0, 3},
                                  {kFm, "ZAP", 50, 0, 0, 3}, {kDr, "PERC TEC", 54, 0, 0, 4},
                                  {kDr, "RIDE TEC", 46, 0, 0, 3}, {kFm, "RIM", 54, 0, 0, 3},
                                  {kFm, "SHAKER", 52, 0, 0, 2}, DEND};
// Minimal: short clicky FM.
constexpr DiceDrum DK_MIN[] = {{kFm, "KICK", 120, -15, 0, 6}, {kDr, "BDA", 118, -10, 0, 4},
                               {kFm, "KICK MIN", 120, 0, 0, 5}, {kDr, "BD MINI", 118, 0, 0, 5},
                               {kDr, "BD TECH", 118, 0, 0, 3}, {kFm, "KICK TCH", 118, 0, 0, 3}, DEND};
constexpr DiceDrum DS_MIN[] = {{kFm, "SNR TGHT", 96, 0, 0, 5}, {kDr, "SDA", 92, 0, 0, 3},
                               {kFm, "SNR HOUS", 94, 0, 0, 2}, DEND};
constexpr DiceDrum DC_MIN[] = {{kFm, "CLAP", 92, 0, 0, 6}, {kDr, "CPX", 90, 0, 0, 4}, {kFm, "CLAP TGT", 92, 0, 0, 5},
                               {kDr, "CP TECH", 90, 0, 0, 4}, {kDr, "CP HOUSE", 90, 0, 0, 2}, DEND};
constexpr DiceDrum DH_MIN[] = {{kFm, "HAT C", 50, -10, 0, 6}, {kDr, "HHR C", 48, 0, 0, 4},
                               {kFm, "HAT TGHT", 50, 0, 0, 5}, {kFm, "HAT MTL", 48, 0, 0, 4},
                               {kDr, "CH TECH", 48, 0, 0, 3}, DEND};
constexpr DiceDrum DO_MIN[] = {{kFm, "HAT O", 52, -20, 0, 5}, {kDr, "HHR O", 50, -10, 0, 4},
                               {kDr, "OH TECH", 52, 0, 0, 3}, DEND};
constexpr DiceDrum DP_MIN[] = {{kFm, "WOODBLK", 60, 0, 0, 6}, {kFm, "CONGA", 60, 0, 0, 4},
                               {kDr, "RSX NOIS", 54, 0, 0, 3}, {kFm, "RIM", 58, 0, 0, 5}, {kFm, "CLAVE", 56, 0, 0, 4},
                               {kFm, "CLICK", 52, 0, 0, 4}, {kDr, "TICK", 50, 0, 0, 3}, {kDr, "CLICK", 50, 0, 0, 3},
                               {kFm, "BONGO", 58, 0, 0, 3}, {kFm, "SHAKER", 54, 0, 0, 3},
                               {kDr, "RIM DUB", 54, 0, 0, 3}, {kDr, "CONGA H", 56, 0, 0, 2},
                               {kFm, "BLIP", 50, 0, 0, 2}, DEND};
// Dub techno: deep soft kick, rim for a snare, dark hats.
constexpr DiceDrum DK_DUB[] = {{kDr, "BD909 S", 118, 10, -10, 6}, {kFm, "KICK SUB", 118, 0, 0, 5},
                               {kFm, "KICK DUB", 118, 0, 0, 5}, {kDr, "BD DUB", 118, 0, 0, 5},
                               {kFm, "KICK DEP", 118, 0, 0, 3}, {kDr, "BD TAIL", 116, 0, 0, 2},
                               {kDr, "BD PULSE", 116, 0, 0, 2}, DEND};
constexpr DiceDrum DS_DUB[] = {{kFm, "SNR TGHT", 88, 0, -10, 5}, {kDr, "SD808 S", 88, 0, 0, 3},
                               {kFm, "SNR HOUS", 86, 0, 0, 2}, DEND};
constexpr DiceDrum DC_DUB[] = {{kDr, "CP808", 90, 0, 0, 6}, {kFm, "CLAP", 88, 0, 0, 4}, {kDr, "CP DUB", 90, 0, 0, 5},
                               {kFm, "CLAP DUB", 88, 0, 0, 4}, {kDr, "CP DEEP", 88, 0, 0, 3}, DEND};
constexpr DiceDrum DH_DUB[] = {{kDr, "CH909", 48, -10, -10, 6}, {kFm, "HAT C", 48, 0, -10, 4},
                               {kFm, "HAT SOFT", 48, 0, 0, 4}, {kDr, "CH TECH", 46, 0, 0, 3}, DEND};
constexpr DiceDrum DO_DUB[] = {{kDr, "OH808", 50, 0, 0, 6}, {kFm, "HAT O", 50, -10, -10, 4},
                               {kDr, "OH TECH", 50, 0, 0, 3}, DEND};
constexpr DiceDrum DP_DUB[] = {{kDr, "RS909", 54, 0, 0, 5}, {kDr, "TOM808 L", 56, 0, 0, 3},
                               {kDr, "CB808", 46, 0, 0, 3}, {kDr, "RIM DUB", 56, 0, 0, 5}, {kFm, "RIM", 54, 0, 0, 3},
                               {kDr, "RIM DEEP", 54, 0, 0, 3}, {kDr, "RIM AIR", 52, 0, 0, 3},
                               {kFm, "TOM LO", 56, 0, 0, 2}, DEND};
// Hard techno: punch with more click, louder top.
constexpr DiceDrum DK_HARD[] = {{kFm, "KICK PNC", 122, 0, 10, 6}, {kDr, "BD909", 122, -10, 15, 5},
                                {kDr, "BDS", 120, 0, 0, 4}, {kDr, "BD HTEK", 120, 0, 0, 4},
                                {kFm, "KICK HRD", 116, 0, 0, 4}, {kDr, "BD TAIL", 120, 0, 0, 3},
                                {kDr, "BD PEAK", 122, 0, 0, 3}, {kDr, "BD NEURO", 118, 0, 0, 1}, DEND};
constexpr DiceDrum DS_HARD[] = {{kDr, "SD909 T", 104, 0, 0, 5}, {kFm, "SNR FAT", 104, 0, 0, 4},
                                {kDr, "SD HTEK", 104, 0, 0, 5}, {kFm, "SNR NEUR", 100, 0, 0, 2}, DEND};
constexpr DiceDrum DC_HARD[] = {{kFm, "CLAP BIG", 100, 0, 0, 6}, {kDr, "CP909", 100, 0, 0, 5},
                                {kDr, "CP TECH", 100, 0, 0, 4}, {kFm, "CLAP 909", 98, 0, 0, 3}, DEND};
constexpr DiceDrum DH_HARD[] = {{kDr, "CH909", 56, 0, 0, 6}, {kFm, "HAT C", 56, 0, 0, 5},
                                {kDr, "CH TECH", 56, 0, 0, 5}, {kFm, "HAT TGHT", 56, 0, 0, 4},
                                {kFm, "HAT MTL", 54, 0, 0, 2}, DEND};
constexpr DiceDrum DO_HARD[] = {{kDr, "OH909", 56, -15, 0, 6}, {kFm, "HAT O", 56, -20, 0, 4},
                                {kDr, "OH TECH", 56, 0, 0, 4}, DEND};
constexpr DiceDrum DP_HARD[] = {{kDr, "RS909", 58, 0, 0, 5}, {kFm, "ZAP", 52, 0, 0, 3},
                                {kDr, "METAL HT", 50, 0, 0, 3}, {kDr, "PERC TEC", 56, 0, 0, 4},
                                {kDr, "RIDE TEC", 48, 0, 0, 3}, {kFm, "RIM", 56, 0, 0, 2}, DEND};
// Trance: big FM punch, 909 top, ride.
constexpr DiceDrum DK_TRANCE[] = {{kFm, "KICK PNC", 122, 0, 0, 6}, {kDr, "BD909", 122, 0, 10, 5},
                                  {kDr, "BD TRNC", 122, 0, 0, 5}, {kFm, "KICK 909", 122, 0, 0, 4},
                                  {kFm, "KICK TCH", 120, 0, 0, 2}, DEND};
constexpr DiceDrum DS_TRANCE[] = {{kDr, "SD909", 100, 0, 0, 5}, {kFm, "SNARE", 100, 0, 0, 4},
                                  {kDr, "SD TRNC", 100, 0, 0, 5}, {kFm, "SNR TRNC", 100, 0, 0, 4}, DEND};
constexpr DiceDrum DC_TRANCE[] = {{kDr, "CP909", 98, 0, 0, 6}, {kFm, "CLAP BIG", 98, 0, 0, 5},
                                  {kDr, "CP TRNC", 98, 0, 0, 5}, {kFm, "CLAP 909", 98, 0, 0, 4}, DEND};
constexpr DiceDrum DH_TRANCE[] = {{kDr, "CH909", 54, 0, 0, 6}, {kFm, "HAT C", 54, 0, 0, 5},
                                  {kDr, "CH TECH", 54, 0, 0, 3}, {kFm, "HAT TGHT", 52, 0, 0, 3}, DEND};
constexpr DiceDrum DO_TRANCE[] = {{kDr, "OH909", 56, -8, 0, 6}, {kFm, "HAT O", 56, -10, 0, 4},
                                  {kDr, "OH TRNC", 56, 0, 0, 5}, {kFm, "HAT O LG", 56, 0, 0, 4},
                                  {kDr, "OH TECH", 54, 0, 0, 3}, DEND};
constexpr DiceDrum DP_TRANCE[] = {{kFm, "RIDE", 48, 0, 0, 5}, {kDr, "RS909", 54, 0, 0, 4},
                                  {kDr, "CRASH SF", 44, 0, 0, 3}, {kFm, "SHAKER", 52, 0, 0, 3},
                                  {kFm, "CRASH", 40, 0, 0, 2}, {kDr, "RIDE TEC", 46, 0, 0, 2}, DEND};
// Psytrance / goa: short tight kick (room for the rolling bass), FM top.
constexpr DiceDrum DK_PSY[] = {{kFm, "KICK PNC", 122, -20, 10, 6}, {kFm, "KICK", 122, -20, 0, 5},
                               {kFm, "KICK PSY", 122, 0, 0, 6}, {kDr, "BD PSY", 122, 0, 0, 5},
                               {kDr, "BD GOA", 120, 0, 0, 4}, {kDr, "BD PEAK", 120, 0, 0, 2}, DEND};
constexpr DiceDrum DS_PSY[] = {{kFm, "SNR TGHT", 100, 0, 0, 5}, {kDr, "SD909 T", 100, 0, 0, 4},
                               {kFm, "SNR TRNC", 100, 0, 0, 3}, DEND};
constexpr DiceDrum DC_PSY[] = {{kFm, "CLAP", 96, 0, 0, 6}, {kDr, "CP909", 96, 0, 0, 4}, {kDr, "CP TRNC", 96, 0, 0, 4},
                               {kFm, "CLAP TGT", 94, 0, 0, 3}, DEND};
constexpr DiceDrum DH_PSY[] = {{kFm, "HAT C", 54, 0, 0, 6}, {kDr, "CH909", 54, 0, 0, 4}, {kDr, "CH PSY", 54, 0, 0, 5},
                               {kFm, "HAT TGHT", 54, 0, 0, 4}, DEND};
constexpr DiceDrum DO_PSY[] = {{kFm, "HAT O", 56, -15, 0, 6}, {kDr, "OH909", 56, -15, 0, 4},
                               {kDr, "OH TRNC", 56, 0, 0, 4}, DEND};
constexpr DiceDrum DP_PSY[] = {{kFm, "ZAP", 50, 0, 0, 5}, {kFm, "WOODBLK", 54, 0, 0, 4}, {kDr, "RSX", 54, 0, 0, 3},
                               {kFm, "TABLA", 56, 0, 0, 3}, {kFm, "AGOGO", 50, 0, 0, 2}, {kFm, "BLIP", 50, 0, 0, 2},
                               {kDr, "PERC TEC", 52, 0, 0, 3}, DEND};
// DnB: punchy kick, fat cracking snare.
constexpr DiceDrum DK_DNB[] = {{kFm, "KICK PNC", 120, -10, 0, 6}, {kDr, "BD909", 120, -15, 0, 4},
                               {kDr, "BDA", 120, 0, 0, 4}, {kFm, "KICK DNB", 120, 0, 0, 5},
                               {kDr, "BD DNB", 120, 0, 0, 5}, {kDr, "BD JUNG", 118, 0, 0, 2}, DEND};
constexpr DiceDrum DS_DNB[] = {{kFm, "SNR FAT", 108, 0, 0, 6}, {kDr, "SD909 T", 106, 0, 0, 5},
                               {kDr, "SDA SNAP", 106, 0, 0, 4}, {kFm, "SNR DNB", 106, 0, 0, 5},
                               {kDr, "SD DNB", 106, 0, 0, 5}, {kDr, "SD JUMP", 104, 0, 0, 4}, DEND};
constexpr DiceDrum DC_DNB[] = {{kFm, "CLAP BIG", 96, 0, 0, 6}, {kDr, "CP909", 94, 0, 0, 4},
                               {kFm, "CLAP 909", 94, 0, 0, 2}, DEND};
constexpr DiceDrum DH_DNB[] = {{kFm, "HAT C", 52, 0, 0, 6}, {kDr, "CH909", 52, 0, 0, 4}, {kDr, "CH DNB", 52, 0, 0, 5},
                               {kFm, "HAT TGHT", 50, 0, 0, 3}, DEND};
constexpr DiceDrum DO_DNB[] = {{kFm, "HAT O", 54, -10, 0, 5}, {kDr, "OH909", 54, -10, 0, 4},
                               {kDr, "OH TECH", 52, 0, 0, 2}, DEND};
constexpr DiceDrum DP_DNB[] = {{kFm, "RIDE", 46, 0, 0, 5}, {kDr, "RS909", 54, 0, 0, 4},
                               {kDr, "RIDE DNB", 44, 0, 0, 4}, {kDr, "RIDE TEC", 44, 0, 0, 2},
                               {kFm, "CRASH", 40, 0, 0, 2}, DEND};
// Liquid: softer kick and snare, ride, dusty tops.
constexpr DiceDrum DK_LIQUID[] = {{kFm, "KICK", 118, -5, -10, 6}, {kDr, "BD909 S", 118, 0, 0, 4},
                                  {kDr, "BD LIQ", 118, 0, 0, 5}, {kFm, "KICK DNB", 118, 0, 0, 4},
                                  {kFm, "KICK LOF", 116, 0, 0, 2}, DEND};
constexpr DiceDrum DS_LIQUID[] = {{kFm, "SNR TGHT", 100, 0, 0, 5}, {kDr, "SD808 S", 100, 0, 0, 4},
                                  {kDr, "SD DUSTY", 100, 0, 0, 3}, {kDr, "SD LIQ", 100, 0, 0, 5},
                                  {kFm, "SNR DNB", 98, 0, 0, 3}, {kDr, "SD AIR", 96, 0, 0, 2}, DEND};
constexpr DiceDrum DC_LIQUID[] = {{kFm, "CLAP", 92, 0, 0, 5}, {kDr, "CP808", 90, 0, 0, 4},
                                  {kFm, "CLAP WRM", 90, 0, 0, 3}, DEND};
constexpr DiceDrum DH_LIQUID[] = {{kFm, "HAT C", 48, 0, -10, 6}, {kDr, "HH DUSTY", 50, 0, 0, 4},
                                  {kDr, "CH DNB", 48, 0, 0, 4}, {kFm, "HAT SOFT", 48, 0, 0, 4},
                                  {kDr, "HH SOFT", 46, 0, 0, 2}, DEND};
constexpr DiceDrum DO_LIQUID[] = {{kFm, "HAT O", 50, -10, -10, 5}, {kDr, "OH808", 50, 0, 0, 4},
                                  {kDr, "OH HOUSE", 50, 0, 0, 2}, DEND};
constexpr DiceDrum DP_LIQUID[] = {{kFm, "RIDE", 44, 0, 0, 6}, {kDr, "RIM BIT", 50, 0, 0, 3},
                                  {kDr, "RIDE DNB", 44, 0, 0, 5}, {kFm, "RIM SOFT", 50, 0, 0, 3},
                                  {kFm, "SHKR SFT", 50, 0, 0, 3}, {kDr, "RIM AIR", 50, 0, 0, 2}, DEND};
// Jungle: dusty break kit.
constexpr DiceDrum DK_JUNGLE[] = {{kDr, "BD DUSTY", 118, 0, 0, 6}, {kFm, "KICK", 118, 0, 0, 5},
                                  {kDr, "BD JUNG", 118, 0, 0, 5}, {kFm, "KICK DNB", 118, 0, 0, 4},
                                  {kDr, "BD BREAK", 118, 0, 0, 3}, DEND};
constexpr DiceDrum DS_JUNGLE[] = {{kDr, "SD DUSTY", 106, 0, 0, 6}, {kFm, "SNR TGHT", 104, 0, 0, 5},
                                  {kDr, "SD JUNG", 106, 0, 0, 5}, {kFm, "SNR BRK", 104, 0, 0, 4},
                                  {kDr, "SD BREAK", 104, 0, 0, 3}, {kDr, "SD BRAIN", 100, 0, 0, 2}, DEND};
constexpr DiceDrum DC_JUNGLE[] = {{kDr, "CP DUSTY", 94, 0, 0, 6}, {kFm, "CLAP", 92, 0, 0, 4},
                                  {kFm, "CLAP HSE", 92, 0, 0, 2}, DEND};
constexpr DiceDrum DH_JUNGLE[] = {{kDr, "HH DUSTY", 52, 0, 0, 6}, {kFm, "HAT C", 50, 0, 0, 4},
                                  {kDr, "CH DNB", 52, 0, 0, 4}, DEND};
constexpr DiceDrum DO_JUNGLE[] = {{kDr, "OH909", 52, -10, 0, 5}, {kFm, "HAT O", 52, -10, 0, 4},
                                  {kDr, "OH TECH", 52, 0, 0, 2}, DEND};
constexpr DiceDrum DP_JUNGLE[] = {{kDr, "RIM BIT", 54, 0, 0, 5}, {kFm, "RIDE", 44, 0, 0, 4},
                                  {kDr, "RIDE DNB", 46, 0, 0, 4}, {kFm, "TOM LO", 56, 0, 0, 3},
                                  {kFm, "TOM HI", 54, 0, 0, 3}, DEND};
// Neuro / halftime: hard punch, fat snare.
constexpr DiceDrum DK_NEURO[] = {{kFm, "KICK PNC", 122, -10, 10, 6}, {kDr, "BDS", 120, 0, 0, 4},
                                 {kFm, "KICK DNB", 122, 0, 0, 4}, {kDr, "BD DNB", 122, 0, 0, 4},
                                 {kDr, "BD NEURO", 118, 0, 0, 2}, {kFm, "KICK TRP", 120, 0, 0, 2}, DEND};
constexpr DiceDrum DS_NEURO[] = {{kFm, "SNR FAT", 108, 0, 0, 6}, {kDr, "SD909 T", 106, 0, 0, 4},
                                 {kDr, "SD NEURO", 106, 0, 0, 3}, {kFm, "SNR NEUR", 104, 0, 0, 3},
                                 {kDr, "SD HALF", 106, 0, 0, 3}, {kDr, "SD DNB", 106, 0, 0, 3}, DEND};
constexpr DiceDrum DC_NEURO[] = {{kFm, "CLAP BIG", 98, 0, 0, 6}, {kDr, "CPX WIDE", 96, 0, 0, 3},
                                 {kFm, "CLAP TRP", 96, 0, 0, 3}, {kDr, "CP TECH", 96, 0, 0, 2}, DEND};
constexpr DiceDrum DH_NEURO[] = {{kFm, "HAT C", 54, 0, 0, 6}, {kDr, "CH909", 54, 0, 0, 4},
                                 {kDr, "CH DNB", 54, 0, 0, 4}, {kFm, "HAT TRAP", 52, 0, 0, 2}, DEND};
constexpr DiceDrum DO_NEURO[] = {{kFm, "HAT O", 54, -15, 0, 5}, {kDr, "OH909", 54, -15, 0, 4},
                                 {kDr, "OH TECH", 54, 0, 0, 2}, DEND};
constexpr DiceDrum DP_NEURO[] = {{kFm, "ZAP", 48, 0, 0, 5}, {kFm, "WOODBLK", 50, 0, 0, 3},
                                 {kDr, "PERC TEC", 48, 0, 0, 3}, {kFm, "BLIP", 46, 0, 0, 2}, DEND};
// Breakbeat: FM punch and 808.
constexpr DiceDrum DK_BREAKS[] = {{kFm, "KICK PNC", 120, 0, 0, 6}, {kDr, "BD909", 120, 0, 0, 5},
                                  {kDr, "BD808", 120, -10, 0, 3}, {kDr, "BD BREAK", 120, 0, 0, 5},
                                  {kFm, "KICK DNB", 120, 0, 0, 3}, {kDr, "BD JUNG", 118, 0, 0, 2}, DEND};
constexpr DiceDrum DS_BREAKS[] = {{kFm, "SNR FAT", 106, 0, 0, 6}, {kDr, "SD808", 104, 0, 0, 5},
                                  {kDr, "SDA SNAP", 104, 0, 0, 3}, {kDr, "SD BREAK", 106, 0, 0, 5},
                                  {kFm, "SNR BRK", 106, 0, 0, 5}, {kDr, "SD JUNG", 104, 0, 0, 3}, DEND};
constexpr DiceDrum DC_BREAKS[] = {{kFm, "CLAP", 94, 0, 0, 6}, {kDr, "CP808", 92, 0, 0, 4},
                                  {kDr, "CP HOUSE", 94, 0, 0, 3}, DEND};
constexpr DiceDrum DH_BREAKS[] = {{kFm, "HAT C", 52, 0, 0, 6}, {kDr, "CH808", 52, 0, 0, 4},
                                  {kDr, "CH DNB", 52, 0, 0, 3}, DEND};
constexpr DiceDrum DO_BREAKS[] = {{kDr, "OH909", 54, -10, 0, 5}, {kFm, "HAT O", 54, -10, 0, 4},
                                  {kDr, "OH HOUSE", 54, 0, 0, 3}, DEND};
constexpr DiceDrum DP_BREAKS[] = {{kFm, "CONGA", 60, 0, 0, 5}, {kDr, "CB808", 48, 0, 0, 4},
                                  {kFm, "TOM LO", 58, 0, 0, 3}, {kFm, "TOM HI", 58, 0, 0, 3},
                                  {kDr, "CRASH SF", 42, 0, 0, 2}, {kDr, "CB DISCO", 50, 0, 0, 2},
                                  {kDr, "TAMB", 50, 0, 0, 2}, DEND};
// UK garage: soft kick, tight snare, shuffled tops.
constexpr DiceDrum DK_UKG[] = {{kFm, "KICK", 118, -5, 0, 6}, {kDr, "BD909 S", 118, 0, 0, 5},
                               {kDr, "BD 2STEP", 118, 0, 0, 5}, {kFm, "KICK 909", 118, 0, 0, 3},
                               {kDr, "BD DEEP", 118, 0, 0, 3}, DEND};
constexpr DiceDrum DS_UKG[] = {{kFm, "SNR TGHT", 100, 0, 0, 6}, {kDr, "SD909", 98, 0, 0, 4},
                               {kDr, "SD 2STEP", 100, 0, 0, 5}, {kFm, "SNR GHST", 96, 0, 0, 2}, DEND};
constexpr DiceDrum DC_UKG[] = {{kFm, "CLAP", 94, 0, 0, 6}, {kDr, "CP909", 92, 0, 0, 4},
                               {kFm, "CLAP HSE", 94, 0, 0, 4}, {kDr, "CP HOUSE", 94, 0, 0, 3}, DEND};
constexpr DiceDrum DH_UKG[] = {{kFm, "HAT C", 50, 0, 0, 6}, {kDr, "CH909", 50, 0, 0, 4},
                               {kDr, "CH HOUSE", 50, 0, 0, 4}, {kFm, "HAT TRAP", 48, 0, 0, 3}, DEND};
constexpr DiceDrum DO_UKG[] = {{kFm, "HAT O", 52, -15, 0, 5}, {kDr, "OH909", 52, -15, 0, 4},
                               {kDr, "OH HOUSE", 52, 0, 0, 4}, DEND};
constexpr DiceDrum DP_UKG[] = {{kDr, "RS909", 56, 0, 0, 5}, {kFm, "WOODBLK", 54, 0, 0, 4},
                               {kFm, "CONGA", 58, 0, 0, 3}, {kDr, "RIM UKG", 56, 0, 0, 5}, {kFm, "RIM", 56, 0, 0, 4},
                               {kFm, "SHAKER", 54, 0, 0, 3}, {kDr, "SHAKER", 54, 0, 0, 3}, DEND};
// Electro: 808.
constexpr DiceDrum DK_ELECTRO[] = {{kDr, "BD808", 120, -10, 0, 6}, {kFm, "KICK", 120, 0, 0, 5},
                                   {kDr, "BD ELECT", 120, 0, 0, 6}, {kFm, "KICK ELC", 120, 0, 0, 5}, DEND};
constexpr DiceDrum DS_ELECTRO[] = {{kDr, "SD808", 100, 0, 0, 6}, {kFm, "SNARE", 100, 0, 0, 4},
                                   {kDr, "SD ELECT", 100, 0, 0, 5}, {kFm, "SNR ELEC", 100, 0, 0, 5}, DEND};
constexpr DiceDrum DC_ELECTRO[] = {{kDr, "CP808", 98, 0, 0, 6}, {kFm, "CLAP", 96, 0, 0, 4},
                                   {kDr, "CP ELECT", 98, 0, 0, 5}, DEND};
constexpr DiceDrum DH_ELECTRO[] = {{kDr, "CH808", 52, 0, 0, 6}, {kFm, "HAT C", 52, 0, 0, 4},
                                   {kFm, "HAT MTL", 50, 0, 0, 3}, DEND};
constexpr DiceDrum DO_ELECTRO[] = {{kDr, "OH808", 54, 0, 0, 6}, {kFm, "HAT O", 54, -10, 0, 3},
                                   {kDr, "OH TECH", 52, 0, 0, 2}, DEND};
constexpr DiceDrum DP_ELECTRO[] = {{kDr, "CL808", 54, 0, 0, 5}, {kDr, "CB808", 50, 0, 0, 4},
                                   {kFm, "ZAP", 48, 0, 0, 3}, {kDr, "CL ELECT", 54, 0, 0, 4},
                                   {kDr, "CB ELECT", 50, 0, 0, 4}, {kDr, "TOM ELEC", 56, 0, 0, 4},
                                   {kFm, "TOM ELEC", 56, 0, 0, 3}, {kFm, "CLAVE", 52, 0, 0, 3},
                                   {kDr, "TOM ZAP", 52, 0, 0, 2}, DEND};
// Boom bap: dusty, fat snare.
constexpr DiceDrum DK_BOOM[] = {{kDr, "BD DUSTY", 120, 0, 0, 6}, {kFm, "KICK SUB", 118, 0, 0, 4},
                                {kDr, "BD808", 118, -20, 0, 3}, {kDr, "BD BOOM", 120, 0, 0, 6},
                                {kFm, "KICK BOM", 120, 0, 0, 5}, {kDr, "BD BREAK", 118, 0, 0, 3}, DEND};
constexpr DiceDrum DS_BOOM[] = {{kDr, "SD DUSTY", 108, 0, 0, 6}, {kFm, "SNR FAT", 106, 0, 0, 5},
                                {kDr, "SD BOOM", 108, 0, 0, 6}, {kFm, "SNR BOOM", 106, 0, 0, 5},
                                {kFm, "SNR GHST", 100, 0, 0, 2}, DEND};
constexpr DiceDrum DC_BOOM[] = {{kDr, "CP DUSTY", 96, 0, 0, 6}, {kFm, "CLAP", 94, 0, 0, 4},
                                {kDr, "CP ELECT", 94, 0, 0, 2}, DEND};
constexpr DiceDrum DH_BOOM[] = {{kDr, "HH DUSTY", 52, 0, 0, 6}, {kFm, "HAT C", 50, 0, 0, 4},
                                {kDr, "HH LOFI", 52, 0, 0, 5}, {kFm, "HAT DUST", 50, 0, 0, 4}, DEND};
constexpr DiceDrum DO_BOOM[] = {{kDr, "OH808", 50, -10, 0, 5}, {kFm, "HAT O", 50, -15, 0, 4},
                                {kDr, "OH TRAP", 48, 0, 0, 2}, DEND};
constexpr DiceDrum DP_BOOM[] = {{kDr, "RIM BIT", 54, 0, 0, 5}, {kFm, "WOODBLK", 52, 0, 0, 4},
                                {kFm, "TOM LO", 56, 0, 0, 3}, {kFm, "RIM SOFT", 52, 0, 0, 3},
                                {kDr, "RIM LOFI", 52, 0, 0, 3}, DEND};
// Lo-fi: duller, quieter tops.
constexpr DiceDrum DK_LOFI[] = {{kDr, "BD DUSTY", 116, 0, -10, 6}, {kFm, "KICK SUB", 116, 0, 0, 4},
                                {kDr, "BD LOFI", 116, 0, 0, 6}, {kFm, "KICK LOF", 116, 0, 0, 5},
                                {kDr, "BD SOFT", 114, 0, 0, 3}, {kFm, "KICK BOM", 114, 0, 0, 3}, DEND};
constexpr DiceDrum DS_LOFI[] = {{kDr, "SD DUSTY", 100, -5, -10, 6}, {kFm, "SNR TGHT", 96, 0, -10, 4},
                                {kDr, "SD LOFI", 100, 0, 0, 6}, {kFm, "SNR LOFI", 100, 0, 0, 5},
                                {kDr, "SD LIQ", 96, 0, 0, 2}, {kDr, "SD CHILL", 96, 0, 0, 3}, DEND};
constexpr DiceDrum DC_LOFI[] = {{kDr, "CP DUSTY", 92, 0, 0, 6}, {kFm, "CLAP", 90, 0, 0, 4},
                                {kFm, "CLAP WRM", 90, 0, 0, 4}, DEND};
constexpr DiceDrum DH_LOFI[] = {{kDr, "HH DUSTY", 46, 0, -10, 6}, {kFm, "HAT C", 44, 0, -15, 4},
                                {kDr, "HH LOFI", 46, 0, 0, 5}, {kFm, "HAT DUST", 46, 0, 0, 4},
                                {kFm, "HAT SOFT", 44, 0, 0, 3}, DEND};
constexpr DiceDrum DO_LOFI[] = {{kFm, "HAT O", 46, -15, -10, 5}, {kDr, "OH808", 46, 0, 0, 4},
                                {kDr, "OH HOUSE", 44, 0, 0, 2}, DEND};
constexpr DiceDrum DP_LOFI[] = {{kDr, "RIM BIT", 50, 0, 0, 5}, {kFm, "WOODBLK", 50, 0, 0, 4},
                                {kDr, "RIM LOFI", 50, 0, 0, 5}, {kFm, "RIM SOFT", 50, 0, 0, 4},
                                {kFm, "SHKR SFT", 48, 0, 0, 3}, {kDr, "SHAKER", 48, 0, 0, 2}, DEND};
// Trap: punchy kick (the 808 is the bass), crisp snare / clap, rolling hats.
constexpr DiceDrum DK_TRAP[] = {{kFm, "KICK PNC", 120, -10, 0, 6}, {kDr, "BD808", 120, 0, 0, 5},
                                {kFm, "KICK TRP", 120, 0, 0, 5}, {kFm, "KICK 909", 118, 0, 0, 2}, DEND};
constexpr DiceDrum DS_TRAP[] = {{kFm, "SNR FAT", 104, 0, 0, 6}, {kDr, "SD808 S", 102, 0, 0, 5},
                                {kDr, "SD TRAP", 104, 0, 0, 5}, {kFm, "SNR TRAP", 104, 0, 0, 5},
                                {kDr, "SD HALF", 102, 0, 0, 3}, DEND};
constexpr DiceDrum DC_TRAP[] = {{kDr, "CP808", 100, 0, 0, 6}, {kFm, "CLAP BIG", 98, 0, 0, 5},
                                {kDr, "CP TRAP", 100, 0, 0, 5}, {kFm, "CLAP TRP", 98, 0, 0, 4}, DEND};
constexpr DiceDrum DH_TRAP[] = {{kDr, "CH808", 50, 0, 0, 6}, {kFm, "HAT C", 50, 0, 0, 5},
                                {kDr, "HH TRAP", 50, 0, 0, 5}, {kFm, "HAT TRAP", 50, 0, 0, 5}, DEND};
constexpr DiceDrum DO_TRAP[] = {{kDr, "OH808", 52, 0, 0, 6}, {kFm, "HAT O", 52, -10, 0, 4},
                                {kDr, "OH TRAP", 52, 0, 0, 5}, DEND};
constexpr DiceDrum DP_TRAP[] = {{kDr, "RS808", 54, 0, 0, 5}, {kDr, "CB808", 46, 0, 0, 3},
                                {kDr, "CB ELECT", 46, 0, 0, 3}, {kFm, "RIM", 52, 0, 0, 2}, DEND};
// IDM: FM, analog models, chip.
constexpr DiceDrum DK_IDM[] = {{kFm, "KICK", 118, -10, 10, 6}, {kDr, "BDA", 118, 0, 0, 4},
                               {kCh, "CHIPKICK", 116, 0, 0, 3}, {kDr, "BD IDM", 118, 0, 0, 5},
                               {kDr, "BD GLTCH", 116, 0, 0, 4}, {kFm, "KICK ELC", 118, 0, 0, 3},
                               {kFm, "KICK MIN", 116, 0, 0, 3}, DEND};
constexpr DiceDrum DS_IDM[] = {{kFm, "SNR TGHT", 100, 0, 0, 6}, {kDr, "SDS FM", 100, 0, 0, 4},
                               {kCh, "NOIS SN", 96, 0, 0, 3}, {kDr, "SD BRAIN", 100, 0, 0, 4},
                               {kDr, "SD GLTCH", 96, 0, 0, 4}, {kFm, "SNR ELEC", 98, 0, 0, 3}, DEND};
constexpr DiceDrum DC_IDM[] = {{kFm, "CLAP", 92, 0, 0, 5}, {kDr, "CPX WIDE", 92, 0, 0, 4},
                               {kFm, "CLAP TGT", 90, 0, 0, 4}, DEND};
constexpr DiceDrum DH_IDM[] = {{kFm, "HAT C", 50, 0, 0, 6}, {kDr, "HHR C", 48, 0, 0, 4},
                               {kCh, "NOIS HH", 46, 0, 0, 3}, {kDr, "HH GLTCH", 48, 0, 0, 5},
                               {kFm, "HAT MTL", 48, 0, 0, 4}, DEND};
constexpr DiceDrum DO_IDM[] = {{kFm, "HAT O", 50, -15, 0, 5}, {kDr, "HHR O", 50, 0, 0, 4},
                               {kDr, "OH TECH", 50, 0, 0, 2}, DEND};
constexpr DiceDrum DP_IDM[] = {{kFm, "WOODBLK", 56, 0, 0, 5}, {kFm, "ZAP", 50, 0, 0, 4}, {kCh, "BLIP", 50, 0, 0, 4},
                               {kDr, "BLIP", 54, 0, 0, 4}, {kDr, "TICK", 52, 0, 0, 4}, {kDr, "CLICK", 52, 0, 0, 3},
                               {kFm, "BLIP", 52, 0, 0, 3}, {kFm, "CLICK", 50, 0, 0, 3}, {kDr, "TOM ZAP", 52, 0, 0, 3},
                               {kFm, "TABLA", 54, 0, 0, 2}, DEND};
// Ambient: soft, long, low.
constexpr DiceDrum DK_AMB[] = {{kFm, "KICK SUB", 108, 10, -15, 6}, {kDr, "BD909 S", 106, 0, -10, 4},
                               {kDr, "BD SOFT", 106, 0, 0, 5}, {kDr, "BD PULSE", 106, 0, 0, 4},
                               {kFm, "KICK LOF", 106, 0, 0, 4}, {kFm, "KICK DUB", 106, 0, 0, 3}, DEND};
constexpr DiceDrum DS_AMB[] = {{kFm, "SNR TGHT", 84, 0, -10, 5}, {kDr, "SD808 S", 82, 0, 0, 4},
                               {kDr, "SD AIR", 82, 0, 0, 5}, {kFm, "SNR LOFI", 82, 0, 0, 3}, DEND};
constexpr DiceDrum DC_AMB[] = {{kFm, "CLAP", 80, 0, 0, 5}, {kDr, "CP808", 78, 0, 0, 4}, {kDr, "CP DEEP", 80, 0, 0, 4},
                               {kFm, "CLAP DUB", 78, 0, 0, 3}, DEND};
constexpr DiceDrum DH_AMB[] = {{kFm, "HAT C", 42, 0, -15, 6}, {kDr, "CH808", 42, 0, 0, 4},
                               {kDr, "HH SOFT", 42, 0, 0, 5}, {kFm, "HAT SOFT", 42, 0, 0, 4}, DEND};
constexpr DiceDrum DO_AMB[] = {{kFm, "HAT O", 44, 0, -10, 5}, {kDr, "OH808", 44, 0, 0, 4},
                               {kDr, "OH HOUSE", 42, 0, 0, 2}, DEND};
constexpr DiceDrum DP_AMB[] = {{kFm, "WOODBLK", 52, 0, 0, 5}, {kFm, "CONGA", 52, 0, 0, 4}, {kFm, "GONG", 46, 0, 0, 3},
                               {kDr, "RIM AIR", 50, 0, 0, 5}, {kDr, "CB GLASS", 46, 0, 0, 4},
                               {kDr, "CY WASH", 40, 0, 0, 3}, {kDr, "TOM AMB", 50, 0, 0, 3},
                               {kFm, "TRIANGL", 42, 0, 0, 2}, {kFm, "RIM SOFT", 48, 0, 0, 3}, DEND};
// Outrun: big snare, 909 top, toms.
constexpr DiceDrum DK_SW[] = {{kFm, "KICK", 120, 0, 0, 6}, {kDr, "BD909", 120, 0, 0, 5},
                              {kDr, "BD OUTRN", 120, 0, 0, 5}, {kFm, "KICK ELC", 120, 0, 0, 4},
                              {kDr, "BD HOUSE", 118, 0, 0, 2}, DEND};
constexpr DiceDrum DS_SW[] = {{kDr, "SD909", 104, 10, 0, 6}, {kFm, "SNR FAT", 106, 0, 0, 5},
                              {kDr, "SD GATE", 104, 0, 0, 5}, {kFm, "SNR TRNC", 100, 0, 0, 3},
                              {kFm, "SNR ELEC", 100, 0, 0, 3}, DEND};
constexpr DiceDrum DC_SW[] = {{kDr, "CP909", 96, 0, 0, 6}, {kFm, "CLAP BIG", 96, 0, 0, 5},
                              {kDr, "CP 80S", 96, 0, 0, 5}, DEND};
constexpr DiceDrum DH_SW[] = {{kDr, "CH909", 50, 0, 0, 6}, {kFm, "HAT C", 50, 0, 0, 5}, {kDr, "HH 80S", 50, 0, 0, 5},
                              DEND};
constexpr DiceDrum DO_SW[] = {{kDr, "OH909", 52, -5, 0, 6}, {kFm, "HAT O", 52, -10, 0, 4},
                              {kFm, "HAT O LG", 52, 0, 0, 3}, DEND};
constexpr DiceDrum DP_SW[] = {{kFm, "TOM", 58, 0, 0, 5}, {kDr, "TOM909", 56, 0, 0, 4}, {kDr, "TOM 80S", 58, 0, 0, 5},
                              {kFm, "TOM ELEC", 56, 0, 0, 4}, {kDr, "TOM ELEC", 56, 0, 0, 3},
                              {kDr, "CRASH SF", 42, 0, 0, 2}, DEND};
// Darksynth: hard punch.
constexpr DiceDrum DK_DARKSW[] = {{kFm, "KICK PNC", 122, 0, 0, 6}, {kDr, "BD909", 122, -10, 10, 5},
                                  {kDr, "BD DARKS", 120, 0, 0, 5}, {kFm, "KICK ELC", 120, 0, 0, 3},
                                  {kFm, "KICK HRD", 114, 0, 0, 2}, DEND};
constexpr DiceDrum DS_DARKSW[] = {{kFm, "SNR FAT", 106, 0, 0, 6}, {kDr, "SD909 T", 104, 0, 0, 5},
                                  {kDr, "SD GATE", 104, 0, 0, 5}, {kFm, "SNR ELEC", 104, 0, 0, 3}, DEND};
constexpr DiceDrum DC_DARKSW[] = {{kFm, "CLAP BIG", 98, 0, 0, 6}, {kDr, "CP909", 96, 0, 0, 4},
                                  {kDr, "CP 80S", 96, 0, 0, 5}, DEND};
constexpr DiceDrum DH_DARKSW[] = {{kFm, "HAT C", 54, 0, 0, 6}, {kDr, "CH909", 54, 0, 0, 4},
                                  {kDr, "HH 80S", 52, 0, 0, 4}, {kFm, "HAT MTL", 52, 0, 0, 2}, DEND};
constexpr DiceDrum DO_DARKSW[] = {{kFm, "HAT O", 54, -15, 0, 5}, {kDr, "OH909", 54, -10, 0, 4},
                                  {kDr, "OH TECH", 52, 0, 0, 2}, DEND};
constexpr DiceDrum DP_DARKSW[] = {{kFm, "TOM", 56, 0, 0, 5}, {kFm, "ZAP", 48, 0, 0, 3}, {kDr, "TOM 80S", 56, 0, 0, 4},
                                  {kFm, "TOM ELEC", 54, 0, 0, 4}, {kDr, "PERC TEC", 50, 0, 0, 2}, DEND};
// Chillwave: soft, warm, quiet tops.
constexpr DiceDrum DK_CHILL[] = {{kFm, "KICK", 114, 0, -10, 6}, {kDr, "BD808", 114, 0, 0, 5},
                                 {kFm, "KICK LOF", 114, 0, 0, 5}, {kDr, "BD SOFT", 112, 0, 0, 4},
                                 {kDr, "BD LOFI", 112, 0, 0, 3}, {kFm, "KICK BOM", 112, 0, 0, 2}, DEND};
constexpr DiceDrum DS_CHILL[] = {{kFm, "SNR TGHT", 96, 0, 0, 6}, {kDr, "SD808 S", 96, 0, 0, 5},
                                 {kDr, "SD CHILL", 96, 0, 0, 5}, {kFm, "SNR LOFI", 94, 0, 0, 4}, DEND};
constexpr DiceDrum DC_CHILL[] = {{kDr, "CP808", 92, 0, 0, 6}, {kFm, "CLAP", 90, 0, 0, 5},
                                 {kFm, "CLAP WRM", 90, 0, 0, 4}, DEND};
constexpr DiceDrum DH_CHILL[] = {{kFm, "HAT C", 46, 0, -10, 6}, {kDr, "CH808", 46, 0, 0, 5},
                                 {kDr, "HH 80S", 46, 0, 0, 4}, {kDr, "HH SOFT", 44, 0, 0, 3},
                                 {kFm, "HAT DUST", 44, 0, 0, 3}, DEND};
constexpr DiceDrum DO_CHILL[] = {{kFm, "HAT O", 48, -10, 0, 5}, {kDr, "OH808", 48, 0, 0, 4},
                                 {kFm, "HAT O LG", 46, 0, 0, 2}, DEND};
constexpr DiceDrum DP_CHILL[] = {{kFm, "CONGA", 54, 0, 0, 5}, {kFm, "WOODBLK", 50, 0, 0, 4},
                                 {kFm, "TRIANGL", 42, 0, 0, 3}, {kFm, "SHKR SFT", 46, 0, 0, 3},
                                 {kDr, "RIM LOFI", 46, 0, 0, 3}, DEND};

#define KIT(x) constexpr DiceKit KI_##x = {{DK_##x, DS_##x, DC_##x, DH_##x, DO_##x, DP_##x}};
KIT(DEEP) KIT(HOUSE) KIT(ACID) KIT(DISCO) KIT(TECHNO) KIT(MIN) KIT(DUB) KIT(HARD) KIT(TRANCE) KIT(PSY) KIT(DNB)
KIT(LIQUID) KIT(JUNGLE) KIT(NEURO) KIT(BREAKS) KIT(UKG) KIT(ELECTRO) KIT(BOOM) KIT(LOFI) KIT(TRAP) KIT(IDM)
KIT(AMB) KIT(SW) KIT(DARKSW) KIT(CHILL)
#undef KIT

// ---- Feel: velocity on the beat / e / & / a, accent, ghost (~-14 dB), humanize at Variety 100 --------
constexpr DiceFeel FL_HOUSE = {{118, 74, 106, 82}, 127, 26, 30};   // offbeat hats lifted
constexpr DiceFeel FL_TECHNO = {{120, 82, 98, 88}, 127, 24, 20};
constexpr DiceFeel FL_TRANCE = {{122, 78, 104, 86}, 127, 24, 12};
constexpr DiceFeel FL_DNB = {{118, 80, 100, 88}, 127, 22, 30};
constexpr DiceFeel FL_BREAKS = {{116, 78, 100, 86}, 127, 26, 40};
constexpr DiceFeel FL_HIPHOP = {{116, 66, 94, 76}, 127, 22, 50};
constexpr DiceFeel FL_TRAP = {{120, 84, 100, 90}, 127, 28, 20};
constexpr DiceFeel FL_IDM = {{112, 88, 100, 94}, 127, 30, 35};
constexpr DiceFeel FL_AMB = {{104, 74, 90, 80}, 116, 22, 30};
constexpr DiceFeel FL_SW = {{118, 80, 102, 86}, 127, 26, 15};

// ---- Mix: per role {vol, pan, cut, res, fenv, atk, rel, drv, crush, dly, rvb} (see DiceTone) ---------
// Cutoff caps: 82 ~ 1.4 kHz, 86 ~ 1.7 kHz, 90 ~ 2.1 kHz, 94 ~ 2.6 kHz, 96 ~ 2.9 kHz (cutoffHz).
// Bass under the kick, pads quieter and darker, lead a little above, arp off-centre with delay.
constexpr uint8_t K = kDiceKeep;
constexpr DiceMix MX_CLUB = {{{112, 64, K, K, K, 0, 0, 24, 0, 0, 14},
                              {100, 64, 82, 40, 24, 0, 0, 20, 0, 0, 0},
                              {80, 64, 88, 30, 20, 0, 62, 12, 0, 24, 48},
                              {92, 58, 94, 35, 24, 0, 62, 16, 0, 36, 36},
                              {82, 78, 90, 35, 24, 0, 55, 12, 0, 46, 32},
                              {72, 46, K, K, K, 0, 0, 20, 0, 30, 50}}};
constexpr DiceMix MX_DEEP = {{{108, 64, K, K, K, 0, 0, 16, 0, 0, 18},
                              {100, 64, 78, 30, 20, 0, 0, 16, 0, 0, 0},
                              {82, 64, 82, 25, 16, 20, 74, 8, 0, 30, 62},
                              {88, 58, 88, 30, 20, 0, 70, 10, 0, 40, 48},
                              {80, 78, 86, 30, 20, 0, 62, 10, 0, 50, 40},
                              {70, 46, K, K, K, 0, 0, 16, 0, 34, 56}}};
constexpr DiceMix MX_ACID = {{{112, 64, K, K, K, 0, 0, 24, 0, 0, 12},
                              {100, 64, K, 100, K, 0, 0, 36, 0, 0, 0},  // the 303: resonance and env stay
                              {78, 64, 86, 30, 20, 0, 60, 12, 0, 24, 44},
                              {90, 58, 94, 35, 24, 0, 62, 16, 0, 36, 36},
                              {86, 72, K, 100, K, 0, 0, 32, 0, 40, 24},
                              {72, 46, K, K, K, 0, 0, 20, 0, 30, 50}}};
constexpr DiceMix MX_DUB = {{{108, 64, K, K, K, 0, 0, 16, 0, 0, 20},
                             {104, 64, 74, 25, 16, 0, 0, 12, 0, 0, 0},
                             {84, 64, 80, 40, 24, 0, 70, 10, 0, 90, 70},  // dub chords: echo
                             {86, 58, 86, 35, 20, 0, 70, 10, 0, 70, 60},
                             {80, 78, 84, 35, 20, 0, 66, 10, 0, 76, 56},
                             {70, 46, K, K, K, 0, 0, 16, 0, 60, 70}}};
constexpr DiceMix MX_HARD = {{{116, 64, K, K, K, 0, 0, 40, 0, 0, 10},
                              {100, 64, 86, 40, 28, 0, 0, 30, 0, 0, 0},
                              {78, 64, 90, 35, 24, 0, 60, 20, 0, 20, 40},
                              {88, 58, 96, 40, 28, 0, 60, 24, 0, 30, 30},
                              {80, 78, 92, 40, 24, 0, 55, 16, 0, 40, 28},
                              {70, 46, K, K, K, 0, 0, 24, 0, 30, 44}}};
constexpr DiceMix MX_TRANCE = {{{112, 64, K, K, K, 0, 0, 20, 0, 0, 14},
                                {98, 64, 84, 35, 24, 0, 0, 16, 0, 0, 0},
                                {80, 64, 92, 30, 20, 50, 88, 10, 0, 24, 64},
                                {94, 60, 96, 30, 24, 0, 70, 12, 0, 44, 48},
                                {84, 76, 92, 35, 26, 0, 60, 10, 0, 56, 40},
                                {74, 46, K, K, K, 0, 0, 16, 0, 36, 60}}};
constexpr DiceMix MX_PSY = {{{114, 64, K, K, K, 0, 0, 24, 0, 0, 10},
                             {100, 64, 90, 45, 30, 0, 0, 20, 0, 0, 0},
                             {76, 64, 88, 30, 20, 50, 80, 10, 0, 30, 56},
                             {90, 58, 96, 40, 28, 0, 60, 16, 0, 40, 40},
                             {84, 78, 94, 50, 28, 0, 50, 14, 0, 48, 32},
                             {74, 46, K, K, K, 0, 0, 20, 0, 40, 50}}};
constexpr DiceMix MX_DNB = {{{114, 64, K, K, K, 0, 0, 24, 0, 0, 16},
                             {98, 64, 80, 35, 24, 0, 0, 28, 0, 0, 0},
                             {78, 64, 86, 30, 20, 60, 88, 10, 0, 24, 60},
                             {90, 58, 94, 35, 24, 0, 66, 14, 0, 40, 44},
                             {82, 76, 90, 35, 24, 0, 60, 10, 0, 50, 40},
                             {72, 46, K, K, K, 0, 0, 20, 0, 36, 56}}};
constexpr DiceMix MX_LIQUID = {{{110, 64, K, K, K, 0, 0, 16, 0, 0, 22},
                                {100, 64, 74, 25, 16, 0, 0, 12, 0, 0, 0},
                                {84, 64, 84, 25, 16, 64, 92, 6, 0, 30, 70},
                                {88, 58, 90, 30, 20, 30, 74, 8, 0, 48, 56},
                                {80, 76, 88, 30, 20, 0, 70, 8, 0, 56, 50},
                                {70, 46, K, K, K, 0, 0, 16, 0, 40, 60}}};
constexpr DiceMix MX_HIPHOP = {{{112, 64, K, K, K, 0, 0, 20, 40, 0, 10},
                                {100, 64, 76, 30, 16, 0, 0, 20, 0, 0, 0},
                                {82, 64, 84, 25, 16, 0, 70, 10, 40, 20, 40},
                                {88, 58, 90, 30, 20, 0, 66, 12, 30, 34, 36},
                                {80, 74, 88, 30, 20, 0, 64, 10, 30, 40, 36},
                                {68, 46, K, K, K, 0, 0, 16, 40, 30, 44}}};
constexpr DiceMix MX_LOFI = {{{108, 64, K, K, K, 0, 0, 16, 60, 0, 14},
                              {98, 64, 70, 20, 12, 0, 0, 16, 0, 0, 0},
                              {84, 64, 78, 20, 12, 10, 80, 8, 60, 24, 50},
                              {84, 58, 84, 25, 16, 0, 74, 8, 50, 40, 44},
                              {80, 74, 82, 25, 16, 0, 74, 8, 50, 44, 44},
                              {66, 46, K, K, K, 0, 0, 12, 60, 30, 50}}};
constexpr DiceMix MX_TRAP = {{{112, 64, K, K, K, 0, 0, 20, 0, 0, 12},
                              {104, 64, 80, 25, 20, 0, 0, 40, 0, 0, 0},  // the 808 may growl a little
                              {76, 64, 84, 25, 16, 50, 88, 8, 0, 24, 64},
                              {88, 58, 92, 30, 20, 0, 66, 12, 0, 40, 46},
                              {78, 76, 90, 30, 20, 0, 66, 10, 0, 50, 48},
                              {68, 46, K, K, K, 0, 0, 16, 0, 30, 50}}};
constexpr DiceMix MX_IDM = {{{108, 64, K, K, K, 0, 0, 24, 40, 0, 18},
                             {98, 64, 84, 45, 28, 0, 0, 20, 0, 0, 0},
                             {80, 64, 90, 40, 24, 30, 74, 12, 30, 36, 56},
                             {88, 58, 94, 45, 28, 0, 66, 14, 30, 44, 44},
                             {82, 76, 92, 45, 28, 0, 60, 12, 30, 52, 44},
                             {70, 46, K, K, K, 0, 0, 20, 40, 40, 56}}};
constexpr DiceMix MX_AMB = {{{92, 64, K, K, K, 0, 0, 12, 0, 10, 40},
                             {98, 64, 72, 20, 12, 40, 90, 8, 0, 0, 20},
                             {84, 64, 82, 20, 12, 90, 100, 6, 0, 40, 96},
                             {84, 58, 86, 25, 16, 66, 92, 8, 0, 60, 80},
                             {78, 76, 88, 25, 16, 20, 84, 8, 0, 64, 72},
                             {72, 46, K, K, K, 0, 0, 12, 0, 50, 90}}};
constexpr DiceMix MX_SW = {{{112, 64, K, K, K, 0, 0, 20, 0, 0, 30},
                            {98, 64, 86, 35, 24, 0, 0, 16, 0, 0, 0},
                            {80, 64, 90, 25, 20, 60, 86, 8, 0, 30, 60},
                            {92, 58, 96, 30, 24, 0, 70, 12, 0, 50, 50},
                            {84, 76, 94, 30, 24, 0, 60, 10, 0, 56, 40},
                            {72, 46, K, K, K, 0, 0, 16, 0, 40, 60}}};
constexpr DiceMix MX_DARKSW = {{{114, 64, K, K, K, 0, 0, 30, 0, 0, 22},
                                {100, 64, 84, 40, 28, 0, 0, 30, 0, 0, 0},
                                {78, 64, 86, 30, 20, 50, 80, 12, 0, 24, 56},
                                {90, 58, 94, 35, 24, 0, 66, 20, 0, 40, 44},
                                {82, 76, 92, 35, 24, 0, 56, 14, 0, 48, 36},
                                {70, 46, K, K, K, 0, 0, 20, 0, 36, 50}}};
constexpr DiceMix MX_CHILL = {{{104, 64, K, K, K, 0, 0, 12, 20, 0, 24},
                               {98, 64, 76, 25, 16, 0, 0, 10, 0, 0, 10},
                               {84, 64, 84, 20, 14, 60, 92, 6, 20, 36, 74},
                               {86, 58, 88, 25, 16, 20, 80, 8, 10, 50, 60},
                               {80, 76, 86, 25, 16, 0, 76, 8, 10, 56, 56},
                               {68, 46, K, K, K, 0, 0, 12, 20, 40, 64}}};

// ---- Drums: kick ---------------------------------------------------------------------------------
constexpr DiceName K_HOUSE[] = {{"x...x...x...x...", 10}, {"x...x...x...x..o", 3}, {"x...x...x..ox...", 2}, END};
constexpr DiceName K_TECH[] = {{"X...X...X...X...", 10}, {"X...X...X...X..x", 3}, {"X...X..oX...X...", 2}, END};
constexpr DiceName K_HARD[] = {{"X...X...X...X...", 8}, {"X..xX...X..xX...", 4}, {"X...X...X...X.xx", 3}, END};
constexpr DiceName K_TRANCE[] = {{"X...X...X...X...", 10}, {"X...X...X...X..x", 2}, END};
constexpr DiceName K_BREAK[] = {{"x.x.......x.....x.........x.x...", 8}, {"x.........xx....x.x.......x.....", 5}, END};
constexpr DiceName K_UKG[] = {{"x.........x.....", 6}, {"x......x..x.....", 5}, {"x.....x...x..x..", 3}, END};
constexpr DiceName K_ELECTRO[] = {{"x.....x...x.....", 7}, {"x.....x.x.....x.", 4}, {"x..x..x...x.....", 3}, END};
constexpr DiceName K_DNB[] = {{"x.........x.....x.........x.....", 8}, {"x.........x.....x.x.......x.....", 4}, END};
constexpr DiceName K_JUMP[] = {{"x.........x..x..x.........x.....", 6}, {"x..x......x.....x.........x..x..", 4}, END};
constexpr DiceName K_NEURO[] = {{"x.........x.....x.....x...x.....", 6}, {"x.x.......x.....x.........x.x...", 4}, END};
constexpr DiceName K_JUNGLE[] = {{"x.x.......xx....x.x.......x.....", 8}, {"x.x.......x.....x.x...x...x.....", 4}, END};
constexpr DiceName K_HALF[] = {{"x.........x.......x.x...........", 6}, {"x...........x.....x.....x.......", 4}, END};
constexpr DiceName K_BOOM[] = {{"x......x.x......", 8}, {"x.........x..x..", 5}, {"x..x......x.....", 4}, END};
constexpr DiceName K_LOFI[] = {{"x......x..x.....", 8}, {"x.........x.....", 5}, {"x....x....x..x..", 3}, END};
constexpr DiceName K_TRAP[] = {{"x......x..x.....x.........x.....", 6}, {"x.........x..x..x......x..x.....", 5},
                               {"x.....x.......x.x...x.....x.....", 3}, END};
constexpr DiceName K_BRAIN[] = {{"x..x..x...x.x...", 5}, {"x.x...x..x..x..x", 4}, {"x.....x.x..x....", 4}, END};
constexpr DiceName K_GLITCH[] = {{"x..x...x..x.x...", 5}, {"x.......x.x..x..", 4}, {"x.x.....x...x.x.", 3}, END};
constexpr DiceName K_ELNICA[] = {{"x.......x.x.....", 6}, {"x..x....x.......", 4}, {"x.....x...x.....", 3}, END};
constexpr DiceName K_AMB[] = {{"x...............", 5}, {"x.......x.......", 4}, END};
constexpr DiceName K_SW[] = {{"x...x...x...x...", 7}, {"x.......x.x.....", 5}, END};
constexpr DiceName K_DARKSW[] = {{"X...X...X...X...", 7}, {"X.....X.X.......", 4}, END};
constexpr DiceName K_CHILL[] = {{"x.......x.x.....", 6}, {"x......x..x.....", 4}, END};

// ---- Drums: snare / clap -------------------------------------------------------------------------
constexpr DiceName S_BACK[] = {{"....x.......x...", 8}, {"....x..o....x...", 4}, {"....x.......x..o", 3}, END};
constexpr DiceName S_DNB[] = {{"....x.......x.......x.......x...", 8}, {"....x.......x..o....x.......x...", 4},
                              {"....x..o....x.......x..o....x.o.", 3}, END};
constexpr DiceName S_JUNGLE[] = {{"....x..o.o..x..o....x..o.o..x...", 6}, {"....x.......x..o.o..x.......x.o.", 4},
                                 {"....x..x.x..x..x....x..o.x..x..x", 3}, END};
constexpr DiceName S_HALF[] = {{"........x...............x.......", 8}, {"........x.......o.......x.....o.", 4}, END};
constexpr DiceName S_TRAP[] = {{"........X...............X.......", 8}, {"........X...............X.....o.", 3}, END};
constexpr DiceName S_BREAK[] = {{"....x.......x.......x.......x...", 6}, {"....x.......x..o....x..o....x...", 5}, END};
constexpr DiceName S_UKG[] = {{"....x.......x...", 8}, {"....x.......x..o", 3}, END};
constexpr DiceName S_BRAIN[] = {{"....x..x....x.xx", 4}, {"....x.......x...", 4}, {"....x...3...x...", 3},
                                {"....x.......4.x.", 3}, END};
constexpr DiceName S_GLITCH[] = {{"....x..3....x...", 4}, {"....x.......x...", 4}, {"....x.....x.x..4", 3}, END};
constexpr DiceName S_SW[] = {{"....X.......X...", 10}, END};
constexpr DiceName C_BACK[] = {{"....x.......x...", 10}, {"....x.......x..x", 2}, END};
constexpr DiceName C_TRANCE[] = {{"....x.......x...", 10}, END};

// ---- Drums: closed / open hats ------------------------------------------------------------------
constexpr DiceName H_HOUSE[] = {{"x.x.x.x.x.x.x.x.", 6}, {"oxo.oxo.oxo.oxo.", 4}, {"xxxxxxxxxxxxxxxx", 2}, END};
constexpr DiceName H_TECH[] = {{"xxxxxxxxxxxxxxxx", 5}, {"x.xxx.xxx.xxx.xx", 5}, {"..x...x...x...x.", 3}, END};
constexpr DiceName H_MIN[] = {{"..x...x...x...x.", 6}, {"..x..xx...x..x.x", 4}, END};
constexpr DiceName H_16[] = {{"xxxxxxxxxxxxxxxx", 6}, {"xoxoxoxoxoxoxoxo", 5}, END};
constexpr DiceName H_TRANCE[] = {{"xx.xxx.xxx.xxx.x", 6}, {"x.x.x.x.x.x.x.x.", 4}, {"xxxxxxxxxxxxxxxx", 3}, END};
constexpr DiceName H_DNB[] = {{"x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", 6}, {"x.xxx.x.x.xxx.x.x.xxx.x.x.xxx.xx", 4}, END};
constexpr DiceName H_JUNGLE[] = {{"x.xox.xxx.xox.xxx.xox.xxx.xox.xx", 5}, {"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 3}, END};
constexpr DiceName H_HALF[] = {{"x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", 5}, {"x...x...x...x...x...x...x...x...", 4}, END};
constexpr DiceName H_TRAP[] = {{"x.x.x.x.x.x.3.x.x.x.x.x.x.x4x.x.", 6}, {"x.x.x.x.x.x.x.x.x.x.x.3.x.x.4.3.", 5},
                               {"xxxxxxxx3.x.x.x.x.x.x.x.4.4.x.x.", 3}, END};
constexpr DiceName H_BOOM[] = {{"x.x.x.x.x.x.x.x.", 8}, {"x.x.x.xxx.x.x.x.", 4}, {"x.xox.xox.xox.xo", 3}, END};
constexpr DiceName H_UKG[] = {{"..x...x...x...x.", 6}, {"x.xxx.x.x.xxx.x.", 4}, {"..xx..x...xx..x.", 3}, END};
constexpr DiceName H_BREAK[] = {{"x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", 6}, {"x.x.x.xxx.x.x.x.x.x.x.xxx.x.x.xo", 4}, END};
constexpr DiceName H_BRAIN[] = {{"x.x3x.x.x4x.x.xx", 4}, {"xxxxxxxxxxxxxxxx", 4}, {"x.xxx.x2x.xxx.4.", 3}, END};
constexpr DiceName H_GLITCH[] = {{"x.x.x..3x.x.x..4", 4}, {"xx.x.x..xx.x2..x", 4}, END};
constexpr DiceName H_8[] = {{"x.x.x.x.x.x.x.x.", 8}, {"xxxxxxxxxxxxxxxx", 4}, {"x.xox.xox.xox.xo", 3}, END};
constexpr DiceName H_AMB[] = {{"..x...x...x...x.", 4}, {"x.......x.......", 4}, END};
constexpr DiceName O_OFF[] = {{"..x...x...x...x.", 10}, {"..x...x...x...xx", 2}, END};
constexpr DiceName O_SOME[] = {{"..............x.", 5}, {"......x.......x.", 4}, END};
constexpr DiceName O_DNB[] = {{"..............x...............x.", 5}, {"......................x.........", 4}, END};

// ---- Drums: perc ---------------------------------------------------------------------------------
constexpr DiceName P_HOUSE[] = {{"...x..x....x..x.", 5}, {"......x.......x.", 4}, {"..x..x....x..x..", 3}, END};
constexpr DiceName P_TECH[] = {{"...x..x....x..x.", 5}, {"..x..x..x..x..x.", 4}, {"x..x..x..x..x..x", 3}, END};
constexpr DiceName P_MIN[] = {{"...x.x.....x...x", 5}, {"..x..x...x..x...", 4}, {"x..x..x..x..x...", 3}, END};
constexpr DiceName P_DUB[] = {{"......x.......x.", 4}, {"...x......x.....", 4}, END};
constexpr DiceName P_ELECTRO[] = {{"..x.......x..x..", 5}, {"...x...x...x.x..", 3}, END};
constexpr DiceName P_IDM[] = {{"x..x.x...x..x.x.", 4}, {".x...x.x..x...x.", 4}, {"..3...x...2..x..", 3}, END};
constexpr DiceName P_UKG[] = {{"...x......x..x..", 5}, {"..x....x..x.....", 4}, END};
constexpr DiceName P_SPARSE[] = {{"..............x.", 4}, {"......x.........", 3}, END};
constexpr DiceName P_AMB[] = {{"......x.........", 4}, {"...........x....", 3}, END};

// ---- FX in percussion mode ------------------------------------------------------------------------
constexpr DiceName FH_CONGA[] = {{"..x..x....x.x..x", 5}, {"x..x..x...x..x..", 4}, {"...x..x.x...x...", 3}, END};
constexpr DiceName FH_SPARSE[] = {{"........x.......", 5}, {"x...............", 4}, END};
constexpr DiceName FH_IDM[] = {{".x..x.x...x.3...", 4}, {"x.3...x..x...2..", 4}, END};

// ---- Sounds ------------------------------------------------------------------------------------
// Shared lists: the roles a style leaves out point here.
constexpr DiceSound B_SUB[] = {{kSy, "SUBBASS", 7}, {kFm, "SUB FM", 5}, {kCh, "SUB SINE", 3}, SEND};
constexpr DiceSound CH_TECHNO[] = {{kSy, "STAB", 6}, {kSy, "DARK PAD", 5}, {kSy, "WTSWEEP", 2}, SEND};
constexpr DiceSound CH_DARK[] = {{kSy, "DARK PAD", 6}, {kSy, "WTSWEEP", 4}, {kSy, "CHOIR", 3}, SEND};
constexpr DiceSound L_HOUSE[] = {{kSy, "SOLO", 6}, {kFm, "FM LEAD", 5}, {kSy, "LEAD", 4}, {kSy, "SQ LEAD", 3}, SEND};
constexpr DiceSound L_PSY[] = {{kSy, "WT LEAD", 6}, {kSy, "SOLO", 4}, {kSy, "SYNCLD", 3}, SEND};
constexpr DiceSound L_TECHNO[] = {{kSy, "WT LEAD", 6}, {kSy, "SOLO", 4}, {kSy, "SYNCLD", 3}, {kSy, "HOOVER", 2}, SEND};
constexpr DiceSound L_HOOVER[] = {{kSy, "WT LEAD", 6}, {kSy, "HOOVER", 5}, {kSy, "SUPERSAW", 3}, SEND};
constexpr DiceSound L_DNB[] = {{kSy, "SOLO", 6}, {kFm, "FM LEAD", 5}, {kSy, "WT LEAD", 4}, SEND};
constexpr DiceSound L_HIPHOP[] = {{kCh, "FLUTE", 6}, {kFm, "FM LEAD", 4}, {kSy, "SOLO", 4}, {kFm, "BRASS", 3}, SEND};
constexpr DiceSound L_IDM[] = {{kSy, "WT LEAD", 6}, {kCh, "FLUTE", 4}, {kCh, "NES LEAD", 4}, {kCh, "C64 LEAD", 3}, {kCh, "GBLEAD", 2}, SEND};
constexpr DiceSound L_AMB[] = {{kCh, "FLUTE", 4}, {kSy, "SOLO", 3}, {kFm, "FM LEAD", 3}, SEND};
constexpr DiceSound A_HOUSE[] = {{kSy, "PLUCK", 6}, {kSy, "BELL", 3}, {kSt, "HARP", 3}, SEND};
constexpr DiceSound A_TECHNO[] = {{kSy, "PING", 5}, {kSy, "PLUCK", 4}, {kSy, "SYNCPLK", 3}, SEND};
constexpr DiceSound A_ACID[] = {{kSy, "ACID", 8}, {kCh, "ACID", 4}, SEND};
constexpr DiceSound A_DNB[] = {{kSy, "PLUCK", 4}, {kSy, "BELL", 4}, {kSt, "HARP", 3}, SEND};
constexpr DiceSound A_LOFI[] = {{kSt, "NYLON", 5}, {kSt, "KALIMBA", 4}, {kSt, "HARP", 3}, SEND};
constexpr DiceSound A_AMB[] = {{kFm, "GLOCK", 4}, {kSt, "HARP", 4}, {kSy, "BELL", 4}, {kFm, "DX BELL", 3}, SEND};

// Per style: the canon first (heaviest), then the style's other fitting presets.
constexpr DiceSound XB_HOUDEEP[] = {{kSy, "SUBBASS", 6}, {kSy, "MOOGISH", 5}, {kFm, "SUB FM", 3}, {kSy, "DEEPHSE", 5},
                                    {kFm, "DEEP SUB", 4}, {kFm, "BASS DEP", 4}, {kSy, "DEEPSUB", 4},
                                    {kSy, "UKGBASS", 3}, {kFm, "BS ROUND", 3}, {kCh, "ROUND SB", 3},
                                    {kCh, "PWM BASS", 2}, {kSt, "STR BASS", 2}, SEND};
constexpr DiceSound XC_HOUDEEP[] = {{kFm, "E.PIANO", 6}, {kSy, "WARM PAD", 5}, {kFm, "EP SOFT", 4}, {kSy, "ORGAN", 3},
                                    {kSy, "EP DEEP", 5}, {kFm, "EP DEEP", 5}, {kSy, "DEEP PAD", 4},
                                    {kSy, "HOUSEORG", 3}, {kSy, "DUBCHORD", 3}, {kFm, "PAD WARM", 3},
                                    {kSy, "VOX PAD", 3}, {kFm, "VIBES", 2}, {kCh, "SOFT PAD", 2}, {kSt, "TINE EP", 2},
                                    SEND};
constexpr DiceSound XL_HOUDEEP[] = {{kSy, "SOLO", 6}, {kFm, "FM LEAD", 5}, {kSy, "LEAD", 4}, {kSy, "SQ LEAD", 3},
                                    {kSy, "SOFTLEAD", 4}, {kCh, "SOFT SQ", 3}, {kFm, "LEAD SFT", 3}, SEND};
constexpr DiceSound XA_HOUDEEP[] = {{kSy, "PLUCK", 6}, {kSy, "BELL", 3}, {kSt, "HARP", 3}, {kSy, "SOFTPLK", 4},
                                    {kSy, "DEEPPLK", 4}, {kFm, "PLK SOFT", 3}, {kSt, "MUTED", 3}, {kSt, "SOFTPLK", 3},
                                    {kSt, "DUB PLK", 2}, {kSy, "MARIMBA", 2}, SEND};
constexpr DiceSound XF_HOUDEEP[] = {{kFm, "CONGA", 6}, {kSy, "RISER", 5}, {kSy, "NOISESWP", 3}, {kSy, "WHOOSH", 3},
                                    {kSy, "SWEEPUP", 3}, {kFm, "SHKR SFT", 3}, {kFm, "BONGO", 3},
                                    {kFm, "RIM SOFT", 2}, SEND};
constexpr DiceSound XB_HOUTECH[] = {{kSy, "MOOGISH", 6}, {kSy, "BASS", 5}, {kFm, "FM BASS", 4}, {kSy, "SUBBASS", 4},
                                    {kFm, "TECH BAS", 5}, {kSy, "MINIBASS", 4}, {kFm, "BASS DEP", 3},
                                    {kSy, "MOOGFAT", 3}, {kSy, "DEEPHSE", 3}, SEND};
constexpr DiceSound XC_HOUTECH[] = {{kFm, "E.PIANO", 6}, {kSy, "STAB", 5}, {kSy, "ORGAN", 4}, {kSy, "PAD", 3},
                                    {kSy, "HOUSEORG", 4}, {kFm, "ORGAN HS", 4}, {kSy, "RAVESTB", 3},
                                    {kCh, "CHIPSTAB", 3}, {kSy, "DUBCHORD", 2}, SEND};
constexpr DiceSound XA_HOUTECH[] = {{kSy, "PING", 5}, {kSy, "PLUCK", 4}, {kSy, "SYNCPLK", 3}, {kSy, "DEEPPLK", 5},
                                    {kSt, "MUTED", 3}, {kSy, "BLIP", 2}, {kFm, "PLK SOFT", 2}, SEND};
constexpr DiceSound XF_HOUTECH[] = {{kFm, "CONGA", 6}, {kSy, "RISER", 5}, {kSy, "NOISESWP", 3}, {kFm, "SHAKER", 4},
                                    {kFm, "CLAVE", 3}, {kFm, "RIM", 3}, {kSy, "SWEEPUP", 3}, SEND};
constexpr DiceSound XB_HOUACID[] = {{kSy, "ACID", 8}, {kCh, "ACID", 4}, {kSy, "ACIDSOFT", 6}, {kSy, "ACIDSQ", 5},
                                    {kCh, "SOFTACID", 4}, {kFm, "ACID FM", 3}, SEND};
constexpr DiceSound XC_HOUACID[] = {{kFm, "E.PIANO", 6}, {kSy, "STAB", 5}, {kSy, "ORGAN", 4}, {kSy, "PAD", 3},
                                    {kSy, "HOUSEORG", 3}, {kSy, "RAVESTB", 2}, SEND};
constexpr DiceSound XF_HOUACID[] = {{kFm, "CONGA", 6}, {kSy, "RISER", 5}, {kSy, "NOISESWP", 3}, {kSy, "SWEEPUP", 3},
                                    {kFm, "SHAKER", 3}, {kSy, "ZAP", 2}, SEND};
constexpr DiceSound XB_HOUDISCO[] = {{kSy, "PLUCKBAS", 6}, {kFm, "DX BASS", 5}, {kSy, "BASS", 4},
                                     {kSy, "DISCOBAS", 5}, {kFm, "DISCO BS", 5}, {kCh, "PWM BASS", 3},
                                     {kSy, "DEEPHSE", 2}, SEND};
constexpr DiceSound XC_HOUDISCO[] = {{kFm, "E.PIANO", 6}, {kCh, "STRINGS", 5}, {kSy, "PWMSTR", 4}, {kSy, "STAB", 3},
                                     {kSy, "KORGPNO", 5}, {kSy, "STRINGS", 4}, {kFm, "EP DX", 4}, {kSy, "CLAV", 3},
                                     {kSy, "BRASS", 3}, {kSy, "HOUSEORG", 3}, {kFm, "ORGAN HS", 2}, SEND};
constexpr DiceSound XL_HOUDISCO[] = {{kSy, "SOLO", 6}, {kFm, "FM LEAD", 5}, {kSy, "LEAD", 4}, {kSy, "SQ LEAD", 3},
                                     {kSy, "BRASS", 3}, {kCh, "WARM PWM", 2}, {kSy, "SOFTLEAD", 2}, SEND};
constexpr DiceSound XA_HOUDISCO[] = {{kSy, "PLUCK", 6}, {kSy, "BELL", 3}, {kSt, "HARP", 3}, {kSt, "STEELPAN", 3},
                                     {kSy, "CLAV", 3}, {kFm, "MALLET", 2}, SEND};
constexpr DiceSound XF_HOUDISCO[] = {{kFm, "CONGA", 6}, {kSy, "RISER", 5}, {kSy, "NOISESWP", 3}, {kFm, "BONGO", 4},
                                     {kFm, "AGOGO", 3}, {kFm, "TRIANGL", 2}, {kSy, "WHOOSH", 2}, SEND};
constexpr DiceSound XB_HOUPROG[] = {{kSy, "BASS", 6}, {kSy, "PLUCKBAS", 4}, {kSy, "MOOGISH", 4}, {kSy, "MOOGFAT", 5},
                                    {kSy, "TRANCEBS", 4}, {kFm, "TECH BAS", 3}, SEND};
constexpr DiceSound XC_HOUPROG[] = {{kSy, "PAD", 6}, {kSy, "PWMSTR", 5}, {kSy, "SHIMMER", 4}, {kSy, "CHOIR", 3},
                                    {kSy, "PROGPAD", 5}, {kSy, "VOX PAD", 3}, {kSy, "TRANCSTB", 3},
                                    {kSy, "DREAMPAD", 3}, SEND};
constexpr DiceSound XL_HOUPROG[] = {{kSy, "SOLO", 6}, {kFm, "FM LEAD", 5}, {kSy, "LEAD", 4}, {kSy, "SQ LEAD", 3},
                                    {kSy, "PROGLEAD", 5}, {kSy, "SAWSOFT", 4}, {kSy, "MELOLEAD", 4},
                                    {kFm, "LEAD SFT", 3}, {kCh, "WARM PWM", 3}, SEND};
constexpr DiceSound XA_HOUPROG[] = {{kSy, "PLUCK", 6}, {kSy, "PING", 4}, {kSy, "SYNCPLK", 2}, {kSy, "PROGPLK", 5},
                                    {kSy, "MELOPLK", 4}, {kFm, "PLUCK FM", 4}, {kFm, "PLK SOFT", 3},
                                    {kCh, "SOFT ARP", 3}, {kFm, "MALLET", 2}, SEND};
constexpr DiceSound XF_HOUPROG[] = {{kSy, "RISER", 8}, {kSy, "NOISESWP", 5}, {kSy, "UPLIFTER", 5},
                                    {kSy, "SWEEPUP", 4}, {kFm, "RISER", 3}, {kSy, "WHOOSH", 3}, {kFm, "SHAKER", 2},
                                    SEND};
constexpr DiceSound XB_TECMINIMAL[] = {{kSy, "SUBBASS", 5}, {kSy, "BASS", 5}, {kFm, "SUB FM", 4}, {kSy, "MOOGISH", 3},
                                       {kSy, "MINIBASS", 5}, {kFm, "TECH BAS", 4}, {kSy, "DEEPSUB", 3},
                                       {kSy, "DUBTECH", 3}, {kFm, "DEEP SUB", 3}, SEND};
constexpr DiceSound XA_TECMINIMAL[] = {{kSy, "PING", 5}, {kSy, "PLUCK", 4}, {kSy, "SYNCPLK", 3}, {kSy, "DEEPPLK", 4},
                                       {kSy, "BLIP", 4}, {kSy, "GLITCHPL", 3}, {kSy, "MARIMBA", 3}, {kSt, "MUTED", 3},
                                       {kSt, "WOOD", 2}, {kSt, "DUB PLK", 2}, SEND};
constexpr DiceSound XF_TECMINIMAL[] = {{kFm, "WOODBLK", 5}, {kFm, "CONGA", 4}, {kSy, "NOISESWP", 3},
                                       {kFm, "CLICK", 4}, {kFm, "CLAVE", 4}, {kFm, "RIM", 4}, {kFm, "BONGO", 3},
                                       {kFm, "SHAKER", 3}, {kFm, "BLIP", 3}, SEND};
constexpr DiceSound XB_TECDUB[] = {{kSy, "SUBBASS", 7}, {kFm, "SUB FM", 5}, {kCh, "SUB SINE", 3}, {kSy, "DUBTECH", 6},
                                   {kFm, "DUB BASS", 5}, {kSy, "DEEPSUB", 4}, {kFm, "DEEP SUB", 3},
                                   {kSy, "RUMBLE", 1}, SEND};
constexpr DiceSound XC_TECDUB[] = {{kSy, "STAB", 6}, {kSy, "DARK PAD", 4}, {kFm, "FM PAD", 3}, {kSy, "DUBCHORD", 6},
                                   {kSy, "DEEP PAD", 3}, {kFm, "PAD DARK", 3}, {kSy, "DRONE", 2}, {kCh, "DARKPAD", 2},
                                   {kSt, "DARKDRON", 1}, SEND};
constexpr DiceSound XF_TECDUB[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 4}, {kFm, "ZAP", 3}, {kSy, "WIND", 4},
                                   {kSy, "WHOOSH", 3}, {kFm, "RIM", 3}, {kSy, "SIREN", 2}, {kSt, "GAMELAN", 2}, SEND};
constexpr DiceSound XB_TECPEAK[] = {{kSy, "SUBBASS", 5}, {kSy, "BASS", 5}, {kFm, "SUB FM", 4}, {kSy, "MOOGISH", 3},
                                    {kSy, "MOOGFAT", 4}, {kFm, "TECH BAS", 4}, {kSy, "RUMBLE", 3},
                                    {kFm, "DARK BS", 2}, {kSy, "DARKBASS", 2}, SEND};
constexpr DiceSound XA_TECPEAK[] = {{kSy, "PING", 5}, {kSy, "PLUCK", 4}, {kSy, "SYNCPLK", 3}, {kSy, "MELOPLK", 3},
                                    {kSy, "DEEPPLK", 3}, {kSy, "BLIP", 2}, SEND};
constexpr DiceSound XF_TECPEAK[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 4}, {kFm, "ZAP", 3}, {kSy, "SWEEPUP", 4},
                                    {kSy, "UPLIFTER", 4}, {kSy, "DOWNLIFT", 3}, {kSy, "IMPACT", 3}, {kFm, "RISER", 3},
                                    {kSy, "WHOOSH", 2}, SEND};
constexpr DiceSound XB_TECHARD[] = {{kSy, "SUBBASS", 5}, {kSy, "BASS", 5}, {kFm, "SUB FM", 4}, {kSy, "MOOGISH", 3},
                                    {kSy, "RUMBLE", 5}, {kSy, "DARKBASS", 4}, {kFm, "DARK BS", 3},
                                    {kSy, "MOOGFAT", 3}, {kSy, "NEURO", 2}, SEND};
constexpr DiceSound XC_TECHARD[] = {{kSy, "STAB", 6}, {kSy, "DARK PAD", 5}, {kSy, "WTSWEEP", 2}, {kSy, "RAVESTB", 4},
                                    {kSy, "STRNGDRK", 3}, {kSy, "DRONEDRK", 2}, SEND};
constexpr DiceSound XF_TECHARD[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 4}, {kFm, "ZAP", 3}, {kSy, "IMPACT", 4},
                                    {kSy, "DOWNLIFT", 4}, {kSy, "WHOOSH", 3}, {kSy, "SIREN", 2}, SEND};
constexpr DiceSound XB_TECACID[] = {{kSy, "SUBBASS", 7}, {kFm, "SUB FM", 5}, {kCh, "SUB SINE", 3},
                                    {kSy, "DEEPSUB", 4}, {kFm, "DEEP SUB", 3}, {kSy, "MINIBASS", 3}, SEND};
constexpr DiceSound XA_TECACID[] = {{kSy, "ACID", 8}, {kCh, "ACID", 4}, {kSy, "ACIDSOFT", 6}, {kSy, "ACIDSQ", 5},
                                    {kCh, "SOFTACID", 4}, {kFm, "ACID FM", 3}, {kSy, "GOALEAD", 2}, SEND};
constexpr DiceSound XF_TECACID[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 4}, {kFm, "ZAP", 3}, {kSy, "SWEEPUP", 3},
                                    {kSy, "ZAP", 3}, {kSy, "LASER", 2}, SEND};
constexpr DiceSound XB_TECMELODIC[] = {{kSy, "SUBBASS", 5}, {kSy, "BASS", 5}, {kFm, "SUB FM", 4}, {kSy, "MOOGISH", 3},
                                       {kSy, "MOOGFAT", 5}, {kFm, "TECH BAS", 3}, {kSy, "DEEPSUB", 3},
                                       {kSy, "DUBTECH", 2}, SEND};
constexpr DiceSound XC_TECMELODIC[] = {{kSy, "PAD", 6}, {kSy, "WTSWEEP", 4}, {kSy, "SHIMMER", 4},
                                       {kSy, "DARK PAD", 3}, {kSy, "PROGPAD", 5}, {kSy, "STRNGDRK", 4},
                                       {kSy, "VOX PAD", 3}, {kSy, "GLASSPAD", 3}, SEND};
constexpr DiceSound XL_TECMELODIC[] = {{kSy, "WT LEAD", 6}, {kSy, "SOLO", 4}, {kSy, "SYNCLD", 3}, {kSy, "HOOVER", 2},
                                       {kSy, "MELOLEAD", 6}, {kSy, "SAWSOFT", 4}, {kSy, "PROGLEAD", 4},
                                       {kFm, "LEAD SFT", 3}, {kCh, "DARKLEAD", 3}, SEND};
constexpr DiceSound XA_TECMELODIC[] = {{kSy, "PING", 5}, {kSy, "PLUCK", 4}, {kSy, "SYNCPLK", 3}, {kSy, "MELOPLK", 5},
                                       {kSy, "DEEPPLK", 4}, {kFm, "PLUCK FM", 4}, {kFm, "PLK SOFT", 3},
                                       {kFm, "MALLET", 3}, SEND};
constexpr DiceSound XF_TECMELODIC[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 4}, {kFm, "ZAP", 3}, {kSy, "SWEEPUP", 4},
                                       {kSy, "UPLIFTER", 3}, {kSy, "WHOOSH", 3}, {kSy, "SPACEDRN", 2}, SEND};
constexpr DiceSound XB_TRAUPLIFT[] = {{kSy, "BASS", 6}, {kSy, "PLUCKBAS", 4}, {kSy, "MOOGISH", 4},
                                      {kSy, "TRANCEBS", 6}, {kSy, "MOOGFAT", 3}, SEND};
constexpr DiceSound XC_TRAUPLIFT[] = {{kSy, "PAD", 6}, {kSy, "PWMSTR", 5}, {kSy, "SHIMMER", 4}, {kSy, "CHOIR", 3},
                                      {kSy, "TRANCEPD", 6}, {kSy, "STRINGS", 4}, {kSy, "TRANCSTB", 4},
                                      {kSy, "DREAMPAD", 3}, {kFm, "STRNG FM", 3}, {kCh, "CHOIR", 3},
                                      {kSt, "SHIMMER", 2}, {kFm, "PAD GLAS", 2}, SEND};
constexpr DiceSound XL_TRAUPLIFT[] = {{kSy, "SUPERSAW", 8}, {kSy, "LEAD", 4}, {kSy, "SOLO", 3}, {kSy, "WT LEAD", 3},
                                      {kSy, "UPLIFT", 7}, {kFm, "TRNC LD", 4}, {kSy, "SAWSOFT", 3}, SEND};
constexpr DiceSound XA_TRAUPLIFT[] = {{kSy, "PLUCK", 6}, {kSy, "PING", 4}, {kSy, "SYNCPLK", 2}, {kSy, "ARPPLK", 6},
                                      {kSy, "PROGPLK", 4}, {kFm, "PLUCK FM", 4}, {kCh, "SOFT ARP", 3},
                                      {kCh, "GLASSARP", 3}, {kCh, "BELL ARP", 3}, {kFm, "CELESTA", 2},
                                      {kSt, "AIRHARP", 2}, SEND};
constexpr DiceSound XF_TRAUPLIFT[] = {{kSy, "RISER", 8}, {kSy, "NOISESWP", 5}, {kSy, "UPLIFTER", 7},
                                      {kSy, "DOWNLIFT", 4}, {kFm, "RISER", 4}, {kSy, "IMPACT", 3},
                                      {kSy, "SWEEPUP", 3}, {kSy, "WHOOSH", 2}, SEND};
constexpr DiceSound XB_TRAFULLON[] = {{kSy, "PLUCKBAS", 6}, {kSy, "BASS", 5}, {kSy, "ACID", 3}, {kFm, "FM BASS", 3},
                                      {kSy, "PSYBASS", 6}, {kFm, "PSY BASS", 5}, {kSy, "GOABASS", 3},
                                      {kFm, "GOA BASS", 3}, SEND};
constexpr DiceSound XL_TRAFULLON[] = {{kSy, "WT LEAD", 6}, {kSy, "SOLO", 4}, {kSy, "SYNCLD", 3}, {kSy, "PSYLEAD", 6},
                                      {kFm, "PSY LEAD", 4}, {kSy, "GOALEAD", 3}, SEND};
constexpr DiceSound XA_TRAFULLON[] = {{kSy, "PING", 6}, {kSy, "SYNCPLK", 4}, {kSy, "CHIPPLK", 3}, {kSy, "PSYPLK", 6},
                                      {kFm, "PLK PSY", 5}, {kSt, "SITAR", 2}, SEND};
constexpr DiceSound XF_TRAFULLON[] = {{kSy, "SCIFI", 6}, {kFm, "ZAP", 5}, {kSy, "RISER", 4}, {kCh, "LASER", 3},
                                      {kSy, "LASER", 4}, {kSy, "ZAP", 4}, {kFm, "LASER", 3}, {kSy, "UPLIFTER", 3},
                                      {kSy, "DOWNLIFT", 2}, {kSy, "IMPACT", 2}, SEND};
constexpr DiceSound XB_TRADARKPSY[] = {{kSy, "PLUCKBAS", 6}, {kSy, "BASS", 5}, {kSy, "ACID", 3}, {kFm, "FM BASS", 3},
                                       {kSy, "PSYBASS", 6}, {kFm, "PSY BASS", 5}, {kSy, "RUMBLE", 2},
                                       {kSy, "NEURO", 1}, SEND};
constexpr DiceSound XA_TRADARKPSY[] = {{kSy, "PING", 6}, {kSy, "SYNCPLK", 4}, {kSy, "CHIPPLK", 3}, {kSy, "PSYPLK", 5},
                                       {kFm, "PLK PSY", 4}, {kSy, "GLITCHPL", 3}, {kSt, "SITAR", 2}, SEND};
constexpr DiceSound XF_TRADARKPSY[] = {{kSy, "SCIFI", 6}, {kFm, "ZAP", 5}, {kSy, "RISER", 4}, {kCh, "LASER", 3},
                                       {kSy, "SPACEDRN", 4}, {kSy, "DOWNLIFT", 3}, {kSy, "ZAP", 3}, {kSy, "LASER", 3},
                                       {kSy, "IMPACT", 2}, SEND};
constexpr DiceSound XB_TRAGOA[] = {{kSy, "PLUCKBAS", 6}, {kSy, "BASS", 5}, {kSy, "ACID", 3}, {kFm, "FM BASS", 3},
                                   {kSy, "GOABASS", 6}, {kFm, "GOA BASS", 5}, {kSy, "PSYBASS", 4},
                                   {kFm, "PSY BASS", 3}, SEND};
constexpr DiceSound XL_TRAGOA[] = {{kSy, "WT LEAD", 6}, {kSy, "SOLO", 4}, {kSy, "SYNCLD", 3}, {kSy, "GOALEAD", 6},
                                   {kSy, "PSYLEAD", 5}, {kFm, "PSY LEAD", 4}, {kFm, "FLUTE FM", 2}, SEND};
constexpr DiceSound XA_TRAGOA[] = {{kSy, "ACID", 8}, {kCh, "ACID", 4}, {kSy, "ACIDSOFT", 4}, {kSy, "PSYPLK", 4},
                                   {kFm, "PLK PSY", 4}, {kSt, "SITAR", 3}, {kCh, "SID ARP", 2}, SEND};
constexpr DiceSound XF_TRAGOA[] = {{kSy, "SCIFI", 6}, {kFm, "ZAP", 5}, {kSy, "RISER", 4}, {kCh, "LASER", 3},
                                   {kSy, "LASER", 4}, {kSy, "ZAP", 4}, {kFm, "LASER", 3}, {kFm, "TABLA", 2}, SEND};
constexpr DiceSound XB_TRAPROG[] = {{kSy, "BASS", 6}, {kSy, "PLUCKBAS", 4}, {kSy, "MOOGISH", 4}, {kSy, "TRANCEBS", 5},
                                    {kSy, "MOOGFAT", 4}, {kFm, "TECH BAS", 2}, SEND};
constexpr DiceSound XC_TRAPROG[] = {{kSy, "PAD", 6}, {kSy, "PWMSTR", 5}, {kSy, "SHIMMER", 4}, {kSy, "CHOIR", 3},
                                    {kSy, "PROGPAD", 6}, {kSy, "TRANCEPD", 4}, {kSy, "VOX PAD", 3},
                                    {kSy, "DREAMPAD", 2}, {kSy, "STRNGDRK", 2}, SEND};
constexpr DiceSound XL_TRAPROG[] = {{kSy, "SUPERSAW", 8}, {kSy, "LEAD", 4}, {kSy, "SOLO", 3}, {kSy, "WT LEAD", 3},
                                    {kSy, "SAWSOFT", 5}, {kSy, "PROGLEAD", 5}, {kSy, "UPLIFT", 4},
                                    {kFm, "TRNC LD", 3}, {kSy, "MELOLEAD", 3}, {kCh, "WARM PWM", 2}, SEND};
constexpr DiceSound XA_TRAPROG[] = {{kSy, "PLUCK", 6}, {kSy, "PING", 4}, {kSy, "SYNCPLK", 2}, {kSy, "PROGPLK", 6},
                                    {kSy, "ARPPLK", 5}, {kFm, "PLUCK FM", 4}, {kCh, "SOFT ARP", 3},
                                    {kCh, "GLASSARP", 3}, {kFm, "PLK SOFT", 2}, SEND};
constexpr DiceSound XF_TRAPROG[] = {{kSy, "RISER", 8}, {kSy, "NOISESWP", 5}, {kSy, "UPLIFTER", 6},
                                    {kSy, "DOWNLIFT", 3}, {kSy, "SWEEPUP", 3}, {kFm, "RISER", 3}, {kSy, "WHOOSH", 2},
                                    SEND};
constexpr DiceSound XB_DNBLIQUID[] = {{kSy, "SUBBASS", 7}, {kFm, "SUB FM", 5}, {kCh, "SUB SINE", 3},
                                      {kSy, "REESEWRM", 5}, {kSy, "DEEPSUB", 5}, {kSy, "JUNGLSUB", 4},
                                      {kFm, "DEEP SUB", 3}, {kFm, "REESE FM", 3}, {kCh, "ROUND SB", 3},
                                      {kSt, "STR BASS", 3}, {kFm, "HALF SUB", 2}, SEND};
constexpr DiceSound XC_DNBLIQUID[] = {{kFm, "EP SOFT", 5}, {kSy, "WARM PAD", 5}, {kSy, "SHIMMER", 4},
                                      {kSy, "CHOIR", 3}, {kSy, "DREAMPAD", 4}, {kSy, "EP SOFT", 4},
                                      {kFm, "EP DEEP", 4}, {kFm, "PAD WARM", 3}, {kSy, "STRINGS", 3},
                                      {kSy, "GLASSPAD", 3}, {kSt, "TINE EP", 3}, {kCh, "SOFT PAD", 2},
                                      {kCh, "EP SOFT", 2}, {kSt, "BOWED", 2}, SEND};
constexpr DiceSound XL_DNBLIQUID[] = {{kSy, "SOLO", 6}, {kFm, "FM LEAD", 5}, {kSy, "WT LEAD", 4},
                                      {kSy, "SOFTLEAD", 5}, {kFm, "LEAD SFT", 4}, {kFm, "CHILL LD", 3}, SEND};
constexpr DiceSound XA_DNBLIQUID[] = {{kSy, "PLUCK", 4}, {kSy, "BELL", 4}, {kSt, "HARP", 3}, {kSy, "SOFTPLK", 4},
                                      {kSy, "DREAMPLK", 4}, {kSt, "AIRHARP", 3}, {kSt, "KOTO", 3}, {kSy, "VIBES", 3},
                                      {kFm, "EP BELL", 3}, {kSt, "VIBRA", 2}, {kFm, "PLK SOFT", 2}, SEND};
constexpr DiceSound XF_DNBLIQUID[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 5}, {kSy, "ROBOT", 2}, {kSy, "WHOOSH", 4},
                                      {kFm, "SHKR SFT", 3}, {kFm, "RIM SOFT", 3}, {kSt, "CHIMES", 2}, SEND};
constexpr DiceSound XB_DNBJUMPUP[] = {{kSy, "REESE", 8}, {kSy, "WOBBLE", 3}, {kSy, "FM GROWL", 2}, {kFm, "JUMPUP", 5},
                                      {kSy, "WOBBLE2", 5}, {kFm, "GROWL FM", 3}, {kSy, "REESEWRM", 3}, SEND};
constexpr DiceSound XL_DNBJUMPUP[] = {{kSy, "WT LEAD", 6}, {kSy, "HOOVER", 5}, {kSy, "SUPERSAW", 3},
                                      {kSy, "GLIDELD", 2}, SEND};
constexpr DiceSound XF_DNBJUMPUP[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 5}, {kSy, "ROBOT", 2}, {kSy, "SIREN", 4},
                                      {kSy, "WHOOSH", 3}, {kSy, "IMPACT", 3}, {kSy, "DOWNLIFT", 3}, SEND};
constexpr DiceSound XB_DNBNEURO[] = {{kSy, "REESE", 6}, {kSy, "FM GROWL", 5}, {kFm, "WOBBLE", 3}, {kSy, "WOBBLE", 3},
                                     {kSy, "NEURO", 5}, {kFm, "NEURO", 4}, {kFm, "GROWL FM", 4}, {kSy, "REESEWRM", 2},
                                     SEND};
constexpr DiceSound XC_DNBNEURO[] = {{kSy, "DARK PAD", 6}, {kSy, "WTSWEEP", 4}, {kSy, "CHOIR", 3},
                                     {kFm, "PAD DARK", 4}, {kSy, "STRNGDRK", 3}, {kSy, "ABYSS", 2}, SEND};
constexpr DiceSound XF_DNBNEURO[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 5}, {kSy, "ROBOT", 2}, {kSy, "IMPACT", 4},
                                     {kSy, "DOWNLIFT", 3}, {kSy, "WHOOSH", 3}, {kSy, "ZAP", 2}, SEND};
constexpr DiceSound XB_DNBJUNGLE[] = {{kSy, "SUBBASS", 7}, {kFm, "SUB FM", 5}, {kCh, "SUB SINE", 3},
                                      {kSy, "JUNGLSUB", 6}, {kSy, "REESEWRM", 4}, {kFm, "REESE FM", 3},
                                      {kFm, "DUB BASS", 3}, {kFm, "HALF SUB", 2}, SEND};
constexpr DiceSound XC_DNBJUNGLE[] = {{kSy, "DARK PAD", 6}, {kSy, "WTSWEEP", 4}, {kSy, "CHOIR", 3},
                                      {kSy, "RAVESTB", 4}, {kSy, "STRINGS", 2}, SEND};
constexpr DiceSound XF_DNBJUNGLE[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 5}, {kSy, "ROBOT", 2}, {kSy, "SIREN", 4},
                                      {kSy, "WHOOSH", 3}, {kFm, "TOM HI", 2}, SEND};
constexpr DiceSound XB_DNBHALFTIM[] = {{kSy, "REESE", 6}, {kSy, "FM GROWL", 5}, {kFm, "WOBBLE", 3},
                                       {kSy, "WOBBLE", 3}, {kFm, "HALF SUB", 4}, {kSy, "WOBBLE2", 4},
                                       {kSy, "808 GLD", 4}, {kFm, "808 FM", 3}, {kSy, "NEURO", 3},
                                       {kFm, "GROWL FM", 3}, {kSy, "REESEWRM", 3}, SEND};
constexpr DiceSound XC_DNBHALFTIM[] = {{kSy, "DARK PAD", 6}, {kSy, "WTSWEEP", 4}, {kSy, "CHOIR", 3},
                                       {kSy, "ABYSS", 3}, {kFm, "PAD DARK", 2}, SEND};
constexpr DiceSound XA_DNBHALFTIM[] = {{kSy, "PLUCK", 4}, {kSy, "BELL", 4}, {kSt, "HARP", 3}, {kSy, "DREAMPLK", 2},
                                       {kSy, "GLITCHPL", 2}, SEND};
constexpr DiceSound XF_DNBHALFTIM[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 5}, {kSy, "ROBOT", 2}, {kSy, "IMPACT", 4},
                                       {kSy, "WHOOSH", 3}, {kSy, "DOWNLIFT", 3}, SEND};
constexpr DiceSound XB_BREBREAKBE[] = {{kSy, "MOOGISH", 5}, {kFm, "FM BASS", 5}, {kSy, "BASS", 4},
                                       {kSy, "ELECBASS", 5}, {kSy, "REESEWRM", 3}, {kFm, "REESE FM", 2},
                                       {kSy, "JUNGLSUB", 2}, {kSy, "WOBBLE2", 2}, SEND};
constexpr DiceSound XC_BREBREAKBE[] = {{kFm, "E.PIANO", 6}, {kSy, "STAB", 5}, {kSy, "ORGAN", 4}, {kSy, "PAD", 3},
                                       {kSy, "RAVESTB", 5}, {kSy, "KORGPNO", 4}, SEND};
constexpr DiceSound XA_BREBREAKBE[] = {{kSy, "PLUCK", 6}, {kSy, "BELL", 3}, {kSt, "HARP", 3}, {kSy, "BLIP", 2},
                                       {kCh, "SID ARP", 2}, {kFm, "PLK SOFT", 2}, SEND};
constexpr DiceSound XF_BREBREAKBE[] = {{kSy, "NOISESWP", 5}, {kFm, "COWBELL", 4}, {kSy, "RISER", 4},
                                       {kSy, "WHOOSH", 4}, {kFm, "TOM HI", 3}, {kFm, "TOM LO", 3}, {kSy, "ZAP", 2},
                                       SEND};
constexpr DiceSound XB_BREUKG2STE[] = {{kSy, "SUBBASS", 7}, {kFm, "SUB FM", 5}, {kCh, "SUB SINE", 3},
                                       {kSy, "UKGBASS", 6}, {kFm, "BASS DEP", 4}, {kSy, "DEEPHSE", 4},
                                       {kSy, "808 BASS", 3}, SEND};
constexpr DiceSound XC_BREUKG2STE[] = {{kFm, "E.PIANO", 5}, {kSy, "ORGAN", 5}, {kSy, "STAB", 4}, {kSy, "EP DEEP", 5},
                                       {kSy, "HOUSEORG", 4}, {kFm, "ORGAN HS", 4}, {kFm, "EP DX", 3},
                                       {kCh, "CHIPSTAB", 3}, {kSt, "STEELPAN", 2}, SEND};
constexpr DiceSound XF_BREUKG2STE[] = {{kFm, "CONGA", 6}, {kSy, "RISER", 5}, {kSy, "NOISESWP", 3}, {kFm, "SHAKER", 4},
                                       {kFm, "RIM", 3}, {kSy, "WHOOSH", 2}, SEND};
constexpr DiceSound XB_BREELECTRO[] = {{kSy, "MOOGISH", 5}, {kFm, "FM BASS", 5}, {kSy, "BASS", 4},
                                       {kSy, "ELECBASS", 5}, {kFm, "ELEC BS", 5}, {kCh, "OUTBASS", 3},
                                       {kCh, "PWM BASS", 2}, {kCh, "GB BASS", 2}, SEND};
constexpr DiceSound XL_BREELECTRO[] = {{kSy, "WT LEAD", 6}, {kCh, "FLUTE", 4}, {kCh, "NES LEAD", 4},
                                       {kCh, "C64 LEAD", 3}, {kCh, "GBLEAD", 2}, {kSy, "ELECTRLD", 6},
                                       {kSy, "BRAINLD", 2}, SEND};
constexpr DiceSound XA_BREELECTRO[] = {{kSy, "PING", 5}, {kSy, "PLUCK", 4}, {kSy, "SYNCPLK", 3}, {kSy, "BLIP", 4},
                                       {kCh, "SID ARP", 3}, SEND};
constexpr DiceSound XF_BREELECTRO[] = {{kSy, "NOISESWP", 5}, {kFm, "COWBELL", 4}, {kSy, "RISER", 4},
                                       {kSy, "LASER", 4}, {kSy, "ZAP", 4}, {kFm, "LASER", 3}, {kFm, "CLAVE", 3},
                                       {kFm, "TOM ELEC", 3}, SEND};
constexpr DiceSound XB_HIPBOOMBAP[] = {{kSy, "SUBBASS", 5}, {kSy, "MOOGISH", 5}, {kFm, "DX BASS", 3},
                                       {kSy, "LOFIBASS", 5}, {kFm, "BS ROUND", 4}, {kSt, "STR BASS", 4},
                                       {kSy, "808 BASS", 2}, SEND};
constexpr DiceSound XC_HIPBOOMBAP[] = {{kFm, "E.PIANO", 6}, {kFm, "EP SOFT", 5}, {kSy, "ORGAN", 3},
                                       {kSy, "WARM PAD", 3}, {kSy, "LOFIKEYS", 4}, {kSy, "EP SOFT", 4},
                                       {kFm, "EP LOFI", 3}, {kSy, "VIBES", 3}, {kSy, "CLAV", 2}, {kCh, "VIBES", 2},
                                       SEND};
constexpr DiceSound XL_HIPBOOMBAP[] = {{kCh, "FLUTE", 6}, {kFm, "FM LEAD", 4}, {kSy, "SOLO", 4}, {kFm, "BRASS", 3},
                                       {kSy, "FLUTE", 3}, {kSy, "SINELEAD", 2}, SEND};
constexpr DiceSound XF_HIPBOOMBAP[] = {{kFm, "WOODBLK", 4}, {kCh, "WIND", 3}, {kSy, "NOISESWP", 3},
                                       {kFm, "RIM SOFT", 3}, {kFm, "SHKR SFT", 2}, {kFm, "TOM LO", 2}, SEND};
constexpr DiceSound XB_HIPLOFI[] = {{kSy, "SUBBASS", 5}, {kSy, "MOOGISH", 5}, {kFm, "DX BASS", 3},
                                    {kSy, "LOFIBASS", 5}, {kFm, "BS ROUND", 4}, {kSt, "STR BASS", 4},
                                    {kCh, "ROUND SB", 3}, SEND};
constexpr DiceSound XC_HIPLOFI[] = {{kFm, "EP SOFT", 6}, {kFm, "E.PIANO", 5}, {kSy, "WARM PAD", 3},
                                    {kCh, "GLASSPAD", 2}, {kSy, "LOFIKEYS", 5}, {kFm, "EP LOFI", 5},
                                    {kSy, "EP SOFT", 4}, {kSy, "CHILLPAD", 4}, {kFm, "EP DEEP", 3},
                                    {kCh, "DRIFTPAD", 3}, {kSt, "TINE EP", 3}, {kCh, "EP SOFT", 3}, {kSy, "HALO", 2},
                                    SEND};
constexpr DiceSound XA_HIPLOFI[] = {{kSt, "NYLON", 5}, {kSt, "KALIMBA", 4}, {kSt, "HARP", 3}, {kSt, "LOFIGTR", 5},
                                    {kSt, "SOFTPLK", 3}, {kSy, "SOFTPLK", 3}, {kFm, "KALIMBA", 3}, {kSy, "VIBES", 3},
                                    {kSt, "VIBRA", 3}, {kCh, "VIBES", 2}, SEND};
constexpr DiceSound XF_HIPLOFI[] = {{kCh, "WIND", 6}, {kFm, "WOODBLK", 3}, {kFm, "RIM SOFT", 3}, {kFm, "SHKR SFT", 3},
                                    {kSy, "WHOOSH", 2}, SEND};
constexpr DiceSound XB_HIPTRAP[] = {{kSy, "SUBBASS", 7}, {kFm, "SUB FM", 5}, {kCh, "SUB SINE", 3},
                                    {kSy, "808 BASS", 6}, {kSy, "808 GLD", 5}, {kFm, "808 FM", 4},
                                    {kFm, "HALF SUB", 3}, {kCh, "ROUND SB", 2}, SEND};
constexpr DiceSound XC_HIPTRAP[] = {{kSy, "DARK PAD", 6}, {kSy, "CHOIR", 5}, {kSy, "BELL", 3}, {kSy, "STRNGDRK", 3},
                                    {kSy, "ABYSS", 2}, {kCh, "DARKPAD", 2}, SEND};
constexpr DiceSound XA_HIPTRAP[] = {{kFm, "GLOCK", 4}, {kSt, "HARP", 4}, {kSy, "BELL", 4}, {kFm, "DX BELL", 3},
                                    {kSt, "BELL", 3}, {kSy, "SINELEAD", 2}, SEND};
constexpr DiceSound XF_HIPTRAP[] = {{kFm, "WOODBLK", 4}, {kCh, "WIND", 3}, {kSy, "NOISESWP", 3}, {kSy, "IMPACT", 3},
                                    {kSy, "WHOOSH", 2}, {kSy, "DOWNLIFT", 2}, SEND};
constexpr DiceSound XB_IDMBRAINDA[] = {{kFm, "FM BASS", 6}, {kSy, "PLUCKBAS", 5}, {kCh, "TRI BASS", 4},
                                       {kCh, "C64 BASS", 2}, {kFm, "ELEC BS", 4}, {kSy, "ELECBASS", 3},
                                       {kCh, "GB BASS", 3}, {kSy, "MINIBASS", 2}, SEND};
constexpr DiceSound XC_IDMBRAINDA[] = {{kSy, "PAD", 6}, {kCh, "GLASSPAD", 5}, {kFm, "DX BELL", 3},
                                       {kSy, "WTSWEEP", 3}, {kSy, "GLASSPAD", 3}, {kSy, "SPACEPAD", 2}, SEND};
constexpr DiceSound XL_IDMBRAINDA[] = {{kSy, "WT LEAD", 6}, {kCh, "FLUTE", 4}, {kCh, "NES LEAD", 4},
                                       {kCh, "C64 LEAD", 3}, {kCh, "GBLEAD", 2}, {kSy, "BRAINLD", 5},
                                       {kCh, "HOLLOW", 3}, {kFm, "GLASS LD", 3}, {kSy, "ELECTRLD", 2}, SEND};
constexpr DiceSound XA_IDMBRAINDA[] = {{kFm, "GLOCK", 6}, {kSy, "CHIPPLK", 4}, {kCh, "ARP PLK", 4},
                                       {kCh, "ZX ARP", 2}, {kSy, "GLITCHPL", 4}, {kSy, "BLIP", 4}, {kCh, "PLINK", 4},
                                       {kCh, "4BIT ARP", 3}, {kCh, "RND BLIP", 3}, {kFm, "MALLET", 3},
                                       {kSt, "WOOD", 2}, SEND};
constexpr DiceSound XF_IDMBRAINDA[] = {{kSy, "ROBOT", 5}, {kSy, "SCIFI", 5}, {kCh, "COIN", 3}, {kCh, "BLIP", 3},
                                       {kFm, "ZAP", 3}, {kSy, "ZAP", 3}, {kFm, "BLIP", 3}, {kFm, "CLICK", 3},
                                       {kSy, "LASER", 2}, SEND};
constexpr DiceSound XB_IDMGLITCH[] = {{kFm, "FM BASS", 6}, {kSy, "PLUCKBAS", 5}, {kCh, "TRI BASS", 4},
                                      {kCh, "C64 BASS", 2}, {kCh, "GB BASS", 4}, {kSy, "MINIBASS", 3}, SEND};
constexpr DiceSound XC_IDMGLITCH[] = {{kSy, "PAD", 6}, {kCh, "GLASSPAD", 5}, {kFm, "DX BELL", 3}, {kSy, "WTSWEEP", 3},
                                      {kSy, "GLASSPAD", 2}, SEND};
constexpr DiceSound XA_IDMGLITCH[] = {{kFm, "GLOCK", 6}, {kSy, "CHIPPLK", 4}, {kCh, "ARP PLK", 4}, {kCh, "ZX ARP", 2},
                                      {kSy, "GLITCHPL", 6}, {kSy, "BLIP", 5}, {kCh, "RND BLIP", 4},
                                      {kCh, "4BIT ARP", 4}, {kSt, "WOOD", 3}, SEND};
constexpr DiceSound XF_IDMGLITCH[] = {{kSy, "ROBOT", 5}, {kSy, "SCIFI", 5}, {kCh, "COIN", 3}, {kCh, "BLIP", 3},
                                      {kFm, "ZAP", 3}, {kFm, "CLICK", 5}, {kFm, "BLIP", 4}, {kSy, "ZAP", 3}, SEND};
constexpr DiceSound XB_IDMELECTRO[] = {{kFm, "FM BASS", 6}, {kSy, "PLUCKBAS", 5}, {kCh, "TRI BASS", 4},
                                       {kCh, "C64 BASS", 2}, {kCh, "GB BASS", 2}, {kFm, "ELEC BS", 2}, SEND};
constexpr DiceSound XC_IDMELECTRO[] = {{kSy, "PAD", 6}, {kCh, "GLASSPAD", 5}, {kFm, "DX BELL", 3},
                                       {kSy, "WTSWEEP", 3}, {kSy, "GLASSPAD", 5}, {kSy, "SPACEPAD", 4},
                                       {kFm, "PAD GLAS", 4}, SEND};
constexpr DiceSound XL_IDMELECTRO[] = {{kSy, "WT LEAD", 6}, {kCh, "FLUTE", 4}, {kCh, "NES LEAD", 4},
                                       {kCh, "C64 LEAD", 3}, {kCh, "GBLEAD", 2}, {kSy, "FLUTE", 4},
                                       {kFm, "FLUTE FM", 4}, {kCh, "SOFT SIN", 4}, {kCh, "HOLLOW", 4},
                                       {kFm, "GLASS LD", 3}, SEND};
constexpr DiceSound XA_IDMELECTRO[] = {{kFm, "GLOCK", 4}, {kSt, "HARP", 4}, {kSy, "BELL", 4}, {kFm, "DX BELL", 3},
                                       {kSy, "MARIMBA", 4}, {kFm, "MALLET", 4}, {kFm, "CELESTA", 3},
                                       {kFm, "KALIMBA", 3}, {kFm, "EP BELL", 3}, {kSt, "KOTO", 3}, {kSt, "VIBRA", 3},
                                       {kSt, "GLOCK", 3}, {kCh, "GLASSARP", 3}, {kCh, "PLINK", 3}, {kSy, "VIBES", 2},
                                       {kSt, "STEELPAN", 2}, SEND};
constexpr DiceSound XF_IDMELECTRO[] = {{kSy, "ROBOT", 5}, {kSy, "SCIFI", 5}, {kCh, "COIN", 3}, {kCh, "BLIP", 3},
                                       {kFm, "ZAP", 3}, {kSy, "SPACEDRN", 3}, {kFm, "TABLA", 2}, {kFm, "CLICK", 2},
                                       SEND};
constexpr DiceSound XC_AMBDREAM[] = {{kSy, "SHIMMER", 6}, {kSy, "WARM PAD", 5}, {kSy, "CHOIR", 4},
                                     {kCh, "GLASSPAD", 3}, {kSy, "DREAMPAD", 6}, {kSy, "HALO", 4},
                                     {kSy, "SPACEPAD", 4}, {kSy, "GLASSPAD", 3}, {kSy, "CHILLPAD", 3},
                                     {kSy, "VOX PAD", 3}, {kFm, "PAD WARM", 3}, {kFm, "PAD GLAS", 3},
                                     {kCh, "SOFT PAD", 3}, {kCh, "CHOIR", 3}, {kSt, "SHIMMER", 3}, {kSt, "BOWED", 2},
                                     SEND};
constexpr DiceSound XL_AMBDREAM[] = {{kCh, "FLUTE", 4}, {kSy, "SOLO", 3}, {kFm, "FM LEAD", 3}, {kSy, "SOFTLEAD", 4},
                                     {kSy, "FLUTE", 4}, {kFm, "FLUTE FM", 4}, {kCh, "SOFT SIN", 4},
                                     {kSy, "SINELEAD", 3}, {kFm, "GLASS LD", 2}, SEND};
constexpr DiceSound XA_AMBDREAM[] = {{kFm, "GLOCK", 4}, {kSt, "HARP", 4}, {kSy, "BELL", 4}, {kFm, "DX BELL", 3},
                                     {kSy, "DREAMPLK", 4}, {kSt, "AIRHARP", 4}, {kSt, "KOTO", 3}, {kSt, "GLOCK", 3},
                                     {kFm, "CELESTA", 3}, {kFm, "KALIMBA", 3}, {kCh, "BELL ARP", 3},
                                     {kCh, "GLASSARP", 3}, {kFm, "EP BELL", 2}, SEND};
constexpr DiceSound XF_AMBDREAM[] = {{kCh, "WIND", 6}, {kSy, "NOISESWP", 5}, {kFm, "GONG", 3}, {kSy, "WIND", 4},
                                     {kSy, "WHOOSH", 3}, {kSt, "CHIMES", 3}, {kFm, "TRIANGL", 2}, SEND};
constexpr DiceSound XB_AMBDARK[] = {{kSy, "SUBBASS", 7}, {kFm, "SUB FM", 5}, {kCh, "SUB SINE", 3},
                                    {kFm, "DUB BASS", 3}, {kSy, "DUBTECH", 3}, SEND};
constexpr DiceSound XC_AMBDARK[] = {{kSy, "DARK PAD", 6}, {kSy, "WTSWEEP", 4}, {kSy, "CHOIR", 3},
                                    {kSy, "STRNGDRK", 4}, {kSy, "DRONEDRK", 4}, {kSy, "ABYSS", 4},
                                    {kFm, "PAD DARK", 4}, {kCh, "DARKPAD", 3}, {kSt, "DARKDRON", 3},
                                    {kSy, "DRONE", 3}, SEND};
constexpr DiceSound XF_AMBDARK[] = {{kFm, "GONG", 5}, {kCh, "WIND", 5}, {kSy, "SCIFI", 3}, {kSy, "WIND", 4},
                                    {kSy, "SPACEDRN", 4}, SEND};
constexpr DiceSound XB_AMBDRONE[] = {{kSy, "SUBBASS", 7}, {kFm, "SUB FM", 5}, {kCh, "SUB SINE", 3},
                                     {kSy, "DEEPSUB", 3}, SEND};
constexpr DiceSound XC_AMBDRONE[] = {{kSy, "SHIMMER", 6}, {kSy, "WARM PAD", 5}, {kSy, "CHOIR", 4},
                                     {kCh, "GLASSPAD", 3}, {kSy, "DRONE", 6}, {kSy, "DRONEDRK", 4},
                                     {kSy, "SPACEPAD", 4}, {kSt, "GLASDRON", 4}, {kSy, "ABYSS", 3},
                                     {kSt, "DARKDRON", 3}, {kSt, "BOWED", 3}, SEND};
constexpr DiceSound XF_AMBDRONE[] = {{kCh, "WIND", 6}, {kSy, "NOISESWP", 5}, {kFm, "GONG", 3}, {kSy, "SPACEDRN", 5},
                                     {kSy, "WIND", 4}, SEND};
constexpr DiceSound XB_SYNOUTRUN[] = {{kSy, "BASS", 5}, {kSy, "MOOGISH", 5}, {kFm, "DX BASS", 4},
                                      {kSy, "PLUCKBAS", 3}, {kSy, "OUTRUNBS", 5}, {kFm, "OUTRUN B", 4},
                                      {kCh, "OUTBASS", 4}, {kFm, "DISCO BS", 2}, {kCh, "PWM BASS", 2}, SEND};
constexpr DiceSound XC_SYNOUTRUN[] = {{kSy, "PAD", 6}, {kSy, "PWMSTR", 5}, {kCh, "PWM PAD", 3}, {kFm, "E.PIANO", 3},
                                      {kSy, "OUTRUNPD", 5}, {kSy, "POLYSYN", 5}, {kSy, "STRINGS", 3},
                                      {kSy, "BRASS", 3}, {kFm, "EP DX", 3}, {kFm, "STRNG FM", 3}, {kCh, "CHOIR", 2},
                                      SEND};
constexpr DiceSound XL_SYNOUTRUN[] = {{kSy, "SOLO", 6}, {kSy, "LEAD", 5}, {kFm, "BRASS", 3}, {kSy, "SUPERSAW", 3},
                                      {kSy, "OUTRUNLD", 6}, {kFm, "RUN LEAD", 4}, {kCh, "OUTRUN", 4},
                                      {kCh, "SOFT SQ", 3}, {kCh, "WARM PWM", 3}, SEND};
constexpr DiceSound XA_SYNOUTRUN[] = {{kSy, "PLUCK", 6}, {kCh, "ARP PLK", 4}, {kSy, "PLUCK WT", 3},
                                      {kSy, "ARPPLK", 4}, {kCh, "SOFT ARP", 4}, {kCh, "SID ARP", 3}, SEND};
constexpr DiceSound XF_SYNOUTRUN[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 5}, {kCh, "SIREN", 2}, {kSy, "WHOOSH", 3},
                                      {kSy, "LASER", 3}, {kSy, "IMPACT", 2}, SEND};
constexpr DiceSound XB_SYNDARKSYN[] = {{kSy, "BASS", 6}, {kSy, "REESE", 5}, {kSy, "FM GROWL", 3},
                                       {kSy, "DARKBASS", 5}, {kFm, "DARK BS", 4}, {kCh, "OUTBASS", 3},
                                       {kSy, "OUTRUNBS", 2}, SEND};
constexpr DiceSound XC_SYNDARKSYN[] = {{kSy, "DARK PAD", 6}, {kSy, "WTSWEEP", 4}, {kSy, "CHOIR", 3},
                                       {kSy, "STRNGDRK", 4}, {kSy, "POLYSYN", 3}, {kFm, "PAD DARK", 3},
                                       {kCh, "DARKPAD", 3}, SEND};
constexpr DiceSound XL_SYNDARKSYN[] = {{kSy, "WT LEAD", 6}, {kSy, "HOOVER", 5}, {kSy, "SUPERSAW", 3},
                                       {kSy, "DARKLEAD", 5}, {kCh, "DARKLEAD", 4}, {kFm, "RUN LEAD", 3},
                                       {kCh, "OUTRUN", 2}, SEND};
constexpr DiceSound XA_SYNDARKSYN[] = {{kSy, "PLUCK", 6}, {kCh, "ARP PLK", 4}, {kSy, "PLUCK WT", 3},
                                       {kSy, "ARPPLK", 3}, SEND};
constexpr DiceSound XF_SYNDARKSYN[] = {{kSy, "NOISESWP", 6}, {kSy, "RISER", 5}, {kCh, "SIREN", 2}, {kSy, "IMPACT", 4},
                                       {kSy, "LASER", 3}, {kSy, "DOWNLIFT", 2}, SEND};
constexpr DiceSound XB_SYNCHILLWA[] = {{kSy, "BASS", 5}, {kSy, "MOOGISH", 5}, {kFm, "DX BASS", 4},
                                       {kSy, "PLUCKBAS", 3}, {kSy, "OUTRUNBS", 3}, {kFm, "OUTRUN B", 3},
                                       {kCh, "ROUND SB", 3}, {kSy, "DISCOBAS", 2}, SEND};
constexpr DiceSound XC_SYNCHILLWA[] = {{kSy, "WARM PAD", 5}, {kFm, "EP SOFT", 5}, {kCh, "GLASSPAD", 4},
                                       {kSy, "CHILLPAD", 5}, {kSy, "HALO", 4}, {kCh, "DRIFTPAD", 4},
                                       {kSy, "DREAMPAD", 3}, {kSy, "POLYSYN", 3}, {kSy, "LOFIKEYS", 3},
                                       {kCh, "SOFT PAD", 3}, {kFm, "EP LOFI", 2}, {kFm, "PAD WARM", 2}, SEND};
constexpr DiceSound XL_SYNCHILLWA[] = {{kSy, "SOLO", 6}, {kSy, "LEAD", 5}, {kFm, "BRASS", 3}, {kSy, "SUPERSAW", 3},
                                       {kSy, "SOFTLEAD", 4}, {kSy, "SINELEAD", 4}, {kFm, "CHILL LD", 4},
                                       {kCh, "SOFT SQ", 3}, {kCh, "SOFT SIN", 3}, {kCh, "HOLLOW", 2}, SEND};
constexpr DiceSound XA_SYNCHILLWA[] = {{kFm, "GLOCK", 4}, {kSt, "HARP", 4}, {kSy, "BELL", 4}, {kFm, "DX BELL", 3},
                                       {kSy, "DREAMPLK", 4}, {kCh, "BELL ARP", 3}, {kSt, "LOFIGTR", 3},
                                       {kSt, "GLOCK", 2}, SEND};
constexpr DiceSound XF_SYNCHILLWA[] = {{kCh, "WIND", 6}, {kSy, "NOISESWP", 5}, {kFm, "GONG", 3}, {kSy, "WHOOSH", 2},
                                       SEND};

// ---- Progressions --------------------------------------------------------------------------------
constexpr DiceName PR_DEEP[] = {{"DEEP 2", 6}, {"DEEP 1MI9", 5}, {"DEEP 21", 4}, {"HOUSE 2", 4}, {"DORIAN 12", 3}, END};
constexpr DiceName PR_TECHH[] = {{"DUB 17", 5}, {"DORIAN 14", 4}, {"PHRYG 12", 3}, END};
constexpr DiceName PR_ACID[] = {{"DUB 17", 5}, {"PHRYG 12", 4}, {"DORIAN 12", 3}, END};
constexpr DiceName PR_DISCO[] = {{"DORIAN 12", 5}, {"FUNK 14", 4}, {"ROYAL RD", 4}, {"CITY POP", 3}, END};
constexpr DiceName PR_PROG[] = {{"MIN 1636", 5}, {"POP 6415", 5}, {"TRNC 6471", 4}, {"TRNC EPIC", 4}, END};
constexpr DiceName PR_TECHNO[] = {{"DUB 17", 5}, {"PHRYG 12", 5}, {"DEEP 2", 3}, END};
constexpr DiceName PR_DUB[] = {{"DUB 1b6", 6}, {"DEEP 2", 5}, {"DUB 17", 4}, {"DEEP 1MI9", 3}, END};
constexpr DiceName PR_MELODIC[] = {{"MIN 1636", 6}, {"TRNC EPIC", 5}, {"MIN 1767", 4}, {"POP 6415", 3}, END};
constexpr DiceName PR_TRANCE[] = {{"TRNC EPIC", 7}, {"TRNC 6471", 5}, {"TRNC 1671", 5}, {"MIN 1636", 4}, {"POP 6415", 3}, END};
constexpr DiceName PR_PSY[] = {{"PHRYG 12", 6}, {"DUB 17", 4}, {"MIN 1451", 3}, END};
constexpr DiceName PR_GOA[] = {{"PHRYG 12", 5}, {"ANDALUS", 5}, {"MIN 1767", 4}, END};
constexpr DiceName PR_LIQUID[] = {{"LIQUID", 7}, {"NEO SOUL", 5}, {"DEEP 1MI9", 4}, {"SOUL 25", 4}, {"JAZZ 3625", 3}, END};
constexpr DiceName PR_DNB[] = {{"DUB 17", 5}, {"MIN 1767", 4}, {"PHRYG 12", 3}, {"DEEP 2", 3}, END};
constexpr DiceName PR_NEURO[] = {{"PHRYG 12", 6}, {"DUB 17", 4}, END};
constexpr DiceName PR_JUNGLE[] = {{"DEEP 2", 5}, {"DUB 1b6", 4}, {"MIN 1767", 3}, {"DORIAN 14", 3}, END};
constexpr DiceName PR_HALF[] = {{"MIN 1636", 5}, {"PHRYG 12", 4}, {"DUB 1b6", 4}, END};
constexpr DiceName PR_BREAKS[] = {{"MIN 1767", 5}, {"DORIAN 14", 4}, {"DUB 17", 4}, END};
constexpr DiceName PR_UKG[] = {{"DEEP 1MI9", 5}, {"SOUL 25", 5}, {"NEO SOUL", 4}, {"HOUSE 2", 4}, END};
constexpr DiceName PR_ELECTRO[] = {{"PHRYG 12", 5}, {"DUB 17", 5}, {"MIN 1767", 3}, END};
constexpr DiceName PR_BOOM[] = {{"SOUL 25", 5}, {"JAZZ 251", 4}, {"MIN 251", 4}, {"DORIAN 12", 4}, {"DEEP 2", 3}, END};
constexpr DiceName PR_LOFI[] = {{"LOFI", 7}, {"NEO SOUL", 5}, {"JAZZ 1625", 4}, {"ROYAL RD", 4}, {"JAZZ 251", 3}, END};
constexpr DiceName PR_TRAP[] = {{"MIN 1636", 5}, {"MIN 1767", 4}, {"ANDALUS", 4}, {"PHRYG 12", 3}, END};
constexpr DiceName PR_IDM[] = {{"LYDIAN 12", 4}, {"DORIAN 12", 4}, {"QUARTAL", 4}, {"SUS DRIFT", 3}, {"CINEMA", 3}, END};
constexpr DiceName PR_GLITCH[] = {{"QUARTAL", 4}, {"DORIAN 14", 4}, {"PHRYG 12", 3}, END};
constexpr DiceName PR_ELNICA[] = {{"CINEMA", 5}, {"SUS DRIFT", 4}, {"LYDIAN 12", 4}, {"POP 1645", 3}, END};
constexpr DiceName PR_DREAM[] = {{"AMBIENT", 7}, {"SUS DRIFT", 5}, {"LYDIAN 12", 4}, {"CINEMA", 3}, END};
constexpr DiceName PR_DARKAMB[] = {{"PHRYG 12", 5}, {"DUB 1b6", 5}, {"MIN 1767", 3}, END};
constexpr DiceName PR_DRONE[] = {{"DUB 17", 4}, {"SUS DRIFT", 3}, {"LYDIAN 12", 3}, END};
constexpr DiceName PR_OUTRUN[] = {{"SW 1671", 7}, {"SW 6455", 5}, {"TRNC 6471", 4}, {"POP 6415", 4}, END};
constexpr DiceName PR_DARKSW[] = {{"SW 1671", 5}, {"MIN 1636", 5}, {"ANDALUS", 4}, {"PHRYG 12", 3}, END};
constexpr DiceName PR_CHILL[] = {{"ROYAL RD", 5}, {"POP 1645", 4}, {"LOFI", 4}, {"CITY POP", 4}, END};

// ---- Chord rhythms -------------------------------------------------------------------------------
constexpr DiceName RH_DEEP[] = {{"HOLD", 4}, {"HOUSE PNO", 4}, {"OFFBEAT", 3}, {"HALF", 3}, END};
constexpr DiceName RH_STAB[] = {{"STAB", 5}, {"OFFBEAT", 4}, {"DUB SKNK", 3}, {"SYNCOPA", 3}, END};
constexpr DiceName RH_DUB[] = {{"OFFBEAT", 6}, {"DUB SKNK", 4}, {"STAB", 3}, END};
constexpr DiceName RH_PAD[] = {{"HOLD", 7}, {"HALF", 4}, {"SWELL", 2}, END};
constexpr DiceName RH_TRANCE[] = {{"HOLD", 5}, {"TRNC GATE", 5}, {"OFFBEAT", 3}, END};
constexpr DiceName RH_DISCO[] = {{"OFFBEAT", 5}, {"8THS", 4}, {"FUNK", 4}, {"POP 8", 3}, END};
constexpr DiceName RH_KEYS[] = {{"PNO COMP", 4}, {"LATE HOLD", 4}, {"HALF", 4}, {"BALLAD", 3}, END};
constexpr DiceName RH_UKG[] = {{"SYNCOPA", 5}, {"STAB", 4}, {"CHOP 3", 3}, {"HOUSE PNO", 3}, END};
constexpr DiceName RH_SW[] = {{"HOLD", 5}, {"8THS", 4}, {"HALF", 4}, {"POP 8", 3}, END};
constexpr DiceName RH_IDM[] = {{"STUTTER", 4}, {"SYNCOPA", 4}, {"HALF", 4}, {"TRESILLO", 3}, END};
constexpr DiceName RH_DNB[] = {{"HOLD", 5}, {"HALF", 4}, {"STAB", 3}, END};
constexpr DiceName RH_AMB[] = {{"HOLD", 8}, {"SWELL", 3}, END};

// ---- Bass patterns -------------------------------------------------------------------------------
constexpr DiceName BA_DEEP[] = {{"DEEP HOUSE", 6}, {"HOUSE OFF", 5}, {"SYNCOPA", 3}, {"OCT HOLD", 3}, END};
constexpr DiceName BA_TECHH[] = {{"TECHNO OFF", 5}, {"HOUSE OFF", 5}, {"ROLLING", 3}, END};
constexpr DiceName BA_ACID[] = {{"ACID 1", 5}, {"ACID 2", 5}, {"ACID 3", 4}, {"ACID 4", 4}, END};
constexpr DiceName BA_DISCO[] = {{"DISCO OCT", 7}, {"NU DISCO", 5}, {"ITALO", 4}, {"FUNK", 3}, END};
constexpr DiceName BA_PROG[] = {{"TRNC OFF", 5}, {"ROLLING", 4}, {"HOUSE OFF", 3}, END};
constexpr DiceName BA_MIN[] = {{"MINIMAL", 5}, {"TECHNO OFF", 5}, {"HOUSE OFF", 3}, END};
constexpr DiceName BA_DUBT[] = {{"MINIMAL", 5}, {"SUB HOLD", 4}, {"DUB", 3}, END};
constexpr DiceName BA_PEAK[] = {{"TECHNO RL", 6}, {"TECHNO 16", 4}, {"ROLLING", 4}, END};
constexpr DiceName BA_HARD[] = {{"TECHNO RL", 6}, {"TECHNO 16", 5}, {"EBM", 3}, END};
constexpr DiceName BA_MELT[] = {{"TECHNO RL", 5}, {"ROLLING", 5}, {"TRNC OFF", 3}, END};
constexpr DiceName BA_TRANCE[] = {{"TRNC OFF", 8}, {"ROLLING", 4}, {"GALLOP", 3}, END};
constexpr DiceName BA_PSY[] = {{"PSY", 7}, {"PSY TRIP", 5}, {"GALLOP", 4}, END};
constexpr DiceName BA_GOA[] = {{"PSY", 5}, {"GALLOP", 5}, {"ROLLING", 4}, END};
constexpr DiceName BA_DNB[] = {{"DNB", 6}, {"SUB HOLD", 4}, {"REESE", 4}, END};
constexpr DiceName BA_JUMP[] = {{"REESE", 4}, {"DNB", 4}, {"OCT HOLD", 4}, {"DUBSTEP", 3}, END};
constexpr DiceName BA_NEURO[] = {{"REESE", 6}, {"DNB", 4}, {"DUBSTEP", 3}, END};
constexpr DiceName BA_JUNGLE[] = {{"SUB HOLD", 5}, {"DNB", 5}, {"DUB", 4}, {"ONE DROP", 3}, END};
constexpr DiceName BA_HALF[] = {{"DUBSTEP", 6}, {"SUB HOLD", 4}, {"REESE", 4}, END};
constexpr DiceName BA_BREAKS[] = {{"BREAKBEAT", 6}, {"SYNCOPA", 4}, {"ELECTRO", 3}, END};
constexpr DiceName BA_UKG[] = {{"UKG", 7}, {"GARAGE", 5}, {"SYNCOPA", 3}, END};
constexpr DiceName BA_ELECTRO[] = {{"ELECTRO", 7}, {"SYNCOPA", 4}, {"EBM", 3}, END};
constexpr DiceName BA_BOOM[] = {{"ROOT 5TH", 4}, {"FUNK", 4}, {"MOTOWN", 4}, {"WALK STEP", 3}, END};
constexpr DiceName BA_LOFI[] = {{"WALKING", 5}, {"ROOT 5TH", 4}, {"BOSSA", 3}, {"MOTOWN", 3}, END};
constexpr DiceName BA_TRAP[] = {{"TRAP 808", 8}, {"SUB HOLD", 4}, {"DUBSTEP", 3}, END};
constexpr DiceName BA_IDM[] = {{"SYNCOPA", 4}, {"ARPEGGIO", 4}, {"ACID 3", 3}, {"BREAKBEAT", 3}, END};
constexpr DiceName BA_GLITCH[] = {{"SYNCOPA", 4}, {"OCT JUMP", 3}, {"ELECTRO", 3}, END};
constexpr DiceName BA_ELNICA[] = {{"ROOT 5TH", 4}, {"SYNCOPA", 4}, {"OCT HOLD", 4}, END};
constexpr DiceName BA_AMB[] = {{"SUB HOLD", 7}, {"OCT HOLD", 3}, END};
constexpr DiceName BA_SW[] = {{"SYNTHW 8", 8}, {"ROCK 8", 5}, {"ITALO", 4}, {"OCT JUMP", 3}, END};
constexpr DiceName BA_DARKSW[] = {{"SYNTHW 8", 5}, {"ROLLING", 4}, {"EBM", 4}, END};
constexpr DiceName BA_CHILL[] = {{"ROOT 5TH", 5}, {"OCT HOLD", 4}, {"MOTOWN", 3}, END};

// ---- Arp patterns --------------------------------------------------------------------------------
constexpr DiceName AR_HOUSE[] = {{"HO OFFBT", 5}, {"HO PIANO", 4}, {"OFFBEAT", 4}, {"8THS", 3}, END};
constexpr DiceName AR_TECHNO[] = {{"TE HYPNO3", 5}, {"TE STAB", 4}, {"TE ROLLER", 4}, {"TE MINIMAL", 4}, END};
constexpr DiceName AR_ACID[] = {{"ACID 1", 5}, {"ACID 2", 4}, {"ACID 3", 4}, END};
constexpr DiceName AR_TRANCE[] = {{"TR GATE", 5}, {"TR ROLL", 5}, {"16THS", 4}, {"TR 332", 4}, {"TR CHUG", 3}, END};
constexpr DiceName AR_PSY[] = {{"PSY GALOP", 6}, {"PSY TRIPL", 4}, {"16THS", 3}, END};
constexpr DiceName AR_DNB[] = {{"DNB ROLL", 4}, {"DNB STAB", 4}, {"8THS", 4}, {"DNB 2STEP", 3}, END};
constexpr DiceName AR_BREAKS[] = {{"BR BREAK", 5}, {"EL FUNK", 4}, {"DOTTED", 4}, END};
constexpr DiceName AR_ELECTRO[] = {{"EL ROBOT", 5}, {"EL FUNK", 5}, {"16THS", 3}, END};
constexpr DiceName AR_HIPHOP[] = {{"QUARTERS", 4}, {"DOTTED", 4}, {"8THS", 3}, END};
constexpr DiceName AR_IDM[] = {{"TRIPLET", 4}, {"CHIP RUN", 4}, {"CHIP OCT", 4}, {"DOTTED", 4}, {"16THS", 3}, END};
constexpr DiceName AR_AMB[] = {{"QUARTERS", 5}, {"DOTTED", 5}, {"8THS", 4}, END};
constexpr DiceName AR_SW[] = {{"SW 8THS", 6}, {"SW DRIVE", 5}, {"16THS", 4}, {"8THS", 3}, END};
constexpr DiceName AR_DUB[] = {{"DUB ECHO", 6}, {"DUB STAB", 5}, END};
constexpr DiceName AR_UKG[] = {{"HO SHUFFLE", 4}, {"DOTTED", 4}, {"OFFBEAT", 3}, END};

constexpr DiceVal MD_UP[] = {{am(ArpMode::Up), 6}, {am(ArpMode::UpDown), 4}, {am(ArpMode::Random), 2}, VEND};
constexpr DiceVal MD_TR[] = {{am(ArpMode::Up), 5}, {am(ArpMode::UpDown), 4}, {am(ArpMode::DownUp), 3},
                             {am(ArpMode::Pedal), 3}, VEND};
constexpr DiceVal MD_PSY[] = {{am(ArpMode::Pedal), 5}, {am(ArpMode::Up), 4}, {am(ArpMode::Random), 4}, VEND};
constexpr DiceVal MD_IDM[] = {{am(ArpMode::Random), 6}, {am(ArpMode::Converge), 4}, {am(ArpMode::Diverge), 4},
                              {am(ArpMode::Up), 3}, VEND};
constexpr DiceVal MD_AMB[] = {{am(ArpMode::UpDown), 5}, {am(ArpMode::Up), 5}, {am(ArpMode::Played), 3}, VEND};

// ---- Lead ----------------------------------------------------------------------------------------
constexpr DiceVal LM_MOTIF[] = {{ld(DiceLead::Motif), 7}, {ld(DiceLead::Arp), 3}, VEND};
constexpr DiceVal LM_ARP[] = {{ld(DiceLead::Arp), 6}, {ld(DiceLead::Motif), 4}, VEND};
constexpr DiceVal LM_SOME[] = {{ld(DiceLead::Motif), 5}, {ld(DiceLead::None), 4}, {ld(DiceLead::Arp), 2}, VEND};

constexpr DiceName MO_SOFT[] = {{"SPARSE", 5}, {"SIGH", 4}, {"CALL", 3}, END};
constexpr DiceName MO_HOUSE[] = {{"HOOK 1", 4}, {"STAB", 4}, {"CALL", 4}, END};
constexpr DiceName MO_TRANCE[] = {{"TRNC 1", 6}, {"ANTHEM", 5}, {"TRNC GATE", 4}, {"HOOK 1", 3}, END};
constexpr DiceName MO_PSY[] = {{"PSY", 6}, {"BLIP", 3}, {"RIFF", 3}, END};
constexpr DiceName MO_TECHNO[] = {{"DRIFT", 4}, {"STAB", 4}, {"CALL", 4}, END};
constexpr DiceName MO_RIFF[] = {{"RIFF", 5}, {"STAB", 4}, {"BOUNCE", 3}, END};
constexpr DiceName MO_HIPHOP[] = {{"BLUE", 4}, {"SIGH", 4}, {"CALL", 3}, END};
constexpr DiceName MO_IDM[] = {{"BLIP", 5}, {"BOUNCE", 4}, {"HOOK 2", 3}, END};
constexpr DiceName MO_AMB[] = {{"DRIFT", 6}, {"SPARSE", 5}, {"SIGH", 3}, END};
constexpr DiceName MO_SW[] = {{"SYNTH", 6}, {"ANTHEM", 4}, {"HOOK 2", 4}, END};

struct Motif {
  const char* name;
  const char* text;
};
constexpr Motif kMotifs[] = {
    {"HOOK 1", "8 . . 7 . . 5 . 6 . 5 . 3 . . ."},
    {"HOOK 2", "5 . 5 . 6 . 5 . 3 - - . 2 . 1 ."},
    {"CALL", "1 . . 3 . . 5 . . . 4 . 3 . . ."},
    {"TRNC 1", "8 . 7 . 8 . 5 . 6 . 5 . 3 . 5 ."},
    {"TRNC GATE", "5 5 8 5 5 8 5 5 7 5 5 7 5 5 6 5"},
    {"ANTHEM", "5 - - . 6 - - . 8 - - . 7 - 5 ."},
    {"PSY", "1 8 1 2 1 8 1 3 1 8 1 2 1 5 1 4"},
    {"SPARSE", "5 - - - - - - - 3 - - - - - - -"},
    {"SIGH", "3 - - 2 - - 1 - - - - - . . . ."},
    {"SYNTH", "1 . 1 . 5 . 4 . 3 - - . 2 - 1 ."},
    {"BLUE", "1 . 3 . 4 . 5 . . . 7 . 8 . . ."},
    {"RIFF", "1 . 1 . 3 . 1 . 4 . 3 . 1 . 7v ."},
    {"STAB", "1 . . 1 . . 1 . . . 3 . . 2 . ."},
    {"BLIP", "8 . 5 3 . 8 . 6 . . 5 . 3 2 . 1"},
    {"DRIFT", "5 - - - - - - - 6 - - - 4 - - -"},
    {"BOUNCE", "1 . 3 5 . 3 1 . 2 . 4 6 . 4 2 ."},
};
constexpr int kMotifN = sizeof(kMotifs) / sizeof(kMotifs[0]);

constexpr DiceFill kFills[] = {
    {"........", "....oxxX", "........", 6},
    {"x.......", "x.x.x.xX", "........", 4},
    {"........", "x...4...", "..x.x.xX", 3},
    {"x.x.....", "....x.3.", "........", 3},
    {"........", "xxxxxxxX", "........", 2},
    {nullptr, nullptr, nullptr, 0},
};

// ---- Recipes -------------------------------------------------------------------------------------
#define DR(k, s, c, h, o, p) {k, s, c, h, o, p}
#define SN(b, c, l, a, f) {nullptr, b, c, l, a, f}

constexpr DiceRecipe kHouse[] = {
    {"DEEP", 118, 122, 124, 64, k16, "MPC 58", 50, SC_DOR, D | B | C | L | A | F, &KI_DEEP,
     DR(K_HOUSE, nullptr, C_BACK, H_HOUSE, O_OFF, P_HOUSE),
     SN(XB_HOUDEEP, XC_HOUDEEP, XL_HOUDEEP, XA_HOUDEEP, XF_HOUDEEP),
     PR_DEEP, RH_DEEP, k7th, 4, BA_DEEP, AR_HOUSE, MD_UP, LM_SOME, MO_SOFT, FH_CONGA, &FL_HOUSE, &MX_DEEP},
    {"TECH", 124, 126, 128, 32, k16, "MPC 54", 50, SC_MIN, D | B | C | A | F, &KI_HOUSE,
     DR(K_HOUSE, nullptr, C_BACK, H_TECH, O_OFF, P_TECH), SN(XB_HOUTECH, XC_HOUTECH, L_TECHNO, XA_HOUTECH, XF_HOUTECH),
     PR_TECHH, RH_STAB, kTri, 4, BA_TECHH, AR_TECHNO, MD_UP, LM_ARP, MO_TECHNO, FH_CONGA, &FL_HOUSE, &MX_CLUB},
    {"ACID", 120, 123, 126, 32, k16, nullptr, 54, SC_PHR, D | B | C | F, &KI_ACID,
     DR(K_HOUSE, nullptr, C_BACK, H_16, O_OFF, P_HOUSE), SN(XB_HOUACID, XC_HOUACID, L_HOUSE, A_ACID, XF_HOUACID),
     PR_ACID, RH_STAB, kTri, 4, BA_ACID, AR_ACID, MD_UP, LM_ARP, MO_HOUSE, FH_CONGA, &FL_HOUSE, &MX_ACID},
    {"DISCO", 115, 120, 124, 64, k16, "HOUSE", 50, SC_DISCO, D | B | C | L | A | F, &KI_DISCO,
     DR(K_HOUSE, nullptr, C_BACK, H_16, O_OFF, P_HOUSE),
     SN(XB_HOUDISCO, XC_HOUDISCO, XL_HOUDISCO, XA_HOUDISCO, XF_HOUDISCO),
     PR_DISCO, RH_DISCO, k7th, 4, BA_DISCO, AR_HOUSE, MD_UP, LM_MOTIF, MO_HOUSE, FH_CONGA, &FL_HOUSE, &MX_CLUB},
    {"PROG", 122, 124, 128, 64, k16, nullptr, 50, SC_MIN, D | B | C | L | A | F, &KI_HOUSE,
     DR(K_HOUSE, nullptr, C_BACK, H_TRANCE, O_OFF, P_HOUSE),
     SN(XB_HOUPROG, XC_HOUPROG, XL_HOUPROG, XA_HOUPROG, XF_HOUPROG),
     PR_PROG, RH_PAD, kTri, 4, BA_PROG, AR_TRANCE, MD_TR, LM_SOME, MO_SOFT, nullptr, &FL_HOUSE, &MX_TRANCE},
};

constexpr DiceRecipe kTechno[] = {
    {"MINIMAL", 124, 126, 130, 32, k16, "MPC 54", 50, SC_MIN, D | B | A | F, &KI_MIN,
     DR(K_TECH, nullptr, nullptr, H_MIN, O_SOME, P_MIN),
     SN(XB_TECMINIMAL, CH_TECHNO, L_TECHNO, XA_TECMINIMAL, XF_TECMINIMAL),
     PR_TECHNO, RH_STAB, kTri, 4, BA_MIN, AR_TECHNO, MD_UP, LM_ARP, MO_TECHNO, FH_CONGA, &FL_TECHNO, &MX_CLUB},
    {"DUB", 118, 122, 126, 32, k16, nullptr, 52, SC_MIN, D | B | C | F, &KI_DUB,
     DR(K_HOUSE, nullptr, nullptr, H_MIN, O_SOME, P_DUB), SN(XB_TECDUB, XC_TECDUB, L_TECHNO, A_TECHNO, XF_TECDUB),
     PR_DUB, RH_DUB, k7th, 4, BA_DUBT, AR_DUB, MD_UP, LM_ARP, MO_TECHNO, FH_SPARSE, &FL_TECHNO, &MX_DUB},
    {"PEAK", 128, 130, 135, 32, k16, nullptr, 50, SC_PHR, D | B | A | F, &KI_TECHNO,
     DR(K_TECH, nullptr, C_BACK, H_TECH, O_OFF, P_TECH), SN(XB_TECPEAK, CH_TECHNO, L_TECHNO, XA_TECPEAK, XF_TECPEAK),
     PR_TECHNO, RH_STAB, kTri, 4, BA_PEAK, AR_TECHNO, MD_PSY, LM_ARP, MO_TECHNO, nullptr, &FL_TECHNO, &MX_CLUB},
    {"HARD", 140, 145, 150, 32, k16, nullptr, 50, SC_PHR, D | B | C | F, &KI_HARD,
     DR(K_HARD, nullptr, C_BACK, H_16, O_OFF, P_TECH), SN(XB_TECHARD, XC_TECHARD, L_HOOVER, A_TECHNO, XF_TECHARD),
     PR_NEURO, RH_STAB, kTri, 3, BA_HARD, AR_TECHNO, MD_PSY, LM_ARP, MO_RIFF, nullptr, &FL_TECHNO, &MX_HARD},
    {"ACID", 130, 135, 140, 32, k16, nullptr, 50, SC_PHR, D | B | A | F, &KI_ACID,
     DR(K_TECH, nullptr, C_BACK, H_16, O_OFF, P_TECH), SN(XB_TECACID, CH_TECHNO, L_TECHNO, XA_TECACID, XF_TECACID),
     PR_ACID, RH_STAB, kTri, 4, BA_MIN, AR_ACID, MD_UP, LM_ARP, MO_RIFF, nullptr, &FL_TECHNO, &MX_ACID},
    {"MELODIC", 120, 124, 126, 64, k16, nullptr, 50, SC_MIN, D | B | C | L | A | F, &KI_TECHNO,
     DR(K_TECH, nullptr, C_BACK, H_TECH, O_OFF, P_MIN),
     SN(XB_TECMELODIC, XC_TECMELODIC, XL_TECMELODIC, XA_TECMELODIC, XF_TECMELODIC),
     PR_MELODIC, RH_PAD, kTri, 4, BA_MELT, AR_TECHNO, MD_TR, LM_MOTIF, MO_TECHNO, nullptr, &FL_TECHNO, &MX_TRANCE},
};

constexpr DiceRecipe kTrance[] = {
    {"UPLIFT", 136, 138, 140, 64, k16, nullptr, 50, SC_EPIC, D | B | C | L | A | F, &KI_TRANCE,
     DR(K_TRANCE, nullptr, C_TRANCE, H_TRANCE, O_OFF, nullptr),
     SN(XB_TRAUPLIFT, XC_TRAUPLIFT, XL_TRAUPLIFT, XA_TRAUPLIFT, XF_TRAUPLIFT),
     PR_TRANCE, RH_TRANCE, kTri, 4, BA_TRANCE, AR_TRANCE, MD_TR, LM_MOTIF, MO_TRANCE, nullptr, &FL_TRANCE, &MX_TRANCE},
    {"FULLON", 140, 143, 146, 64, k16, nullptr, 50, SC_PHR, D | B | L | A | F, &KI_PSY,
     DR(K_TRANCE, nullptr, C_TRANCE, H_16, O_OFF, nullptr),
     SN(XB_TRAFULLON, CH_DARK, XL_TRAFULLON, XA_TRAFULLON, XF_TRAFULLON),
     PR_PSY, RH_PAD, kTri, 4, BA_PSY, AR_PSY, MD_PSY, LM_MOTIF, MO_PSY, nullptr, &FL_TRANCE, &MX_PSY},
    {"DARK PSY", 146, 150, 155, 32, k16, nullptr, 50, SC_PHR, D | B | A | F, &KI_PSY,
     DR(K_TRANCE, nullptr, C_TRANCE, H_16, O_OFF, P_TECH),
     SN(XB_TRADARKPSY, CH_DARK, L_PSY, XA_TRADARKPSY, XF_TRADARKPSY),
     PR_PSY, RH_PAD, kTri, 3, BA_PSY, AR_PSY, MD_PSY, LM_ARP, MO_PSY, nullptr, &FL_TRANCE, &MX_PSY},
    {"GOA", 140, 145, 148, 64, k16, nullptr, 50, SC_HMI, D | B | L | A | F, &KI_PSY,
     DR(K_TRANCE, nullptr, C_TRANCE, H_TRANCE, O_OFF, nullptr), SN(XB_TRAGOA, CH_DARK, XL_TRAGOA, XA_TRAGOA, XF_TRAGOA),
     PR_GOA, RH_PAD, kTri, 4, BA_GOA, AR_ACID, MD_PSY, LM_MOTIF, MO_PSY, nullptr, &FL_TRANCE, &MX_PSY},
    {"PROG", 130, 134, 136, 64, k16, nullptr, 50, SC_MIN, D | B | C | L | A | F, &KI_TRANCE,
     DR(K_TRANCE, nullptr, C_TRANCE, H_TRANCE, O_OFF, P_HOUSE),
     SN(XB_TRAPROG, XC_TRAPROG, XL_TRAPROG, XA_TRAPROG, XF_TRAPROG),
     PR_PROG, RH_PAD, kTri, 4, BA_PROG, AR_TRANCE, MD_TR, LM_SOME, MO_TRANCE, nullptr, &FL_TRANCE, &MX_TRANCE},
};

constexpr DiceRecipe kDnb[] = {
    {"LIQUID", 170, 174, 176, 64, k16, nullptr, 50, SC_DOR, D | B | C | L | A | F, &KI_LIQUID,
     DR(K_DNB, S_DNB, nullptr, H_DNB, O_DNB, nullptr),
     SN(XB_DNBLIQUID, XC_DNBLIQUID, XL_DNBLIQUID, XA_DNBLIQUID, XF_DNBLIQUID),
     PR_LIQUID, RH_DNB, k7th, 4, BA_DNB, AR_DNB, MD_UP, LM_SOME, MO_SOFT, nullptr, &FL_DNB, &MX_LIQUID},
    {"JUMPUP", 172, 174, 176, 32, k16, nullptr, 50, SC_MIN, D | B | L | F, &KI_DNB,
     DR(K_JUMP, S_DNB, nullptr, H_DNB, O_DNB, nullptr), SN(XB_DNBJUMPUP, CH_DARK, XL_DNBJUMPUP, A_DNB, XF_DNBJUMPUP),
     PR_DNB, RH_DNB, kTri, 4, BA_JUMP, AR_DNB, MD_UP, LM_MOTIF, MO_RIFF, nullptr, &FL_DNB, &MX_DNB},
    {"NEURO", 172, 174, 176, 32, k16, nullptr, 50, SC_PHR, D | B | C | F, &KI_NEURO,
     DR(K_NEURO, S_DNB, nullptr, H_DNB, O_DNB, nullptr), SN(XB_DNBNEURO, XC_DNBNEURO, L_HOOVER, A_DNB, XF_DNBNEURO),
     PR_NEURO, RH_DNB, kTri, 3, BA_NEURO, AR_DNB, MD_UP, LM_ARP, MO_RIFF, nullptr, &FL_DNB, &MX_DNB},
    {"JUNGLE", 160, 165, 170, 32, k16, nullptr, 50, SC_MIN, D | B | C | F, &KI_JUNGLE,
     DR(K_JUNGLE, S_JUNGLE, nullptr, H_JUNGLE, O_DNB, nullptr),
     SN(XB_DNBJUNGLE, XC_DNBJUNGLE, L_DNB, A_DNB, XF_DNBJUNGLE),
     PR_JUNGLE, RH_DNB, k7th, 4, BA_JUNGLE, AR_DNB, MD_UP, LM_SOME, MO_SOFT, nullptr, &FL_BREAKS, &MX_DNB},
    {"HALFTIME", 170, 172, 175, 64, k16, nullptr, 50, SC_MIN, D | B | C | A | F, &KI_NEURO,
     DR(K_HALF, S_HALF, nullptr, H_HALF, O_DNB, nullptr),
     SN(XB_DNBHALFTIM, XC_DNBHALFTIM, L_DNB, XA_DNBHALFTIM, XF_DNBHALFTIM),
     PR_HALF, RH_DNB, kTri, 4, BA_HALF, AR_DNB, MD_UP, LM_SOME, MO_RIFF, nullptr, &FL_DNB, &MX_DNB},
};

constexpr DiceRecipe kBreaks[] = {
    {"BREAKBEAT", 126, 130, 136, 32, k16, "MPC 54", 50, SC_MIN, D | B | C | A | F, &KI_BREAKS,
     DR(K_BREAK, S_BREAK, nullptr, H_BREAK, O_SOME, nullptr),
     SN(XB_BREBREAKBE, XC_BREBREAKBE, L_HOUSE, XA_BREBREAKBE, XF_BREBREAKBE),
     PR_BREAKS, RH_STAB, kTri, 4, BA_BREAKS, AR_BREAKS, MD_UP, LM_ARP, MO_RIFF, FH_SPARSE, &FL_BREAKS, &MX_CLUB},
    {"UKG 2STEP", 128, 132, 136, 32, k16, "MPC 62", 50, SC_DOR, D | B | C | F, &KI_UKG,
     DR(K_UKG, S_UKG, nullptr, H_UKG, O_SOME, P_UKG), SN(XB_BREUKG2STE, XC_BREUKG2STE, L_HOUSE, A_HOUSE, XF_BREUKG2STE),
     PR_UKG, RH_UKG, k7th, 4, BA_UKG, AR_UKG, MD_UP, LM_SOME, MO_HOUSE, FH_CONGA, &FL_BREAKS, &MX_DEEP},
    {"ELECTRO", 120, 127, 132, 32, k16, nullptr, 50, SC_PHR, D | B | L | A | F, &KI_ELECTRO,
     DR(K_ELECTRO, nullptr, C_BACK, H_16, nullptr, P_ELECTRO),
     SN(XB_BREELECTRO, CH_TECHNO, XL_BREELECTRO, XA_BREELECTRO, XF_BREELECTRO),
     PR_ELECTRO, RH_STAB, kTri, 4, BA_ELECTRO, AR_ELECTRO, MD_UP, LM_ARP, MO_RIFF, FH_SPARSE, &FL_TECHNO, &MX_CLUB},
};

constexpr DiceRecipe kHiphop[] = {
    {"BOOMBAP", 86, 90, 96, 64, k16, "BOOM BAP", 50, SC_DOR, D | B | C | L | F, &KI_BOOM,
     DR(K_BOOM, S_BACK, nullptr, H_BOOM, nullptr, P_SPARSE),
     SN(XB_HIPBOOMBAP, XC_HIPBOOMBAP, XL_HIPBOOMBAP, A_LOFI, XF_HIPBOOMBAP),
     PR_BOOM, RH_KEYS, k7th, 4, BA_BOOM, AR_HIPHOP, MD_UP, LM_SOME, MO_HIPHOP, FH_SPARSE, &FL_HIPHOP, &MX_HIPHOP},
    {"LOFI", 70, 80, 90, 64, k16, "LAID BACK", 50, SC_DOR, D | B | C | A | F, &KI_LOFI,
     DR(K_LOFI, S_BACK, nullptr, H_BOOM, nullptr, P_SPARSE),
     SN(XB_HIPLOFI, XC_HIPLOFI, L_HIPHOP, XA_HIPLOFI, XF_HIPLOFI),
     PR_LOFI, RH_KEYS, k7th, 4, BA_LOFI, AR_HIPHOP, MD_AMB, LM_SOME, MO_HIPHOP, FH_SPARSE, &FL_HIPHOP, &MX_LOFI},
    {"TRAP", 130, 140, 150, 32, k16, nullptr, 50, SC_HMI, D | B | C | A | F, &KI_TRAP,
     DR(K_TRAP, S_TRAP, nullptr, H_TRAP, nullptr, nullptr),
     SN(XB_HIPTRAP, XC_HIPTRAP, L_HIPHOP, XA_HIPTRAP, XF_HIPTRAP),
     PR_TRAP, RH_PAD, kTri, 4, BA_TRAP, AR_HIPHOP, MD_UP, LM_SOME, MO_HIPHOP, FH_SPARSE, &FL_TRAP, &MX_TRAP},
};

constexpr DiceRecipe kIdm[] = {
    {"BRAINDANCE", 135, 150, 165, 64, k16, nullptr, 50, SC_LYD, D | B | C | L | A | F, &KI_IDM,
     DR(K_BRAIN, S_BRAIN, nullptr, H_BRAIN, nullptr, P_IDM),
     SN(XB_IDMBRAINDA, XC_IDMBRAINDA, XL_IDMBRAINDA, XA_IDMBRAINDA, XF_IDMBRAINDA),
     PR_IDM, RH_IDM, k7th, 4, BA_IDM, AR_IDM, MD_IDM, LM_MOTIF, MO_IDM, FH_IDM, &FL_IDM, &MX_IDM},
    {"GLITCH", 100, 115, 130, 32, k16, "DRUNK", 50, SC_DOR, D | B | C | A | F, &KI_IDM,
     DR(K_GLITCH, S_GLITCH, nullptr, H_GLITCH, nullptr, P_IDM),
     SN(XB_IDMGLITCH, XC_IDMGLITCH, L_IDM, XA_IDMGLITCH, XF_IDMGLITCH),
     PR_GLITCH, RH_IDM, kSus, 4, BA_GLITCH, AR_IDM, MD_IDM, LM_ARP, MO_IDM, FH_IDM, &FL_IDM, &MX_IDM},
    {"ELECTRONICA", 100, 110, 124, 64, k16, "MPC 54", 50, SC_LYD, D | B | C | L | A | F, &KI_IDM,
     DR(K_ELNICA, S_BACK, nullptr, H_8, nullptr, P_IDM),
     SN(XB_IDMELECTRO, XC_IDMELECTRO, XL_IDMELECTRO, XA_IDMELECTRO, XF_IDMELECTRO),
     PR_ELNICA, RH_PAD, k9th, 4, BA_ELNICA, AR_AMB, MD_AMB, LM_MOTIF, MO_IDM, FH_IDM, &FL_IDM, &MX_IDM},
};

constexpr DiceRecipe kAmbient[] = {
    {"DREAM", 60, 70, 80, 64, k8, nullptr, 50, SC_LYD, C | L | A | F, &KI_AMB,
     DR(K_AMB, nullptr, nullptr, H_AMB, nullptr, nullptr),
     SN(B_SUB, XC_AMBDREAM, XL_AMBDREAM, XA_AMBDREAM, XF_AMBDREAM),
     PR_DREAM, RH_AMB, k9th, 4, BA_AMB, AR_AMB, MD_AMB, LM_MOTIF, MO_AMB, FH_SPARSE, &FL_AMB, &MX_AMB},
    {"DARK", 55, 65, 75, 64, k8, nullptr, 50, SC_PHR, D | B | C | F, &KI_AMB,
     DR(K_AMB, nullptr, nullptr, nullptr, nullptr, P_AMB), SN(XB_AMBDARK, XC_AMBDARK, L_AMB, A_AMB, XF_AMBDARK),
     PR_DARKAMB, RH_AMB, kTri, 3, BA_AMB, AR_AMB, MD_AMB, LM_SOME, MO_AMB, FH_SPARSE, &FL_AMB, &MX_AMB},
    {"DRONE", 40, 60, 70, 64, k8, nullptr, 50, SC_DOR, B | C | F, &KI_AMB,
     DR(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr), SN(XB_AMBDRONE, XC_AMBDRONE, L_AMB, A_AMB, XF_AMBDRONE),
     PR_DRONE, RH_AMB, kSus, 3, BA_AMB, AR_AMB, MD_AMB, LM_SOME, MO_AMB, FH_SPARSE, &FL_AMB, &MX_AMB},
};

constexpr DiceRecipe kSynthwave[] = {
    {"OUTRUN", 100, 110, 118, 64, k16, nullptr, 50, SC_EPIC, D | B | C | L | A | F, &KI_SW,
     DR(K_SW, S_SW, nullptr, H_8, nullptr, nullptr),
     SN(XB_SYNOUTRUN, XC_SYNOUTRUN, XL_SYNOUTRUN, XA_SYNOUTRUN, XF_SYNOUTRUN),
     PR_OUTRUN, RH_SW, kTri, 4, BA_SW, AR_SW, MD_UP, LM_MOTIF, MO_SW, nullptr, &FL_SW, &MX_SW},
    {"DARKSYNTH", 110, 118, 130, 64, k16, nullptr, 50, SC_PHR, D | B | C | L | A | F, &KI_DARKSW,
     DR(K_DARKSW, S_SW, nullptr, H_16, nullptr, nullptr),
     SN(XB_SYNDARKSYN, XC_SYNDARKSYN, XL_SYNDARKSYN, XA_SYNDARKSYN, XF_SYNDARKSYN),
     PR_DARKSW, RH_SW, kTri, 3, BA_DARKSW, AR_SW, MD_TR, LM_ARP, MO_RIFF, nullptr, &FL_SW, &MX_DARKSW},
    {"CHILLWAVE", 80, 90, 100, 64, k16, "LAID BACK", 50, SC_MAJ, D | B | C | L | A | F, &KI_CHILL,
     DR(K_CHILL, S_SW, nullptr, H_8, nullptr, nullptr),
     SN(XB_SYNCHILLWA, XC_SYNCHILLWA, XL_SYNCHILLWA, XA_SYNCHILLWA, XF_SYNCHILLWA),
     PR_CHILL, RH_PAD, k7th, 4, BA_CHILL, AR_AMB, MD_AMB, LM_SOME, MO_SW, nullptr, &FL_SW, &MX_CHILL},
};

struct Genre {
  const char* name;
  const DiceRecipe* styles;
  int n;
};
#define G(name, a) {name, a, sizeof(a) / sizeof(a[0])}
constexpr Genre kGenres[] = {
    G("HOUSE", kHouse),   G("TECHNO", kTechno), G("TRANCE", kTrance),   G("DNB", kDnb),
    G("BREAKS", kBreaks), G("HIPHOP", kHiphop), G("IDM", kIdm), G("AMBIENT", kAmbient),
    G("SYNTHWAVE", kSynthwave),
};
constexpr int kGenreN = sizeof(kGenres) / sizeof(kGenres[0]);

bool degreeToken(const char* p, int n) {
  if (n < 1 || n > 2 || p[0] < '1' || p[0] > '9') return false;
  return n == 1 || p[1] == '^' || p[1] == 'v';
}

}  // namespace

int diceGenreCount() { return kGenreN; }
const char* diceGenreName(int g) { return g >= 0 && g < kGenreN ? kGenres[g].name : nullptr; }
int diceStyleCount(int g) { return g >= 0 && g < kGenreN ? kGenres[g].n : 0; }
const char* diceStyleName(int g, int s) {
  const DiceRecipe* r = diceRecipe(g, s);
  return r ? r->style : nullptr;
}
const DiceRecipe* diceRecipe(int g, int s) {
  if (g < 0 || g >= kGenreN || s < 0 || s >= kGenres[g].n) return nullptr;
  return &kGenres[g].styles[s];
}

int diceMotifCount() { return kMotifN; }
const char* diceMotifName(int i) { return i >= 0 && i < kMotifN ? kMotifs[i].name : nullptr; }
const char* diceMotifText(int i) { return i >= 0 && i < kMotifN ? kMotifs[i].text : nullptr; }

int diceMotifSteps(const char* text) {
  if (!text) return -1;
  int n = 0;
  for (const char* p = text; *p;) {
    if (*p == ' ') {
      ++p;
      continue;
    }
    const char* q = p;
    while (*q && *q != ' ') ++q;
    const int len = static_cast<int>(q - p);
    if (!(len == 1 && (*p == '.' || *p == '-')) && !degreeToken(p, len)) return -1;
    ++n;
    p = q;
  }
  return n;
}

const DiceFill* diceFills() { return kFills; }

}  // namespace mt
