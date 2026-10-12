#pragma once
#include <stdint.h>

namespace mt {

enum class AbAction : uint8_t {
  None,          // consumed, nothing to do
  Pass,          // not a chord: handle the event as usual
  EditTurn,      // A + turn: edit the value under the cursor (delta, shift)
  EditEnd,       // A released after editing: commit
  EditCancel,    // B pressed while A edits: restore the value
  ATap,          // A pressed and released alone (shift)
  TabTurn,       // B + turn (delta)
  PageTurn,      // B + Shift + turn (delta)
  Undo,          // Shift + B tap
  Back,          // B tap
  Solo,          // A + track button (delta = button)
  QueuePattern,  // B + track button (delta = button, shift)
};

struct AbOut {
  AbAction act;
  int8_t delta;
  bool shift;
};

// Chords of buttons A and B with the encoder and the track buttons. A tap counts on release,
// only if nothing else happened while the button was held. With both held B wins.
class AbKeys {
 public:
  AbOut aDown() {
    a_ = true;
    aUsed_ = editing_ = false;
    if (b_) aUsed_ = bUsed_ = true;  // a B + A chord: neither taps
    return out(AbAction::None);
  }
  AbOut aUp(bool shift) {
    if (!a_) return out(AbAction::None);  // its press was dropped (reset)
    a_ = false;
    if (editing_) {
      editing_ = false;
      return out(AbAction::EditEnd);
    }
    return out(aUsed_ ? AbAction::None : AbAction::ATap, 0, shift);
  }
  AbOut bDown() {
    b_ = true;
    bUsed_ = false;
    if (!a_) return out(AbAction::None);
    aUsed_ = bUsed_ = true;
    if (!editing_) return out(AbAction::None);
    editing_ = false;
    return out(AbAction::EditCancel);
  }
  AbOut bUp(bool shift) {
    if (!b_) return out(AbAction::None);  // its press was dropped (reset)
    b_ = false;
    if (bUsed_) return out(AbAction::None);
    return out(shift ? AbAction::Undo : AbAction::Back);
  }
  AbOut turn(int delta, bool shift) {
    if (b_) {
      bUsed_ = true;
      if (a_) aUsed_ = true;
      return out(shift ? AbAction::PageTurn : AbAction::TabTurn, delta);
    }
    if (a_) {
      aUsed_ = editing_ = true;
      return out(AbAction::EditTurn, delta, shift);
    }
    return out(AbAction::Pass);
  }
  AbOut track(int n, bool shift) {
    if (b_) {
      bUsed_ = true;
      if (a_) aUsed_ = true;
      return out(AbAction::QueuePattern, n, shift);
    }
    if (a_) {
      aUsed_ = true;
      return out(AbAction::Solo, n);
    }
    return out(AbAction::Pass);
  }
  bool aHeld() const { return a_; }
  bool bHeld() const { return b_; }
  // Input was dropped (long operation): forget held buttons.
  void reset() { a_ = b_ = aUsed_ = bUsed_ = editing_ = false; }

 private:
  static AbOut out(AbAction a, int d = 0, bool s = false) { return {a, static_cast<int8_t>(d), s}; }
  bool a_ = false, b_ = false, aUsed_ = false, bUsed_ = false, editing_ = false;
};

}  // namespace mt
