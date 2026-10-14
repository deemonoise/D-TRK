#include "stream_loader.h"

namespace mt {

namespace {

constexpr uint32_t kOutRate = 44100;
int16_t stage_[kLoadMax * kStreamBlock];  // one load's read

bool inHead(const StreamSlot& x, uint32_t b) {
  const uint32_t lo = b * kStreamBlock;
  uint32_t hi = lo + kStreamBlock;
  if (hi > x.frames) hi = x.frames;
  for (int h = 0; h < x.nHeads; ++h)
    if (x.heads[h].frame <= lo && x.heads[h].frame + x.heads[h].len >= hi) return true;
  return false;
}
bool have(const StreamPool& pool, int s, uint32_t b) {
  return pool.block(s, b) || inHead(pool.slot(s), b);
}

}  // namespace

int streamWant(const PlaySnap& p, uint32_t want[kAhead + 1]) {
  int n = 0;
  auto add = [&](uint32_t b) {
    for (int i = 0; i < n; ++i)
      if (want[i] == b) return;
    want[n++] = b;
  };
  const uint32_t lo = p.loopLo, hi = p.loopHi > p.loopLo ? p.loopHi : p.loopLo + 1;
  const uint8_t mode = p.loopMode;
  int dir = p.step16 < 0 ? -1 : 1;
  uint32_t f = p.pos;
  add(f / kStreamBlock);
  for (int guard = 0; guard < 2 * (kAhead + 1) + 4 && n < kAhead + 1; ++guard) {
    const uint32_t b = f / kStreamBlock;
    if (dir > 0) {
      const uint32_t next = (b + 1) * kStreamBlock;
      if (next >= hi) {  // the edge is in this block
        if (mode == static_cast<uint8_t>(LoopMode::Forward)) {
          f = lo;
        } else if (mode == static_cast<uint8_t>(LoopMode::PingPong)) {
          dir = -1;
          if (b * kStreamBlock <= lo) break;
          f = b * kStreamBlock - 1;
        } else {
          break;
        }
      } else {
        f = next;
      }
    } else {
      const uint32_t start = b * kStreamBlock;
      if (start <= lo) {
        if (mode == static_cast<uint8_t>(LoopMode::Forward)) {
          f = hi - 1;
        } else if (mode == static_cast<uint8_t>(LoopMode::PingPong)) {
          dir = 1;
          if ((b + 1) * kStreamBlock >= hi) break;
          f = (b + 1) * kStreamBlock;
        } else {
          break;
        }
      } else {
        f = start - 1;
      }
    }
    add(f / kStreamBlock);
  }
  return n;
}

uint32_t streamReserveUs(const StreamPool& pool, int s) {
  PlaySnap p;
  if (!pool.snap(s, p)) return UINT32_MAX;
  uint32_t want[kAhead + 1];
  const int n = streamWant(p, want);
  uint64_t frames = 0;
  int i = 0;
  for (; i < n && have(pool, s, want[i]); ++i) {
    if (i == 0) {  // the current block: only what is left of it in the play direction
      const uint32_t off = p.pos % kStreamBlock;
      frames += p.step16 < 0 ? off + 1 : kStreamBlock - off;
    } else {
      frames += kStreamBlock;
    }
  }
  if (i == n) return UINT32_MAX;  // everything it can want is in
  const uint32_t step = static_cast<uint32_t>(p.step16 < 0 ? -p.step16 : p.step16);
  if (!step) return UINT32_MAX;
  return static_cast<uint32_t>((frames << 16) / step * 1000000 / kOutRate);
}

bool streamPlan(const StreamPool& pool, int s, StreamLoad& out) {
  PlaySnap p;
  if (!pool.snap(s, p)) return false;
  const StreamSlot& x = pool.slot(s);
  uint32_t want[kAhead + 1];
  const int n = streamWant(p, want);
  int i = 0;
  while (i < n && have(pool, s, want[i])) ++i;
  if (i == n) return false;
  // A run of missing blocks, ascending on the card whichever way it plays.
  uint32_t lo = want[i], hi = want[i];
  for (int j = i + 1; j < n && hi - lo + 1 < kLoadMax; ++j) {
    if (have(pool, s, want[j])) break;
    if (want[j] == hi + 1) {
      hi = want[j];
    } else if (want[j] + 1 == lo) {
      lo = want[j];
    } else {
      break;
    }
  }
  out.slot = s;
  out.key = x.key;
  out.gen = x.gen;
  out.block = lo;
  out.n = static_cast<int>(hi - lo + 1);
  // Ways: empty ones first, then those holding blocks no longer wanted.
  int k = 0;
  for (int pass = 0; pass < 2 && k < out.n; ++pass)
    for (int w = 0; w < kSlotBlocks && k < out.n; ++w) {
      const uint32_t t = x.tag[w];
      bool free = t == 0 || (t >> 24) != x.gen;
      if (!free && pass == 1) {
        free = true;
        for (int j = 0; j < n; ++j)
          if ((t & 0xFFFFFF) == want[j] + 1) free = false;
      }
      bool taken = false;
      for (int q = 0; q < k; ++q) taken |= out.way[q] == w;
      if (free && !taken) out.way[k++] = w;
    }
  out.n = k;
  return k > 0;
}

bool streamLoad(StreamPool& pool, int s, BlockReader& r) {
  StreamLoad l;
  if (!streamPlan(pool, s, l)) return false;
  if (!r.read(l.key, l.block, l.n, stage_)) return false;
  pool.commit(l, stage_);
  return true;
}

}  // namespace mt
