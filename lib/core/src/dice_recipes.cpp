#include "dice_recipes.h"
#include "scale.h"
#include "dice_hybrid.h"
#include <cstring>
namespace mt {
namespace {
struct Sequence { uint32_t first; uint16_t count; };
struct Group { const char* name; uint16_t first; uint8_t count; };
#include "dice_catalog.inc"
constexpr int kGroupsCount = sizeof(kGroups) / sizeof(kGroups[0]);
}
int diceGenreCount() { return kGroupsCount; }
const char* diceGenreName(int genre) {
  return genre >= 0 && genre < kGroupsCount ? kGroups[genre].name : nullptr;
}
int diceStyleCount(int genre) {
  return genre >= 0 && genre < kGroupsCount ? kGroups[genre].count : 0;
}
const DiceRecipe* diceRecipe(int genre, int style) {
  return style >= 0 && style < diceStyleCount(genre) ? &kRecipes[kGroups[genre].first + style] : nullptr;
}
const char* diceStyleName(int genre, int style) {
  const auto* recipe = diceRecipe(genre, style);
  return recipe ? recipe->style : nullptr;
}
const DiceVariant* diceVariant(int genre, int style, int variant) {
  const auto* recipe = diceRecipe(genre, style);
  return recipe && variant >= 0 && variant < kDiceVariants ? &kVariants[recipe->firstVariant + variant] : nullptr;
}
const DiceVoice& diceVoice(const DiceVariant& variant, int track) {
  return kVoices[variant.voices[track >= 0 && track < kDiceRoles ? track : 0]];
}
const DiceTimbre& diceTimbre(const DiceVoice& voice) { return kTimbres[voice.timbre]; }
const DiceVoice& diceVoiceForTimbre(const char* name,uint32_t seed) {
  const int count=sizeof(kVoices)/sizeof(kVoices[0]);const DiceVoice* fallback=nullptr;
  for(int i=0;i<count;++i) {
    const auto& voice=kVoices[(seed+i)%count];
    if(!strcmp(diceTimbre(voice).name,name)) return voice;
    if(!fallback && !dicePercussion(voice)) fallback=&voice;
  }
  return *fallback; // every catalogue contains pitched voices
}
const char* dicePartName(DicePart part) {
  static const char* names[]={"BASS","CHORDS","LEAD","ARP","FX","PAD","COUNTER","TEXTURE","PULSE"};
  const int i=static_cast<int>(part);return i<static_cast<int>(DicePart::Count) ? names[i] : "-";
}
uint8_t diceChord(const DiceVoice& voice) {
  static constexpr uint8_t chords[]={255,kChordTriad,kChordSeventh,kChordMaj7,kChordMin7,kChordMaj9,kChordMin9};
  const int chord=diceTimbre(voice).chord;
  return chord<static_cast<int>(sizeof(chords)) ? chords[chord] : 255;
}
int diceEventCount(const DiceVoice& voice) { return kSequences[voice.sequence].count; }
const DiceEvent& diceEvent(const DiceVoice& voice, int index) {
  const auto& seq = kSequences[voice.sequence];
  return kEvents[kEventIds[seq.first + index]];  // caller bounds by diceEventCount
}
bool dicePercussion(const DiceVoice& voice) {
  const auto kind = static_cast<DiceVoiceKind>(voice.kind);
  return kind != DiceVoiceKind::Bass && kind != DiceVoiceKind::Stab && kind != DiceVoiceKind::Bell &&
         kind != DiceVoiceKind::Lead && kind != DiceVoiceKind::Pad && kind != DiceVoiceKind::Vox;
}
DiceLayout diceLayout(const DiceVariant& variant) {
  DiceLayout out;
  for(int i=0;i<kDiceRoles;++i) {
    out.source[i]=kDiceUnusedTrack; out.lane[i]=-1;
    if(dicePercussion(diceVoice(variant,i))) out.drums|=1u<<i;
  }
  int next=0; uint16_t used=0;
  if(out.drums) { out.source[0]=kDiceKitTrack; out.used=1; next=1; }
  static constexpr uint8_t preferred[]={0,1,2,0,0,6,0,0,4,5,7,0,0,8,9,3};
  for(int i=0;i<kDiceRoles;++i) {
    if(out.drums&(1u<<i)) {
      int lane=preferred[diceVoice(variant,i).kind];
      if(used&(1u<<lane)) { lane=0; while(used&(1u<<lane)) ++lane; }
      out.lane[i]=lane; used|=1u<<lane;
    } else { out.source[next]=i; out.used|=1u<<next; ++next; }
  }
  dice_detail::completeLayout(variant,out);
  return out;
}
const char* diceTrackName(const DiceVariant& variant, int track) {
  if(track<0 || track>=kDiceRoles) return "-";
  const int source=diceLayout(variant).source[track];
  return source==kDiceKitTrack ? "DRUMS" : source>=0 ? diceVoice(variant,source).name : diceSupplement(source) ? dicePartName(dicePartKind(source)) : "-";
}
}  // namespace mt
