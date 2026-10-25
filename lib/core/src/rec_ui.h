#pragma once
#include <stdint.h>
#include "link_msg.h"
#include "rec.h"

namespace mt {

// The REC window's logic (src/ui/rec_dialog.*): row steps, the meter, the time and the toasts.

// Short, upper case, for toasts: NO SD, BUSY, NO SHIELD, NO PSRAM, TOO SHORT, CARD TOO SLOW, WRITE FAIL...
const char* recResultText(link::RecResult r);

// SOURCE: MIC / LINE / RESAMPLE by d (clamped); without the codec only RESAMPLE.
RecSource recSourceStep(RecSource s, int d, bool codec);
// THRESH: kRecThreshOff, then -60..-6 dB in steps of 2.
constexpr int8_t kRecThreshMin = -60, kRecThreshMax = -6;
int8_t recThreshStep(int8_t db, int d);
// GAIN default of a source when the window opens on it.
int recGainDefault(RecSource s);

// "00:12.4": mm:ss.tenths of frames at kRecRate (out >= 8).
void recFmtTime(uint32_t frames, char* out, int n);
// Peak as whole dB (rounded), -96 for silence.
int recPeakDb(uint16_t peak);
// The meter bar: -60..0 dB onto 0..width pixels.
int recMeterPx(uint16_t peak, int width);
// At or above -1 dBFS: the meter is red.
constexpr uint16_t kRecPeakRed = 29204;
inline bool recPeakRed(uint16_t peak) { return peak >= kRecPeakRed; }

// The meter's peak held for kRecHoldMs, then the latest poll's.
constexpr uint32_t kRecHoldMs = 1000;
struct RecPeakHold {
  uint16_t held = 0, last = 0;
  uint32_t at = 0;
  void feed(uint16_t peak, uint32_t nowMs);
  uint16_t value(uint32_t nowMs) const;
  void clear() { held = last = 0; }
};

// The toast after a take: "REC1 12.4s, 3 CLIPS" (clips only when > 0), ", CARD TOO SLOW" appended.
void recToast(int number, uint32_t frames, uint32_t clips, bool cardSlow, char* out, int n);

}  // namespace mt
