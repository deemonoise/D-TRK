// Synth board (Teensy 4.1): mt::Synth on I2S1 (PCM5102A), driven over the link by the ESP.
// LINK_DESK: the bare board on a computer - the link on USB Serial, the sound on USB audio too.
#include <Arduino.h>
#include <Audio.h>
#include <new>
#include <string.h>
#include "audio_out.h"
#include "bank.h"
#include "bench.h"
#include "codec.h"
#include "fs_server.h"
#include "fw_update.h"
#include "link_frame.h"
#include "link_server.h"
#include "preview_stream.h"
#include "psram.h"
#include "render_server.h"
#include "sample_tools.h"
#include "stream.h"
#include "synth.h"
#include "synth_reverb.h"
#include "wdog.h"
#if defined(LINK_DESK) && !defined(AUDIO_BENCH_MAX)  // the ceiling bench leaves the desk extras out (RAM1)
#include "demos.h"
#include "stream_bench.h"
#endif

extern "C" char* __brkval;
extern "C" char _heap_end;

namespace {

// Delay line (mono send, returned to both sides) from the RAM2 (OCRAM) heap: 2 s (176,400 B), 1.5 s
// when that would leave less than kRam2Reserve for the jobs (wavetable loads, previews).
constexpr uint32_t kDelayLen = 2 * mt::kSynthRate;
constexpr uint32_t kDelayLenShort = 3 * mt::kSynthRate / 2;
constexpr uint32_t kRam2Reserve = 64 * 1024;

mt::SynthModel model;
mt::Synth synth(model);
float reverbBuf[mt::Reverb::kBufLen];

SynthStream out;
AudioOutputI2S i2s;
AudioConnection toL(out, 0, i2s, 0);
AudioConnection toR(out, 1, i2s, 1);
#if defined(LINK_DESK) && !defined(AUDIO_BENCH_MAX)  // the ceiling bench: USB_SERIAL only, its RAM1 goes to the raised voice pool
AudioOutputUSB usbOut;
AudioConnection toUsbL(out, 0, usbOut, 0);
AudioConnection toUsbR(out, 1, usbOut, 1);

// No ESP to mirror the sound state: start from the first demo song's instruments and mixer. The
// Project (476 KB) only fits RAM2 before the delay line takes it.
void loadDemoModel() {
  void* mem = malloc(sizeof(mt::Project));
  if (!mem) return;
  mt::Project* p = new (mem) mt::Project();
  mt::demoBuild(0, *p);
  memcpy(static_cast<void*>(&model), static_cast<const mt::SynthModel*>(p), sizeof model);
  p->~Project();
  free(mem);
}
#endif

uint32_t ram2Free() { return static_cast<uint32_t>(&_heap_end - __brkval); }

// PERF ring buffer (REPEAT .. TAPE STOP): 6 s stereo from the reserved start of the PSRAM (CS0) arena,
// never from RAM2 (the stream pool and the delay line need it). Without PSRAM there is none: 0 =
// buffer effects silent. Returns the seconds held.
uint8_t allocPerf() {
  uint32_t frames = 0;
  int16_t* b = psram::perfBuffer(frames);
  if (!b) return 0;
  synth.setPerfBuffer(b, frames);  // cleared: the self-test left patterns there
  return psram::kPerfSec;
}

// The delay line takes what RAM2 can spare (malloc, not DMAMEM: the desk build borrows RAM2 for a
// whole Project at boot first).
// Returns the line length (0 = no delay).
uint32_t allocDelay() {
  uint32_t len = kDelayLen;
  if (ram2Free() < len * sizeof(int16_t) + kRam2Reserve) len = kDelayLenShort;
  auto* line = static_cast<int16_t*>(malloc(len * sizeof(int16_t)));
  if (!line) return 0;
  synth.setDelayBuffer(line, len);
  return len;
}

}  // namespace

#ifdef LINK_ECHO_TEST
#ifndef LINK_ECHO_BAUD
#define LINK_ECHO_BAUD mt::link::kBaud
#endif
// teensy41-echo: every byte from the ESP goes straight back (wt32-echo measures).
DMAMEM uint8_t echoRx[4096];
void setup() {
  Serial1.addMemoryForRead(echoRx, sizeof echoRx);
  Serial1.begin(LINK_ECHO_BAUD);
}

void loop() {
  while (Serial1.available()) Serial1.write(static_cast<uint8_t>(Serial1.read()));
}
#else
void setup() {
  wdog::captureReset();
  Serial.begin(115200);
#if defined(LINK_DESK) && !defined(AUDIO_BENCH_MAX)
  loadDemoModel();
#endif
  // The card first (the bank reads nothing from it at boot, but jobs need it mounted), then the bank:
  // its built-in wavetables need a 96 KB table from the RAM2 (OCRAM) heap before the delay line.
  card::begin();
  bank::begin(model, synth, out);
  stream::begin(synth);  // the slot pool (96 KB DMAMEM) and the loop() scheduler
  tools::begin();        // WavePeaks / Onsets of store samples as background work
  psram::begin(model, synth);  // CS0 chip (or off), self-test, the PERF reserve, the fill of the model
  const uint8_t perfSec = allocPerf();
  const uint32_t delayLen = allocDelay();
  synth.setReverbBuffer(reverbBuf, mt::Reverb::kBufLen);
  AudioMemory(12);
  out.begin(&synth);
  const bool shield = codec::begin();
  preview::begin(model, out);
  render::begin(synth, out);
  fw::begin(out);
  link::begin(model, synth, out, perfSec);
  link::log("synth board up, model %u B, voices %d / heavy %d, delay %lu ms, PERF %u s, RAM2 free %lu B, PSRAM %s",
            sizeof model, mt::kVoices, mt::kFmVoiceMax, static_cast<unsigned long>(delayLen * 1000 / mt::kSynthRate),
            perfSec, static_cast<unsigned long>(ram2Free()), psram::state() == psram::State::On ? "8 MB" : "off");
  bank::logState();
  link::log("audio: %s", shield ? "SGTL5000 shield (line out + headphones)" : "I2S DAC, no SGTL5000");
#if defined(LINK_DESK) && !defined(AUDIO_BENCH_MAX)
  sbench::begin(model, synth, out);
#endif
#ifdef AUDIO_BENCH_POOL
  bench::begin(model, synth, out);
#endif
  wdog::begin();
}

void loop() {
  wdog::feed();
  link::poll();
  stream::step();  // voice / preview loads, then one background portion (bank job, ...)
  psram::step();   // the watchdog
#if defined(LINK_DESK) && !defined(AUDIO_BENCH_MAX)
  sbench::step();  // the desk stream bench (link_desk.py bench-stream)
#endif
  preview::step();
  render::step();
  fw::step();
#ifdef AUDIO_BENCH_POOL
  bench::step();
#endif
}
#endif
