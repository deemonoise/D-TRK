#include "stream_pool.h"
#include <atomic>
#include <string.h>

namespace mt {

namespace {
inline void fence() { std::atomic_signal_fence(std::memory_order_seq_cst); }
inline uint32_t tagOf(uint8_t gen, uint32_t b) { return (static_cast<uint32_t>(gen) << 24) | (b + 1); }
}  // namespace

void StreamPool::init(int16_t* mem, int slots) {
  n_ = slots < kStreamSlots ? slots : kStreamSlots;
  for (int s = 0; s < kStreamSlots; ++s) {
    slot_[s] = StreamSlot();
    slot_[s].buf = s < n_ ? mem + static_cast<uint32_t>(s) * kSlotBlocks * kStreamBlock : nullptr;
  }
  age_ = 0;
  underruns = 0;
}

void StreamPool::take(int s, uint32_t key, uint32_t frames, uint32_t rate, const SampleHead* heads,
                      uint8_t nHeads, const SampleRegion& g, SlotOwner o) {
  StreamSlot& x = slot_[s];
  if (x.key != key) {
    x.gen = static_cast<uint8_t>(x.gen + 1);
    for (auto& t : x.tag) t = 0;
    fence();
    x.key = key;
  }
  x.frames = frames;
  x.rate = rate;
  x.heads = heads;
  x.nHeads = nHeads;
  x.age = ++age_;
  x.play.from = g.from;
  x.play.to = g.to;
  x.play.loopLo = g.loopLo;
  x.play.loopHi = g.loopHi;
  x.play.loopMode = g.loopMode;
  const int64_t step = (static_cast<int64_t>(rate) << 32) / 44100;  // nominal pitch until published
  fence();
  x.owner = o;
  publish(s, g.pos, g.dir < 0 ? -step : step);
}

int StreamPool::acquire(uint32_t key, uint32_t frames, uint32_t rate, const SampleHead* heads, uint8_t nHeads,
                        const SampleRegion& g, SlotOwner o) {
  int same = -1, freeOld = -1, pre = -1;
  for (int s = 0; s < n_; ++s) {
    const StreamSlot& x = slot_[s];
    if (x.owner == SlotOwner::Prefetch && x.key == key && o == SlotOwner::Voice) {
      slot_[s].owner = SlotOwner::Voice;  // adopt: the loads it made stay
      slot_[s].age = ++age_;
      publish(s, g.pos, (g.dir < 0 ? -1 : 1) * ((static_cast<int64_t>(rate) << 32) / 44100));
      return s;
    }
    if (x.owner == SlotOwner::Free) {
      if (x.key == key) same = s;
      if (freeOld < 0 || x.age < slot_[freeOld].age) freeOld = s;
    } else if (x.owner == SlotOwner::Prefetch && (pre < 0 || x.age < slot_[pre].age)) {
      pre = s;
    }
  }
  const int s = same >= 0 ? same : (freeOld >= 0 ? freeOld : (o != SlotOwner::Prefetch ? pre : -1));
  if (s < 0) return -1;
  take(s, key, frames, rate, heads, nHeads, g, o);
  return s;
}

void StreamPool::release(int s) {
  if (s < 0 || s >= n_) return;
  slot_[s].owner = SlotOwner::Free;
  slot_[s].age = ++age_;
}

int StreamPool::expirePrefetch(uint32_t nowMs, uint32_t maxMs) {
  int n = 0;
  for (int s = 0; s < n_; ++s)
    if (slot_[s].owner == SlotOwner::Prefetch && nowMs - slot_[s].stamp > maxMs) {
      release(s);
      ++n;
    }
  return n;
}

const int16_t* StreamPool::block(int s, uint32_t b) const {
  const StreamSlot& x = slot_[s];
  const uint32_t t = tagOf(x.gen, b);
  for (int w = 0; w < kSlotBlocks; ++w)
    if (x.tag[w] == t) return x.buf + w * kStreamBlock;
  return nullptr;
}

void StreamPool::publish(int s, int64_t pos, int64_t step) {
  StreamSlot& x = slot_[s];
  x.seq = x.seq + 1;
  fence();
  x.play.pos = static_cast<uint32_t>(pos >> 32);
  x.play.step16 = static_cast<int32_t>(step >> 16);
  fence();
  x.seq = x.seq + 1;
}

bool StreamPool::snap(int s, PlaySnap& out) const {
  const StreamSlot& x = slot_[s];
  for (;;) {
    const uint32_t a = x.seq;
    fence();
    out = x.play;
    fence();
    if (!(a & 1) && a == x.seq) break;
  }
  return x.owner != SlotOwner::Free;
}

bool StreamPool::commit(const StreamLoad& l, const int16_t* data) {
  StreamSlot& x = slot_[l.slot];
  if (x.key != l.key || x.gen != l.gen || x.owner == SlotOwner::Free) return false;
  for (int i = 0; i < l.n; ++i) {
    x.tag[l.way[i]] = 0;  // the interrupt never reads a way being filled
    fence();
    memcpy(x.buf + l.way[i] * kStreamBlock, data + i * kStreamBlock, kStreamBlock * sizeof(int16_t));
    fence();
    if (x.key != l.key || x.gen != l.gen) return false;  // re-keyed meanwhile: leave it empty
    x.tag[l.way[i]] = tagOf(l.gen, l.block + i);
  }
  return true;
}

}  // namespace mt
