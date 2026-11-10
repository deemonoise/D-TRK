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
  uint8_t source = 0, clean = 1, fixedSeed = 0, locks = 0;
  uint32_t seed = 1;
  bool operator!=(const Prefs& o) const {
    return density != o.density || target != o.target || change != o.change || form != o.form || feel != o.feel || genre != o.genre || style != o.style || variety != o.variety || scope != o.scope || song != o.song ||
           len != o.len || source != o.source || clean != o.clean || fixedSeed != o.fixedSeed ||
           locks != o.locks || seed != o.seed;
  }
};
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
  o.source = static_cast<mt::DiceSource>(prefs.source);
  o.clean = prefs.clean != 0;
  o.lockMask = prefs.locks;
  o.seed = prefs.fixedSeed ? prefs.seed : 0;
  return o;
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
