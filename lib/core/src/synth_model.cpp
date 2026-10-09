#include "synth_model.h"
#include <stddef.h>

namespace mt {

namespace {

constexpr uint32_t kTracksOff = offsetof(SynthModel, tracks);
constexpr uint32_t kInstrOff = offsetof(SynthModel, instruments);
constexpr uint32_t kSamplesOff = offsetof(SynthModel, samples);
constexpr uint32_t kWtOff = offsetof(SynthModel, wavetables);

static_assert(offsetof(SynthModel, bpm) == 0, "master chunk starts the model");
static_assert(offsetof(SynthModel, djFilter) < kTracksOff, "master fields before the tracks");
static_assert(kTracksOff < kInstrOff && kInstrOff < kSamplesOff && kSamplesOff < kWtOff,
              "chunk order in SynthModel");
static_assert(offsetof(SynthModel, sampleCount) < kWtOff, "sampleCount in the samples chunk");
static_assert(offsetof(SynthModel, wavetableCount) > kWtOff, "wavetableCount in the wavetables chunk");

}  // namespace

bool chunkRange(uint16_t id, uint32_t& off, uint32_t& len) {
  const uint32_t i0 = static_cast<uint32_t>(Chunk::Instr0);
  switch (static_cast<Chunk>(id)) {
    case Chunk::Master: off = 0, len = kTracksOff; return true;
    case Chunk::Tracks: off = kTracksOff, len = kInstrOff - kTracksOff; return true;
    case Chunk::Samples: off = kSamplesOff, len = kWtOff - kSamplesOff; return true;
    case Chunk::Wavetables: off = kWtOff, len = sizeof(SynthModel) - kWtOff; return true;
    default: break;
  }
  if (id < i0 || id >= i0 + kInstruments) return false;
  const uint32_t i = id - i0;
  off = kInstrOff + i * sizeof(Instrument);
  // The last instrument takes the padding before the samples.
  len = i + 1 < kInstruments ? sizeof(Instrument) : kSamplesOff - off;
  return true;
}

uint16_t chunkId(int i) {
  // Master first (tempo, levels), then tracks, instruments, the lists.
  static const uint16_t kHead[] = {static_cast<uint16_t>(Chunk::Master), static_cast<uint16_t>(Chunk::Tracks)};
  if (i < 2) return kHead[i];
  if (i < 2 + kInstruments) return static_cast<uint16_t>(static_cast<int>(Chunk::Instr0) + i - 2);
  return i == 2 + kInstruments ? static_cast<uint16_t>(Chunk::Samples) : static_cast<uint16_t>(Chunk::Wavetables);
}

int chunkIndex(uint16_t id) {
  for (int i = 0; i < kChunks; ++i)
    if (chunkId(i) == id) return i;
  return -1;
}

}  // namespace mt
