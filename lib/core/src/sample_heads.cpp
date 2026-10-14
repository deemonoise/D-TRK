#include "sample_heads.h"
#include <string.h>
#include "sample_region.h"
#include "sample_set.h"

namespace mt {

namespace {
void put32(int16_t* at, uint32_t v) { memcpy(at, &v, 4); }
uint32_t get32(const int16_t* at) {
  uint32_t v;
  memcpy(&v, at, 4);
  return v;
}
}  // namespace

int headPoints(const SynthModel& m, uint32_t crc, uint32_t frames, uint32_t rate, HeadPoint* out, int max) {
  if (!frames || max <= 0) return 0;
  const uint32_t lStart = rate * kHeadStartMs / 1000, lSlice = rate * kHeadSliceMs / 1000;
  int n = 0;
  auto push = [&](uint32_t at, uint32_t len, bool back) {
    if (n >= max || at >= frames) return;
    HeadPoint h;
    if (back) {
      h.frame = at + 1 >= len ? at + 1 - len : 0;
      h.len = at + 1 - h.frame;
    } else {
      h.frame = at;
      h.len = frames - at < len ? frames - at : len;
    }
    out[n++] = h;
  };
  auto uses = [&](const char* name) {
    const int k = projSampleFind(m, name);
    return k >= 0 && m.samples[k].crc == crc && m.samples[k].frames == frames;
  };
  for (int pass = 0; pass < 2; ++pass)
    for (const Instrument& ins : m.instruments) {
      if (ins.type == InstrType::Kit) {
        if (pass == 0)
          for (const KitLane& l : ins.kit)
            if (l.instr == kNoInstr && uses(l.sample)) push(0, lStart, false);
        continue;
      }
      if (ins.type != InstrType::Sample || !uses(ins.sample)) continue;
      if (pass == 0) {
        const SampleRegion g = sampleRegion(ins, -1, frames, 0);
        push(static_cast<uint32_t>(g.pos >> 32), lStart, g.dir < 0);
        if (g.loopMode == static_cast<uint8_t>(LoopMode::Forward))
          push(g.dir < 0 ? g.loopHi - 1 : g.loopLo, lStart, g.dir < 0);
      } else {
        const int cnt = ins.sliceCount < kMaxSlices ? ins.sliceCount : kMaxSlices;
        for (int i = 0; i < cnt; ++i) {
          const SampleRegion g = sampleRegion(ins, i, frames, 0);
          push(static_cast<uint32_t>(g.pos >> 32), lSlice, g.dir < 0);
        }
      }
    }
  // Sort by frame (n <= kMaxHeads: insertion sort), then merge overlapping / touching runs.
  for (int i = 1; i < n; ++i)
    for (int j = i; j > 0 && out[j - 1].frame > out[j].frame; --j) {
      const HeadPoint t = out[j];
      out[j] = out[j - 1];
      out[j - 1] = t;
    }
  int k = 0;
  for (int i = 0; i < n; ++i) {
    if (k && out[i].frame <= out[k - 1].frame + out[k - 1].len) {
      const uint32_t end = out[i].frame + out[i].len, have = out[k - 1].frame + out[k - 1].len;
      if (end > have) out[k - 1].len = end - out[k - 1].frame;
    } else {
      out[k++] = out[i];
    }
  }
  return k;
}

void headKey(uint32_t crc, char out[kSampleNameMax + 1]) {
  out[0] = 'h';
  sampleKey(crc, out + 1);
}

uint32_t headLayout(const HeadPoint* p, int n, int16_t index[kHeadIndexFrames]) {
  memset(index, 0, kHeadIndexFrames * sizeof(int16_t));
  index[0] = static_cast<int16_t>(n);
  uint32_t off = kHeadIndexFrames;
  for (int i = 0; i < n; ++i) {
    put32(index + 2 + 6 * i, p[i].frame);
    put32(index + 4 + 6 * i, p[i].len);
    put32(index + 6 + 6 * i, off);
    off += p[i].len;
  }
  return off;
}

bool headParse(const int16_t* d, uint32_t entryFrames, SampleHead* out, int& n) {
  n = 0;
  if (!d || entryFrames < static_cast<uint32_t>(kHeadIndexFrames)) return false;
  const int cnt = d[0];
  if (cnt < 0 || cnt > kMaxHeads) return false;
  for (int i = 0; i < cnt; ++i) {
    const uint32_t frame = get32(d + 2 + 6 * i), len = get32(d + 4 + 6 * i), off = get32(d + 6 + 6 * i);
    if (off < static_cast<uint32_t>(kHeadIndexFrames) || off > entryFrames || len > entryFrames - off) return false;
    out[i].frame = frame;
    out[i].len = len;
    out[i].d = d + off;
  }
  n = cnt;
  return true;
}

bool headSame(const int16_t* d, uint32_t entryFrames, const HeadPoint* p, int n) {
  SampleHead h[kMaxHeads];
  int k = 0;
  if (!headParse(d, entryFrames, h, k) || k != n) return false;
  for (int i = 0; i < n; ++i)
    if (h[i].frame != p[i].frame || h[i].len != p[i].len) return false;
  return true;
}

}  // namespace mt
