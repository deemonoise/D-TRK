#pragma once
#include <stdint.h>

namespace mt {

struct SchedEvent {
  uint64_t t;    // absolute time, us
  uint32_t id;   // pairs a NoteOn with its NoteOff; 0 for other messages
  uint32_t tag;  // owner, for removeIf (the sequencer's step serial)
  uint8_t b[3];
  uint8_t len;
  bool cont;     // NoteOn that continues a held note: silent if the note still owns its voice
  uint16_t seq;  // push order, set by EventHeap (wraps; fits the padding)
  bool isNoteOff() const { return (b[0] & 0xF0) == 0x80; }
  // Order at the same time: NoteOff, then other messages (CC, program, bend), then NoteOn.
  uint8_t rank() const {
    const uint8_t k = b[0] & 0xF0;
    return k == 0x80 ? 0 : (k == 0x90 ? 2 : 1);
  }
};

static_assert(sizeof(SchedEvent) == 24, "seq must fit the padding");

// Fixed-capacity min-heap by time. At the same time NoteOffs come first and
// NoteOns last (a control sent with a note reaches the synth before it), and
// equal ranks keep push order. NoteOffs keep a reserved share of the capacity
// so a flood of NoteOns can never strand a note.
class EventHeap {
 public:
  static constexpr int kCap = 1024;
  static constexpr int kOffReserve = 256;

  bool push(const SchedEvent& e);
  void pop();
  const SchedEvent& top() const { return buf_[0]; }
  bool empty() const { return n_ == 0; }
  int size() const { return n_; }
  void clear() { n_ = 0; }
  template <class F>
  void removeIf(F&& f) {
    int w = 0;
    for (int i = 0; i < n_; ++i)
      if (!f(buf_[i])) buf_[w++] = buf_[i];
    n_ = w;
    for (int i = n_ / 2 - 1; i >= 0; --i) siftDown(i);
  }

 private:
  void siftDown(int i);
  static bool before(const SchedEvent& a, const SchedEvent& b) {
    if (a.t != b.t) return a.t < b.t;
    if (a.rank() != b.rank()) return a.rank() < b.rank();
    return static_cast<int16_t>(a.seq - b.seq) < 0;
  }
  SchedEvent buf_[kCap];
  int n_ = 0;
  uint16_t seq_ = 0;
};

}  // namespace mt
