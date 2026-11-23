#pragma once
#include <Arduino.h>

namespace hw {

// EditTurn / EditEnd / EditCancel are made by ui::App from A chords (delta, shift as in EncTurn);
// drivers never queue them.
enum class InputType : uint8_t {
  EncTurn, EncClick, EncLong, PlayPress, ShiftDown, ShiftUp, TrackPress, PlayRelease, TrackRelease,
  ADown, AUp, BDown, BUp, EditTurn, EditEnd, EditCancel
};

#ifdef DTRK_BRICK
enum class InputAxis : uint8_t { Encoder, Horizontal, Vertical };
#endif

struct InputEvent {
  InputType type;
  int8_t delta;  // detents for EncTurn, track 0..7 for TrackPress / TrackRelease
  bool shift;    // Shift held when the event happened
#ifdef DTRK_BRICK
  InputAxis axis = InputAxis::Encoder;
  bool physicalShift = false;  // The Select button, independent of the encoder navigation mapping.
#endif
};

// A physical D-pad moves down through lists but up increases an edited value.
inline int editDirection(const InputEvent& ev) {
#ifdef DTRK_BRICK
  if (ev.axis == InputAxis::Vertical && ev.type == InputType::EncTurn) return -ev.delta;
#endif
  return ev.delta;
}

inline bool coarseEdit(const InputEvent& ev) {
#ifdef DTRK_BRICK
  if (ev.axis != InputAxis::Encoder) return ev.axis == InputAxis::Vertical;
#endif
  return ev.shift;
}

constexpr uint32_t kLongPressMs = 500;

void inputBegin();
bool inputPoll(InputEvent& ev, TickType_t wait);
// For other input drivers: queues an event with the current Shift state.
void inputPush(InputType t, int8_t delta);

}  // namespace hw
