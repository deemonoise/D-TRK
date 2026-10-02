#pragma once
#include <Arduino.h>

namespace hw {

enum class InputType : uint8_t { EncTurn, EncClick, EncLong, PlayPress, ShiftDown, ShiftUp };

struct InputEvent {
  InputType type;
  int8_t delta;  // detents for EncTurn
  bool shift;    // Shift held when the event happened
};

constexpr uint32_t kLongPressMs = 500;

void inputBegin();
bool inputPoll(InputEvent& ev, TickType_t wait);

}  // namespace hw
