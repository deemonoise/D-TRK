#include "core_ram.h"
#include "audio/audio.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "sequencer.h"
#include "synth.h"

namespace core_ram {
namespace {
constexpr uint32_t kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
void* synthStorage = nullptr;
bool synthIsInternal = false;
void* seqStorage = nullptr;  // process lifetime: engine constructs it once, never moved for Wi-Fi

bool moveSynth(uint32_t caps, bool internal) {
  if (!synthStorage) return false;
  void* destination = heap_caps_malloc(sizeof(mt::Synth), caps);
  if (!destination) return false;
  if (!audio::moveSynth(destination)) {
    heap_caps_free(destination);
    return false;
  }
  void* previous = synthStorage;
  synthStorage = destination;
  synthIsInternal = internal;
  heap_caps_free(previous);
  return true;
}
}  // namespace

void reserve() {
  synthStorage = heap_caps_malloc(sizeof(mt::Synth), kInternal);
  synthIsInternal = synthStorage != nullptr;
  // Keep ownership even when boot internal RAM is short: a later restore can retry.
  if (!synthStorage) synthStorage = heap_caps_malloc(sizeof(mt::Synth), MALLOC_CAP_SPIRAM);
  seqStorage = heap_caps_malloc(sizeof(mt::Sequencer), MALLOC_CAP_SPIRAM);
  audio::reserve(synthStorage);
  engine::reserve(seqStorage);
}

bool internal() { return synthStorage && synthIsInternal; }

bool toPsram() {
  return internal() && moveSynth(MALLOC_CAP_SPIRAM, false);
}

bool toInternal() {
  return synthStorage && !synthIsInternal && moveSynth(kInternal, true);
}
}  // namespace core_ram
