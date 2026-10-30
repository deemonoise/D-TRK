#include "dice_variation.h"
#include <algorithm>
#include <cstring>
#include "rng.h"
#include "scale.h"

namespace mt {
namespace {
int clamp(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }
// Follow the last sounding chord, including one held across the loop boundary.
int harmonicNote(int note, const Step* chords, int len, int at, uint8_t root, ScaleType scale) {
  const Step* chord = nullptr;
  for (int i = 0; i < len; ++i) if (chords[i].hasNote()) chord = &chords[i];
  for (int i = 0; i <= at; ++i) if (chords[i].hasNote()) chord = &chords[i];
  if (!chord) return note;
  const FxSlot* fx = chord->find(Fx::CHD);
  uint8_t tones[4];
  const int n = chordNotes(chord->note, fx ? fx->val : kChordTriad, root, scale, tones);
  int best = note, distance = 128;
  for (int pitch = 0; pitch < 128; ++pitch)
    for (int j = 0; j < n; ++j)
      if (pitch % 12 == tones[j] % 12 && abs(pitch - note) < distance) {
        best = pitch; distance = abs(pitch - note);
      }
  return best;
}
}

void diceVaryDraft(const Project& original, const DiceOpts& opts, uint32_t seed, DiceSnap& draft) {
  if (opts.change == DiceChange::Wild) return;
  const bool subtle = opts.change == DiceChange::Subtle;
  for (int pat = 0; pat < kDicePats; ++pat) {
    DicePat& dst = draft.parts.pats[pat];
    const Pattern& old = original.patterns[draft.first + pat];
    const int len = std::min<int>(kMaxSteps, std::min<int>(dst.length, old.length));
    // Blend chords first so changed melodic notes follow the resulting harmony.
    const int order[kDiceRoles] = {2, 0, 1, 3, 4, 5};
    for (int role : order) {
      const uint8_t bit = static_cast<uint8_t>(1u << role);
      if (!(draft.patMask & opts.targetMask & bit) || (opts.lockMask & bit)) continue;
      Step* fresh = dst.steps[role];
      const Step* prior = old.steps[kDiceTrack[role]];
      uint8_t candidates[kMaxSteps];
      bool changed[kMaxSteps] = {};
      int active = 0, count = 0;
      for (int i = 0; i < len; ++i) {
        active += prior[i].hasNote();
        if (memcmp(&prior[i], &fresh[i], sizeof(Step)) && (prior[i].hasNote() || fresh[i].hasNote()))
          candidates[count++] = static_cast<uint8_t>(i);
      }
      if (!active) continue;  // a new/empty role needs a complete playable phrase
      // A style may omit this role, but the retained phrase keeps its existing audibility.
      draft.parts.tracks[role].mute = original.tracks[kDiceTrack[role]].mute;
      const int budget = std::min(count, std::max(1, active / (subtle ? 4 : 2)));
      Rng rng(seed ^ (0x9E3779B9u * (role + 1)) ^ (0x85EBCA6Bu * (pat + 1)));
      for (int j = 0; j < budget; ++j) {
        const int pick = j + rng.below(count - j);
        std::swap(candidates[j], candidates[pick]);
        changed[candidates[j]] = true;
      }
      for (int i = 0; i < len; ++i) {
        if (!changed[i]) { fresh[i] = prior[i]; continue; }
        if (subtle && prior[i].hasNote()) {
          const Step suggestion = fresh[i];
          fresh[i] = prior[i];
          if (role == 0) {
            const int target = suggestion.hasNote() ? suggestion.note : prior[i].note + 5;
            fresh[i].note = static_cast<uint8_t>(clamp(target, std::max(1, prior[i].note - 6), std::min(127, prior[i].note + 6)));
          } else {
            if (suggestion.hasNote())
              fresh[i].note = static_cast<uint8_t>(clamp(suggestion.note, std::max(0, prior[i].note - 3), std::min(127, prior[i].note + 3)));
            const int velocity = prior[i].vel ? prior[i].vel : original.tracks[kDiceTrack[role]].defVel;
            const int target = suggestion.vel ? suggestion.vel : velocity + 5;
            fresh[i].vel = static_cast<uint8_t>(clamp(target, std::max(1, velocity - 8), std::min(127, velocity + 8)));
          }
        }
        if (role != 1 && role != 3 && role != 4) continue;
        if (!fresh[i].hasNote() || (i % 4 && role != 4)) continue;
        const Step* harmony = (opts.lockMask & 4) || !(draft.patMask & 4) ? old.steps[2] : dst.steps[2];
        const ScaleType scale = original.scaleType < static_cast<uint8_t>(ScaleType::Count)
                                    ? static_cast<ScaleType>(original.scaleType) : ScaleType::Minor;
        fresh[i].note = static_cast<uint8_t>(harmonicNote(fresh[i].note, harmony, len, i, original.scaleRoot, scale));
      }
    }
  }
}
}  // namespace mt
