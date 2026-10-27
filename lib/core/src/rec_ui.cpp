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
    case RecResult::CardFull: return "CARD FULL";
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

void recFmtClock(uint32_t seconds, bool pad, char* out, int n) {
  const unsigned long h = seconds / 3600, m = seconds / 60 % 60, sec = seconds % 60;
  if (h) snprintf(out, n, "%lu:%02lu:%02lu", h, m, sec);
  else snprintf(out, n, pad ? "%02lu:%02lu" : "%lu:%02lu", m, sec);
}

void recSdToast(int number, uint32_t frames, uint32_t clips, link::RecResult r, link::RecEnd ended, char* out,
                int n) {
  using link::RecEnd;
  using link::RecResult;
  if (r != RecResult::Ok && r != RecResult::CardSlow) {
    snprintf(out, n, "%s", recResultText(r));
    return;
  }
  char t[16];
  recFmtClock(frames / kRecRate, false, t, sizeof t);
  const char* how = ended == RecEnd::Space ? "CARD FULL" : (ended == RecEnd::Length ? "4GB LIMIT" : "SAVED");
  int k = snprintf(out, n, "REC%d.WAV %s %s", number, t, how);
  if (clips && k >= 0 && k < n)
    k += snprintf(out + k, n - k, ", %lu CLIP%s", static_cast<unsigned long>(clips), clips == 1 ? "" : "S");
  if (r == RecResult::CardSlow && k >= 0 && k < n) snprintf(out + k, n - k, ", CARD TOO SLOW");
}

RecRect recBtnRect(int screenW, int rowsBottom, int areaBottom, int margin) {
  RecRect r;
  r.x = margin;
  r.y = rowsBottom + margin;
  r.w = screenW - 2 * margin;
  r.h = areaBottom - margin - r.y;
  if (r.w < 0) r.w = 0;
  if (r.h < 0) r.h = 0;
  return r;
}

RecBtn recBtnState(bool taking, bool starting, RecState st, bool thresh, bool waitPlay) {
  if (taking && st == RecState::Saving) return RecBtn::Saving;
  if (starting && !taking) return RecBtn::Starting;
  if (taking) return st == RecState::Armed ? RecBtn::Armed : RecBtn::Stop;
  if (waitPlay) return RecBtn::WaitPlay;
  return thresh ? RecBtn::Arm : RecBtn::Start;
}

RecTap recTap(RecBtn b, RecSource src, bool playing) {
  switch (b) {
    case RecBtn::Start:
    case RecBtn::Arm: return src == RecSource::Resample && !playing ? RecTap::WaitPlay : RecTap::Start;
    case RecBtn::WaitPlay: return RecTap::Unwait;
    case RecBtn::Armed:
    case RecBtn::Stop: return RecTap::Stop;
    default: return RecTap::None;
  }
}

RecCancel recCancelAct(RecBtn b) {
  switch (b) {
    case RecBtn::Start:
    case RecBtn::Arm: return RecCancel::Close;
    case RecBtn::WaitPlay: return RecCancel::Unwait;
    case RecBtn::Armed:
    case RecBtn::Stop:
    case RecBtn::Starting: return RecCancel::Drop;
    default: return RecCancel::None;
  }
}

const char* recCancelText(RecBtn b) {
  switch (b) {
    case RecBtn::Start:
    case RecBtn::Arm: return "CLOSE";
    case RecBtn::WaitPlay: return "CANCEL WAIT";
    case RecBtn::Armed: return "CANCEL ARM";
    case RecBtn::Saving: return "SAVING...";
    default: return "DROP THE TAKE";
  }
}

void recBtnText(RecBtn b, uint32_t frames, char* out, int n) {
  switch (b) {
    case RecBtn::Start: snprintf(out, n, "START"); break;
    case RecBtn::Arm: snprintf(out, n, "ARM"); break;
    case RecBtn::Armed: snprintf(out, n, "ARMED"); break;
    case RecBtn::WaitPlay: snprintf(out, n, "ARMED: PLAY"); break;
    case RecBtn::Stop: {
      char t[16];
      recFmtClock(frames / kRecRate, true, t, sizeof t);
      snprintf(out, n, "STOP %s", t);
      break;
    }
    case RecBtn::Starting: snprintf(out, n, "STARTING"); break;
    case RecBtn::Saving: snprintf(out, n, "SAVING..."); break;
  }
}

int recBtnScale(int cols, int charW, int charH, int w, int h, int maxScale) {
  int s = maxScale;
  while (s > 1 && (cols * charW * s > w || charH * s > h)) --s;
  return s < 1 ? 1 : s;
}

RecPrefs recPrefsFrom(uint8_t src, uint8_t micGain, uint8_t lineGain, uint8_t thresh, uint8_t mon) {
  RecPrefs p;
  if (src <= static_cast<uint8_t>(RecSource::Resample)) p.src = static_cast<RecSource>(src);
  p.micGain = static_cast<int8_t>(recGainClamp(RecSource::Mic, micGain));
  p.lineGain = static_cast<int8_t>(recGainClamp(RecSource::Line, lineGain));
  const int8_t th = static_cast<int8_t>(thresh);
  p.thresh = th > 0 ? kRecThreshOff : recThreshStep(th, 0);  // OFF stays OFF
  p.mon = mon != 0;
  return p;
}

}  // namespace mt
