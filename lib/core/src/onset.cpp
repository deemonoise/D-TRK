#include "onset.h"
#include <math.h>
#include "slices.h"

namespace mt {
namespace {

constexpr int kWin = 256, kHop = 128, kRefine = 32;
constexpr float kGateDb = -60.f, kRiseDb = 3.f;

uint32_t absDiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

int absAt(const int16_t* d, uint32_t i) { return d[i] < 0 ? -d[i] : d[i]; }

// Start of the first kRefine block of [from, to) reaching half its peak.
uint32_t refine(const int16_t* d, uint32_t from, uint32_t to) {
  int peak = 0;
  for (uint32_t i = from; i < to; ++i) peak = absAt(d, i) > peak ? absAt(d, i) : peak;
  for (uint32_t b = from; b < to; b += kRefine) {
    const uint32_t e = b + kRefine < to ? b + kRefine : to;
    for (uint32_t i = b; i < e; ++i)
      if (2 * absAt(d, i) >= peak) return b;
  }
  return from;
}

struct Cands {
  Onset* out;
  float fl[kMaxOnsets];
  int n = 0, max;

  void remove(int i) {
    for (int k = i; k + 1 < n; ++k) {
      out[k] = out[k + 1];
      fl[k] = fl[k + 1];
    }
    --n;
  }

  void add(uint32_t pos, float f, uint32_t gap) {
    // Closer than gap: the stronger one stays.
    for (int i = 0; i < n; ++i)
      if (absDiff(out[i].pos, pos) < gap && fl[i] >= f) return;
    for (int i = n - 1; i >= 0; --i)
      if (absDiff(out[i].pos, pos) < gap) remove(i);
    if (n >= max) {
      int w = 0;
      for (int i = 1; i < n; ++i)
        if (fl[i] < fl[w]) w = i;
      if (fl[w] >= f) return;
      remove(w);
    }
    int i = n;
    while (i > 0 && out[i - 1].pos > pos) {
      out[i] = out[i - 1];
      fl[i] = fl[i - 1];
      --i;
    }
    out[i] = {pos, 0};
    fl[i] = f;
    ++n;
  }
};

}  // namespace

int detectOnsets(const int16_t* d, uint32_t frames, uint32_t rate, Onset* out, int max) {
  if (max > kMaxOnsets) max = kMaxOnsets;
  if (!d || max <= 0 || frames < static_cast<uint32_t>(kWin)) return 0;
  Cands c;
  c.out = out;
  c.max = max;
  const uint32_t gap = rate * 40 / 1000;
  const uint32_t windows = (frames - kWin) / kHop + 1;
  float prevDb = -120.f;
  float flux[3] = {0, 0, 0};  // k - 2, k - 1, k: the peak test runs on k - 1
  for (uint32_t k = 0; k <= windows; ++k) {
    float f = 0;
    if (k < windows) {
      uint64_t sum = 0;
      for (uint32_t i = k * kHop; i < k * kHop + kWin; ++i) sum += static_cast<int64_t>(d[i]) * d[i];
      const float e = static_cast<float>(sum) / kWin / (32768.f * 32768.f);
      const float db = 10.f * log10f(e + 1e-12f);
      f = db > kGateDb ? db - prevDb : 0.f;
      prevDb = db;
    }
    flux[0] = flux[1];
    flux[1] = flux[2];
    flux[2] = f;
    if (k == 0 || !(flux[1] >= kRiseDb && flux[1] > flux[0] && flux[1] >= flux[2])) continue;
    const uint32_t p = k - 1;  // the peak window
    const uint32_t from = p > 0 ? (p - 1) * kHop : 0;
    const uint32_t to = p * kHop + kWin < frames ? p * kHop + kWin : frames;
    c.add(refine(d, from, to), flux[1], gap);
  }
  float top = 0;
  for (int i = 0; i < c.n; ++i) top = c.fl[i] > top ? c.fl[i] : top;
  for (int i = 0; i < c.n; ++i) {
    const int s = static_cast<int>(lroundf(c.fl[i] / top * 1000.f));
    out[i].strength = static_cast<uint16_t>(s < 1 ? 1 : s > 1000 ? 1000 : s);
  }
  return c.n;
}

int nearestOnset(const Onset* o, int n, uint32_t pos) {
  int best = -1;
  for (int i = 0; i < n; ++i)
    if (best < 0 || absDiff(o[i].pos, pos) < absDiff(o[best].pos, pos)) best = i;
  return best;
}

int stepOnset(const Onset* o, int n, uint32_t pos, int dir) {
  if (dir > 0) {
    for (int i = 0; i < n; ++i)
      if (o[i].pos > pos) return i;
  } else if (dir < 0) {
    for (int i = n - 1; i >= 0; --i)
      if (o[i].pos < pos) return i;
  }
  return -1;
}

void chopTransients(Instrument& m, const Onset* o, int n, uint32_t len, uint32_t rate) {
  const uint32_t from = fracToFrame(m.start, len), to = fracToFrame(m.end, len);
  const int thr = m.chopThresh > 100 ? 100 : m.chopThresh;
  const uint16_t minS = static_cast<uint16_t>((100 - thr) * 10);
  const uint32_t skip = rate / 100;
  Onset pick[kMaxOnsets];
  int k = 0;
  for (int i = 0; i < n && k < kMaxOnsets; ++i)
    if (o[i].pos >= from + skip && o[i].pos < to && o[i].strength >= minS) pick[k++] = o[i];
  // Too many: the strongest first (partial selection sort), then back in order of pos.
  constexpr int kKeep = kMaxSlices - 1;
  if (k > kKeep) {
    for (int i = 0; i < kKeep; ++i) {
      int b = i;
      for (int j = i + 1; j < k; ++j)
        if (pick[j].strength > pick[b].strength) b = j;
      const Onset t = pick[i];
      pick[i] = pick[b];
      pick[b] = t;
    }
    k = kKeep;
    for (int i = 1; i < k; ++i)
      for (int j = i; j > 0 && pick[j - 1].pos > pick[j].pos; --j) {
        const Onset t = pick[j];
        pick[j] = pick[j - 1];
        pick[j - 1] = t;
      }
  }
  sliceClear(m);
  m.slices[0] = m.start;
  int c = 1;
  for (int i = 0; i < k; ++i) {
    const uint16_t f = frameToFrac(pick[i].pos, len);
    if (f > m.slices[c - 1]) m.slices[c++] = f;
  }
  m.sliceCount = static_cast<uint8_t>(c);
}

}  // namespace mt
