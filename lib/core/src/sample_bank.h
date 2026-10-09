#pragma once
#include <stdint.h>
#include "model.h"

namespace mt {

// Flash access behind an interface: RAM in tests, the Teensy's program flash on the synth board.
struct BankFlash {
  virtual uint32_t size() const = 0;
  virtual bool read(uint32_t off, void* d, uint32_t n) = 0;
  virtual bool erase(uint32_t off, uint32_t n) = 0;  // kBankAlign aligned
  // Only into erased (0xFF) bytes.
  virtual bool write(uint32_t off, const void* d, uint32_t n) = 0;
  virtual const uint8_t* mapped() const = 0;  // whole region, or nullptr
};

constexpr int kBankEntries = 128;
constexpr uint32_t kBankAlign = 4096;
constexpr uint32_t kBankHeader = 8192;  // two table copies, one sector each

struct BankEntry {
  char name[kSampleNameMax + 1];
  uint32_t offset;  // bytes from the region start, kBankAlign aligned
  uint32_t frames;  // int16 mono
  uint32_t rate;
  uint8_t root;
  uint8_t loop;     // LoopMode default
};

// Progress of a long operation: bytes done of total.
using BankProgress = void (*)(uint32_t done, uint32_t total, void* ctx);

// Sample bank in a flash region. Sectors 0 and 1 hold two copies of the table ("MTSB",
// version, count, sequence, crc32, entries); every change writes the older copy, so a power cut
// leaves the previous table. Data: mono int16, each sample starts on a kBankAlign boundary,
// first-fit allocation. Entries are sorted by name; names are unique ignoring case.
class SampleBank {
 public:
  explicit SampleBank(BankFlash& f) : f_(f) {}
  bool mount();  // reads the table; formats an empty bank if no valid copy is found
  int count() const { return n_; }
  const BankEntry* entry(int i) const { return i >= 0 && i < n_ ? &e_[i] : nullptr; }
  int find(const char* name) const;  // ignoring case, -1 if absent
  uint32_t capacity() const;         // data area, bytes
  uint32_t freeBytes() const;        // total, may be fragmented
  // Streaming add: begin reserves the first hole that fits frames, write appends (up to the
  // reserved frames), commit stores the entry with the frames written. False on a duplicate name,
  // a full table or no hole large enough.
  bool begin(const char* name, uint32_t frames, uint32_t rate, uint8_t root);
  bool write(const int16_t* d, uint32_t frames);
  bool commit();
  void abort() { adding_ = false; }
  // True if begin would find room for frames now (table slot, size limit, a hole large enough).
  bool fits(uint32_t frames) const;
  bool remove(int i);
  // New name for entry i (the entry is re-sorted). False on a bad name, one taken by another
  // entry (ignoring case) or a failed table save (then nothing changes).
  bool rename(int i, const char* name);
  // Entry src takes the name (and place) of entry i, which is removed: one table save, so a
  // power cut leaves either both entries or the replaced one. False if nothing changed.
  bool replace(int i, int src);
  // Moves data down to close the holes; the table is written after every moved sample. A sample
  // that would overlap its own old place goes through a free area above it first (two moves, the
  // table points at intact data after a power cut at any moment). Without such an area it is
  // left in place and compaction goes on after it.
  bool compact(BankProgress cb = nullptr, void* ctx = nullptr);
  // Changes with every successful commit / remove / rename / moved sample (cache key for UI).
  uint32_t generation() const { return gen_; }
  const int16_t* data(int i) const;  // via mapped(), nullptr without a mapping
  // n frames of entry i from frame on, through BankFlash::read. False past the end.
  bool readData(int i, uint32_t frame, int16_t* d, uint32_t n) const;

 private:
  static uint32_t span(uint32_t frames) { return (frames * 2 + kBankAlign - 1) / kBankAlign * kBankAlign; }
  uint32_t end() const { return f_.size() / kBankAlign * kBankAlign; }
  bool readCopy(int sector, uint32_t& seq);
  bool saveTable();
  bool format();
  int byOffset(int* idx) const;  // entry indices sorted by offset, returns count
  // First hole of need bytes at or above from (current offsets), false if none.
  bool firstFit(uint32_t from, uint32_t need, uint32_t& at) const;
  // Copies len bytes (kBankAlign multiple) between non-overlapping areas.
  bool copy(uint32_t to, uint32_t from, uint32_t len, uint8_t* buf, uint32_t& done, uint32_t total,
            BankProgress cb, void* ctx);

  BankFlash& f_;
  BankEntry e_[kBankEntries];
  int n_ = 0;
  uint32_t seq_ = 0;
  int cur_ = 1;  // sector of the newest table copy
  bool adding_ = false;
  BankEntry pend_{};
  uint32_t maxFrames_ = 0;  // reserved by begin
  uint32_t gen_ = 0;
};

}  // namespace mt
