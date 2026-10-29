#include "midi_devices.h"
#include <string.h>
#include <strings.h>

namespace mt {

namespace {

constexpr uint8_t kBlobVersion = 2;
constexpr int kAddrLen = kDevAddr - 1, kNameLen = kDevName - 1;
constexpr int kModes = static_cast<int>(PadMode::Count);

uint8_t bits(DevRole r) { return static_cast<uint8_t>(r); }
bool validRole(uint8_t r) { return r >= bits(DevRole::In) && r <= bits(DevRole::Both); }

// Copies at most max chars and always terminates (dst holds max + 1); null src = "".
void copyStr(char* dst, const char* src, int max) {
  int k = 0;
  if (src)
    for (; k < max && src[k]; ++k) dst[k] = src[k];
  dst[k] = 0;
}

// A fixed-width field: the string padded with zeros (kept NUL-terminated on load by copyStr).
void putStr(uint8_t*& o, const char* s, int len) {
  const int k = static_cast<int>(strnlen(s, len));
  memcpy(o, s, k);
  memset(o + k, 0, len - k);
  o += len;
}
void getStr(const uint8_t*& in, char* dst, int len) {
  char tmp[kDevName > kDevAddr ? kDevName : kDevAddr] = {};
  memcpy(tmp, in, len);
  copyStr(dst, tmp, len);
  in += len;
}

}  // namespace

int MidiDevices::find(const char* addr) const {
  if (!addr || !addr[0]) return -1;
  for (int i = 0; i < n_; ++i)
    if (strncasecmp(dev_[i].addr, addr, kAddrLen) == 0) return i;
  return -1;
}

uint8_t MidiDevices::enforce(int i) {
  uint8_t mask = 0;
  if (!dev_[i].on) return 0;
  for (int j = 0; j < n_; ++j) {
    if (j == i || !dev_[j].on || !(bits(dev_[j].role) & bits(dev_[i].role))) continue;
    dev_[j].on = false;
    mask |= static_cast<uint8_t>(1u << j);
  }
  return mask;
}

int MidiDevices::add(const char* addr, uint8_t type, const char* name, DevRole role, uint8_t& offMask) {
  offMask = 0;
  if (n_ >= kDevMax || !addr || !addr[0] || !validRole(bits(role)) || find(addr) >= 0) return -1;
  MidiDevice& d = dev_[n_];
  copyStr(d.addr, addr, kAddrLen);
  d.addrType = type;
  copyStr(d.name, name, kNameLen);
  d.role = role;
  d.on = true;
  d.pads.clear();
  d.kitMidiBase = 0xFF;
  const int i = n_++;
  offMask = enforce(i);
  return i;
}

void MidiDevices::remove(int i) {
  if (i < 0 || i >= n_) return;
  for (int j = i; j + 1 < n_; ++j) dev_[j] = dev_[j + 1];
  --n_;
}

void MidiDevices::setOn(int i, bool on, uint8_t& offMask) {
  offMask = 0;
  if (i < 0 || i >= n_) return;
  dev_[i].on = on;
  offMask = enforce(i);
}

void MidiDevices::setRole(int i, DevRole role, uint8_t& offMask) {
  offMask = 0;
  if (i < 0 || i >= n_ || !validRole(bits(role))) return;
  dev_[i].role = role;
  offMask = enforce(i);
}

int MidiDevices::activeIn() const {
  for (int i = 0; i < n_; ++i)
    if (dev_[i].on && (bits(dev_[i].role) & bits(DevRole::In))) return i;
  return -1;
}

int MidiDevices::activeOut() const {
  for (int i = 0; i < n_; ++i)
    if (dev_[i].on && (bits(dev_[i].role) & bits(DevRole::Out))) return i;
  return -1;
}

int MidiDevices::save(uint8_t* out, int cap) const {
  const int size = kBlobHead + n_ * kDevRec;
  if (!out || cap < size) return 0;
  uint8_t* o = out;
  *o++ = kBlobVersion;
  *o++ = static_cast<uint8_t>(n_);
  for (int i = 0; i < n_; ++i) {
    const MidiDevice& d = dev_[i];
    putStr(o, d.addr, kAddrLen);
    *o++ = d.addrType;
    putStr(o, d.name, kNameLen);
    *o++ = bits(d.role);
    *o++ = d.on ? 1 : 0;
    for (int m = 0; m < kModes; ++m) *o++ = d.pads.ch[m];
    for (int m = 0; m < kModes; ++m) *o++ = d.pads.base[m];
    *o++ = d.kitMidiBase;
  }
  return size;
}

bool MidiDevices::load(const uint8_t* in, int n) {
  n_ = 0;
  if (!in || n < kBlobHead || (in[0] != 1 && in[0] != kBlobVersion) || in[1] > kDevMax) return false;
  const int count = in[1];
  const int rec = in[0] == 1 ? kDevRec - 1 : kDevRec;
  const bool hasKitBase = in[0] >= 2;
  if (n != kBlobHead + count * rec) return false;
  for (int i = 0; i < count; ++i)
    if (!validRole(in[kBlobHead + i * rec + kAddrLen + 1 + kNameLen])) return false;
  // A record with an empty or repeated address is dropped; a pad bank with a bad channel / note is
  // forgotten (the rest of the list stays).
  const uint8_t* p = in + kBlobHead;
  for (int i = 0; i < count; ++i) {
    MidiDevice& d = dev_[n_];
    getStr(p, d.addr, kAddrLen);
    d.addrType = *p++;
    getStr(p, d.name, kNameLen);
    d.role = static_cast<DevRole>(*p++);
    d.on = *p++ != 0;
    for (int m = 0; m < kModes; ++m) d.pads.ch[m] = *p++;
    for (int m = 0; m < kModes; ++m) d.pads.base[m] = *p++;
    d.kitMidiBase = hasKitBase ? *p++ : 0xFF;
    if (d.kitMidiBase > 128 - kKitLanes) d.kitMidiBase = 0xFF;
    for (int m = 0; m < kModes; ++m)
      if (d.pads.ch[m] > 15 || (d.pads.base[m] > 127 && d.pads.base[m] != 0xFF)) {
        d.pads.ch[m] = 0;
        d.pads.base[m] = 0xFF;
      }
    if (d.addr[0] && find(d.addr) < 0) ++n_;
  }
  // A device clashing with an earlier on device goes off: walk in order, an on device only turns off
  // later ones (earlier clashing ones are already off).
  for (int i = 0; i < n_; ++i) {
    if (!dev_[i].on) continue;
    for (int j = i + 1; j < n_; ++j)
      if (dev_[j].on && (bits(dev_[j].role) & bits(dev_[i].role))) dev_[j].on = false;
  }
  return true;
}

void MidiDevices::migrate(const char* inAddr, uint8_t inType, const char* outAddr, uint8_t outType,
                          const PadLearn& pads) {
  if (n_ != 0) return;
  const bool hasIn = inAddr && inAddr[0], hasOut = outAddr && outAddr[0];
  uint8_t off = 0;
  if (hasIn && hasOut && strncasecmp(inAddr, outAddr, kAddrLen) == 0) {
    if (add(inAddr, inType, inAddr, DevRole::Both, off) >= 0) dev_[0].pads = pads;
    return;
  }
  if (hasIn) {
    const int i = add(inAddr, inType, inAddr, DevRole::In, off);
    if (i >= 0) dev_[i].pads = pads;
  }
  if (hasOut) add(outAddr, outType, outAddr, DevRole::Out, off);
}

}  // namespace mt
