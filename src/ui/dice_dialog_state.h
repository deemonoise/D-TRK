#pragma once
#include <stdint.h>
#include <stdio.h>
#include "dice.h"

// Hardware-free dialog policy. Scope/Form values are persisted in NVS; keep their order.
namespace ui { namespace dice_dialog {
enum Scope : uint8_t { kScAll, kScTempo, kScSounds, kScPatterns, kScopes };
constexpr uint8_t kLens[] = {0, 16, 32, 64, 128};
constexpr int kLenCount = sizeof(kLens) / sizeof(kLens[0]);

struct Prefs {
  uint8_t genre = 0, style = 0, variety = mt::kDiceVarietyDefault, scope = kScAll, song = 1;
  uint8_t len = 0;  // index into kLens: 0 = AUTO
  uint8_t target = 0, change = 2, form = 1, feel = 35, density = 1;
  uint8_t variant = mt::kDiceVariantAuto, clean = 1, fixedSeed = 0, locks = 0, keepKey = 0;
  uint32_t seed = 1;
  bool operator!=(const Prefs& o) const {
    return density != o.density || target != o.target || change != o.change || form != o.form || feel != o.feel || genre != o.genre || style != o.style || variety != o.variety || scope != o.scope || song != o.song ||
           len != o.len || variant != o.variant || clean != o.clean || fixedSeed != o.fixedSeed ||
           locks != o.locks || keepKey != o.keepKey || seed != o.seed;
  }
};
inline mt::DiceMenuPosition menuPosition(const Prefs& prefs) {
  return mt::diceMenuPosition({prefs.genre, prefs.style});
}
inline void select(Prefs& prefs, mt::DiceMenuPosition position) {
  const auto choice = mt::diceMenuChoice(position);
  prefs.genre = choice.genre;
  prefs.style = choice.style;
}
inline const char* genreLabel(const Prefs& prefs) {
  return mt::diceFamilyName(menuPosition(prefs).family);
}
inline const char* styleLabel(const Prefs& prefs) {
  const auto* label = mt::diceStyleName(prefs.genre, prefs.style);
  return label ? label : "-";
}
inline uint8_t moveChoice(int index, int delta, int count) {
  return static_cast<uint8_t>((index + delta % count + count) % count);
}
inline void stepGenre(Prefs& prefs, int delta) {
  const auto position = menuPosition(prefs);
  const auto family = moveChoice(position.family, delta, mt::diceFamilyCount());
  if (family != position.family) select(prefs, {family, 0});
}
inline void stepStyle(Prefs& prefs, int delta) {
  auto position = menuPosition(prefs);
  position.style = moveChoice(position.style, delta, mt::diceFamilyStyleCount(position.family));
  select(prefs, position);
}
inline void clampSelection(Prefs& prefs) {
  select(prefs, menuPosition(prefs));
  if (prefs.variant > mt::kDiceVariantAuto) prefs.variant = mt::kDiceVariantAuto;
}
inline void loadSelection(Prefs& prefs, uint8_t (*load)(const char*, uint8_t)) {
  const Prefs defaults;
  prefs.genre = load("dg2gen", defaults.genre);
  prefs.style = load("dg2sty", defaults.style);
  prefs.variant = load("dg2variant", defaults.variant);
  clampSelection(prefs);
}
inline void saveSelection(const Prefs& prefs, const Prefs& saved, void (*save)(const char*, uint8_t)) {
  // The loaded selection may be a fallback for an older pair on disk.
  if (prefs.genre != saved.genre || prefs.style != saved.style) {
    save("dg2gen", prefs.genre);
    save("dg2sty", prefs.style);
  }
  if (prefs.variant != saved.variant) save("dg2variant", prefs.variant);
}
inline uint8_t clampForm(uint8_t form) {
  return form <= static_cast<uint8_t>(mt::DiceForm::Arrange) ? form :
         static_cast<uint8_t>(mt::DiceForm::Develop);
}
inline const char* formName(uint8_t form) {
  static constexpr const char* names[] = {"CLASSIC", "DEVELOP", "ARRANGE"};
  return names[clampForm(form)];
}
inline uint8_t changeForm(uint8_t form, int delta) {
  const int next = clampForm(form) + delta;
  return static_cast<uint8_t>(next < 0 ? 0 : next > 2 ? 2 : next);
}
inline bool patterns(const Prefs& prefs) {
  return prefs.scope == kScAll || prefs.scope == kScPatterns;
}
inline mt::DiceOpts options(const Prefs& prefs) {
  static constexpr uint8_t scopeLayers[] = {mt::kDiceAll, mt::kDiceTempo, mt::kDiceSounds, mt::kDicePattern};
  mt::DiceOpts o;
  o.targetMask = prefs.target && prefs.scope != kScTempo ? 1u << (prefs.target - 1) : 0x3f;
  o.change = patterns(prefs) ? static_cast<mt::DiceChange>(prefs.change) : mt::DiceChange::Wild;
  o.form = static_cast<mt::DiceForm>(clampForm(prefs.form));
  o.groove = prefs.feel;
  o.density = static_cast<mt::DiceDensity>(prefs.density);
  o.genre = prefs.genre;
  o.style = prefs.style;
  o.variety = prefs.variety;
  o.len = kLens[prefs.len % kLenCount];
  o.layers = scopeLayers[prefs.scope % kScopes];
  if (patterns(prefs) && prefs.song) o.layers |= mt::kDiceSong;
  o.variant = prefs.variant <= mt::kDiceVariantAuto ? prefs.variant : mt::kDiceVariantAuto;
  o.clean = prefs.clean != 0;
  o.lockMask = prefs.locks;
  o.seed = prefs.fixedSeed ? prefs.seed : 0;
  o.keepKey = prefs.keepKey != 0;
  return o;
}
struct LastRoll {
  uint8_t genre=255,style=255,variant=255;
  void remember(const Prefs& p,uint8_t selected) {genre=p.genre;style=p.style;variant=selected;}
  uint8_t avoid(const Prefs& p) const {return !p.fixedSeed && p.genre==genre && p.style==style ? variant : 255;}
};
// Audition the generated MAIN; partial edits stay on an existing generated section.
inline int audition(const mt::DiceOpts& o,uint8_t applied,int current,int first) {
  if(!(applied&mt::kDicePattern)) return -1;
  const bool full=(o.targetMask&63)==63 && o.change==mt::DiceChange::Wild;
  if(!full && current>=first && current<first+mt::dicePatternCount(o)) return -1;
  return first+(o.form==mt::DiceForm::Classic ? 0 : 1);
}
inline void rollSummary(char* out,size_t size,uint8_t layers,uint8_t root,uint8_t scale,uint32_t seed,bool fixed,bool partial,mt::DiceChange change=mt::DiceChange::Wild) {
  static const char* roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  if(!(layers&mt::kDicePattern) && (layers&mt::kDiceSounds)) {
    snprintf(out,size,"SOUNDS ONLY #%lu%s",static_cast<unsigned long>(seed),partial ? " PARTIAL" : "");return;
  }
  if((layers&mt::kDicePattern) && change==mt::DiceChange::Subtle) {
    snprintf(out,size,"DYNAMICS ONLY #%lu",static_cast<unsigned long>(seed));return;
  }
  const char* scope=(layers&mt::kDiceAll)==mt::kDiceAll ? "ALL" : (layers&mt::kDicePattern) ? "NOTES" : "KEY/TEMPO";
  snprintf(out,size,"%s %s %s %s #%lu%s",change==mt::DiceChange::Normal ? "BLEND" : fixed ? "FIXED" : "NEW",scope,roots[root%12],mt::scaleName(static_cast<mt::ScaleType>(scale)),static_cast<unsigned long>(seed),partial ? " PARTIAL" : "");
}
struct Range {
  int first = -1;
  int count = 0;
  bool operator==(const Range& other) const { return first == other.first && count == other.count; }
  bool operator!=(const Range& other) const { return !(*this == other); }
};
inline void rangeTitle(char* out, size_t size, const Range& target, bool overwrite) {
  snprintf(out, size, overwrite ? "OVERWRITE P%02d-P%02d?" : "DICE P%02d-P%02d",
           target.first + 1, target.first + target.count);
}
inline Range range(const mt::DiceOpts& opts, int editPattern) {
  const int count = mt::dicePatternCount(opts);
  return {mt::diceFirstPattern(editPattern, count), count};
}
}}  // namespace ui::dice_dialog
