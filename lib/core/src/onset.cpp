#include "onset.h"
#include <math.h>
#include "slices.h"

namespace mt {
namespace {

// Original causal filter-bank implementation; no external source code is used.
// Positive band-energy change is motivated by Dixon, "Onset Detection Revisited", DAFx 2006:
// https://www.dafx.de/paper-archive/details/_kscRgr98aSFW4l-j1YJuA
constexpr float kGateDb = -60.f, kRiseDb = 3.f;

uint32_t absDiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

MT_CONTROL float coefficient(float frequency, uint32_t rate) {
  return rate ? 1.f - expf(-6.28318530718f * frequency / rate) : 0;
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
  hop_ = rate / 500;
  if (!hop_) hop_ = 1;
  windows_ = c_.max > 0 && rate && frames >= hop_ ? (frames - 1) / hop_ + 1 : 0;
  dcAlpha_ = coefficient(20.f, rate);
  lowAlpha_ = coefficient(250.f, rate);
  middleAlpha_ = coefficient(2500.f, rate);
  smoothAlpha_ = rate ? 1.f - expf(-static_cast<float>(hop_) / (rate * .020f)) : 0;
}

void OnsetScan::feed(const int16_t* d, uint32_t n) {
  for (uint32_t i = 0; i < n && got_ < frames_; ++i) {
    const float x = d[i];
    if (!got_) dc_ = x;  // a constant offset present at the start is not an attack
    dc_ += dcAlpha_ * (x - dc_);
    const float ac = x - dc_;
    low_ += lowAlpha_ * (ac - low_);
    middle_ += middleAlpha_ * (ac - middle_);
    const float bands[kBands] = {low_, middle_ - low_, ac - middle_};
    const uint32_t block = (got_ / kRefine) % kPeakBlocks;
    for (int band = 0; band < kBands; ++band) {
      if (got_ % kRefine == 0) peak_[band][block] = 0;
      const float magnitude = fabsf(bands[band]);
      const uint16_t value = static_cast<uint16_t>(magnitude > 65535.f ? 65535.f : magnitude);
      if (value > peak_[band][block]) peak_[band][block] = value;
      energy_[band] += bands[band] * bands[band];
    }
    ++got_;
    if (++hopCount_ == hop_ || got_ == frames_) window(k_++);
  }
}

uint32_t OnsetScan::refineBand(uint32_t from, uint32_t to, int band) const {
  const uint32_t first = from / kRefine, last = (to - 1) / kRefine;
  uint16_t maximum = 0;
  for (uint32_t block = first; block <= last; ++block)
    if (peak_[band][block % kPeakBlocks] > maximum) maximum = peak_[band][block % kPeakBlocks];
  for (uint32_t block = first; block <= last; ++block)
    if (2u * peak_[band][block % kPeakBlocks] >= maximum) return block * kRefine;
  return from;
}

void OnsetScan::window(uint32_t k) {
  float f = 0;
  uint8_t strongestBand = 0;
  if (hopCount_) {
    for (int band = 0; band < kBands; ++band) {
      smooth_[band] += smoothAlpha_ * (energy_[band] / hopCount_ - smooth_[band]);
      const float db = 10.f * log10f(smooth_[band] / (32768.f * 32768.f) + 1e-12f);
      const float previous = prevDb_[band] > kGateDb ? prevDb_[band] : kGateDb;
      const float rise = db > kGateDb ? db - previous : 0;
      if (rise > f) { f = rise; strongestBand = static_cast<uint8_t>(band); }
      prevDb_[band] = db;
      energy_[band] = 0;
    }
    hopCount_ = 0;
  }
  flux_[0] = flux_[1]; flux_[1] = flux_[2]; flux_[2] = f;
  fluxBand_[0] = fluxBand_[1]; fluxBand_[1] = fluxBand_[2]; fluxBand_[2] = strongestBand;
  if (!windows_ || k == 0 || !(flux_[1] >= kRiseDb && flux_[1] > flux_[0] && flux_[1] >= flux_[2])) return;
  const uint32_t attack = k - 1;
  const uint32_t from = attack ? (attack - 1) * hop_ : 0;
  const uint32_t end = (attack + 1) * hop_;
  const uint32_t to = end < frames_ ? end : frames_;
  c_.add(refineBand(from, to, fluxBand_[1]), flux_[1], gap_);
}

int OnsetScan::finish(Onset* out) {
  if (!windows_) return 0;
  window(k_);  // the last peak test (flux 0 after the end)
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
