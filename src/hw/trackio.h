#pragma once
#include <stdint.h>

namespace hw {

// 8 track buttons (to GND) and 8 LEDs (sink, active low) on a PCF8575; pins in pins.h.
// Presses arrive as InputType::TrackPress. False when the expander does not answer:
// everything else works without it.
bool trackioBegin();
// Bit = LED on. Written by the expander task when it changes.
void trackLeds(uint8_t mask);

}  // namespace hw
