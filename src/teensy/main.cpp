// Synth board (Teensy 4.1): mt::Synth on I2S1 (PCM5102A), driven over the link by the ESP.
// LINK_DESK: the bare board on a computer - the link on USB Serial, the sound on USB audio too.
#include <Arduino.h>
#include <Audio.h>
#include <new>
#include <string.h>
#include "audio_out.h"
#include "bank.h"
#include "bench.h"
#include "fs_server.h"
#include "fw_update.h"
#include "link_frame.h"
#include "link_server.h"
#include "preview_stream.h"
#include "render_server.h"
#include "synth.h"
#include "synth_reverb.h"
#ifdef LINK_DESK
#include "demos.h"
#endif

extern "C" char* __brkval;
extern "C" char _heap_end;
extern "C" uint8_t external_psram_size;

namespace {

// Delay line (mono send, returned to both sides) from the RAM2 (OCRAM) heap: 2 s (176,400 B), 1.5 s
// when that would leave less than kRam2Reserve for the jobs (wavetable loads, previews).
constexpr uint32_t kDelayLen = 2 * mt::kSynthRate;
constexpr uint32_t kDelayLenShort = 3 * mt::kSynthRate / 2;
constexpr uint32_t kRam2Reserve = 64 * 1024;

mt::SynthModel model;
mt::Synth synth(model);
float reverbBuf[mt::Reverb::kBufLen];

SynthStream stream;
AudioOutputI2S i2s;
AudioConnection toL(stream, 0, i2s, 0);
AudioConnection toR(stream, 1, i2s, 1);
#ifdef LINK_DESK
AudioOutputUSB usbOut;
AudioConnection toUsbL(stream, 0, usbOut, 0);
AudioConnection toUsbR(stream, 1, usbOut, 1);

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

// PERF ring buffer (REPEAT .. TAPE STOP): 6 s, else 4 s, else 2 s from PSRAM when the board has
// it; without PSRAM 1 s from RAM2 if that still leaves the short delay line and kRam2Reserve (taken
// before the delay line: 1 s of PERF beats the delay's last 0.5 s). Held for good (no screen sprite
// here to share the memory with). Returns the seconds held, 0 = buffer effects silent.
uint8_t allocPerf() {
  if (external_psram_size) {
    for (const uint32_t sec : {6u, 4u, 2u}) {
      const uint32_t frames = sec * mt::kSynthRate;
      auto* b = static_cast<int16_t*>(extmem_calloc(frames * 2, sizeof(int16_t)));
      if (!b) continue;
      synth.setPerfBuffer(b, frames, false);
      return static_cast<uint8_t>(sec);
    }
  }
  const uint32_t frames = mt::kSynthRate;
  if (ram2Free() < (frames * 2 + kDelayLenShort) * sizeof(int16_t) + kRam2Reserve) return 0;
  auto* b = static_cast<int16_t*>(calloc(frames * 2, sizeof(int16_t)));
  if (!b) return 0;
  synth.setPerfBuffer(b, frames, false);
  return 1;
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
  Serial.begin(115200);
#ifdef LINK_DESK
  loadDemoModel();
#endif
  // The card first (the bank reads nothing from it at boot, but jobs need it mounted), then the bank:
  // its built-in wavetables need a 96 KB table from the RAM2 (OCRAM) heap before the delay line.
  card::begin();
  bank::begin(model, synth, stream);
  const uint8_t perfSec = allocPerf();
  const uint32_t delayLen = allocDelay();
  synth.setReverbBuffer(reverbBuf, mt::Reverb::kBufLen);
  AudioMemory(12);
  stream.begin(&synth);
  preview::begin(model, stream);
  render::begin(synth, stream);
  fw::begin(stream);
  link::begin(model, synth, stream, perfSec);
  link::log("synth board up, model %u B, voices %d / heavy %d, delay %lu ms, PERF %u s, RAM2 free %lu B, PSRAM %u MB",
            sizeof model, mt::kVoices, mt::kFmVoiceMax, static_cast<unsigned long>(delayLen * 1000 / mt::kSynthRate),
            perfSec, static_cast<unsigned long>(ram2Free()), external_psram_size);
  bank::logState();
#ifdef AUDIO_BENCH_POOL
  bench::begin(model, synth, stream);
#endif
}

void loop() {
  link::poll();
  bank::step();
  preview::step();
  render::step();
  fw::step();
#ifdef AUDIO_BENCH_POOL
  bench::step();
#endif
}
#endif
