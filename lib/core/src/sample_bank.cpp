#include "sample_bank.h"
#include <string.h>
#include <memory>
#include <new>
#include "project_io.h"

namespace mt {
namespace {

constexpr uint16_t kVersion = 1;
constexpr uint32_t kHeadBytes = 16;  // "MTSB" u16 version u16 count u32 seq u32 crc
constexpr uint32_t kEntryBytes = 30;  // name[16] u32 offset u32 frames u32 rate u8 root u8 loop
constexpr uint32_t kTableBytes = kHeadBytes + kBankEntries * kEntryBytes;
static_assert(kTableBytes <= kBankAlign, "a table copy fits one sector");
static_assert(2 * kBankAlign == kBankHeader, "two table copies");

void put16(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
  put16(p, v & 0xFFFF);
  put16(p + 2, v >> 16);
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
int cmpName(const char* a, const char* b) {
  for (;; ++a, ++b) {
    const char x = lower(*a), y = lower(*b);
    if (x != y || !x) return x - y;
  }
}

// Table CRC: header without the crc field, then the entries.
uint32_t tableCrc(const uint8_t* t, int count) {
  return crc32(t + kHeadBytes, count * kEntryBytes, crc32(t, kHeadBytes - 4));
}

}  // namespace

int SampleBank::find(const char* name) const {
  for (int i = 0; i < n_; ++i)
    if (cmpName(e_[i].name, name) == 0) return i;
  return -1;
}

uint32_t SampleBank::capacity() const { return end() > kBankHeader ? end() - kBankHeader : 0; }

uint32_t SampleBank::freeBytes() const {
  uint32_t used = 0;
  for (int i = 0; i < n_; ++i) used += span(e_[i].frames);
  return capacity() - used;
}

const int16_t* SampleBank::data(int i) const {
  const uint8_t* m = f_.mapped();
  if (!m || i < 0 || i >= n_) return nullptr;
  return reinterpret_cast<const int16_t*>(m + e_[i].offset);
}

bool SampleBank::readData(int i, uint32_t frame, int16_t* d, uint32_t n) const {
  if (i < 0 || i >= n_ || frame > e_[i].frames || n > e_[i].frames - frame) return false;
  return f_.read(e_[i].offset + frame * 2, d, n * 2);
}

int SampleBank::byOffset(int* idx) const {
  for (int i = 0; i < n_; ++i) {
    int j = i;
    for (; j > 0 && e_[idx[j - 1]].offset > e_[i].offset; --j) idx[j] = idx[j - 1];
    idx[j] = i;
  }
  return n_;
}

bool SampleBank::readCopy(int sector, uint32_t& seq) {
  std::unique_ptr<uint8_t[]> t(new (std::nothrow) uint8_t[kTableBytes]);
  if (!t || !f_.read(sector * kBankAlign, t.get(), kHeadBytes)) return false;
  const int count = get16(t.get() + 6);
  if (memcmp(t.get(), "MTSB", 4) != 0 || get16(t.get() + 4) != kVersion || count > kBankEntries) return false;
  if (!f_.read(sector * kBankAlign + kHeadBytes, t.get() + kHeadBytes, count * kEntryBytes)) return false;
  if (tableCrc(t.get(), count) != get32(t.get() + 12)) return false;
  for (int i = 0; i < count; ++i) {
    const uint8_t* p = t.get() + kHeadBytes + i * kEntryBytes;
    BankEntry& e = e_[i];
    memcpy(e.name, p, kSampleNameMax);
    e.name[kSampleNameMax] = 0;
    e.offset = get32(p + 16);
    e.frames = get32(p + 20);
    e.rate = get32(p + 24);
    e.root = p[28];
    e.loop = p[29];
    if (!e.name[0] || e.frames == 0 || e.offset % kBankAlign || e.offset < kBankHeader ||
        e.offset > end() || span(e.frames) > end() - e.offset)
      return false;
  }
  n_ = count;
  seq = get32(t.get() + 8);
  return true;
}

bool SampleBank::saveTable() {
  std::unique_ptr<uint8_t[]> t(new (std::nothrow) uint8_t[kTableBytes]);
  if (!t) return false;
  memcpy(t.get(), "MTSB", 4);
  put16(t.get() + 4, kVersion);
  put16(t.get() + 6, static_cast<uint32_t>(n_));
  put32(t.get() + 8, seq_ + 1);
  for (int i = 0; i < n_; ++i) {
    uint8_t* p = t.get() + kHeadBytes + i * kEntryBytes;
    const BankEntry& e = e_[i];
    memset(p, 0, kSampleNameMax);
    memcpy(p, e.name, strlen(e.name));
    put32(p + 16, e.offset);
    put32(p + 20, e.frames);
    put32(p + 24, e.rate);
    p[28] = e.root;
    p[29] = e.loop;
  }
  put32(t.get() + 12, tableCrc(t.get(), n_));
  const int target = 1 - cur_;
  const uint32_t bytes = kHeadBytes + n_ * kEntryBytes;
  if (!f_.erase(target * kBankAlign, kBankAlign) || !f_.write(target * kBankAlign, t.get(), bytes)) return false;
  cur_ = target;
  ++seq_;
  ++gen_;
  return true;
}

bool SampleBank::format() {
  n_ = 0;
  seq_ = 0;
  cur_ = 1;
  return f_.erase(0, kBankHeader) && saveTable();
}

bool SampleBank::mount() {
  adding_ = false;
  if (end() <= kBankHeader) return false;
  // Load the copy with the higher sequence: read the other one first.
  uint32_t s0 = 0, s1 = 0;
  const bool ok0 = readCopy(0, s0);
  const bool ok1 = readCopy(1, s1);  // overwrites the entries even when it fails
  if (ok0 && (!ok1 || s0 > s1)) {
    readCopy(0, s0);
    seq_ = s0;
    cur_ = 0;
    return true;
  }
  if (ok1) {
    seq_ = s1;
    cur_ = 1;
    return true;
  }
  return format();
}

bool SampleBank::firstFit(uint32_t from, uint32_t need, uint32_t& at) const {
  int idx[kBankEntries];
  byOffset(idx);
  at = from;
  for (int k = 0; k < n_; ++k) {
    const BankEntry& e = e_[idx[k]];
    if (e.offset >= at && e.offset - at >= need) return true;
    if (e.offset + span(e.frames) > at) at = e.offset + span(e.frames);
  }
  return at <= end() && end() - at >= need;
}

bool SampleBank::fits(uint32_t frames) const {
  uint32_t at;
  return frames > 0 && frames <= capacity() / 2 && n_ < kBankEntries && firstFit(kBankHeader, span(frames), at);
}

bool SampleBank::begin(const char* name, uint32_t frames, uint32_t rate, uint8_t root) {
  adding_ = false;
  if (!name[0] || strlen(name) > kSampleNameMax || frames == 0 || n_ >= kBankEntries || find(name) >= 0) return false;
  if (frames > capacity() / 2) return false;
  uint32_t at;
  if (!firstFit(kBankHeader, span(frames), at)) return false;
  memset(&pend_, 0, sizeof(pend_));
  memcpy(pend_.name, name, strlen(name));
  pend_.offset = at;
  pend_.rate = rate;
  pend_.root = root;
  maxFrames_ = frames;
  adding_ = true;
  return true;
}

bool SampleBank::write(const int16_t* d, uint32_t frames) {
  if (!adding_ || frames > maxFrames_ - pend_.frames) return false;
  uint32_t pos = pend_.offset + pend_.frames * 2;
  uint32_t left = frames * 2;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(d);
  while (left > 0) {
    // Sectors are erased on first use: holes and aborted adds leave old data behind.
    if (pos % kBankAlign == 0 && !f_.erase(pos, kBankAlign)) return false;
    const uint32_t room = kBankAlign - pos % kBankAlign;
    const uint32_t n = left < room ? left : room;
    if (!f_.write(pos, p, n)) return false;
    pos += n;
    p += n;
    left -= n;
  }
  pend_.frames += frames;
  return true;
}

bool SampleBank::commit() {
  if (!adding_) return false;
  adding_ = false;
  if (pend_.frames == 0) return false;
  int at = n_;
  while (at > 0 && cmpName(e_[at - 1].name, pend_.name) > 0) --at;
  for (int i = n_; i > at; --i) e_[i] = e_[i - 1];
  e_[at] = pend_;
  ++n_;
  if (saveTable()) return true;
  for (int i = at; i + 1 < n_; ++i) e_[i] = e_[i + 1];  // not stored: forget it
  --n_;
  return false;
}

bool SampleBank::remove(int i) {
  if (i < 0 || i >= n_ || adding_) return false;
  const BankEntry gone = e_[i];
  for (int k = i; k + 1 < n_; ++k) e_[k] = e_[k + 1];
  --n_;
  if (saveTable()) return true;
  for (int k = n_; k > i; --k) e_[k] = e_[k - 1];  // not stored: keep it
  e_[i] = gone;
  ++n_;
  return false;
}

bool SampleBank::rename(int i, const char* name) {
  if (i < 0 || i >= n_ || adding_ || !name[0] || strlen(name) > kSampleNameMax) return false;
  const int other = find(name);
  if (other >= 0 && other != i) return false;
  const BankEntry old = e_[i];
  BankEntry e = e_[i];
  memset(e.name, 0, sizeof(e.name));
  memcpy(e.name, name, strlen(name));
  // Take it out, insert by the new name.
  for (int k = i; k + 1 < n_; ++k) e_[k] = e_[k + 1];
  int at = n_ - 1;
  while (at > 0 && cmpName(e_[at - 1].name, e.name) > 0) --at;
  for (int k = n_ - 1; k > at; --k) e_[k] = e_[k - 1];
  e_[at] = e;
  if (saveTable()) return true;
  for (int k = at; k + 1 < n_; ++k) e_[k] = e_[k + 1];  // undo: back to index i
  for (int k = n_ - 1; k > i; --k) e_[k] = e_[k - 1];
  e_[i] = old;
  return false;
}

bool SampleBank::replace(int i, int src) {
  if (i < 0 || i >= n_ || src < 0 || src >= n_ || i == src || adding_) return false;
  const BankEntry was = e_[i], moved = e_[src];
  // Same name as i: same sorted place.
  memcpy(e_[i].name, was.name, sizeof(was.name));
  e_[i].offset = moved.offset;
  e_[i].frames = moved.frames;
  e_[i].rate = moved.rate;
  e_[i].root = moved.root;
  e_[i].loop = moved.loop;
  for (int k = src; k + 1 < n_; ++k) e_[k] = e_[k + 1];
  --n_;
  if (saveTable()) return true;
  for (int k = n_; k > src; --k) e_[k] = e_[k - 1];  // not stored: back as it was
  e_[src] = moved;
  ++n_;
  e_[i] = was;
  return false;
}

bool SampleBank::copy(uint32_t to, uint32_t from, uint32_t len, uint8_t* buf, uint32_t& done,
                      uint32_t total, BankProgress cb, void* ctx) {
  for (uint32_t o = 0; o < len; o += kBankAlign) {
    if (!f_.read(from + o, buf, kBankAlign) || !f_.erase(to + o, kBankAlign) || !f_.write(to + o, buf, kBankAlign))
      return false;
    done += kBankAlign;
    if (cb) cb(done < total ? done : total, total, ctx);
  }
  return true;
}

bool SampleBank::compact(BankProgress cb, void* ctx) {
  if (adding_) return false;
  int idx[kBankEntries];
  byOffset(idx);
  // Progress estimate: an overlapping move costs two copies.
  uint32_t total = 0, done = 0;
  uint32_t dest = kBankHeader;
  for (int k = 0; k < n_; ++k) {
    const BankEntry& e = e_[idx[k]];
    const uint32_t len = span(e.frames);
    if (e.offset != dest) total += dest + len > e.offset ? 2 * len : len;
    dest += len;
  }
  if (total == 0) return true;
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kBankAlign]);
  if (!buf) return false;
  dest = kBankHeader;
  for (int k = 0; k < n_; ++k) {
    BankEntry& e = e_[idx[k]];
    const uint32_t len = span(e.frames);
    if (e.offset != dest) {
      // dest < offset. The table may only point at fully written data, and the old copy must stay
      // intact until the table moves away from it: an overlapping move goes via a free area above.
      if (dest + len > e.offset) {
        uint32_t tmp;
        if (!firstFit(e.offset + len, len, tmp)) {
          dest = e.offset + len;  // no room to move it safely: leave it here
          continue;
        }
        if (!copy(tmp, e.offset, len, buf.get(), done, total, cb, ctx)) return false;
        const uint32_t was = e.offset;
        e.offset = tmp;
        if (!saveTable()) {
          e.offset = was;
          return false;
        }
      }
      if (!copy(dest, e.offset, len, buf.get(), done, total, cb, ctx)) return false;
      const uint32_t was = e.offset;
      e.offset = dest;
      if (!saveTable()) {
        e.offset = was;
        return false;
      }
    }
    dest += len;
  }
  return true;
}

}  // namespace mt
