#pragma once
#include <stdint.h>
#include "model.h"
#include "synth_env.h"
#include "synth_fm.h"
#include "synth_osc.h"

namespace mt {

constexpr int kVoices = 16;
constexpr int kPolyPerTrack = 4;
constexpr int kFmVoiceMax = 8;  // FM voices sounding at once (CPU)

struct Voice {
  bool on = false;          // allocated (until the envelope goes idle)
  uint8_t track = 0, note = 0;
  uint8_t instr = 0;        // instrument index at note-on
  bool sample = false;      // SAMPLE instrument (else CHIP)
  uint32_t age = 0;         // allocation order, for stealing
  float pitch = 0;          // current note incl. transpose, fine and slide, fractional
  float target = 0;         // slide target
  float slideStep = 0;      // semitones per sample, 0 = none
  float gain = 1;           // velocity * VSL state
  // Control-rate values, refreshed every Synth::kControl samples.
  float inc = 0;            // CHIP: phase increment; SAMPLE: frames per output sample
  float duty = 0.5f;        // pulse width incl. PWM
  float amp = 0;            // gain * instrument vol * track vol
  float pwmPhase = 0;       // PWM sweep LFO, 0..1
  uint8_t wave = 0;         // Wave
  // Synth fx state (Synth::control).
  float vibPhase = 0;       // VIB LFO, 0..1
  uint32_t arpT = 0;        // samples since the ARP cycle started
  float vslStep = 0;        // VSL: gain change per sample while vslLeft > 0
  int32_t vslLeft = 0;      // samples
  int32_t cutLeft = -1;     // CUT: samples until the voice is killed, -1 = none
  uint8_t ofs = 0;          // OFS: sample start offset, /256 of start..end
  // Sample resolved at note-on. Position and step are 32.32 fixed point frames.
  const int16_t* smp = nullptr;
  uint32_t smpLen = 0, smpRate = 0;
  int64_t pos = 0;
  int64_t step = 0;         // from inc, at control rate
  int8_t dir = 1;           // +1 forward, -1 backward
  uint8_t loopMode = 0;     // LoopMode
  uint32_t from = 0, to = 0;      // played region [from, to)
  uint32_t loopLo = 0, loopHi = 0;  // loop region [loopLo, loopHi), mirrored for reverse
  ChipOsc osc;
  Env env;
  // FM instrument.
  bool fm = false;
  uint8_t machine = 0;               // FmMachine, latched at note-on: a sounding note keeps it
  uint8_t lockMask = 0;              // macros locked for this note: bit = FmMacro
  uint8_t lock[kFmMacros] = {0};
  float lfoPhase = 0;                // 0..1
  float lfoRnd = 0;                  // Random wave: value of the current cycle
  FmVoice fmv;
  // fmMachine() cache: params for the key below, recomputed when a macro moves >= 0.5 or the
  // pitch >= 1 cent from it (Synth::controlFm). fpValid = false forces a recompute (note-on).
  bool fpValid = false;
  float fpPitch = 0;
  float fpMac[kFmMacros] = {0};
  FmParams fp;
};

// Picks a voice for track and marks it allocated. Mono: the track's voice if any (legato), else a free one.
// Poly: a free voice if the track has < kPolyPerTrack, else the track's oldest.
// No free voice: the globally oldest releasing voice, else the globally oldest.
// fm: the note is FM. With kFmVoiceMax FM voices on, a pick that would add one more takes the
// oldest releasing FM voice instead, else the oldest FM voice (a mono track's own FM voice is
// reused as before; a mono track's CHIP / SAMPLE voice is released).
int allocVoice(Voice (&v)[kVoices], uint8_t track, bool mono, uint32_t& ageCounter, bool& legato,
               bool fm = false);

}  // namespace mt
