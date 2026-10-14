#pragma once
#include <stdint.h>
#include "sample_region.h"
#include "sample_source.h"

namespace mt {

// Slots of streamed sample data, shared by the audio interrupt (voices: acquire / block / publish /
// release) and the loader in loop() (plan / commit). A slot holds kSlotBlocks blocks of kStreamBlock
// frames of one sample (key), each way tagged (gen << 24) | (block + 1). A new key bumps gen, so a
// load planned for the old key never matches: the loader checks key / gen again when it commits.
// The interrupt may preempt the loader, not the reverse; acquire / release from loop() (prefetch,
// preview) run with the audio interrupt masked.
#ifndef MT_STREAM_SLOTS
#define MT_STREAM_SLOTS 16
#endif
constexpr int kStreamSlots = MT_STREAM_SLOTS;
constexpr int kStreamBlock = 512;  // frames (1 KB, 2 card sectors)
constexpr int kSlotBlocks = 6;
constexpr int kLoadMax = 4;  // blocks per card read
constexpr uint32_t kPreviewKey = 0xFFFFFFFFu;  // PreviewFile's ring, not a sample

enum class SlotOwner : uint8_t { Free, Voice, Prefetch, Preview };

// Where a slot's consumer plays, published by the interrupt for the loader.
struct PlaySnap {
  uint32_t pos = 0;   // frame
  int32_t step16 = 0; // frames per output sample, 16.16, < 0 backwards
  uint32_t loopLo = 0, loopHi = 1, from = 0, to = 1;
  uint8_t loopMode = 0;
};

struct StreamSlot {
  volatile uint32_t key = 0;
  volatile uint8_t gen = 0;
  volatile SlotOwner owner = SlotOwner::Free;
  volatile uint32_t tag[kSlotBlocks] = {};  // 0 = empty
  uint32_t frames = 0, rate = 0;
  uint32_t age = 0;       // acquire / release order
  uint32_t stamp = 0;     // Prefetch: loop() time (ms) it was asked
  const SampleHead* heads = nullptr;
  uint8_t nHeads = 0;
  volatile uint32_t seq = 0;  // snapshot seqlock (odd: being written)
  PlaySnap play;
  int16_t* buf = nullptr;  // kSlotBlocks * kStreamBlock frames
};

// A planned load: blocks [block, block + n) of key into ways way[0..n).
struct StreamLoad {
  int slot = -1;
  uint32_t key = 0;
  uint8_t gen = 0;
  uint32_t block = 0;
  int n = 0;
  int way[kLoadMax] = {};
};

// Card access for loads: n blocks from block on (frames past the end: anything).
struct BlockReader {
  virtual bool read(uint32_t key, uint32_t block, int n, int16_t* out) = 0;
};

class StreamPool {
 public:
  // mem: slots * kSlotBlocks * kStreamBlock frames; slots <= kStreamSlots.
  void init(int16_t* mem, int slots = kStreamSlots);
  int slots() const { return n_; }

  // Takes a slot for key starting at g: a Prefetch slot of key, a free slot of key (its blocks
  // stay), the longest-free slot, the oldest Prefetch slot. -1 if none (all Voice / Preview).
  int acquire(uint32_t key, uint32_t frames, uint32_t rate, const SampleHead* heads, uint8_t nHeads,
              const SampleRegion& g, SlotOwner o);
  void release(int s);
  // Releases Prefetch slots asked (stamp) more than maxMs before nowMs: the note never came.
  // loop() with the audio interrupt masked (the interrupt adopts Prefetch slots). Returns how many.
  int expirePrefetch(uint32_t nowMs, uint32_t maxMs);
  // Block b of slot s, nullptr if not loaded.
  const int16_t* block(int s, uint32_t b) const;
  // The consumer's position and signed step (32.32) after it rendered.
  void publish(int s, int64_t pos, int64_t step);
  bool snap(int s, PlaySnap& out) const;  // false: the slot is free
  const StreamSlot& slot(int s) const { return slot_[s]; }
  StreamSlot& slotRef(int s) { return slot_[s]; }

  // Loader: copies l.n blocks from data into l's ways and tags them, if the slot still holds l.key at
  // l.gen. False (nothing tagged) otherwise.
  bool commit(const StreamLoad& l, const int16_t* data);

  BlockReader* sync = nullptr;     // offline render: a miss loads at once (see Synth::frameRun)
  volatile uint32_t underruns = 0; // voices that ran out of data, counted once per underrun
  volatile bool psramOk = false;   // PSRAM copies usable; cleared for good on a failure

 private:
  StreamSlot slot_[kStreamSlots];
  int n_ = 0;
  uint32_t age_ = 0;
  void take(int s, uint32_t key, uint32_t frames, uint32_t rate, const SampleHead* heads, uint8_t nHeads,
            const SampleRegion& g, SlotOwner o);
};

}  // namespace mt
