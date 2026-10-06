#pragma once
#include <stdint.h>
#include "model.h"
#include "synth_env.h"
#include "synth_crush.h"
#include "synth_drive.h"
#include "synth_drum.h"
#include "synth_filter.h"
#include "synth_fm.h"
#include "synth_osc.h"
#include "synth_syn.h"

namespace mt {

constexpr int kVoices = 16;
constexpr int kPolyPerTrack = 4;
constexpr int kFmVoiceMax = 8;  // heavy voices (FM, DRUM, wavetable SYNTH) sounding at once (CPU)
constexpr uint16_t kStealMs = 4;  // fade of a heavy voice stolen past kFmVoiceMax

struct Voice {
  bool on = false;          // allocated (until the envelope goes idle)
  uint8_t track = 0, note = 0;
  uint8_t instr = 0;        // instrument index at note-on (a KIT sampler lane: the KIT's)
  bool lane = false;        // KIT sampler lane: the instrument is laneInst, not Project::instruments[instr]
  bool sample = false;      // SAMPLE instrument (else CHIP)
  uint8_t gen = 0;          // TrackRt::gen at note-on (sample choke)
  uint32_t age = 0;         // allocation order, for stealing
  float pitch = 0;          // current note incl. transpose, fine and slide, fractional
  float target = 0;         // slide target
  float slideStep = 0;      // semitones per sample, 0 = none
  float gain = 1;           // velocity * VSL state
  uint8_t vel = 0;          // note-on velocity (velocity -> cutoff / DECAY)
  // Control-rate values, refreshed every Synth::kControl samples.
  float inc = 0;            // CHIP: phase increment; SAMPLE: frames per output sample
  float duty = 0.5f;        // pulse width incl. PWM
  float amp = 0;            // gain * instrument vol * track vol
  float send = 0;           // delay send 0..1: DLY lock or Instrument::send
  float rsend = 0;          // reverb send 0..1: RVB lock or Instrument::rsend
  Drive drive;              // DRV lock or Instrument::drive, + LFO
  Crush crush;              // BIT / SRR locks (0 = off)
  float pwmPhase = 0;       // PWM sweep LFO, 0..1
  uint8_t wave = 0;         // Wave
  // Synth fx state (Synth::control).
  float vibPhase = 0;       // VIB LFO, 0..1
  uint32_t arpT = 0;        // samples since the ARP cycle started
  uint32_t arpK = UINT32_MAX;  // ARP slot of arpIdx (RANDOM picks once per slot)
  uint8_t arpIdx = 0;
  int8_t arpOff = 0;        // semitones the ARP adds now (tests)
  float vslStep = 0;        // VSL: gain change per sample while vslLeft > 0
  int32_t vslLeft = 0;      // samples
  int32_t cutLeft = -1;     // CUT: samples until the voice is killed, -1 = none
  uint8_t ofs = 0;          // OFS: sample start offset, /256 of start..end
  int8_t slice = -1;        // played slice, -1 = the whole region
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
  uint8_t machine = 0;               // FmMachine / DrumMachine, latched at note-on: a sounding note keeps it
  uint16_t lockMask = 0;             // locked for this note: bit = LockBit (macros: FM / DRUM only)
  uint8_t lock[kLocks] = {0};
  float lfoPhase[kLfos] = {};        // 0..1, per LFO
  float lfoRnd[kLfos] = {};          // Random wave: value of the current cycle
  FmVoice fmv;
  // fmMachine() cache: params for the key below, recomputed when a macro moves >= 0.5 or the
  // pitch >= 1 cent from it (Synth::controlFm). fpValid = false forces a recompute (note-on).
  bool fpValid = false;
  float fpPitch = 0;
  float fpMac[kFmMacros] = {0};
  FmParams fp;
  // DRUM instrument (machine above holds its DrumMachine). drumMachine() cache: dp for the fp*
  // key above, as fp for FM (Synth::controlDrum).
  bool drum = false;
  DrumVoice drv;
  DrumParams dp;
  // SYNTH instrument: tables resolved at note-on (a table changed mid-note applies to the next note).
  bool syn = false;
  bool synHeavy = false;  // SYNTH with a wavetable oscillator: counts against kFmVoiceMax
  bool stolen = false;    // fading out (kStealMs) for a heavy note past kFmVoiceMax
  uint32_t senvT = 0;                // samples since the env -> SHAPE trigger
  const int16_t* synWt[2] = {nullptr, nullptr};
  SynVoice sv;
  // Filter, every type (Synth::controlFilter).
  bool fltOn = false;
  uint32_t fenvT = 0;                // samples since the filter envelope's trigger
  bool fenvDone = false;             // the envelope has decayed to -60 dB: 0 until the next trigger
  float fltRes = -1, fltQ = 0.5f;    // resoQ(fltRes), cached
  Svf flt;
  Instrument laneInst;  // KIT sampler lane: scratch SAMPLE instrument built at note-on (see lane)
};

// Counts against kFmVoiceMax: a sounding heavy voice. A filter tail (env idle) costs only the
// filter and a stolen voice is on its way out.
inline bool heavyLoad(const Voice& x) {
  return x.on && (x.fm || x.drum || x.synHeavy) && !x.stolen && !x.env.idle();
}

// Picks a voice for track and marks it allocated. Mono: the track's voice if any (legato), else a free one.
// Poly: a free voice if the track has < polyMax, else the track's oldest.
// No free voice: the globally oldest releasing voice, else the globally oldest.
// heavy: the note is FM, DRUM or a wavetable SYNTH (synHeavy). With kFmVoiceMax heavy voices on
// (heavyLoad), a pick that would add one more fades the oldest releasing heavy voice, else the
// oldest heavy voice, over kStealMs (stolen) and takes a free voice; with none free it takes the
// victim itself. A mono track's own heavy voice is reused as before; a mono track's CHIP / SAMPLE
// voice is released.
// polyMax: poly voices the track may hold (KIT lanes: kKitLanes).
int allocVoice(Voice (&v)[kVoices], uint8_t track, bool mono, uint32_t& ageCounter, bool& legato,
               bool heavy = false, int polyMax = kPolyPerTrack);

}  // namespace mt
