#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// SynthModel cut into chunks for the ESP -> synth board mirror. The chunks tile the struct: every
// byte (padding included) belongs to exactly one.
enum class Chunk : uint16_t { Tracks = 0, Master = 1, Samples = 2, Wavetables = 3, Instr0 = 16 };  // Instr0 + i
constexpr int kChunks = 4 + kInstruments;

// Byte range of a chunk inside SynthModel; false for an unknown id.
bool chunkRange(uint16_t id, uint32_t& off, uint32_t& len);
uint16_t chunkId(int i);  // i-th chunk in send order, 0..kChunks-1
int chunkIndex(uint16_t id);  // inverse of chunkId, -1 for an unknown id

}  // namespace mt
