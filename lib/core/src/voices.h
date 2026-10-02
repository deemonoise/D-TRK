#pragma once
#include <stdint.h>
#include <string.h>

namespace mt {

// Which note is sounding on which channel, tagged with the id of its NoteOn.
// A NoteOff only goes out if its id still owns the note.
class Voices {
 public:
  // Returns true if the note was already sounding: send a NoteOff before this NoteOn.
  bool noteOn(uint8_t ch, uint8_t note, uint32_t id) {
    const bool was = id_[ch & 15][note & 127] != 0;
    id_[ch & 15][note & 127] = id;
    return was;
  }
  // Returns true if this NoteOff must be sent.
  bool noteOff(uint8_t ch, uint8_t note, uint32_t id) {
    uint32_t& cur = id_[ch & 15][note & 127];
    if (cur == 0 || cur != id) return false;
    cur = 0;
    return true;
  }
  bool active(uint8_t ch, uint8_t note) const { return id_[ch & 15][note & 127] != 0; }
  template <class F>
  void releaseAll(F&& f) {
    for (uint8_t ch = 0; ch < 16; ++ch)
      for (uint8_t n = 0; n < 128; ++n)
        if (id_[ch][n]) {
          id_[ch][n] = 0;
          f(ch, n);
        }
  }
  void clear() { memset(id_, 0, sizeof(id_)); }

 private:
  uint32_t id_[16][128] = {};
};

}  // namespace mt
