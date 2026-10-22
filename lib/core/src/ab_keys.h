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
  Solo,          // Shift + A + track button (delta = button)
  QueuePattern,  // B + Shift + track button (delta = button)
  ATrack,        // A + track button, no Shift (delta = button; MIX: scene N on crossfader end A)
  BTrack,        // B + track button, no Shift (delta = button; MIX: scene N on crossfader end B)
  Both,          // A and B pressed together and released, nothing else meanwhile (on the first release)
  AClick,        // A held alone + encoder click (aClick: only where the screen asks for it)
};

struct AbOut {
  AbAction act;
  int8_t delta;
  bool shift;
};

// Chords of buttons A and B with the encoder and the track buttons. A tap counts on release,
// only if nothing else happened while the button was held. With both held B wins. A and B
// together, released with nothing else: Both.
class AbKeys {
 public:
  AbOut aDown() {
    a_ = true;
    aUsed_ = editing_ = false;
    if (b_) {  // a B + A chord: neither taps; alone (B not used yet) it is Both
      both_ = !bUsed_;
      aUsed_ = bUsed_ = true;
    }
    return out(AbAction::None);
  }
  AbOut aUp(bool shift) {
    if (!a_) return out(AbAction::None);  // its press was dropped (reset)
    a_ = false;
    if (both_) {
      both_ = false;
      return out(AbAction::Both);
    }
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
    if (!editing_) both_ = !aUsed_;  // alone (A not used yet): Both
    aUsed_ = bUsed_ = true;
    if (!editing_) return out(AbAction::None);
    editing_ = false;
    return out(AbAction::EditCancel);
  }
  AbOut bUp(bool shift) {
    if (!b_) return out(AbAction::None);  // its press was dropped (reset)
    b_ = false;
    if (both_) {
      both_ = false;
      return out(AbAction::Both);
    }
    if (bUsed_) return out(AbAction::None);
    return out(shift ? AbAction::Undo : AbAction::Back);
  }
  AbOut turn(int delta, bool shift) {
    both_ = false;
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
    both_ = false;
    if (b_) {
      bUsed_ = true;
      if (a_) aUsed_ = true;
      return out(shift ? AbAction::QueuePattern : AbAction::BTrack, n);
    }
    if (a_) {
      aUsed_ = true;
      return out(shift ? AbAction::Solo : AbAction::ATrack, n);
    }
    return out(AbAction::Pass);
  }
  // An encoder click while A is held alone: AClick, A counts as used (no tap, no Both); else Pass.
  // The app asks only where A + click means something (MIX SCENE view); elsewhere the click passes.
  AbOut aClick() {
    if (!a_ || b_) return out(AbAction::Pass);
    both_ = false;
    aUsed_ = true;
    return out(AbAction::AClick);
  }
  bool aHeld() const { return a_; }
  bool bHeld() const { return b_; }
  // Input was dropped (long operation): forget held buttons.
  void reset() { a_ = b_ = aUsed_ = bUsed_ = editing_ = both_ = false; }

 private:
  static AbOut out(AbAction a, int d = 0, bool s = false) { return {a, static_cast<int8_t>(d), s}; }
  bool a_ = false, b_ = false, aUsed_ = false, bUsed_ = false, editing_ = false, both_ = false;
};

}  // namespace mt
