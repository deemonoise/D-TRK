#pragma once
#include <stdint.h>

namespace hw {

// 8 track buttons (to GND) and 8 LEDs (sink, active low) on a PCF8575; pins in pins.h.
// Presses arrive as InputType::TrackPress. Also carries the encoder button, Play and Shift
// (expanderDown). Reads the port once before returning. False when the expander does not answer.
bool trackioBegin();
// Raw (not debounced) state of an expander pin from the last poll: true = pulled to GND.
// False for an unwired bit or without the expander.
bool expanderDown(uint8_t bit);
// Bit = LED on. Written by the expander task when it changes.
void trackLeds(uint8_t mask);

}  // namespace hw
