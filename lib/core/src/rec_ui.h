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

// A clock for long takes: "3:41" / "1:02:03" (pad: "03:41" under an hour), whole seconds.
void recFmtClock(uint32_t seconds, bool pad, char* out, int n);
// The toast after a REC -> SD take (the stop's result, how it ended): "REC12.WAV 3:41 SAVED", the end
// on its own as "REC12.WAV 3:41 CARD FULL" / "... 4GB LIMIT", then ", 3 CLIPS" and ", CARD TOO SLOW"
// when so; a failure (TOO SHORT, WRITE FAIL...) as recResultText.
void recSdToast(int number, uint32_t frames, uint32_t clips, link::RecResult r, link::RecEnd ended, char* out,
                int n);

// The REC window's big START / STOP button (a silent tap target for mic takes): the free space below
// the rows (rowsBottom .. areaBottom) inside a margin, the screen's width less the margins.
struct RecRect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
RecRect recBtnRect(int screenW, int rowsBottom, int areaBottom, int margin);
// What the button shows: READY as START (ARM with a Thresh), ARMED (the threshold, blinking), WaitPlay
// (RESAMPLE started with the transport stopped: the take starts with Play, blinking), STOP while
// recording, STARTING (a start not yet answered) and SAVING greyed, not tappable.
enum class RecBtn : uint8_t { Start, Arm, Armed, WaitPlay, Stop, Starting, Saving };
RecBtn recBtnState(bool taking, bool starting, RecState st, bool thresh, bool waitPlay);
inline bool recBtnLive(RecBtn b) { return b != RecBtn::Starting && b != RecBtn::Saving; }
inline bool recBtnBlinks(RecBtn b) { return b == RecBtn::Armed || b == RecBtn::WaitPlay; }
// A tap on the button (or the encoder's long press) by what it shows: Start (RecStart now), WaitPlay
// (RESAMPLE with the transport stopped: wait for Play, so its first note is in the take), Unwait (back
// to READY), Stop (the take saved, an arm ended), None (STARTING / SAVING). MIC / LINE never wait.
enum class RecTap : uint8_t { None, Start, WaitPlay, Unwait, Stop };
RecTap recTap(RecBtn b, RecSource src, bool playing);
// The label: "START", "ARM", "ARMED", "ARMED: PLAY", "STOP 03:41" (the clock of frames, pad),
// "STARTING", "SAVING...".
void recBtnText(RecBtn b, uint32_t frames, char* out, int n);
// The Cancel row by what the button shows: a take or an arm (Armed / Stop) or a start not yet answered
// (Starting) is dropped (Drop: RecCancel, the .tmp deleted), a wait for Play ends (Unwait), each with
// the window left open in READY; READY (Start / Arm) closes the window (Close); None while SAVING.
enum class RecCancel : uint8_t { None, Drop, Unwait, Close };
RecCancel recCancelAct(RecBtn b);
// Its label: "DROP THE TAKE" (a take, a start), "CANCEL ARM" (ARMED), "CANCEL WAIT" (ARMED: PLAY),
// "CLOSE" (READY), "SAVING..." (SAVING).
const char* recCancelText(RecBtn b);
// The biggest text scale 1..maxScale for which cols x charW by charH (at scale 1) fits w x h; at least 1.
int recBtnScale(int cols, int charW, int charH, int w, int h, int maxScale);

// The REC window's settings (device settings in NVS, shared by its SAMPLE and SD modes).
struct RecPrefs {
  RecSource src = RecSource::Mic;
  int8_t micGain = 32, lineGain = 5;  // recGainDefault
  int8_t thresh = kRecThreshOff;
  bool mon = false;
  bool operator==(const RecPrefs& o) const {
    return src == o.src && micGain == o.micGain && lineGain == o.lineGain && thresh == o.thresh && mon == o.mon;
  }
  bool operator!=(const RecPrefs& o) const { return !(*this == o); }
};
// The stored bytes into settings: out of range = the default (a source), clamped (gains), snapped to
// the THRESH steps; thresh is the int8_t as a byte.
RecPrefs recPrefsFrom(uint8_t src, uint8_t micGain, uint8_t lineGain, uint8_t thresh, uint8_t mon);

}  // namespace mt
