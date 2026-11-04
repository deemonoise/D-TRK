#include "stream_loader.h"
#include "hot.h"

namespace mt {

namespace {

constexpr uint32_t kOutRate = 44100;
#if MT_STREAM_LOAD_BLOCKS > 4
// Larger experimental reads leave RAM1 for the renderer. Only a successful read's l.n blocks
// are committed, and each such block is completely overwritten before use (RAM2 is NOLOAD).
MT_RAM2 int16_t stage_[kLoadMax * kStreamBlock] __attribute__((aligned(32)));
#else
int16_t stage_[kLoadMax * kStreamBlock];  // one load's read
#endif

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

// Mapping the whole filter window follows the same region rules as SAMPLE playback: an
// initial forward prefix/reverse suffix is available until the first loop turn. This planner
// runs outside the audio interrupt; only loop turns need the frame-by-frame fallback.
uint32_t guardedFrame(const PlaySnap& p, int64_t frame) {
  const int64_t lo = p.loopLo, hi = p.loopHi;
  const bool wrap = (frame < lo && (p.looped || p.step16 < 0)) ||
                    (frame >= hi && (p.looped || p.step16 >= 0));
  if (wrap && hi > lo) {
    if (p.loopMode == static_cast<uint8_t>(LoopMode::Forward)) {
      const int64_t span = hi - lo;
      int64_t phase = (frame - lo) % span;
      if (phase < 0) phase += span;
      frame = lo + phase;
    } else if (p.loopMode == static_cast<uint8_t>(LoopMode::PingPong)) {
      const int64_t length = hi - lo - 1;
      if (!length) return p.loopLo;
      int64_t phase = (frame - lo) % (2 * length);
      if (phase < 0) phase += 2 * length;
      frame = lo + (phase <= length ? phase : 2 * length - phase);
    }
  }
  if (p.to <= p.from) return p.from;
  return frame < p.from ? p.from : (frame >= p.to ? p.to - 1 : static_cast<uint32_t>(frame));
}

bool addWant(uint32_t block, uint32_t* want, int& n) {
  for (int i = 0; i < n; ++i) if (want[i] == block) return true;
  if (n == kWantMax) return false;
  want[n++] = block;
  return true;
}

bool guardedWant(const PlaySnap& p, uint32_t* want, int& n) {
  n = 0;
  addWant(p.pos / kStreamBlock, want, n);
  if (!p.before && !p.after) return true;
  const int64_t first = int64_t(p.pos) - p.before, last = int64_t(p.pos) + p.after;
  const bool looping = p.loopHi > p.loopLo &&
      (p.loopMode == static_cast<uint8_t>(LoopMode::Forward) ||
       p.loopMode == static_cast<uint8_t>(LoopMode::PingPong));
  const bool folds = looping && ((first < p.loopLo && (p.looped || p.step16 < 0)) ||
                                  (last >= p.loopHi && (p.looped || p.step16 >= 0)));
  if (!folds) {
    // Identity + region clamping is monotone: every tap in a block has the same cache need.
    // Enumerate crossed blocks directly, preserving the frame oracle's first-visit order.
    const uint32_t begin = guardedFrame(p, first) / kStreamBlock;
    const uint32_t end = guardedFrame(p, last) / kStreamBlock;
    for (uint32_t block = begin; block <= end; ++block)
      if (!addWant(block, want, n)) return false;
    return true;
  }
  for (int64_t f = first; f <= last; ++f)
    if (!addWant(guardedFrame(p, f) / kStreamBlock, want, n)) return false;
  return true;
}

// The cursor path stays separate from guard priority, so guard blocks never count as future
// playback time in streamReserveUs.
int pathWant(const PlaySnap& p, uint32_t want[kWantMax], bool* complete = nullptr,
             uint32_t* spans = nullptr) {
  const int limit = p.before || p.after ? kWantMax : kAhead + 1;
  int n = 0;
  bool ended = false;
  const uint32_t lo = p.loopLo, hi = p.loopHi > p.loopLo ? p.loopHi : p.loopLo + 1;
  uint8_t directions[kWantMax] = {};
  auto add = [&](uint32_t frame, int dir) {
    const uint32_t b = frame / kStreamBlock;
    const uint8_t bit = dir > 0 ? 1 : 2;
    for (int i = 0; i < n; ++i)
      if (want[i] == b) {
        if (directions[i] & bit) return false;  // a whole loop has been visited
        directions[i] |= bit;
        return true;
      }
    directions[n] = bit;
    if (spans) {
      const uint32_t start = b * kStreamBlock > lo ? b * kStreamBlock : lo;
      const uint32_t end = (b + 1) * kStreamBlock < hi ? (b + 1) * kStreamBlock : hi;
      // The first visit's actual cursor handles partial loop blocks and initial attack/suffix.
      // A reflected revisit can add more playback time, so counting only this visit is conservative.
      spans[n] = dir < 0 ? (frame >= start ? frame - start + 1 : 0)
                          : (end > frame ? end - frame : 0);
    }
    want[n++] = b;
    return true;
  };
  const uint8_t mode = p.loopMode;
  int dir = p.step16 < 0 ? -1 : 1;
  uint32_t f = p.pos;
  add(f, dir);
  for (int guard = 0; guard < 2 * kWantMax + 4 && n < limit; ++guard) {
    const uint32_t b = f / kStreamBlock;
    if (dir > 0) {
      const uint32_t next = (b + 1) * kStreamBlock;
      if (next >= hi) {  // the edge is in this block
        if (mode == static_cast<uint8_t>(LoopMode::Forward)) {
          f = lo;
        } else if (mode == static_cast<uint8_t>(LoopMode::PingPong)) {
          dir = -1;
          if (b * kStreamBlock <= lo) { ended = true; break; }
          f = b * kStreamBlock - 1;
        } else {
          ended = true;
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
          if ((b + 1) * kStreamBlock >= hi) { ended = true; break; }
          f = (b + 1) * kStreamBlock;
        } else {
          ended = true;
          break;
        }
      } else {
        f = start - 1;
      }
    }
    if (!add(f, dir)) { ended = true; break; }
  }
  if (complete) *complete = ended;
  return n;
}

}  // namespace

int streamWant(const PlaySnap& p, uint32_t want[kWantMax]) {
  int n;
  guardedWant(p, want, n);
  uint32_t future[kWantMax];
  const int count = pathWant(p, future);
  for (int i = 0; i < count && n < kWantMax; ++i) addWant(future[i], want, n);
  return n;
}

uint32_t streamReserveUs(const StreamPool& pool, int s) {
  PlaySnap p;
  if (!pool.snap(s, p)) return UINT32_MAX;
  uint32_t want[kWantMax];
  int required;
  if (!guardedWant(p, want, required)) return 0;  // the FIR window exceeds this slot's capacity
  for (int i = 0; i < required; ++i) if (!have(pool, s, want[i])) return 0;
  bool complete;
  uint32_t spans[kWantMax];
  const int n = pathWant(p, want, &complete, spans);
  uint64_t frames = 0;
  int i = 0;
  for (; i < n && have(pool, s, want[i]); ++i) {
    frames += spans[i];
  }
  if (i == n && complete) return UINT32_MAX;  // cached through the end or a whole loop
  const uint32_t step = static_cast<uint32_t>(p.step16 < 0 ? -p.step16 : p.step16);
  if (!step) return UINT32_MAX;
  const uint32_t lead = p.step16 < 0 ? p.before : p.after;
  frames = frames > lead ? frames - lead : 0;
  return static_cast<uint32_t>((frames << 16) / step * 1000000 / kOutRate);
}

bool streamNeeds(const StreamPool& pool, int s, uint32_t& reserveUs) {
  PlaySnap p;
  if (!pool.snap(s, p)) { reserveUs = UINT32_MAX; return false; }
  uint32_t want[kWantMax];
  int required;
  bool guardsReady = guardedWant(p, want, required);
  uint32_t future[kWantMax], spans[kWantMax];
  bool complete;
  const int count = pathWant(p, future, &complete, spans);
  bool cached[kWantMax];
  for (int i = 0; i < count; ++i) cached[i] = have(pool, s, future[i]);
  bool missing = false;
  for (int i = 0; i < required; ++i) {
    int path = 0;
    while (path < count && future[path] != want[i]) ++path;
    const bool ready = path < count ? cached[path] : have(pool, s, want[i]);
    guardsReady &= ready;
    missing |= !ready;
  }
  int n = required;
  for (int i = 0; i < count && n < kWantMax; ++i) {
    const int before = n;
    addWant(future[i], want, n);
    if (n != before && !cached[i]) missing = true;
  }
  reserveUs = 0;
  if (guardsReady) {
    uint64_t frames = 0;
    int i = 0;
    for (; i < count && cached[i]; ++i) frames += spans[i];
    const uint32_t step = static_cast<uint32_t>(p.step16 < 0 ? -p.step16 : p.step16);
    if ((i == count && complete) || !step) reserveUs = UINT32_MAX;
    else {
      const uint32_t lead = p.step16 < 0 ? p.before : p.after;
      frames = frames > lead ? frames - lead : 0;
      reserveUs = static_cast<uint32_t>((frames << 16) / step * 1000000 / kOutRate);
    }
  }
  if (!missing) return false;
  // streamPlan can only load a miss when at least one way is empty/stale or no longer wanted.
  // This preserves its result even if several ways happen to contain the same wanted block.
  const StreamSlot& x = pool.slot(s);
  for (int way = 0; way < kSlotBlocks; ++way) {
    const uint32_t tag = x.tag[way];
    if (!tag || (tag >> 24) != x.gen) return true;
    bool wanted = false;
    for (int i = 0; i < n; ++i)
      if ((tag & 0xFFFFFF) == want[i] + 1) { wanted = true; break; }
    if (!wanted) return true;
  }
  return false;
}

bool streamPlan(const StreamPool& pool, int s, StreamLoad& out) {
  PlaySnap p;
  if (!pool.snap(s, p)) return false;
  const StreamSlot& x = pool.slot(s);
  uint32_t want[kWantMax];
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
