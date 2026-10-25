#include "rec_ui.h"
#include <math.h>
#include <stdio.h>

namespace mt {

const char* recResultText(link::RecResult r) {
  using link::RecResult;
  switch (r) {
    case RecResult::Ok: return "OK";
    case RecResult::NoSd: return "NO SD";
    case RecResult::Busy: return "BUSY";
    case RecResult::NoCodec: return "NO SHIELD";
    case RecResult::NoPsram: return "NO PSRAM";
    case RecResult::NotRec: return "NOT RECORDING";
    case RecResult::TooShort: return "TOO SHORT";
    case RecResult::CardSlow: return "CARD TOO SLOW";
    case RecResult::WriteFail: return "WRITE FAIL";
    case RecResult::Bank: return "BANK FAIL";
    case RecResult::BadArg: return "BAD REQUEST";
  }
  return "REC FAILED";
}

RecSource recSourceStep(RecSource s, int d, bool codec) {
  if (!codec) return RecSource::Resample;
  int v = static_cast<int>(s) + d;
  const int hi = static_cast<int>(RecSource::Resample);
  return static_cast<RecSource>(v < 0 ? 0 : (v > hi ? hi : v));
}

int8_t recThreshStep(int8_t db, int d) {
  // Index 0 = OFF, 1.. = -60, -58, .. -6.
  const int last = (kRecThreshMax - kRecThreshMin) / 2 + 1;
  int i = 0;
  if (db != kRecThreshOff) {
    const int v = db < kRecThreshMin ? kRecThreshMin : (db > kRecThreshMax ? kRecThreshMax : db);
    i = (v - kRecThreshMin + 1) / 2 + 1;
  }
  i += d;
  i = i < 0 ? 0 : (i > last ? last : i);
  return i == 0 ? kRecThreshOff : static_cast<int8_t>(kRecThreshMin + (i - 1) * 2);
}

int recGainDefault(RecSource s) {
  return s == RecSource::Mic ? 32 : (s == RecSource::Line ? 5 : 0);
}

void recFmtTime(uint32_t frames, char* out, int n) {
  const uint32_t t = static_cast<uint32_t>(static_cast<uint64_t>(frames) * 10 / kRecRate);
  snprintf(out, n, "%02lu:%02lu.%lu", static_cast<unsigned long>(t / 600), static_cast<unsigned long>(t / 10 % 60),
           static_cast<unsigned long>(t % 10));
}

static float peakDbF(uint16_t peak) { return 20.f * log10f(peak / 32767.f); }

int recPeakDb(uint16_t peak) { return peak ? static_cast<int>(lroundf(peakDbF(peak))) : -96; }

int recMeterPx(uint16_t peak, int width) {
  if (!peak) return 0;
  const float db = peakDbF(peak);
  if (db <= -60.f) return 0;
  if (db >= 0.f) return width;
  return static_cast<int>(lroundf((db + 60.f) / 60.f * width));
}

void RecPeakHold::feed(uint16_t peak, uint32_t nowMs) {
  last = peak;
  if (peak >= value(nowMs)) {
    held = peak;
    at = nowMs;
  }
}

uint16_t RecPeakHold::value(uint32_t nowMs) const { return nowMs - at < kRecHoldMs ? held : last; }

void recToast(int number, uint32_t frames, uint32_t clips, bool cardSlow, char* out, int n) {
  const uint32_t t = static_cast<uint32_t>(static_cast<uint64_t>(frames) * 10 / kRecRate);
  int k = snprintf(out, n, "REC%d %lu.%lus", number, static_cast<unsigned long>(t / 10), static_cast<unsigned long>(t % 10));
  if (clips && k < n)
    k += snprintf(out + k, n - k, ", %lu CLIP%s", static_cast<unsigned long>(clips), clips == 1 ? "" : "S");
  if (cardSlow && k < n) snprintf(out + k, n - k, ", CARD TOO SLOW");
}

}  // namespace mt
