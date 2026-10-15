#pragma once
#include <stdint.h>
#include <atomic>
#include "model.h"
#include "sample_source.h"
#include "stream_pool.h"
#include "synth_comp.h"
#include "synth_crush.h"
#include "synth_delay.h"
#include "synth_drive.h"
#include "synth_eq.h"
#include "synth_modfx.h"
#include "synth_perf.h"
#include "synth_shimmer.h"
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
// here (the sequencer sends it on the first INT track, see PerfBus::stepAt), 5 song clock: 1/16
// step val (mod 64) since play starts here (the trance gate's position, see gatePos), 0xF7 t (on
// kPreviewTrack): the preview track plays through pattern track t's insert (t >= kTracks: none),
// 0xFD (any track): the transport stopped (trance gates bypassed until the next 0xFA).
//
// Each track is a channel strip (TrackStrip): its voices add into one mono signal, which goes through
// the insert (drive, then bit / sample-rate crush, then the trance gate; TrackCfg), the fader
// (TrackCfg::vol), the sends and the pan. The strip takes the locks and LFO targets of the track's
// newest voice (DRV, BIT, SRR, DTY, GTE, PAN, DLY, RVB, MOD): one value for the whole track, a KIT
// shares the KIT's delay / reverb sends (the MOD send is the track's).
//
// Master chain (renderCore): PERF throws -> MOD bus (ModFx, SynthModel::mod*: the tracks' MOD sends ->
// a stereo return into the mix) -> delay (SynthModel::dlyMode, dlyChar) -> freeze duck -> reverb (SHIMMER:
// the pitch shifter in its feedback) -> compressor -> + sidechain key -> master EQ (eqLow / eqHigh)
// -> PERF -> master gain, soft clip. The defaults (OFF, DIGITAL, PLAIN, flat) are bit-exact to the
// chain before them.
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
  // Shimmer line (len int16 >= PitchShift::kLen) and its feedback block (wet: kBlock floats), both
  // owned by the caller, next to the reverb buffer; nullptr = rvbMode SHIMMER sounds PLAIN. Clears it.
  void setShimmerBuffer(int16_t* buf, uint32_t len, float* wet) {
    shimmer_.setBuffer(buf, len);
    shimWet_ = wet;
    shimOn_ = buf && wet && len >= PitchShift::kLen;
    shimRan_ = false;
  }
  // MOD bus line (len int16 >= ModFx::kLineLen, owned by the caller, next to the reverb buffer),
  // nullptr = CHORUS / ENSEMBLE / FLANGER silent (PHASER runs). Resets the MOD bus.
  void setModBuffer(int16_t* buf, uint32_t len) { modfx_.setBuffer(buf && len >= ModFx::kLineLen ? buf : nullptr); }
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

  // A track's channel strip (see the class comment). Gains: pan x fader, current, per-sample steps
  // of the ramp, targets reached at the next control update; panSet false: the next refresh snaps.
  struct TrackStrip {
    float gl = 1, gr = 1, dgl = 0, dgr = 0, tgl = 1, tgr = 1;
    bool panSet = false;
    float fader = 1;  // TrackCfg::vol / 127
    // The newest voice's send levels x fader: current, per-sample steps, targets (ramped like the
    // gains, landing at the next control update).
    float send = 0, rsend = 0, dsend = 0, drsend = 0, tsend = 0, trsend = 0;
    float msend = 0, dmsend = 0, tmsend = 0;  // the MOD send, the same way
    // Drive: a change (amount, model) crossfades from drvOld (the settings before it, its state
    // running on) to drive over kControl samples; drvFade 1 = settled (drvOld unused).
    ModelDrive drive, drvOld;
    float drvFade = 1;
    Crush crush;
    // Trance gate: gain g moves toward the step's target (1 open, floor closed) by up / down per
    // sample. on: depth > 0 (still runs while g ramps back to 1). len: the open part of a step.
    struct Gate {
      float g = 1, up = 1, down = 1, floor = 1, len = 1, steps = 1;  // steps: 1/16 per gate step
      uint16_t mask = 0;
      uint16_t ad = 0xFFFF;  // gateAtk | gateRel << 8 that up / down were made for
      bool on = false;
    } gate;
  };
  const TrackStrip& strip(uint8_t track) const { return strips_[track < kSynthTracks ? track : 0]; }
  // Trance gate position in 1/16 steps since play (0 .. 64, wraps), as of the last block; -1 while
  // the transport is stopped (gates bypassed). Read by the UI without a lock.
  float gatePos() const { return gateOut_; }
  // Envelope follower of pattern track t (TrackCfg::fol*): its envelope 0..1 as of the last control
  // update (0 = off or silent source). Read by the UI without a lock.
  float followerLevel(uint8_t t) const { return t < kTracks ? folEnv_[t] : 0.f; }

  // CPU guard: load = the last block's render time / the block's duration, after every block.
  // Above kLoadHigh (smoothed) the oldest voice, releasing first, fades out (kStealMs) and the
  // pool shrinks to the voices left; below kLoadLow it grows back by one every kCapUpBlocks, up
  // to kVoices. A late block alone is a spike the DMA buffers absorb (audio.cpp, kDmaBlocks): over
  // kLoadPanic for kPanicBlocks blocks in a row, or over kLoadHard once, a voice fades the same
  // way but the pool keeps its size. Not called (tests, offline render): the whole pool.
  static constexpr float kLoadHigh = 0.80f, kLoadPanic = 1.0f, kLoadHard = 1.5f, kLoadLow = 0.65f;
  static constexpr int kPanicBlocks = 2;
  static constexpr int kCapUpBlocks = 25;  // 73 ms on the Teensy, 100 ms on the ESP
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
  // mix = clearing the buses and the segment loop; strip = the tracks' strips. Counts (takeProfile): ctlMod = control updates,
  // the voice stages = voices sounding at a block's start, summed over the blocks.
  enum ProfStage : uint8_t {
    kProfQueue, kProfEvents, kProfCache, kProfInstLfo, kProfCtlMod, kProfCtlFlt, kProfCtlEng, kProfChip,
    kProfSample, kProfFm, kProfDrum, kProfSyn, kProfMix, kProfDelay, kProfReverb, kProfComp, kProfPerf,
    kProfMaster, kProfStrip, kProfMod, kProfShim, kProfEq, kProfStages
  };
  static const char* profName(int stage);
  // Any task: arms (or, nullptr, stops) the profiler. It takes effect at the start of the next block
  // (syncProfiler), never inside one: a block that began without the clock has no valid last mark.
  // Each arming clears the sums, counts and blocks when it takes effect.
  void setProfiler(uint32_t (*clock)()) {
    clockReq_.store(clock, std::memory_order_relaxed);
    profReq_.fetch_add(1, std::memory_order_release);
  }
  // Audio task, at a block's start (renderCore calls it too; idempotent): applies a pending
  // setProfiler. True = profiling (re)started with this block (the caller resets its own peaks).
  MT_INLINE bool syncProfiler() {  // per block: a new setProfiler call (rare) out of line
    return profReq_.load(std::memory_order_acquire) != profSeen_ && applyProfiler();
  }
  bool applyProfiler();
  // The clock in effect for the current block (audio task).
  bool profiling() const { return clock_ != nullptr; }
  // Requested state (any task): what the last setProfiler asked for.
  bool profilerArmed() const { return clockReq_.load(std::memory_order_relaxed) != nullptr; }
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
  MT_INLINE const Instrument& instrOf(const Voice& v) const { return v.lane ? laneOf(v) : inst(v.instr); }
  const Instrument& laneOf(const Voice& v) const;  // builds the lane (flash)
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
  // The insert settings of a runtime track: its TrackCfg, the preview track's previewIns_ one, or
  // nullptr (no insert).
  const TrackCfg* insertCfg(uint8_t track) const;
  // The strip's values from the track's newest voice nv (-1: none, the next refresh snaps); the pan
  // ramp reaches its target in len samples (the next control update).
  void refreshStrip(uint8_t track, int nv, int len);
  // The strip of track over the segment trk_[0..n) at block position pos: insert, sends,
  // peak, pan into the mix (or the sidechain bus).
  void runStrip(uint8_t track, int pos, int n, int scTrack, bool keyAround);
  // Envelope followers, at each control boundary before the voices' updates: the sources' levels
  // over the last segment (srcPk_) into the destination tracks' envelopes and their targets.
  void updateFollowers();
  // The trance gate's gain target at gate position p (in gate steps).
  float gateTarget(const TrackStrip::Gate& g, float p) const;
  void gateSlopes(TrackStrip::Gate& g, uint16_t ad);
  void snapStrip(TrackStrip& s);
  void runGate(TrackStrip::Gate& g, float* x, int n);
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
  int over_ = 0;  // blocks in a row over kLoadPanic
  uint32_t age_ = 0;
  TrackRt rt_[kSynthTracks];
  Ev ev_[kMaxEvents];
  int nEv_ = 0;
  static constexpr uint8_t kNoArpChord = 0xFF;
  void resetArp(TrackRt& r) const;
  float mixL_[kBlock], mixR_[kBlock];
  float send_[kBlock];   // delay send bus (mono, pre-pan)
  float rsend_[kBlock];  // reverb send bus (mono, pre-pan)
  float msend_[kBlock];  // MOD send bus (mono, pre-pan)
  float* shimWet_ = nullptr;  // SHIMMER: the reverb's last block (kBlock, mono, before the level)
  float scL_[kBlock], scR_[kBlock];  // sidechain key: the strip of Project::scTrack (bypasses the compressor)
  // Channel strips: one control segment of each track's mono sum, the tracks touched by an event
  // since their last refresh (bit = track), the preview track's insert (0xF7).
  float trk_[kControl];  // the mono sum of the track being rendered (renderCore -> runStrip)
  TrackStrip strips_[kSynthTracks];
  uint32_t stripDirty_ = 0;
  uint8_t previewIns_ = kTracks;
  static_assert(kSynthTracks <= 32, "stripDirty_ / hot masks are uint32_t");
  Reverb reverb_;
  Compressor comp_;
  ModFx modfx_;  // its 2 KB line in the reverb block (setModBuffer)
  MasterEq eq_;
  Shimmer shimmer_;
  bool shimOn_ = false;   // a shimmer buffer is set
  bool shimRan_ = false;  // the shimmer ran last block (shimWet_ holds that block's reverb)
  // PERF: the punch-in effects on the master, and the synth's side of throws / freeze. Ramps
  // (per sample, kPerfRamp): the throw send gains and the dry gain under a freeze, so a press or
  // release never clicks. perfDRet_ / perfRRet_: the delay / reverb return floor while its throw /
  // freeze (or its tail) runs, up kPerfRetUp per block, back down 1 per block once the tail ends.
  PerfBus perf_;
  float perfDs_ = 0, perfRs_ = 0, perfMs_ = 0;  // throw send gains (delay, reverb, MOD), 0..1
  float perfDry_ = 1;              // live mix gain under a freeze
  uint8_t perfDRet_ = 0, perfRRet_ = 0, perfMRet_ = 0;  // 0..kPerfRet
  bool rvbRan_ = false;  // the reverb ran last block (an effective level > 0)
  static constexpr float kPerfRamp = 1.f / 64.f;
  static constexpr uint8_t kPerfRet = 100, kPerfRetUp = 8;
  void perfMsg(uint8_t op, uint8_t val);
  Delay delay_;
  float trackPeak_[kSynthTracks] = {};
  bool meters_ = false;
  uint32_t (*clock_)() = nullptr;                   // in effect (audio task only)
  std::atomic<uint32_t (*)()> clockReq_{nullptr};   // requested by setProfiler
  std::atomic<uint32_t> profReq_{0};                // setProfiler calls
  uint32_t profSeen_ = 0;                           // the last applied profReq_
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
  // Song clock for the trance gates: 1/16 steps since play (wraps at 64, a multiple of every gate
  // cycle); 0xFA zeroes it, 0xF6 5 re-anchors it, it free-runs per segment while running and not
  // paused. running_: between 0xFA and 0xFD (stop). Kept by reset() (audio park mid-play).
  double gatePos_ = 0;
  bool running_ = false;
  volatile float gateOut_ = -1;  // gatePos()
  float sp16_ = 1;  // samples per 1/16 step, this block
  float gateSp16_ = 0;     // sp16_ of gateCtlInc_
  double gateCtlInc_ = 0;  // kControl / gateSp16_
  // Envelope followers (TrackCfg::fol*, see updateFollowers). srcPk_: a source track's peak after its
  // insert, before its fader, since the last boundary (tracks of folSrcMask_). Per destination track:
  // folEnv_ the envelope, folMod_ = folAmt / 64 x envelope, folLa_ the control() target (an LfoDest,
  // -1 = none). folVoice_: tracks whose voices take it (control: la, or the LFO mods for L1..L4
  // RATE / DEPTH), folVolMask_: tracks whose fader it moves (refreshStrip). folKey_ / folK_: cached
  // attack / release coefficients (folCoef).
  float srcPk_[kSynthTracks] = {};
  volatile float folEnv_[kTracks] = {};
  float folMod_[kTracks] = {};
  int8_t folLa_[kTracks] = {};
  uint32_t folSrcMask_ = 0, folVoice_ = 0, folVolMask_ = 0;
  bool folIdle_ = false;  // the last updateFollowers found no source: all envelopes, mods and masks 0
  uint16_t folKey_[kTracks];
  float folK_[kTracks][2] = {};
  bool fmCache_ = true;
  uint32_t fmCalls_ = 0;
  uint32_t drumCalls_ = 0;
};

// Where an event stamped t (us) lands in the block that starts playing at blockT: sample offset,
// clamped to [0, kBlock - 1]. Late events go to 0, events past the block return -1 (keep for later).
int eventOffset(uint64_t t, uint64_t blockT);

}  // namespace mt
