#pragma once
#include <stdint.h>
#include "model.h"
#include "synth_comp.h"
#include "synth_delay.h"
#include "synth_reverb.h"
#include "synth_voice.h"

namespace mt {

// Runtime tracks inside the synth: the kTracks pattern tracks plus the preview track (INST / GRID preview).
constexpr int kSynthTracks = kTracks + 1;
constexpr uint8_t kPreviewTrack = kTracks;
constexpr uint8_t kPreviewVol = 100;  // track volume of the preview track

// Sample data for SAMPLE instruments: mono int16 frames at rate Hz, or nullptr if absent.
struct SampleSource {
  virtual const int16_t* find(const char* name, uint32_t& frames, uint32_t& rate) const = 0;
};

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
// pause / resume, any track: TEMPO LFOs with Retrig OFF back to phase 0 / hold / run on).
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
// Reads Project without locks: the fields it uses are single bytes / aligned halfwords.
class Synth {
 public:
  static constexpr int kBlock = 128;     // samples per render()
  static constexpr int kControl = 32;    // samples per control update (1 ms)
  static constexpr int kMaxEvents = 64;  // per block

  explicit Synth(const Project& p);
  // Sample source for SAMPLE instruments; nullptr = sampler silent.
  void setBank(const SampleSource* b) { bank_ = b; }
  // Wavetables for SYNTH instruments, resolved at note-on; nullptr = WT oscillators silent.
  void setWavetables(const WtSource* w) { wt_ = w; }
  // Delay line (len samples, owned by the caller), nullptr = no delay. Clears it.
  void setDelayBuffer(int16_t* buf, uint32_t len) { delay_.setBuffer(buf, len); }
  // Reverb buffer (len floats >= Reverb::kBufLen, owned by the caller), nullptr = no reverb. Clears it.
  void setReverbBuffer(float* buf, int len) { reverb_.setBuffer(buf, len); }
  // Queues a message for the next render(). offset: sample index inside that block, clamped to
  // 0..kBlock-1. False if dropped (bad track, empty or full queue; offs evict other messages).
  bool event(int offset, uint8_t track, const uint8_t* b, uint8_t len);
  // Resets runtime state of a track: instrument back to TrackCfg::instr, bend 0.
  void startTrack(uint8_t track);
  // Silences everything at once, clears the queue and the delay line.
  void reset();
  // Renders kBlock mono samples, applying pending events at their offsets.
  void render(int16_t* out);

  int activeVoices() const;

  // CPU guard: load = the last block's render time / the block's duration, after every block.
  // Above kLoadHigh (smoothed) or kLoadPanic (that block) the oldest voice, releasing first, fades
  // out (kStealMs) and the pool shrinks to the voices left; below kLoadLow it grows back by one
  // every kCapUpBlocks, up to kVoices. Not called (tests, offline render): the whole pool.
  static constexpr float kLoadHigh = 0.80f, kLoadPanic = 1.0f, kLoadLow = 0.65f;
  static constexpr int kCapUpBlocks = 25;  // 100 ms
  static constexpr int kMinVoices = 4;     // never shed below
  void setLoad(float load);
  int voiceCap() const { return cap_; }

  // Level meters (MIX): the peak of each pattern track's voices since the last call, before the
  // master gain (1.0 = full scale at MAIN 100 %), then cleared. Read without a lock: a block may land
  // on either side of the reset.
  void takeTrackPeaks(float out[kTracks]);
  // Meters on (MIX shown): the voices go through their segment buffer to measure the peaks; off,
  // a voice without sends renders straight into the mix (a little cheaper). Peaks stay 0 when off.
  void setMeters(bool on) { meters_ = on; }

  // Profiling (PROJ -> SYS -> CPU profile): cycles spent per stage, summed until takeProfile().
  // clock = a cycle counter; nullptr (the default) = off, no cost.
  enum ProfStage : uint8_t {
    kProfQueue, kProfEvents, kProfControl, kProfChip, kProfSample, kProfFm, kProfDrum, kProfSyn,
    kProfDelay, kProfReverb, kProfMaster, kProfStages
  };
  static const char* profName(int stage);
  void setProfiler(uint32_t (*clock)()) { clock_ = clock; }
  bool profiling() const { return clock_ != nullptr; }
  void addProfile(int stage, uint32_t cycles) { prof_[stage] += cycles; }
  // Copies the sums (and the blocks rendered) and clears them.
  void takeProfile(uint32_t out[kProfStages], uint32_t& blocks);
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
  void resetLfos(Voice& v);
  void advanceInstLfos();
  void transportStart();
  static float cachedHz(Voice& v, int k, float note);  // noteHz, reused while note stays
  static bool oneShot(const Voice& v);
  static uint8_t machineOf(const Instrument& m);
  float rnd();
  void renderVoice(Voice& v, float* out, int n);
  void startSample(Voice& v, const Instrument& m) const;
  static void renderSample(Voice& v, float* out, int n);
  static void renderChip(Voice& v, float* out, int n);
  uint8_t trackInstr(uint8_t track) const;
  // The voice's instrument: a KIT sampler lane's, built into laneScratch_ (valid until the next
  // call), else the project's.
  const Instrument& instrOf(const Voice& v) const;
  uint8_t trackVol(uint8_t track) const;

  const Project& p_;
  const SampleSource* bank_ = nullptr;
  const WtSource* wt_ = nullptr;
  mutable Instrument laneScratch_;  // instrOf: KIT sampler lane
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
  float mix_[kBlock];
  float send_[kBlock];  // delay send bus
  float rsend_[kBlock];  // reverb send bus
  float sc_[kBlock];     // sidechain key: the voices of Project::scTrack (bypass the compressor)
  Reverb reverb_;
  Compressor comp_;
  Svf dj_;  // master DJ filter
  void djFilter(float* x, int n);
  Delay delay_;
  float trackPeak_[kSynthTracks] = {};
  bool meters_ = false;
  uint32_t (*clock_)() = nullptr;
  uint32_t prof_[kProfStages] = {};
  uint32_t profBlocks_ = 0;
  int ctlLeft_ = kControl;     // samples to the next control update (FM ramps of mid-segment updates)
  uint32_t rng_ = 0x2545F491;  // LFO Random
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
