#include "time_stretch.h"
#include <limits.h>
#include <new>
#include <string.h>
#include "hot.h"

namespace mt {
namespace {
constexpr uint32_t kSourceSamples = 3072;
constexpr int32_t kScoreOne = 1 << 20;

MT_CONTROL uint32_t minimum(uint32_t a, uint32_t b) { return a < b ? a : b; }
MT_CONTROL uint32_t distance(uint32_t a, uint32_t b) { return a < b ? b - a : a - b; }

// Integer square root and a fixed-point correlation score keep candidate selection reproducible.
// Energies are scaled together before multiplication, which stays below 2^60 even for int16_min.
MT_CONTROL uint64_t integerRoot(uint64_t value) {
  uint64_t root = 0, bit = uint64_t(1) << 62;
  while (bit > value) bit >>= 2;
  while (bit) {
    if (value >= root + bit) {
      value -= root + bit;
      root = (root >> 1) + bit;
    } else root >>= 1;
    bit >>= 2;
  }
  return root;
}

MT_CONTROL int32_t normalizedScore(int64_t dot, uint64_t a, uint64_t b) {
  if (!a || !b) return INT32_MIN;
  uint64_t largest = a > b ? a : b;
  unsigned shift = 0;
  while (largest > (uint64_t(1) << 30)) { largest >>= 1; ++shift; }
  const uint64_t denominator = integerRoot((a >> shift) * (b >> shift));
  if (!denominator) return INT32_MIN;
  const int64_t scaled = dot / (int64_t(1) << shift);
  int64_t score = scaled * kScoreOne / static_cast<int64_t>(denominator);
  // Quantization of energies / sqrt must not rank a nominally perfect correlation above unity.
  if (score > kScoreOne) score = kScoreOne;
  if (score < -kScoreOne) score = -kScoreOne;
  return static_cast<int32_t>(score);
}

MT_CONTROL int16_t blend(int16_t before, int16_t after, uint32_t index, uint32_t length) {
  const int32_t mixed = int32_t(before) * int32_t(length - index) + int32_t(after) * int32_t(index);
  const int32_t rounded = mixed < 0 ? mixed - int32_t(length / 2) : mixed + int32_t(length / 2);
  return static_cast<int16_t>(rounded / int32_t(length));
}
}  // namespace

static_assert(sizeof(TimeStretch) < 200, "offline stretch state must stay small");

MT_CONTROL TimeStretch::~TimeStretch() { reset(); }

MT_CONTROL void TimeStretch::reset() {
  delete[] work_;
  work_ = nullptr;
  state_ = TimeStretchResult::BadArg;
  done_ = outputFrames_ = inputFrames_ = sourceFirst_ = chosen_ = 0;
  hop_ = seek_ = cursor_ = grainFrames_ = 0;
  ready_ = haveTail_ = endGrain_ = false;
}

MT_CONTROL TimeStretchResult TimeStretch::start(uint32_t inputFrames, uint32_t rate, uint32_t outputFrames) {
  reset();
  if (!inputFrames || !outputFrames || !rate || rate > 44100 ||
      uint64_t(outputFrames) * 2 < inputFrames || outputFrames > uint64_t(inputFrames) * 2)
    return state_;
  inputFrames_ = inputFrames;
  outputFrames_ = outputFrames;
  if (inputFrames != outputFrames) {
    // 46 ms grains / 23 ms synthesis hops at normal sample rates, shortened for tiny inputs.
    uint32_t hop = minimum(1024, static_cast<uint32_t>((uint64_t(rate) * 1024 + 22050) / 44100));
    if (!hop) hop = 1;
    hop = minimum(hop, inputFrames / 2 + (inputFrames & 1));
    hop = minimum(hop, outputFrames > 1 ? outputFrames / 2 : 1);
    hop_ = static_cast<uint16_t>(hop);
    seek_ = hop_ / 2;
    work_ = new (std::nothrow) int16_t[kWorkBytes / sizeof(int16_t)];
    if (!work_) return state_ = TimeStretchResult::NoMemory;
  }
  return state_ = TimeStretchResult::Running;
}

MT_CONTROL uint32_t TimeStretch::select(uint32_t nominal) const {
  const int16_t* reference = work_ + kSourceSamples;
  const uint32_t first = nominal > seek_ ? nominal - seek_ : 0;
  const uint32_t last = nominal + minimum(seek_, inputFrames_ - 1 - nominal);
  int64_t sumReference = 0, energyReference = 0;
  for (uint32_t i = 0; i < hop_; ++i) {
    const int64_t v = reference[i];
    sumReference += v;
    energyReference += v * v;
  }
  const uint64_t varianceReference = energyReference - sumReference * sumReference / hop_;
  if (!varianceReference) return nominal - sourceFirst_;  // silence/DC never causes a search drift
  int64_t sum = 0, energy = 0;
  const int16_t* candidate = work_ + first - sourceFirst_;
  for (uint32_t i = 0; i < hop_; ++i) {
    const int64_t v = candidate[i];
    sum += v;
    energy += v * v;
  }
  uint32_t best = nominal;
  int32_t bestScore = INT32_MIN;
  for (uint32_t at = first;; ++at, ++candidate) {
    int64_t dot = 0;
    for (uint32_t i = 0; i < hop_; ++i) dot += int64_t(reference[i]) * candidate[i];
    const int64_t centered = dot - sumReference * sum / hop_;
    const uint64_t variance = energy - sum * sum / hop_;
    const int32_t score = normalizedScore(centered, varianceReference, variance);
    if (score > bestScore || (score == bestScore &&
        (distance(at, nominal) < distance(best, nominal) ||
         (distance(at, nominal) == distance(best, nominal) && at < best)))) {
      best = at;
      bestScore = score;
    }
    if (at == last) break;
    const int64_t leaving = candidate[0], entering = candidate[hop_];
    sum += entering - leaving;
    energy += entering * entering - leaving * leaving;
  }
  return best - sourceFirst_;
}

MT_CONTROL bool TimeStretch::prepare(TimeStretchReader& reader) {
  const uint32_t nominal = static_cast<uint32_t>(uint64_t(done_) * inputFrames_ / outputFrames_);
  const uint32_t remaining = outputFrames_ - done_;
  endGrain_ = remaining <= hop_;
  grainFrames_ = static_cast<uint16_t>(endGrain_ ? remaining : hop_);
  // The final (possibly partial) hop is anchored to the actual source end. Without this finite
  // boundary rule, compressing could discard the last attack and expansion could hold its edge.
  sourceFirst_ = endGrain_ ? inputFrames_ - grainFrames_
      : (haveTail_ && nominal > seek_ ? nominal - seek_ : 0);
  const uint32_t count = endGrain_ ? grainFrames_ : 2 * hop_ + 2 * seek_;
  const uint32_t valid = minimum(count, inputFrames_ - sourceFirst_);
  for (uint32_t at = 0; at < valid;) {
    const uint32_t n = minimum(kMaxStepFrames, valid - at);
    if (!reader.read(sourceFirst_ + at, n, work_ + at)) return false;
    at += n;
  }
  // Replicate only the finite edge inside a grain. This keeps DC/tiny signals unchanged and
  // avoids reading beyond the selected source; the explicit output length always terminates it.
  for (uint32_t i = valid; i < count; ++i) work_[i] = work_[valid - 1];
  chosen_ = haveTail_ && !endGrain_ ? select(nominal) : 0;
  cursor_ = 0;
  ready_ = true;
  return true;
}

MT_CONTROL TimeStretchResult TimeStretch::step(TimeStretchReader& reader, int16_t* out,
                                               uint32_t capacity, uint32_t& produced) {
  produced = 0;
  if (state_ != TimeStretchResult::Running) return state_;
  if (!out || !capacity) return state_ = TimeStretchResult::BadArg;
  uint32_t n = minimum(minimum(capacity, kMaxStepFrames), outputFrames_ - done_);
  if (!work_) {
    if (!reader.read(done_, n, out)) return state_ = TimeStretchResult::ReadFail;
  } else {
    if (!ready_ && !prepare(reader)) return state_ = TimeStretchResult::ReadFail;
    n = minimum(n, grainFrames_ - cursor_);
    const int16_t* candidate = work_ + chosen_;
    const int16_t* tail = work_ + kSourceSamples;
    for (uint32_t i = 0; i < n; ++i) {
      const uint32_t pos = cursor_ + i;
      if (endGrain_ && grainFrames_ == 1) out[i] = candidate[pos];
      else if (haveTail_) {
        const uint32_t fade = endGrain_ ? grainFrames_ - 1 : hop_;
        out[i] = blend(tail[pos], candidate[pos], pos, fade);
      } else out[i] = candidate[pos];
    }
    cursor_ += n;
    if (cursor_ == grainFrames_ && !endGrain_) {
      memcpy(work_ + kSourceSamples, candidate + hop_, hop_ * sizeof(int16_t));
      haveTail_ = true;
      ready_ = false;
    }
  }
  done_ += n;
  produced = n;
  if (done_ == outputFrames_) state_ = TimeStretchResult::Done;
  return state_;
}
}  // namespace mt
