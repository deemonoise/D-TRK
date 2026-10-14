#include "psram_cache.h"
#include "sample_set.h"

namespace mt {

namespace {

constexpr uint32_t kGuardStride = kPsramGuardBytes / 4 / kGuardWords;  // words between checked ones
static_assert(kGuardStride >= 1, "guard words");

// A word that depends on its index and the pass: a stuck, shorted or stale word shows.
uint32_t mix(uint32_t i, uint32_t seed) {
  uint32_t x = i * 0x9E3779B1u ^ seed * 0x85EBCA6Bu;
  x ^= x >> 15;
  return x * 0x2C1B3C6Du;
}

}  // namespace

void PsramArena::init(void* base, uint32_t bytes) {
  base_ = static_cast<uint8_t*>(base);
  size_ = bytes;
  used_ = 0;
  allocated_ = false;
}

void* PsramArena::reserve(uint32_t bytes) {
  if (allocated_ || used_ || bytes > size_) return nullptr;
  const uint32_t at = (bytes + kPsramAlign - 1) & ~(kPsramAlign - 1);
  uint8_t* const r = base_;
  const uint32_t skip = at < size_ ? at : size_;
  base_ += skip;
  size_ -= skip;
  return r;
}

void* PsramArena::alloc(uint32_t bytes) {
  const uint32_t at = (used_ + kPsramAlign - 1) & ~(kPsramAlign - 1);
  if (at > size_ || bytes > size_ - at) return nullptr;
  used_ = at + bytes;
  allocated_ = true;
  return base_ + at;
}

bool psramSelfTest(uint32_t* base, uint32_t bytes, PsramHw& hw) {
  volatile uint32_t* m = base;
  const uint32_t words = bytes / 4;
  // Address lines: a stuck or shorted line makes two markers share a word.
  m[0] = 0x5A5A5A5Au;
  hw.flush(base, 4);
  for (uint32_t k = 1; k < words; k <<= 1) {
    m[k] = 0xA5000000u | k;
    hw.flush(base + k, 4);
  }
  hw.drop(base, 4);
  if (m[0] != 0x5A5A5A5Au) return false;
  for (uint32_t k = 1; k < words; k <<= 1) {
    hw.drop(base + k, 4);
    if (m[k] != (0xA5000000u | k)) return false;
  }
  // Patterns: every bit both ways, then a word-unique one.
  const uint32_t n = (kSelfTestBytes < bytes ? kSelfTestBytes : bytes) / 4;
  for (uint32_t pass = 0; pass < 3; ++pass) {
    auto want = [&](uint32_t i) { return pass == 0 ? 0x55555555u : (pass == 1 ? 0xAAAAAAAAu : mix(i, 1)); };
    for (uint32_t i = 0; i < n; ++i) m[i] = want(i);
    hw.flush(base, n * 4);
    hw.drop(base, n * 4);
    for (uint32_t i = 0; i < n; ++i)
      if (m[i] != want(i)) return false;
  }
  return true;
}

void PsramWatch::init(uint32_t* guard, PsramHw& hw) {
  g_ = guard;
  hw_ = &hw;
  last_ = seed_ = 0;
  started_ = failed_ = false;
}

bool PsramWatch::step(uint32_t nowMs) {
  if (failed_) return false;
  if (!g_ || (started_ && nowMs - last_ < kWatchMs)) return true;
  started_ = true;
  last_ = nowMs;
  ++seed_;
  for (int i = 0; i < kGuardWords; ++i) g_[i * kGuardStride] = mix(static_cast<uint32_t>(i), seed_);
  uint32_t* const g = const_cast<uint32_t*>(g_);
  hw_->flush(g, kPsramGuardBytes);
  hw_->drop(g, kPsramGuardBytes);
  for (int i = 0; i < kGuardWords; ++i)
    if (g_[i * kGuardStride] != mix(static_cast<uint32_t>(i), seed_)) {
      failed_ = true;
      return false;
    }
  return true;
}

int psramFillOrder(const uint32_t* frames, int n, uint32_t capBytes, int* order) {
  for (int i = 0; i < n; ++i) order[i] = i;
  for (int i = 1; i < n; ++i) {  // insertion sort: stable, n <= kStoreEntries, once per load
    const int x = order[i];
    int j = i;
    for (; j > 0 && frames[order[j - 1]] > frames[x]; --j) order[j] = order[j - 1];
    order[j] = x;
  }
  uint32_t used = 0;
  int k = 0;
  for (; k < n; ++k) {
    const uint32_t b = psramBytes(frames[order[k]]);
    if (b > capBytes - used) break;  // longer ones do not fit either
    used += b;
  }
  return k;
}

void PsramFill::restart(const uint32_t* crcs, int n) {
  for (int i = 0; i < t_.count(); ++i) t_.setPsram(i, nullptr);
  a_.reset();
  n_ = cur_ = ready_ = bad_ = 0;
  total_ = done_ = finished_ = 0;
  dst_ = nullptr;
  uint32_t frames[kStoreEntries];
  int idx[kStoreEntries];
  int m = 0;
  for (int k = 0; k < n && m < kStoreEntries; ++k) {
    const int i = t_.find(crcs[k]);
    if (i < 0 || !t_.at(i).sector) continue;  // not in the store, or not readable by blocks
    bool dup = false;
    for (int j = 0; j < m && !dup; ++j) dup = idx[j] == i;
    if (dup) continue;
    idx[m] = i;
    frames[m++] = t_.at(i).frames;
  }
  int order[kStoreEntries];
  n_ = psramFillOrder(frames, m, a_.size(), order);
  for (int k = 0; k < n_; ++k) {
    item_[k] = idx[order[k]];
    total_ += psramBytes(frames[order[k]]);
  }
  phase_ = n_ ? Phase::Copy : Phase::Idle;
}

bool PsramFill::step() {
  if (phase_ == Phase::Idle) return false;
  if (!p_.psramOk) {  // the watchdog failed: no more copies
    phase_ = Phase::Idle;
    return false;
  }
  const StoreEntry& e = t_.at(item_[cur_]);
  const uint32_t key = e.crc;
  if (!dst_) {
    dst_ = static_cast<int16_t*>(a_.alloc(psramBytes(e.frames)));  // planned to fit
    blk_ = vfr_ = crc_ = 0;
  }
  if (phase_ == Phase::Copy) {
    const uint32_t blocks = (e.frames + kStreamBlock - 1) / kStreamBlock;
    const int n = static_cast<int>(blocks - blk_ < kLoadMax ? blocks - blk_ : kLoadMax);
    int16_t* to = dst_ + blk_ * kStreamBlock;
    if (!r_.read(key, blk_, n, to)) {
      next(false);
      return true;
    }
    hw_.flush(to, static_cast<uint32_t>(n) * kStreamBlock * 2);  // on the chip, out of the cache
    blk_ += n;
    done_ = finished_ + blk_ * kStreamBlock;  // half of the bytes copied so far
    if (blk_ == blocks) phase_ = Phase::Verify;
    return true;
  }
  // Verify: the crc of the copy as the chip holds it (its lines were flushed, so reads miss).
  uint32_t k = e.frames - vfr_;
  if (k > kVerifyFrames) k = kVerifyFrames;
  crc_ = sampleCrc(dst_ + vfr_, k, crc_);
  vfr_ += k;
  done_ = finished_ + psramBytes(e.frames) / 2 + vfr_;  // the copy's half and half the bytes checked
  if (vfr_ == e.frames) next(crc_ == key);
  return true;
}

uint8_t PsramFill::pct() const {
  if (!total_) return 100;
  const uint32_t p = static_cast<uint32_t>(100ull * done_ / total_);
  if (phase_ != Phase::Idle && p > 99) return 99;
  return static_cast<uint8_t>(p > 100 ? 100 : p);
}

void PsramFill::next(bool ok) {
  if (ok) {
    t_.setPsram(item_[cur_], dst_);
    ++ready_;
  } else {
    ++bad_;  // its arena space stays unused until the next restart
  }
  finished_ += psramBytes(t_.at(item_[cur_]).frames);
  done_ = finished_;
  dst_ = nullptr;
  phase_ = ++cur_ < n_ ? Phase::Copy : Phase::Idle;
}

}  // namespace mt
