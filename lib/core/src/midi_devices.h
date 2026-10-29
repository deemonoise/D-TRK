#pragma once
#include <stdint.h>
#include "midi_router.h"

namespace mt {

constexpr int kDevMax = 6;    // remembered BLE MIDI devices
constexpr int kDevName = 24;  // name buffer, NUL included
constexpr int kDevAddr = 18;  // "aa:bb:cc:dd:ee:ff" + NUL

// Which way a device is used. Bits: In = 1, Out = 2; Both = In | Out.
enum class DevRole : uint8_t { In = 1, Out = 2, Both = 3 };

struct MidiDevice {
  char addr[kDevAddr];
  uint8_t addrType;  // BLE address type, kept as given
  char name[kDevName];
  DevRole role;
  bool on;
  PadLearn pads;  // the controller's learned banks (meaningful for an In role)
};

// Pure model of the remembered BLE MIDI devices (no radio, no NVS). One-IN / one-OUT rule: at most
// one on device holds the In bit and at most one holds the Out bit (Both holds both). Switching a
// device on (add, setOn, setRole of an on device) turns off every other on device sharing a bit;
// `offMask` returns those devices as bits (1 << index, indices after the call), 0 = none. Up to two
// can go off: a Both device turned on against a separate In device and a separate Out device.
class MidiDevices {
 public:
  int count() const { return n_; }
  const MidiDevice& at(int i) const { return dev_[i]; }
  MidiDevice& at(int i) { return dev_[i]; }
  int find(const char* addr) const;  // index, -1 if not there; case-insensitive

  // Appends an on device with pads cleared; the address is cut to 17 chars and the name to
  // kDevName - 1. -1 when the list is full, the role is not In / Out / Both, the address is null /
  // empty or already there (any case) (offMask 0).
  int add(const char* addr, uint8_t type, const char* name, DevRole role, uint8_t& offMask);
  void remove(int i);  // later entries shift down; out of range: no-op
  void clear() { n_ = 0; }
  void setOn(int i, bool on, uint8_t& offMask);
  // A new role; when the device is on, the rule is applied with the new role.
  void setRole(int i, DevRole role, uint8_t& offMask);
  int activeIn() const;   // the on device with the In bit, -1 = none
  int activeOut() const;  // the on device with the Out bit, -1 = none

  // Blob (stable byte layout, no struct copies): version 1, count, then count records of kDevRec
  // bytes: addr 17, addrType 1, name 23, role 1, on 1, pads ch[4] base[4]. Returns the bytes
  // written, 0 when cap is too small.
  static constexpr int kDevRec = 17 + 1 + 23 + 1 + 1 + 8;
  static constexpr int kBlobHead = 2;
  static constexpr int kBlobMax = kBlobHead + kDevMax * kDevRec;
  int save(uint8_t* out, int cap) const;
  // False on a bad size / version / count / role: the list is left empty. The one-IN / one-OUT rule
  // is applied again in list order (a later clashing on device is turned off). Records with an empty
  // or repeated address are dropped; a pad bank with ch > 15 or base > 127 (not 0xFF) is cleared.
  bool load(const uint8_t* in, int n);

  // From the old two-slot keys (empty string or null = slot not set); only on an empty list. in only
  // -> In device; out only -> Out device; in == out -> one Both device; else In + Out devices. All on,
  // named by their address; `pads` go to the device with the In bit, the Out-only device gets none.
  void migrate(const char* inAddr, uint8_t inType, const char* outAddr, uint8_t outType, const PadLearn& pads);

 private:
  uint8_t enforce(int i);  // turns off the other on devices clashing with on device i; their bits
  MidiDevice dev_[kDevMax];
  int n_ = 0;
};

}  // namespace mt
