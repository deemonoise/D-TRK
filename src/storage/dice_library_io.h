#pragma once
#include <stddef.h>
#include <stdint.h>
#include "dice_library.h"

namespace storage {
// One roll's six selected phrases. The directory is streamed from SD; the complete library
// is never allocated. Construct/load outside the project lock, before diceDraft.
class DiceLibraryData final : public mt::DicePhraseSource {
 public:
  static constexpr size_t kMaxBytes = mt::kDiceLibraryMaxBytes;
  DiceLibraryData() = default;
  ~DiceLibraryData() override;
  DiceLibraryData(const DiceLibraryData&) = delete;
  DiceLibraryData& operator=(const DiceLibraryData&) = delete;
  bool load(int genre = 0, uint32_t seed = 1);
  bool pick(int genre, mt::DiceRole role, uint32_t seed, mt::DicePhrase& out) const override;
  explicit operator bool() const { return ready_; }
  size_t size() const { return size_; }
  const char* error() const { return error_; }
 private:
  void clear();
  mt::DicePhrase* phrases_ = nullptr;
  uint8_t mask_ = 0;
  int genre_ = 0;
  uint32_t seed_ = 1;
  size_t size_ = 0;
  bool ready_ = false;
  const char* error_ = "MIDI LIBRARY MISSING";
};
}  // namespace storage
