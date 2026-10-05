#pragma once
#include <stdint.h>

namespace pins {
constexpr int kMidiTx = 10;
constexpr int kEncA = 11;
constexpr int kEncB = 12;
constexpr int kEncSw = 13;
constexpr int kPlay = 14;
constexpr int kShift = 21;
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
// Onboard NS4168 class-D amp (SPK connector), I2S per the Wireless-Tag datasheet.
// Bridge output: neither SPK pin is ground.
constexpr int kI2sBclk = 36;
constexpr int kI2sWs = 35;
constexpr int kI2sDout = 37;
}  // namespace pins
