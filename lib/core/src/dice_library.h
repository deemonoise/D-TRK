#pragma once
#include <stddef.h>
#include <stdint.h>
#include "dice_recipes.h"

namespace mt {
constexpr int kDiceMidiEvents = 512;
constexpr uint32_t kDiceLibraryMaxBytes = 4u * 1024 * 1024;
struct DicePhraseEvent {
  uint16_t tick = 0, duration = 1;  // 96 PPQ
  uint8_t note = 60, vel = 100, lane = 255;
};
struct DicePhrase {
  uint16_t count = 0;
  uint8_t bars = 1, root = 0, scale = static_cast<uint8_t>(ScaleType::Minor);
  char name[48] = {};
  DicePhraseEvent events[kDiceMidiEvents];
};
struct DicePhraseSource {
  virtual bool pick(int genre, DiceRole role, uint32_t seed, DicePhrase& out) const = 0;
  virtual ~DicePhraseSource() = default;
};
// Shared bounded candidate selection for the memory view and streamed SD reader.
constexpr int kDicePhraseCandidates = 3;
int diceCandidateSlot(uint32_t seen, uint32_t random);
struct DicePhraseFeatures {
  uint16_t attacks = 0, strong = 0, previous = 65535;
  uint8_t low = 127, high = 0;
  void add(const DicePhraseEvent& event);
  int score(DiceRole role, uint8_t bars, uint32_t roleSeed) const;
};
// Validated, read-only view of DML1 data owned by the caller; no heap or I/O.
class DiceMidiLibrary final : public DicePhraseSource {
 public:
  DiceMidiLibrary(const uint8_t* data, size_t size);
  bool valid() const { return count_ != 0; }
  uint32_t count() const { return count_; }
  bool pick(int genre, DiceRole role, uint32_t seed, DicePhrase& out) const override;
 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  uint32_t count_ = 0;
};
struct DicePhraseResult { bool changed = false, approximate = false; uint16_t dropped = 0; };
// Repeats/clips a phrase to the destination, scales pitches, preserves velocity, GAT and NDG.
// Chords choose the closest representable voicing, explicitly reporting any simplification.
DicePhraseResult diceWritePhrase(const DicePhrase& phrase, DiceRole role, Step* out, int len,
                                Resolution res, uint8_t root, ScaleType scale, const Instrument* kit);
}  // namespace mt
