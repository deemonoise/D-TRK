#pragma once
#include <stdint.h>

namespace pins {
constexpr int kMidiTx = 10;
constexpr int kEncA = 11;
constexpr int kEncB = 12;
// microSD on its own SPI host (the LCD uses the 8080 bus).
constexpr int kSdCs = 41;
constexpr int kSdMosi = 40;
constexpr int kSdClk = 39;
constexpr int kSdMiso = 38;
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
// Encoder button, Play and Shift on the expander (to GND), freeing GPIO 13/14/21 for the DAC.
constexpr uint8_t kEncSwBit = 11;  // P13
constexpr uint8_t kPlayBit = 12;   // P14
constexpr uint8_t kShiftBit = 13;  // P15
// Buttons A and B (under Shift / Play), not used by the firmware yet.
constexpr uint8_t kBtnABit = 14;   // P16
constexpr uint8_t kBtnBBit = 15;   // P17
// External PCM5102A DAC (SCK to GND: clock from BCK by its PLL) on the extended IO header.
// The onboard NS4168 (GPIO 35/36/37, SPK connector) is not used.
constexpr int kI2sBclk = 13;
constexpr int kI2sWs = 14;
constexpr int kI2sDout = 21;
}  // namespace pins
