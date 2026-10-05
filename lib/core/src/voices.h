#pragma once
#include <stdint.h>
#include <string.h>

namespace mt {

// Which note is sounding on which channel, tagged with the id of its NoteOn.
// A NoteOff only goes out if its id still owns the note. kCh channels (a power of two; the
// channel is taken modulo kCh).
template <int kCh>
class VoicesN {
  static_assert(kCh > 0 && kCh <= 16 && (kCh & (kCh - 1)) == 0, "power of two, up to 16");
  static constexpr uint8_t kMask = kCh - 1;

 public:
  // Returns true if the note was already sounding: send a NoteOff before this NoteOn.
  bool noteOn(uint8_t ch, uint8_t note, uint32_t id) {
    const bool was = id_[ch & kMask][note & 127] != 0;
    id_[ch & kMask][note & 127] = id;
    return was;
  }
  // Returns true if this NoteOff must be sent.
  bool noteOff(uint8_t ch, uint8_t note, uint32_t id) {
    uint32_t& cur = id_[ch & kMask][note & 127];
    if (cur == 0 || cur != id) return false;
    cur = 0;
    return true;
  }
  bool active(uint8_t ch, uint8_t note) const { return id_[ch & kMask][note & 127] != 0; }
  template <class F>
  void releaseAll(F&& f) {
    for (uint8_t ch = 0; ch < kCh; ++ch)
      for (uint8_t n = 0; n < 128; ++n)
        if (id_[ch][n]) {
          id_[ch][n] = 0;
          f(ch, n);
        }
  }
  template <class F>
  void releaseChannel(uint8_t ch, F&& f) {
    for (uint8_t n = 0; n < 128; ++n)
      if (id_[ch & kMask][n]) {
        id_[ch & kMask][n] = 0;
        f(n);
      }
  }
  void clear() { memset(id_, 0, sizeof(id_)); }

 private:
  uint32_t id_[kCh][128] = {};
};

using Voices = VoicesN<16>;

}  // namespace mt
