#include "onset.h"
#include <math.h>
#include "slices.h"

namespace mt {
namespace {

constexpr int kWin = 256, kHop = 128, kRefine = 32;
constexpr float kGateDb = -60.f, kRiseDb = 3.f;

uint32_t absDiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

// Start of the first kRefine block of [from, to) reaching half its peak; at(i) = frame i.
template <class At>
uint32_t refine(At at, uint32_t from, uint32_t to) {
  auto absAt = [&](uint32_t i) {
    const int v = at(i);
    return v < 0 ? -v : v;
  };
  int peak = 0;
  for (uint32_t i = from; i < to; ++i) peak = absAt(i) > peak ? absAt(i) : peak;
  for (uint32_t b = from; b < to; b += kRefine) {
    const uint32_t e = b + kRefine < to ? b + kRefine : to;
    for (uint32_t i = b; i < e; ++i)
      if (2 * absAt(i) >= peak) return b;
  }
  return from;
}

}  // namespace

void OnsetScan::Cands::remove(int i) {
  for (int k = i; k + 1 < n; ++k) {
    out[k] = out[k + 1];
    fl[k] = fl[k + 1];
  }
  --n;
}

void OnsetScan::Cands::add(uint32_t pos, float f, uint32_t gap) {
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

OnsetScan::OnsetScan(uint32_t frames, uint32_t rate, int max) : frames_(frames), gap_(rate * 40 / 1000) {
  c_.max = max > kMaxOnsets ? kMaxOnsets : (max < 0 ? 0 : max);
  // No room for candidates: no windows either (finish returns 0, like detectOnsets).
  windows_ = c_.max > 0 && frames >= static_cast<uint32_t>(kWin) ? (frames - kWin) / kHop + 1 : 0;
}

void OnsetScan::feed(const int16_t* d, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    ring_[got_ % kRing] = d[i];
    ++got_;
    while (k_ < windows_ && got_ >= k_ * kHop + kWin) window(k_++);
  }
}

void OnsetScan::window(uint32_t k) {
  auto at = [&](uint32_t i) { return static_cast<int>(ring_[i % kRing]); };
  float f = 0;
  if (k < windows_) {
    uint64_t sum = 0;
    for (uint32_t i = k * kHop; i < k * kHop + kWin; ++i) sum += static_cast<int64_t>(at(i)) * at(i);
    const float e = static_cast<float>(sum) / kWin / (32768.f * 32768.f);
    const float db = 10.f * log10f(e + 1e-12f);
    f = db > kGateDb ? db - prevDb_ : 0.f;
    prevDb_ = db;
  }
  flux_[0] = flux_[1];
  flux_[1] = flux_[2];
  flux_[2] = f;
  if (k == 0 || !(flux_[1] >= kRiseDb && flux_[1] > flux_[0] && flux_[1] >= flux_[2])) return;
  const uint32_t p = k - 1;  // the peak window
  const uint32_t from = p > 0 ? (p - 1) * kHop : 0;
  const uint32_t to = p * kHop + kWin < frames_ ? p * kHop + kWin : frames_;
  c_.add(refine(at, from, to), flux_[1], gap_);
}

int OnsetScan::finish(Onset* out) {
  if (!windows_) return 0;
  window(windows_);  // the last peak test (flux 0 after the end)
  float top = 0;
  for (int i = 0; i < c_.n; ++i) top = c_.fl[i] > top ? c_.fl[i] : top;
  for (int i = 0; i < c_.n; ++i) {
    const int s = static_cast<int>(lroundf(c_.fl[i] / top * 1000.f));
    out[i] = {c_.out[i].pos, static_cast<uint16_t>(s < 1 ? 1 : s > 1000 ? 1000 : s)};
  }
  return c_.n;
}

int detectOnsets(const int16_t* d, uint32_t frames, uint32_t rate, Onset* out, int max) {
  if (!d) return 0;
  OnsetScan s(frames, rate, max);
  s.feed(d, frames);
  return s.finish(out);
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
