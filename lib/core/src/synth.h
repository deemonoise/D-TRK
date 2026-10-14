#pragma once
#include <stdint.h>
#include "model.h"
#include "sample_source.h"
#include "stream_pool.h"
#include "synth_comp.h"
#include "synth_delay.h"
#include "synth_perf.h"
#include "synth_reverb.h"
#include "synth_voice.h"

namespace mt {

// Runtime tracks inside the synth: the kTracks pattern tracks plus the preview track (INST / GRID preview).
constexpr int kSynthTracks = kTracks + 1;
constexpr uint8_t kPreviewTrack = kTracks;
constexpr uint8_t kPreviewVol = 100;  // track volume of the preview track


// Mip-mapped wavetables (wt_mip.h layout) for SYNTH instruments: a built-in "*NAME" or a name of
// the project's list; nullptr if absent.
struct WtSource {
  virtual const int16_t* findWt(const char* name) const = 0;
};

// LFO -> LFO modulation of an instrument's LFOs for one control update, per target j: rate octaves,
// depth factor (0..2) and a restart (bit j).
struct LfoMods {
  float oct[kLfos] = {};
  float depth[kLfos] = {1, 1, 1, 1};
  uint8_t rtrg = 0;
};
static_assert(kLfos == 4, "LfoMods::depth initializer");

// Hardware-free synth for INT tracks. Messages are MIDI-like, channel nibble ignored:
// 0x90 note vel (vel 0 = off), 0x80 note, 0xE0 lsb msb (bend, +-2 semitones), 0xC0 prog (instrument),
// 0xF5 cmd val (synth fx: cmd = Fx SLD..SLC, or kSynthStep), 0xFE (start: reset the track's runtime
// state), 0xFF (all off: release every voice of the track), 0xFA / 0xFC / 0xFB (transport start /
// pause / resume, any track: TEMPO LFOs with Retrig OFF back to phase 0 / hold / run on; 0xFA also
// restarts the PERF grid), 0xF6 op val (PERF on the master mix, any track; the UI sends it on
// kPreviewTrack): op 0 press PerfFx val, 1 release PerfFx val, 2 turn the last pressed by
// static_cast<int8_t>(val), 3 release all, 4 step clock: 1/16 step val (mod 64) of the grid starts
// here (the sequencer sends it on the first INT track, see PerfBus::stepAt).
//
// Synth fx follow the step model of the sequencer: 0xF5 kSynthStep (ticks per step | 0x80 if the step
// has a note) opens a step, then come the step's fx, then its notes. On a step with a note the fx apply
// to its note-ons (all of them: chord notes, ratchet hits), except SLD and OFS, which only the first
// note-on takes; a new step with a note ends VIB and ARP. On a step without a note the fx act on the
// track's sounding voices. DCY..CON lock macros (FM / DRUM / SYNTH voices only), FLT / RES the filter (every type),
// DLY the delay send (every type).
// Without any kSynthStep (tests, preview) fx wait for the next note-on. On the preview track SLC picks
// the slice in every slice mode, played at the root (slice audition).
// Tick length comes from Project::bpm, the step length is ticks per step x tick.
// Reads the SynthModel without locks: the fields it uses are single bytes / aligned halfwords.
class Synth {
 public:
  static constexpr int kBlock = 128;     // samples per render()
  static constexpr int kControl = 32;    // samples per control update (0.73 ms)
  static constexpr int kMaxEvents = 64;  // per block

  explicit Synth(const SynthModel& p);
  // Sample source for SAMPLE instruments; nullptr = sampler silent.
  void setBank(const SampleSource* b) { bank_ = b; }
  // Streamed samples (SampleData without whole) play from this pool; nullptr: such samples are silent.
  void setPool(StreamPool* p) { pool_ = p; }
  // Prefetch hint (kSynthHint note ofs, from the link ahead of the step): takes a Prefetch slot where
  // the track's next SAMPLE note-on would start, so the loader fills it before the note. loop() with
  // the audio interrupt masked (StreamPool::acquire). Reads the project's instrument, not the
  // interrupt's cache. The slot, or -1: nothing to load (not a streamed sample, a whole copy, FX
  // slices, no slot free).
  int prefetch(uint8_t track, uint8_t note, uint8_t ofs);
  // Wavetables for SYNTH instruments, resolved at note-on; nullptr = WT oscillators silent.
  void setWavetables(const WtSource* w) { wt_ = w; }
  // Delay line (len samples, owned by the caller), nullptr = no delay. Clears it.
  void setDelayBuffer(int16_t* buf, uint32_t len) { delay_.setBuffer(buf, len); }
  // Reverb buffer (len floats >= Reverb::kBufLen, owned by the caller), nullptr = no reverb. Clears it.
  void setReverbBuffer(float* buf, int len) { reverb_.setBuffer(buf, len); }
  // PERF ring buffer (frames stereo frames, 2 x frames int16, owned by the caller), nullptr = the
  // buffer effects (REPEAT .. TAPE STOP) silent. clear: see PerfBus::setBuffer.
  void setPerfBuffer(int16_t* buf, uint32_t frames, bool clear = true) { perf_.setBuffer(buf, frames, clear); }
  const PerfBus& perf() const { return perf_; }  // what PERF holds (UI, tests); 0xF6 changes it
  // Queues a message for the next render(). offset: sample index inside that block, clamped to
  // 0..kBlock-1. False if dropped (bad track, empty or full queue; offs evict other messages).
  bool event(int offset, uint8_t track, const uint8_t* b, uint8_t len);
  // Resets runtime state of a track: instrument back to TrackCfg::instr, bend 0.
  void startTrack(uint8_t track);
  // Silences everything at once, clears the queue and the delay line.
  void reset();
  // Renders kBlock stereo frames, interleaved L R (lr[2 * kBlock]), applying pending events at
  // their offsets.
  void renderStereo(int16_t* lr);
  // The same block planar: l[kBlock], r[kBlock] (the synth board's audio out).
  void render(int16_t* l, int16_t* r);
  // Renders kBlock mono samples: the downmix (L + R) >> 1 of the same block (centre pan and no
  // reverb: bit-exact to either side).
  void render(int16_t* out);

  int activeVoices() const;

  // CPU guard: load = the last block's render time / the block's duration, after every block.
  // Above kLoadHigh (smoothed) or kLoadPanic (that block) the oldest voice, releasing first, fades
  // out (kStealMs) and the pool shrinks to the voices left; below kLoadLow it grows back by one
  // every kCapUpBlocks, up to kVoices. Not called (tests, offline render): the whole pool.
  static constexpr float kLoadHigh = 0.80f, kLoadPanic = 1.0f, kLoadLow = 0.65f;
  static constexpr int kCapUpBlocks = 25;  // 73 ms
  static constexpr int kMinVoices = 4;     // never shed below
  void setLoad(float load);
  int voiceCap() const { return cap_; }

  // Level meters (MIX): the peak of each pattern track's voices since the last call, before the
  // master gain (1.0 = full scale at MAIN 100 %), then cleared. Read without a lock: a block may land
  // on either side of the reset.
  void takeTrackPeaks(float out[kTracks]);
  // Meters on (MIX shown): the voices go through their segment buffer to measure the peaks; off,
  // a voice without sends skips the peak scan. Peaks stay 0 when off.
  void setMeters(bool on) { meters_ = on; }

  // Profiling (PROJ -> SYS -> CPU profile): cycles spent per stage, summed until takeProfile().
  // clock = a cycle counter; nullptr (the default) = off, no cost. cache = cacheInstruments(). Control: instLfo = the shared
  // LFOs, ctlMod / ctlFlt / ctlEng = a voice's update up to the filter, the filter, the engine's.
  // mix = clearing the buses and the segment loop. Counts (takeProfile): ctlMod = control updates,
  // the voice stages = voices sounding at a block's start, summed over the blocks.
  enum ProfStage : uint8_t {
    kProfQueue, kProfEvents, kProfCache, kProfInstLfo, kProfCtlMod, kProfCtlFlt, kProfCtlEng, kProfChip,
    kProfSample, kProfFm, kProfDrum, kProfSyn, kProfMix, kProfDelay, kProfReverb, kProfComp, kProfPerf,
    kProfMaster, kProfStages
  };
  static const char* profName(int stage);
  void setProfiler(uint32_t (*clock)()) { clock_ = clock; }
  bool profiling() const { return clock_ != nullptr; }
  void addProfile(int stage, uint32_t cycles) { prof_[stage] += cycles; }
  // Copies the sums, counts (and the blocks rendered) and clears them.
  void takeProfile(uint32_t out[kProfStages], uint32_t count[kProfStages], uint32_t& blocks);
  uint32_t profNow() const { return clock_ ? clock_() : 0; }
  // Newest allocated voice of the track, or -1.
  int trackVoice(uint8_t track) const;
  int voiceInstr(int v) const { return voices_[v].instr; }
  const Voice& voice(int v) const { return voices_[v]; }
  // FM / DRUM params cache (on by default); off recomputes every control update (tests, diagnostics).
  void setFmCache(bool on) { fmCache_ = on; }
  // fmMachine() / drumMachine() evaluations so far.
  uint32_t fmMachineCalls() const { return fmCalls_; }
  uint32_t drumMachineCalls() const { return drumCalls_; }

  // Voice i's instrument as it plays (a KIT sampler lane: the SAMPLE instrument built from the lane).
  const Instrument& voiceInstrument(int i) const { return instrOf(voices_[i]); }
  // Phase (0..1) LFO i of voice v reads: its own, or with Retrig OFF (lfoFree) its instrument's.
  float lfoPhase(int v, int i) const;

 private:
  struct Ev {
    uint8_t off, track, len;
    uint8_t b[3];
  };
  struct TrackRt {
    uint8_t instr;     // after PGM, kNoInstr = TrackCfg::instr
    uint8_t gen;       // steps with a note so far (wraps): a SAMPLE note chokes older steps' samples
    float bend;        // semitones
    uint8_t tps;       // ticks per step (kSynthStep)
    bool noteStep;     // the current step has a note: fx wait for its note-ons
    uint8_t vib, arp;  // xy, 0 = off; until a new step with a note
    uint8_t arm;       // ARP mode (ARM), kArmDefault until set; reset with the ARP
    uint8_t arpChord;  // kSynthArpChord: the CHD value the next note-on arpeggiates, kNoArpChord = none
    uint8_t arpN;      // > 1: the ARP cycles arpNotes (semitones above the note) instead of 0, x, y
    int8_t arpNotes[4];
    uint8_t cut;       // ticks, 0 = none; this step's note-ons
    bool vslSet;
    int8_t vsl;        // per step, this step's note-ons
    uint8_t sld;       // x4 ms, 0 = none; the next note-on
    bool ofsSet;
    uint8_t ofs;       // the next SAMPLE note-on
    bool slcSet;
    uint8_t slc;       // SLC: slice of the next SAMPLE note-on
    float lastPitch;   // of the last note-on, < 0 = none (SLD)
    uint16_t lockMask;  // LockBit: this step's note-ons
    uint8_t lock[kLocks];
    uint8_t lfoSel;         // LFO fx: LFO 0..kLfos-1 this step's LFD .. LFR act on
    uint16_t lfoLockMask;   // LFO locks (see Voice::lfoLockMask): this step's note-ons
    LfoCfg lfoLock[kLfos];
    uint8_t lfoRst;         // LFR: bit per LFO, this step's note-ons start at lfoRstPh / 256
    uint8_t lfoRstPh[kLfos];
  };
  static constexpr uint8_t kNoInstr = mt::kNoInstr;

  void apply(const Ev& e);
  void fx(uint8_t track, uint8_t cmd, uint8_t val);
  void startVsl(Voice& v, int8_t val) const;
  void startCut(Voice& v, uint8_t ticks) const;
  void slideTo(Voice& v, float target, uint8_t sld) const;
  float tickSamples() const;
  uint32_t delaySamples() const;
  float stepSamples(uint8_t track) const { return rt_[track].tps * tickSamples(); }
  void noteOn(uint8_t track, uint8_t note, uint8_t vel);
  void noteOff(uint8_t track, uint8_t note);
  void releaseTrack(uint8_t track);
  void control(Voice& v, int dt);
  void controlFm(Voice& v, const Instrument& m, float pitch, int dt, const float* lm, float vol);
  void controlDrum(Voice& v, const Instrument& m, float pitch, int dt, const float* lm, float vol);
  void controlSyn(Voice& v, const Instrument& m, float pitch, int dt, const float* lm, float vol, const float* la);
  static void macros(const Voice& v, const Instrument& m, const float* lm, float (&mac)[kFmMacros]);
  static uint8_t velDecay(const Voice& v, const Instrument& m, uint8_t dec);
  void controlFilter(Voice& v, const Instrument& m, float pitch, const float* la);  // la: control()'s LFO sums
  float lfo(Voice& v, const LfoCfg& c, int i, int dt, const LfoMods& mods);
  float lfoRateHz(const LfoCfg& c) const;
  static LfoCfg lfoCfgOf(const Voice* v, const Instrument& m, int i);  // the instrument's + v's locks
  void lfoFx(uint8_t track, Fx f, uint8_t val, bool now);
  void resetLfos(Voice& v);
  void advanceInstLfos();
  void transportStart();
  static float cachedHz(Voice& v, int k, float note);  // noteHz, reused while note stays
  static bool oneShot(const Voice& v);
  static uint8_t machineOf(const Instrument& m);
  float rnd();
  void renderVoice(Voice& v, float* out, int n);
  void startSample(Voice& v, const Instrument& m);
  static void renderSample(Voice& v, float* out, int n);
  struct FrameRun {
    const int16_t* d;  // frame i is d[i - lo]
    uint32_t lo, hi;
  };
  // Frames around idx from the heads / v's slot; a sync pool loads a miss, planned from idx along step.
  bool frameRun(Voice& v, uint32_t idx, int64_t step, FrameRun& r);
  void renderStream(Voice& v, float* out, int n);
  void startSlot(Voice& v, const SampleRegion& g);
  void dropSlot(Voice& v);
  static void renderChip(Voice& v, float* out, int n);
  uint8_t trackInstr(uint8_t track) const;
  // The voice's instrument: a KIT sampler lane's, built into laneScratch_ (valid until the next
  // call), else inst(v.instr).
  const Instrument& instrOf(const Voice& v) const;
  // Instrument i: its copy in internal RAM if cached, else the project's.
  MT_INLINE const Instrument& inst(uint8_t i) const {
    const int s = i < kInstruments ? instSlot_[i] : -1;
    return s >= 0 ? instCopy_[s] : p_.instruments[i < kInstruments ? i : 0];
  }
  // Block start: the sounding voices' instruments into the slots, the LFO settings of theirs and of
  // kLfoScan more (round robin).
  void cacheInstruments();
  // Copies instrument i into its slot (a free one if it has none; none left: not cached) and its
  // LFO settings.
  void cacheInstrument(uint8_t i);
  void cacheLfos(int m);  // instrument m's LFO settings into instLfoCfg_ / instShared_
  uint8_t trackVol(uint8_t track) const;
  uint8_t trackPan(uint8_t track) const;
  // Pan gains towards pan (0..127): the ramp reaches them in len samples (the next control update);
  // a voice without panSet snaps.
  static void setPanTarget(Voice& v, uint8_t pan, int len);
  // The block through the master chain into mixL_ / mixR_ (master gain and soft clip applied).
  void renderCore();

  const SynthModel& p_;
  const SampleSource* bank_ = nullptr;
  StreamPool* pool_ = nullptr;
  const WtSource* wt_ = nullptr;
  mutable Instrument laneScratch_;  // instrOf: KIT sampler lane
  // The project sits in PSRAM: a cache miss there costs ~1 us on the ESP32-S3 and the UI's frame
  // buffer keeps evicting it, so the control updates read copies in internal RAM. The sounding
  // voices' instruments are copied at each block's start and at their note-ons (an edit lands at
  // the next block); more instruments than slots are read from the project.
  static constexpr int kInstSlots = 8;
  Instrument instCopy_[kInstSlots];
  uint8_t slotInst_[kInstSlots] = {};
  int8_t instSlot_[kInstruments];  // the slot of an instrument, -1 = none
  int nInstSlots_ = 0;
  // Every instrument's LFO settings and Retrig OFF mask (sharedLfos) for advanceInstLfos. A silent
  // instrument's are refreshed round robin, kLfoScan per block (all 32 in 64 ms): reading every one
  // per block costs ~100 cache misses. Only the shared phases of silent instruments read them.
  static constexpr int kLfoScan = 2;
  LfoCfg instLfoCfg_[kInstruments][kLfos];
  uint8_t instShared_[kInstruments] = {};
  uint8_t lfoScan_ = 0;  // the next instrument of the round robin
  Voice voices_[kVoices];
  float load_ = 0;  // smoothed render load (setLoad)
  int cap_ = kVoices;
  int capUp_ = 0;
  uint32_t age_ = 0;
  TrackRt rt_[kSynthTracks];
  Ev ev_[kMaxEvents];
  int nEv_ = 0;
  static constexpr uint8_t kNoArpChord = 0xFF;
  void resetArp(TrackRt& r) const;
  float mixL_[kBlock], mixR_[kBlock];
  float send_[kBlock];   // delay send bus (mono, pre-pan)
  float rsend_[kBlock];  // reverb send bus (mono, pre-pan)
  float scL_[kBlock], scR_[kBlock];  // sidechain key: the voices of Project::scTrack (bypass the compressor)
  Reverb reverb_;
  Compressor comp_;
  // PERF: the punch-in effects on the master, and the synth's side of throws / freeze. Ramps
  // (per sample, kPerfRamp): the throw send gains and the dry gain under a freeze, so a press or
  // release never clicks. perfDRet_ / perfRRet_: the delay / reverb return floor while its throw /
  // freeze (or its tail) runs, up kPerfRetUp per block, back down 1 per block once the tail ends.
  PerfBus perf_;
  float perfDs_ = 0, perfRs_ = 0;  // throw send gains, 0..1
  float perfDry_ = 1;              // live mix gain under a freeze
  uint8_t perfDRet_ = 0, perfRRet_ = 0;  // 0..kPerfRet
  bool rvbRan_ = false;  // the reverb ran last block (an effective level > 0)
  static constexpr float kPerfRamp = 1.f / 64.f;
  static constexpr uint8_t kPerfRet = 100, kPerfRetUp = 8;
  void perfMsg(uint8_t op, uint8_t val);
  Delay delay_;
  float trackPeak_[kSynthTracks] = {};
  bool meters_ = false;
  uint32_t (*clock_)() = nullptr;
  uint32_t prof_[kProfStages] = {};
  uint32_t profN_[kProfStages] = {};
  uint32_t profBlocks_ = 0;
  uint32_t profT_ = 0;  // the cycle counter at the last mark
  // Books the cycles since the last mark to stage.
  MT_INLINE void mark(int stage) {
    if (!clock_) return;
    const uint32_t now = clock_();
    prof_[stage] += now - profT_;
    profT_ = now;
  }
  int ctlLeft_ = kControl;     // samples to the next control update (FM ramps of mid-segment updates)
  static constexpr uint32_t kRngSeed = 0x2545F491;
  uint32_t rng_ = kRngSeed;  // LFO Random, reseeded by reset(): a render repeats exactly
  // Retrig OFF LFOs (lfoFree): one phase (and Random value) per instrument, shared by its voices;
  // advanceInstLfos moves them once per control update, before the voices read them. Kept by reset()
  // (audio park mid-play): only 0xFA restarts the TEMPO ones.
  float instLfoPhase_[kInstruments][kLfos] = {};
  float instLfoRnd_[kInstruments][kLfos] = {};
  // Their last outputs (x depth / 64 x DEPTH from shared LFOs) and wraps (bit i): LFO -> LFO sources
  // for the shared LFOs' RATE / RTRG (advanceInstLfos) and for the voices' targets (control).
  float instLfoOut_[kInstruments][kLfos] = {};
  uint8_t instLfoWrap_[kInstruments] = {};
  bool paused_ = false;  // transport paused (0xFC .. 0xFB / 0xFA): TEMPO ones hold
  bool fmCache_ = true;
  uint32_t fmCalls_ = 0;
  uint32_t drumCalls_ = 0;
};

// Where an event stamped t (us) lands in the block that starts playing at blockT: sample offset,
// clamped to [0, kBlock - 1]. Late events go to 0, events past the block return -1 (keep for later).
int eventOffset(uint64_t t, uint64_t blockT);

}  // namespace mt
