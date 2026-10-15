#include "core_ram.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "sequencer.h"

namespace core_ram {

namespace {
constexpr uint32_t kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

void* block = nullptr;   // the internal block (null: not taken, or moved out)
bool moved = false;      // toPsram() moved the sequencer out of the block
void* seqPs = nullptr;   // its PSRAM copy while moved
}  // namespace

void reserve() {
  block = heap_caps_malloc(sizeof(mt::Sequencer), kInternal);
  engine::reserve(block);
}

bool internal() { return block && !moved; }

bool toPsram() {
  if (!internal()) return false;
  void* q = heap_caps_malloc(sizeof(mt::Sequencer), MALLOC_CAP_SPIRAM);
  if (!q) return false;
  engine::moveSeq(q);
  heap_caps_free(block);
  block = nullptr;
  seqPs = q;
  moved = true;
  return true;
}

bool toInternal() {
  if (!moved) return false;
  void* nb = heap_caps_malloc(sizeof(mt::Sequencer), kInternal);
  if (!nb) return false;
  engine::moveSeq(nb);
  heap_caps_free(seqPs);
  seqPs = nullptr;
  block = nb;
  moved = false;
  return true;
}

}  // namespace core_ram
