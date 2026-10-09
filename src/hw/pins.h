#pragma once
#include <stdint.h>

namespace pins {
constexpr int kMidiTx = 10;
constexpr int kEncA = 11;
constexpr int kEncB = 12;
// GPIO 38-41 (the old microSD SPI) are free: the card is on the synth board.
// LCD backlight: always fully on (no PWM, see lgfx_config.h). Strapping pin, set only after boot.
constexpr int kLcdBacklight = 45;
// PCF8575 expander (track buttons + LEDs) on I2C port 0; the touch panel owns port 1.
// Debug header UART0 pins: free because the console runs over native USB CDC.
// GPIO 1/2/42 are wired to the onboard RS485 transceiver, not to a header.
constexpr int kXSda = 43;
constexpr int kXScl = 44;
// Expander pin per track (0-7 = P00-P07, 8-15 = P10-P17, 0xFF = not wired).
// Default: buttons on P00-P07, LED cathodes on P10-P17. A button may move to a pin
// whose LED is not fitted; set that LED to 0xFF.
// This unit: P00 and P10 read low, button 1 lives on P12; no LEDs fitted.
constexpr uint8_t kTrackBtnBit[8] = {10, 1, 2, 3, 4, 5, 6, 7};
constexpr uint8_t kTrackLedBit[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// Encoder button, Play and Shift on the expander (to GND), freeing GPIO 13/14/21 for the link.
constexpr uint8_t kEncSwBit = 11;  // P13
constexpr uint8_t kPlayBit = 12;   // P14
constexpr uint8_t kShiftBit = 13;  // P15
// Buttons A and B (under Shift / Play), not used by the firmware yet.
constexpr uint8_t kBtnABit = 14;   // P16
constexpr uint8_t kBtnBBit = 15;   // P17
// Link to the synth board (Teensy 4.1) on the extended IO header: UART TX -> Teensy RX1 (pin 0),
// RX <- Teensy TX1 (pin 1); the spare goes to Teensy pin 2 (unused in v1). The onboard NS4168
// (GPIO 35/36/37, SPK connector) is not used.
constexpr int kLinkTx = 13;
constexpr int kLinkRx = 14;
constexpr int kLinkSpare = 21;
}  // namespace pins
