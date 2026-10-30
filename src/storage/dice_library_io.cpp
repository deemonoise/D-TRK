#include "dice_library_io.h"
#include <new>
#include <limits.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "hw/sdcard.h"
#include "rng.h"

namespace storage {
namespace {
constexpr const char* kPaths[] = {"/midi/dice-library.dml", "/samples/midi/dice-library.dml"};
constexpr uint32_t kHeader = 16, kEntry = 64, kEvent = 8;
uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return u16(p) | static_cast<uint32_t>(u16(p + 2)) << 16; }
bool read(fs::File& file, uint8_t* out, size_t n) {
  size_t pos = 0;
  while (pos < n) {
    const size_t got = file.read(out + pos, n - pos);
    if (!got || got > n - pos) return false;
    pos += got;
  }
  return true;
}
bool validEntry(const uint8_t* e, size_t bytes, uint32_t end) {
  const uint32_t at = u32(e + 8), n = u32(e + 12);
  return u16(e) && !(u16(e) & ~0x1FF) && e[2] < mt::kDiceRoles && e[3] < 12 &&
         e[4] < static_cast<int>(mt::ScaleType::Count) && e[5] >= 1 && e[5] <= 8 &&
         memchr(e + 16, 0, 48) && at >= end && at <= bytes && n &&
         n % kEvent == 0 && n <= mt::kDiceMidiEvents * kEvent && n <= bytes - at;
}
// Evaluate candidate events without a second phrase buffer on the small UI stack.
const char* readPhrase(fs::File& file, const uint8_t* e, mt::DicePhraseFeatures& features,
                       mt::DicePhrase* ph = nullptr) {
  const int count = u32(e + 12) / kEvent;
  if (!file.seek(u32(e + 8))) return "MIDI LIBRARY READ ERROR";
  if (ph) {
    ph->root = e[3]; ph->scale = e[4]; ph->bars = e[5]; ph->count = count;
    memcpy(ph->name, e + 16, sizeof(ph->name));
  }
  uint16_t prev = 0;
  for (int i = 0; i < count; ++i) {
    uint8_t event[kEvent];
    if (!read(file, event, sizeof(event))) return "MIDI LIBRARY READ ERROR";
    const uint16_t tick = u16(event), duration = u16(event + 2);
    if (tick < prev || tick >= e[5] * 384 || !duration || event[4] > 127 ||
        !event[5] || event[5] > 127 || (event[6] != 255 && event[6] >= mt::kKitLanes))
      return "BAD MIDI LIBRARY";
    const mt::DicePhraseEvent ev{tick, duration, event[4], event[5], event[6]};
    features.add(ev);
    if (ph) ph->events[i] = ev;
    prev = tick;
  }
  return nullptr;
}
uint32_t roleSeed(uint32_t seed, int role) { return seed ^ (0x9E3779B9u * (role + 1)); }
}

DiceLibraryData::~DiceLibraryData() { clear(); }
void DiceLibraryData::clear() {
  if (phrases_) {
    for (int i = 0; i < mt::kDiceRoles; ++i) phrases_[i].~DicePhrase();
    heap_caps_free(phrases_);
  }
  phrases_ = nullptr;
  size_ = 0; mask_ = 0; ready_ = false;
}

bool DiceLibraryData::load(int genre, uint32_t seed) {
  clear();
  error_ = "MIDI LIBRARY MISSING";
  if (!hw::sdReady()) { error_ = "NO SD CARD"; return false; }
  fs::File file;
  for (const char* path : kPaths) {
    file = hw::sdFs().open(path, "r");
    if (file && !file.isDirectory()) break;
    file.close();
  }
  if (!file) return false;
  struct Close { fs::File& file; ~Close() { file.close(); } } close{file};
  const size_t bytes = file.size();
  if (bytes < kHeader || bytes > kMaxBytes) {
    error_ = bytes > kMaxBytes ? "MIDI LIBRARY TOO LARGE" : "BAD MIDI LIBRARY"; return false;
  }
  uint8_t head[kHeader];
  if (!read(file, head, sizeof(head))) { error_ = "MIDI LIBRARY READ ERROR"; return false; }
  const uint32_t count = u32(head + 8);
  if (memcmp(head, "DML1", 4) || u16(head + 4) != 1 || u16(head + 6) != kEntry ||
      !count || count > 16000 || count > (bytes - kHeader) / kEntry) {
    error_ = "BAD MIDI LIBRARY"; return false;
  }
  genre_ = genre >= 0 && genre < 9 ? genre : 0;
  seed_ = seed;
  mt::Rng rng[mt::kDiceRoles];
  uint32_t seen[mt::kDiceRoles] = {};
  uint8_t selected[mt::kDiceRoles][mt::kDicePhraseCandidates][kEntry] = {};
  for (int k = 0; k < mt::kDiceRoles; ++k) rng[k] = mt::Rng(roleSeed(seed, k));
  const uint32_t tableEnd = kHeader + count * kEntry;
  for (uint32_t i = 0; i < count; ++i) {
    uint8_t e[kEntry];
    if (!read(file, e, sizeof(e))) { error_ = "MIDI LIBRARY READ ERROR"; return false; }
    if (!validEntry(e, bytes, tableEnd)) { error_ = "BAD MIDI LIBRARY"; return false; }
    const int role = e[2];
    if (!(u16(e) & (1u << genre_))) continue;
    ++seen[role];
    const int slot = mt::diceCandidateSlot(seen[role], rng[role].below(seen[role]));
    if (slot >= 0) memcpy(selected[role][slot], e, sizeof(e));
  }
  // About 25 KiB rather than the library's 735 KiB; the File owns its small read-ahead buffer.
  auto* memory = heap_caps_malloc(sizeof(mt::DicePhrase) * mt::kDiceRoles, MALLOC_CAP_SPIRAM);
  if (!memory) { error_ = "NO MEMORY FOR MIDI"; return false; }
  phrases_ = static_cast<mt::DicePhrase*>(memory);
  for (int k = 0; k < mt::kDiceRoles; ++k) new (&phrases_[k]) mt::DicePhrase();
  for (int role = 0; role < mt::kDiceRoles; ++role) {
    if (!seen[role]) continue;
    const uint8_t* chosen = nullptr; int best = INT_MAX;
    for (int candidate = 0; candidate < mt::kDicePhraseCandidates; ++candidate) {
      if (static_cast<uint32_t>(candidate) >= seen[role]) break;
      const uint8_t* e = selected[role][candidate];
      mt::DicePhraseFeatures features;
      if (const char* error = readPhrase(file, e, features)) {
        clear(); error_ = error; return false;
      }
      const int score = features.score(static_cast<mt::DiceRole>(role), e[5], roleSeed(seed, role));
      if (score < best) { best = score; chosen = e; }
    }
    mt::DicePhraseFeatures ignored;
    if (const char* error = readPhrase(file, chosen, ignored, &phrases_[role])) {
      clear(); error_ = error; return false;
    }
    mask_ |= static_cast<uint8_t>(1u << role);
  }
  size_ = bytes;
  ready_ = true;
  return true;
}

bool DiceLibraryData::pick(int genre, mt::DiceRole role, uint32_t seed, mt::DicePhrase& out) const {
  const int k = static_cast<int>(role);
  if (!ready_ || genre != genre_ || k < 0 || k >= mt::kDiceRoles || !(mask_ & (1u << k)) ||
      seed != roleSeed(seed_, k)) return false;
  out = phrases_[k];
  return true;
}
}  // namespace storage
