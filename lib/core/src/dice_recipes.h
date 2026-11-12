#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {
constexpr int kDiceRoles = 6;
constexpr int kDiceVariants = 16;
constexpr uint8_t kDiceVariantAuto = kDiceVariants;
enum class DiceVoiceKind : uint8_t {
  Kick, Snare, Hat, Bass, Stab, Fx, Bell, Lead, Clap, Perc, Shaker, Pad, Vox, Rim, Break, OpenHat
};
struct DiceEvent {
  uint8_t at, velocity;
  int8_t note;
  uint8_t probability, condition, repeats, cutoff, resonance, delay, reverb;
};
struct DiceVoice {
  const char* name;
  uint16_t timbre, sequence;
  uint8_t kind;
  int8_t pitch;
  uint8_t decay;
  int8_t cut;
  uint8_t resonance, volume, delay, reverb, chance, length, speed;
  uint8_t lfo[4];  // dest, wave, synced rate, depth
};
// Complete numerical blueprint interpreted on existing engines; no factory preset dependency.
struct DiceTimbre {
  const char* name;
  uint8_t type,machine,osc[2];
  int8_t semi;
  uint8_t params[8],env[4];
  uint8_t sub,noise,glide,drive,level,filter,cutoff,resonance;
  int8_t fenv;
  uint8_t fdec,chord,lfo[4];
};
struct DiceVariant {
  const char* name;
  uint16_t bpm;
  uint8_t swing, root, scale;
  uint16_t voices[kDiceRoles];
};
struct DiceRecipe {
  const char* style;
  uint16_t firstVariant;
  uint8_t length, resolution;
};
int diceGenreCount();
const char* diceGenreName(int genre);
int diceStyleCount(int genre);
const char* diceStyleName(int genre, int style);
const DiceRecipe* diceRecipe(int genre, int style);
const DiceVariant* diceVariant(int genre, int style, int variant);
const DiceVoice& diceVoice(const DiceVariant& variant, int track);
const DiceTimbre& diceTimbre(const DiceVoice& voice);
uint8_t diceChord(const DiceVoice& voice);  // tracker CHD value, 255 for a single note
int diceEventCount(const DiceVoice& voice);
const DiceEvent& diceEvent(const DiceVoice& voice, int index);
bool dicePercussion(const DiceVoice& voice);
// Destination tracks: one drum KIT, followed by the recipe's pitched voices.
constexpr int8_t kDiceKitTrack = -1, kDiceUnusedTrack = -2;
enum class DicePart : uint8_t { Bass, Chords, Lead, Arp, Fx, Pad, Counter, Texture, Pulse, Count };
constexpr int8_t dicePartSource(DicePart part) { return static_cast<int8_t>(-3-static_cast<int>(part)); }
constexpr bool diceSupplement(int source) { return source<=-3 && source>-3-static_cast<int>(DicePart::Count); }
constexpr DicePart dicePartKind(int source) { return static_cast<DicePart>(-3-source); }
const char* dicePartName(DicePart part);
const DiceVoice& diceVoiceForTimbre(const char* name,uint32_t seed);
struct DiceLayout {
  int8_t source[kDiceRoles];
  int8_t lane[kDiceRoles];  // source voice -> KIT lane, -1 for pitched voices
  uint8_t drums = 0, used = 0;
};
DiceLayout diceLayout(const DiceVariant& variant);
const char* diceTrackName(const DiceVariant& variant, int track);
}  // namespace mt
