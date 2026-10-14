#pragma once
#include <stdint.h>
#include "sample_store.h"
#include "stream_pool.h"
#include "stream_sched.h"

namespace mt {

// Optional PSRAM (one 8 MB chip on FlexSPI2): an arena for whole-sample copies, a boot self-test and a
// watchdog on a reserved guard region. The chip sits behind the data cache (write-back), so the checks
// push their writes to the chip and drop the lines before they read back.
constexpr uint32_t kPsramBytes = 8u << 20;
constexpr uint32_t kPsramGuardBytes = 4096;  // the chip's last 4 KB: the watchdog's
constexpr uint32_t kPsramAlign = 32;         // cache line
constexpr uint32_t kWatchMs = 100;
constexpr int kGuardWords = 64;              // checked words, one per 64 B of the guard
constexpr uint32_t kSelfTestBytes = 64 * 1024;

// Cache maintenance (Teensy: arm_dcache_*; host: nothing).
struct PsramHw {
  virtual void flush(const void* p, uint32_t bytes) = 0;  // dirty lines to the chip, then dropped
  virtual void drop(const void* p, uint32_t bytes) = 0;   // lines dropped: the next reads hit the chip
};

// Bump allocator over the copies; reset() only while nothing reads them (synth parked).
class PsramArena {
 public:
  void init(void* base, uint32_t bytes);  // base: 32-byte aligned (0x70000000)
  // Before the first alloc: bytes at the arena's start (the PERF buffer) that reset() keeps; the
  // arena shrinks to what follows (from the next cache line). nullptr after an alloc or if too big.
  void* reserve(uint32_t bytes);
  // Offset from base aligned to kPsramAlign; nullptr if it does not fit.
  void* alloc(uint32_t bytes);
  void reset() { used_ = 0; }
  uint32_t used() const { return used_; }
  uint32_t size() const { return size_; }

 private:
  uint8_t* base_ = nullptr;
  uint32_t size_ = 0, used_ = 0;
  bool allocated_ = false;
};

// Address lines (a marker at every power-of-two word, word 0 must survive them all), then
// kSelfTestBytes of patterns at base. Destroys what is there. False on any mismatch.
bool psramSelfTest(uint32_t* base, uint32_t bytes, PsramHw& hw);

// Every kWatchMs writes a fresh pattern into the guard, pushes it to the chip and reads it back.
// After a mismatch it stays failed: PSRAM comes back only after a reboot.
class PsramWatch {
 public:
  void init(uint32_t* guard, PsramHw& hw);  // guard: kPsramGuardBytes, nothing else uses it
  // False once failed. Not due: true without touching the chip.
  bool step(uint32_t nowMs);
  bool failed() const { return failed_; }

 private:
  volatile uint32_t* g_ = nullptr;
  PsramHw* hw_ = nullptr;
  uint32_t last_ = 0, seed_ = 0;
  bool started_ = false, failed_ = false;
};

// Arena bytes for a copy: whole blocks (the loader reads blocks; the tail of the last is padding).
inline uint32_t psramBytes(uint32_t frames) {
  return (frames + kStreamBlock - 1) / kStreamBlock * kStreamBlock * 2;
}
// Indices of frames[0..n) shortest first (equal lengths keep their order), as many as fit in
// capBytes in that order. Returns how many.
int psramFillOrder(const uint32_t* frames, int n, uint32_t capBytes, int* order);

// Copies store samples into the arena, shortest first, one load (<= kLoadMax blocks) per step();
// a copy reaches the table (and the voices) only after the crc of the copy matches its key.
class PsramFill final : public BgWork {
 public:
  PsramFill(StoreTable& t, const StreamPool& p, BlockReader& r, PsramArena& a, PsramHw& hw)
      : t_(t), p_(p), r_(r), a_(a), hw_(hw) {}
  // The samples to hold (crcs, duplicates allowed; ones not in the store or not contiguous are
  // left out). The synth is parked: the old copies go, the arena starts over.
  void restart(const uint32_t* crcs, int n);
  bool step() override;  // false: nothing to do (done, or PSRAM off)
  int planned() const { return n_; }
  int ready() const { return ready_; }
  int bad() const { return bad_; }
  uint32_t plannedBytes() const { return total_; }
  // Progress in bytes: a copy counts half, its crc check the other half.
  uint32_t doneBytes() const { return done_; }
  // Percent of the plan copied and checked: 100 only once the last check is through (100 when
  // nothing is planned).
  uint8_t pct() const;

 private:
  static constexpr uint32_t kVerifyFrames = 4096;  // per step: 8 KB read back from the chip
  enum class Phase : uint8_t { Idle, Copy, Verify };
  void next(bool ok);
  StoreTable& t_;
  const StreamPool& p_;
  BlockReader& r_;
  PsramArena& a_;
  PsramHw& hw_;
  int item_[kStoreEntries];  // table indices in fill order
  int n_ = 0, cur_ = 0, ready_ = 0, bad_ = 0;
  Phase phase_ = Phase::Idle;
  int16_t* dst_ = nullptr;
  uint32_t blk_ = 0, vfr_ = 0, crc_ = 0;
  uint32_t total_ = 0, done_ = 0, finished_ = 0;
};

}  // namespace mt
