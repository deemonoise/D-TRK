#pragma once
#include <Arduino.h>

namespace hw {

// EditTurn / EditEnd / EditCancel are made by ui::App from A chords (delta, shift as in EncTurn);
// drivers never queue them.
enum class InputType : uint8_t {
  EncTurn, EncClick, EncLong, PlayPress, ShiftDown, ShiftUp, TrackPress, PlayRelease, TrackRelease,
  ADown, AUp, BDown, BUp, EditTurn, EditEnd, EditCancel
};

struct InputEvent {
  InputType type;
  int8_t delta;  // detents for EncTurn, track 0..7 for TrackPress / TrackRelease
  bool shift;    // Shift held when the event happened
};

constexpr uint32_t kLongPressMs = 500;

void inputBegin();
bool inputPoll(InputEvent& ev, TickType_t wait);
// For other input drivers: queues an event with the current Shift state.
void inputPush(InputType t, int8_t delta);

}  // namespace hw
