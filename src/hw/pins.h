#pragma once

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
}  // namespace pins
