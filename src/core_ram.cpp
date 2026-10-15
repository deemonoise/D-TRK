#include "core_ram.h"
#include "audio/audio.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "sequencer.h"
#include "synth.h"

namespace core_ram {

namespace {
constexpr size_t kSeqOff = (sizeof(mt::Synth) + 15) & ~size_t{15};
constexpr size_t kBytes = kSeqOff + sizeof(mt::Sequencer);
constexpr uint32_t kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

void* block = nullptr;   // the internal block (null: not taken, or moved out)
bool moved = false;      // toPsram() moved both out of the block
void* synthPs = nullptr;  // their PSRAM copies while moved
void* seqPs = nullptr;
}  // namespace

void reserve() {
  block = heap_caps_malloc(kBytes, kInternal);
  audio::reserve(block);
  engine::reserve(block ? static_cast<char*>(block) + kSeqOff : nullptr);
}

bool internal() { return block && !moved; }

bool toPsram() {
  if (!internal()) return false;
  void* s = heap_caps_malloc(sizeof(mt::Synth), MALLOC_CAP_SPIRAM);
  void* q = heap_caps_malloc(sizeof(mt::Sequencer), MALLOC_CAP_SPIRAM);
  if (!s || !q || !audio::moveSynth(s)) {
    heap_caps_free(s);
    heap_caps_free(q);
    return false;
  }
  engine::moveSeq(q);
  heap_caps_free(block);
  block = nullptr;
  synthPs = s;
  seqPs = q;
  moved = true;
  return true;
}

bool toInternal() {
  if (!moved) return false;
  void* nb = heap_caps_malloc(kBytes, kInternal);
  if (!nb) return false;
  if (!audio::moveSynth(nb)) {
    heap_caps_free(nb);
    return false;
  }
  engine::moveSeq(static_cast<char*>(nb) + kSeqOff);
  heap_caps_free(synthPs);
  heap_caps_free(seqPs);
  synthPs = seqPs = nullptr;
  block = nb;
  moved = false;
  return true;
}

}  // namespace core_ram
