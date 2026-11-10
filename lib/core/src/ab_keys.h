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
  PageTurn,      // A + B + turn, or B + Shift + turn (delta)
  Undo,          // Shift + B tap
  Back,          // B tap
  Solo,          // Shift + A + track button (delta = button)
  QueuePattern,  // B + Shift + track button (delta = button)
  ATrack,        // A + track button, no Shift (delta = button; MIX: scene N on crossfader end A)
  BTrack,        // B + track button, no Shift (delta = button; MIX: scene N on crossfader end B)
  Both,          // A and B pressed together and released, nothing else meanwhile (on the first release)
  RecordAll,     // A + Play: notes and motion
  RecordMotion,  // B + Play: motion only
  AClick,        // A held alone + encoder click (aClick: only where the screen asks for it)
  AThenB,        // A then B, released without another gesture: GRID clear, otherwise Both
};

struct AbOut {
  AbAction act;
  int8_t delta;
  bool shift;
};

// Chords of buttons A and B with the encoder and the track buttons. A tap counts on release,
// only if nothing else happened while the button was held. With both held a turn changes pages; track buttons use B. A and B
// together, released with nothing else: Both.
class AbKeys {
 public:
  AbOut aDown() {
    a_ = true;
    aUsed_ = editing_ = false;
    if (b_) {  // a B + A chord: neither taps; alone (B not used yet) it is Both
      aFirst_ = false;
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
      return out(aFirst_ ? AbAction::AThenB : AbAction::Both);
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
    const bool alone = !aUsed_;
    if (!editing_) {
      both_ = aFirst_ = alone;  // defer A-first until release: a turn may still follow
    }
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
      return out(aFirst_ ? AbAction::AThenB : AbAction::Both);
    }
    if (bUsed_) return out(AbAction::None);
    return out(shift ? AbAction::Undo : AbAction::Back);
  }
  AbOut turn(int delta, bool shift) {
    both_ = false;
    if (b_) {
      bUsed_ = true;
      if (a_) aUsed_ = true;
      return out((a_ || shift) ? AbAction::PageTurn : AbAction::TabTurn, delta);
    }
    if (a_) {
      aUsed_ = editing_ = true;
      return out(AbAction::EditTurn, delta, shift);
    }
    return out(AbAction::Pass);
  }
  // Play chords consume the modifier taps; an ongoing A edit still ends on release.
  AbOut play() {
    if (!a_ && !b_) return out(AbAction::Pass);
    both_ = false;
    if (a_) aUsed_ = true;
    if (b_) bUsed_ = true;
    return out(b_ ? AbAction::RecordMotion : AbAction::RecordAll);
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
  void reset() { a_ = b_ = aUsed_ = bUsed_ = editing_ = both_ = aFirst_ = false; }

 private:
  static AbOut out(AbAction a, int d = 0, bool s = false) { return {a, static_cast<int8_t>(d), s}; }
  bool a_ = false, b_ = false, aUsed_ = false, bUsed_ = false, editing_ = false, both_ = false, aFirst_ = false;
};

}  // namespace mt
