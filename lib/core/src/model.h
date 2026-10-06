#pragma once
#include <stdint.h>

namespace mt {

constexpr int kTracks = 16;
constexpr int kMaxSteps = 128;
constexpr int kMinSteps = 4;
constexpr int kDefaultSteps = 16;
constexpr int kPatterns = 16;
constexpr int kChainMax = 64;
constexpr int kChainRepMax = 16;  // passes of a chain item
constexpr int kChainTrMax = 24;   // chain item transpose, semitones
constexpr int kScenes = 8;        // mute scenes
constexpr uint16_t kSceneEmpty = 0xFFFF;  // a scene slot with nothing stored (all 16 muted is no use)
constexpr int kPpqn = 96;

constexpr uint8_t kNoteEmpty = 0xFF;
constexpr uint8_t kNoteOff = 0xFE;
constexpr uint8_t kVelDefault = 0;
constexpr uint8_t kNoProgram = 0xFF;

constexpr int kInstruments = 16;
constexpr int kWavetables = 16;
constexpr int kSampleNameMax = 16;
constexpr int kProjSamples = 128;  // = kBankEntries

// A sample of the project: name seen by instruments, data identified by crc32 of the int16 data.
struct ProjSample {
  char name[kSampleNameMax + 1] = {0};
  uint32_t crc = 0;
  uint32_t frames = 0;
};

// A wavetable of the project: name seen by SYNTH instruments, data identified by crc32 of the
// canonical 64 x 256 source (wtKey).
constexpr int kProjWavetables = 32;
struct ProjWavetable {
  char name[kSampleNameMax + 1] = {0};
  uint32_t crc = 0;
};

enum class TrackOut : uint8_t { Midi, Int, Count };
// Wt1..Wt16 follow Metal: wave = Wave::Wt1 + n.
enum class Wave : uint8_t { Pulse, Triangle, Saw, Noise, Metal, Wt1 };
constexpr int kWaveCount = static_cast<int>(Wave::Wt1) + kWavetables;
enum class LoopMode : uint8_t { Off, Forward, PingPong, Count };
// Stored in files: new values go before Count only.
enum class SliceMode : uint8_t { Off, Note, Fx, Count };  // NOTE: note - root = slice; FX: SLC picks it
enum class ChopMode : uint8_t { Equal, Trans, Count };
constexpr int kMaxSlices = 32;
enum class InstrType : uint8_t { Chip, Sample, Fm, Drum, Synth, Kit, Count };
// UI order of the types (FM, SYNTH, DRUM, SAMPLE, CHIP, KIT): position k -> type, type -> position.
InstrType instrTypeAt(int k);
int instrTypePos(InstrType t);
constexpr int kKitLanes = 8;
constexpr uint8_t kNoInstr = 0xFF;

// A KIT lane. instr == kNoInstr: a mini sampler on the project sample `sample` (empty / missing =
// silent), played at its own pitch (root = note) + pitch semitones, vol, decay (0 = whole sample, else
// envTimeMs(decay) then silence). instr < kInstruments: the lane plays that instrument at `note`.
// note is the lane's MIDI note: unique in the kit, sent on MIDI tracks. Stored in files (KITS).
struct KitLane {
  char sample[kSampleNameMax + 1] = {0};
  uint8_t instr = kNoInstr;
  uint8_t vol = 100;
  int8_t pitch = 0;   // -24..24
  uint8_t decay = 0;
  uint8_t note = 60;
};
static_assert(sizeof(KitLane) == kSampleNameMax + 6, "KitLane layout (file format)");
// FM machines (Model:Cycles style). Stored in files: new machines go before Count only.
enum class FmMachine : uint8_t { Kick, Snare, Metal, Perc, Tone, Chord, Clap, Hat, Count };
// FM macros: Instrument::macro index, fx DEC..CON = Fx::DCY + index.
enum FmMacro : uint8_t { kMacDec, kMacCol, kMacShp, kMacSwp, kMacCon, kFmMacros };
// SYNTH oscillator mode. Stored in files: new modes before Count only.
enum class SynOsc : uint8_t { Saw, Square, Tri, Wt, Count };
// SYNTH macros: same slots as FM / DRUM (fx DCY..CON, LFO Dec..Con).
enum SynMacro : uint8_t { kMacShp1 = kMacDec, kMacShp2 = kMacCol, kMacMix = kMacShp, kMacDet = kMacSwp,
                          kMacSenv = kMacCon };
// DRUM machines (808 / 909 models). Stored in files: new machines go before Count only.
enum class DrumMachine : uint8_t {
  Bd8, Sd8, Tom8, Cp8, Rs8, Cl8, Cb8, Hh8, Cy8, Bd9, Sd9, Tom9, Cp9, Rs9, Hh9, Cy9, Count
};
// Instrument::machine of either type fits below this.
constexpr int kMachineMax = 16;
static_assert(static_cast<int>(FmMachine::Count) <= kMachineMax, "machine field");
static_assert(static_cast<int>(DrumMachine::Count) <= kMachineMax, "machine field");
enum class FltMode : uint8_t { Off, Lp, Bp, Hp, Count };
// Lock bits (Voice / TrackRt lockMask, lock[]): FM / DRUM macros 0..4, then the filter, the delay
// send, the drive, the reverb send.
enum LockBit : uint8_t { kLockFlt = kFmMacros, kLockRes, kLockDly, kLockDrv, kLockRvb, kLockBit, kLockSrr, kLocks };
static_assert(kLocks <= 16, "lockMask is a uint16_t");
enum class LfoWave : uint8_t { Sine, Tri, Saw, Square, Random, Count };
// Dec..Con = macro index + 1 (FM / DRUM only). Stored in files: new targets before Count only.
enum class LfoDest : uint8_t { Pitch, Dec, Col, Shp, Swp, Con, Vol, Cutoff, Drive, Count };

// Internal synth instrument. One-byte fields: the audio task reads them without a lock.
constexpr uint8_t kMasterVolMax = 200;  // master volume %, above 100 = up to +6 dB
constexpr uint8_t kDlyTimeMax = 16;     // delay time, sixteenths

struct Instrument {
  char name[9] = {0};
  InstrType type = InstrType::Chip;
  uint8_t vol = 100;      // 0..127
  int8_t transpose = 0;   // -24..24 semitones
  int8_t fine = 0;        // -50..50 cents
  uint8_t attack = 0, decay = 40, sustain = 100, release = 30;  // times via envTimeMs, sustain 0..127
  bool mono = false;
  uint8_t glide = 0;      // MONO portamento, SLD units (x4 ms), 0 = off
  uint8_t wave = 0;       // Wave
  uint8_t duty = 50;      // pulse width 1..99 %
  uint8_t pwmRate = 0;    // 0..127, sweep speed
  uint8_t pwmDepth = 0;   // 0..49 %
  char sample[kSampleNameMax + 1] = {0};
  uint8_t root = 60;
  uint16_t start = 0, end = 0xFFFF;  // fraction of the sample, /0xFFFF
  uint8_t loop = 0;                  // LoopMode
  uint16_t loopStart = 0;
  bool reverse = false;
  // Slices: start points, fractions of the whole sample (/0xFFFF), ascending. Slice i plays
  // slices[i]..slices[i+1], the last one to end. Cleared when the sample changes.
  uint8_t sliceMode = 0;      // SliceMode
  uint8_t chopMode = 0;       // ChopMode, the CHOP button
  uint8_t chopN = 8;          // EQUAL: 2..kMaxSlices
  uint8_t chopThresh = 50;    // TRANS: 0..100, higher = more slices
  uint8_t sliceCount = 0;     // 0..kMaxSlices
  uint16_t slices[kMaxSlices] = {0};
  // FM / DRUM: machine and its macros (see fmSetMachine, drumSetMachine), LFO.
  uint8_t machine = 0;                                // FmMachine or DrumMachine, by type
  uint8_t macro[kFmMacros] = {85, 40, 32, 70, 50};    // DECAY..CONTOUR 0..127 (FM / DRUM), Kick defaults
  uint8_t lfoWave = 0;   // LfoWave
  uint8_t lfoRate = 64;  // 0..127, see lfoHz
  int8_t lfoDepth = 0;   // -64..63, 0 = off
  uint8_t lfoDest = 0;   // LfoDest
  // Filter, every type: see cutoffHz, resoQ, filterEnv.
  uint8_t fltMode = 0;          // FltMode
  uint8_t cutoff = 127;         // 0..127
  uint8_t reso = 0;             // 0..127
  int8_t fenv = 0;              // -64..63: +-6 octaves at the envelope's peak
  uint8_t fAtk = 0, fDec = 40;  // envTimeMs; fDec 0 = hold
  uint8_t keytrack = 0;         // 0..127 = 0..100 %
  uint8_t send = 0;             // delay send 0..127 (every type)
  uint8_t drive = 0;            // tanh drive before the filter, 0..127 (0 = off), every type
  uint8_t rsend = 0;            // reverb send 0..127 (every type)
  int8_t velCut = 0;            // velocity -> cutoff, -64..63 (+-6 octaves at full depth and velocity)
  int8_t velMac = 0;            // velocity -> DECAY macro, -64..63 (FM / DRUM / SYNTH)
  // SYNTH (see synth_syn.h): oscillators 1, 2 (SynOsc), their wavetables (project list name or a
  // built-in "*NAME"), osc 2 semitones, hard sync, sub (level, 0 = -1 / 1 = -2 octaves), noise,
  // env->SHAPE attack / decay (envTimeMs, decay 0 = hold). Macros: SHP1, SHP2, MIX, DET, SENV.
  uint8_t synOsc[2] = {0, 0};
  char synWt[2][kSampleNameMax + 1] = {{0}, {0}};
  int8_t synSemi = 0;     // -24..24
  bool synSync = false;
  uint8_t synSub = 0;     // 0..127
  uint8_t synSubOct = 0;  // 0..1
  uint8_t synNoise = 0;   // 0..127
  uint8_t synEAtk = 0, synEDec = 40;
  KitLane kit[kKitLanes];  // KIT: the lanes (see kitSetDefaults)
};

// Envelope stage time: 0 -> 0 ms, 1..127 -> 1..10000 ms exponentially.
uint16_t envTimeMs(uint8_t v);
// FM DECAY: 0..127 -> 5..4000 ms exponentially (time to -60 dB).
uint16_t fmDecayMs(uint8_t v);
// LFO rate: 0..127 -> 0.05..30 Hz exponentially.
float lfoHz(uint8_t v);
// TONE, CHORD: held while the note is, with the instrument's attack / sustain / release.
// The other machines are one-shot drums. Out of range = Kick.
bool fmGated(uint8_t machine);
// Sets the machine (clamped) and its default macros.
void fmSetMachine(Instrument& m, uint8_t machine);
// Filter cutoff 0..127 (fractional after locks / LFO) -> 20 Hz x 700^(v/127): 20 Hz .. 14 kHz.
float cutoffHz(float v);
// Resonance 0..127 -> Q 0.5 x 40^(v/127): 0.5 .. 20.
float resoQ(float v);
// Filter envelope t samples after the trigger: 0 -> 1 linearly over envTimeMs(fAtk), then down to
// -60 dB over envTimeMs(fDec) (exponential); fDec 0 holds 1.
float filterEnv(uint32_t t, uint8_t fAtk, uint8_t fDec);
// DRUM: sets the machine (clamped) and its default macros.
void drumSetMachine(Instrument& m, uint8_t machine);
// Changes the type. FM / DRUM: the machine (clamped) with its default macros (they mean other
// things per type); SYNTH: its default macros; KIT: its default lanes. CHIP / SAMPLE: a macro LFO
// target becomes PITCH.
void instrSetType(Instrument& m, InstrType t);
// KIT: every lane a silent sampler (no sample, no instrument), notes 60..67.
void kitSetDefaults(Instrument& m);
// LFO target d steps from dest (clamped at the ends); macros = false (CHIP / SAMPLE) skips DECAY..CONTOUR.
uint8_t lfoDestStep(uint8_t dest, int d, bool macros);

// Values are stored in project files: new commands go before Count only.
// SLD..SLC act on INT tracks only (synth fx, see fxSynthOnly). DCY..CON lock FM / DRUM / SYNTH macros
// (Fx::DCY + FmMacro). DCY is shown as "DEC": Arduino's Print.h #defines DEC.
// FLT, RES lock the filter cutoff / resonance (Fx::DCY + kLockFlt / kLockRes), every INT instrument.
// SLC picks the slice of the next SAMPLE note-on (FX slice mode).
// DLY locks the delay send (lock bit kLockDly), INT tracks only (see fxSynthOnly).
// OFF: note off val ticks after the step start, MIDI and INT (on INT it releases every voice of the
// track, samples too, see expandStep / Sequencer::pushOff).
// ACC: drum tracks, lane mask: lanes in it play at the step velocity, the others at 60 % (fxDrumOnly).
enum class Fx : uint8_t {
  None = 0, CHN, RAT, PRB, GAT, TIE, NDG, CHD, STR, CND, VRN, NRN, CCA, CCB, PBN, PGM,
  SLD, VIB, ARP, VSL, OFS, CUT, DCY, COL, SHP, SWP, CON, FLT, RES, SLC, OFF, DLY, ACC, DRV, RVB, ARM, ARS, BIT, SRR, Count
};
// BIT, SRR lock the voice's bit-depth / sample-rate reduction (kLockBit / kLockSrr, 0 = off; no
// instrument setting), INT tracks only.
// DRV, RVB lock the drive / reverb send (kLockDrv / kLockRvb), INT tracks only.
// ARM (arp mode): mode << 4 | rate (1..kArmRateMax notes per step); modes UP, DOWN, UPDOWN, RANDOM.
// kArmDefault (UP, 3) is the plain ARP. INT tracks only.
// ARS (step arp, the sequencer's): (octaves - 1) << 6 | mode << 4 | steps per note (1..kArmRateMax),
// the ARM modes, 1..kArsOctMax octaves. The step plays the first arp note, the next steps without a
// note play the next ones (real notes, MIDI too) until a note step, OFF or a pattern change; notes =
// CHD chord, else ARP 0 x y, else the root (at least 2 octaves then), repeated an octave up per
// extra octave. Not on drum tracks; ARP / ARM do not reach the synth on its step.
constexpr uint8_t kArmModes = 4, kArmRateMax = 8, kArmDefault = 0x03, kArsDefault = 0x01, kArsOctMax = 4;
// CND values beyond FST (0) and A:B (b = 2..8): FIL plays only while fill is held, NFL only while it is
// not. Their low nibble (< 2) is never an A:B value.
constexpr uint8_t kCndFill = 0x01, kCndNoFill = 0x02;
// RAT: ramp << 4 | hits (2..8); ramp 0 = even, kRatUp = velocity rising to the step's, kRatDown = falling.
constexpr uint8_t kRatUp = 1, kRatDown = 2;
// PRE / !PRE: the last condition evaluated on this track (CND or PRB, not PRE / NEI) passed / failed;
// NEI / !NEI: the same for the track on the left (track 1: always false). Shown PRE, !PR, NEI, !NE.
constexpr uint8_t kCndPre = 0x03, kCndNotPre = 0x04, kCndNei = 0x05, kCndNotNei = 0x06;

// Punch-in effects held on the track buttons (PERF mode); Project::perfMap says which one each button
// holds (saved: new values go before Count only). The sequencer adds them to the steps of the track
// as they play (Sequencer::perfSlot).
enum class PerfFx : uint8_t {
  None, Rat2, Rat4, FltLow, FltHigh, DlyMax, Crush, Fade, Mute,
  DecShort, Rat3, Rat8, RatUp, RvbMax, Srr, Drive, Count
};
constexpr int kPerfButtons = 8;

// Synth message 0xF5 cmd val: cmd is an Fx (synth fx) or kSynthStep, a step start on the INT
// track with val = ticks per step | 0x80 if the step has a note (sent by the sequencer).
constexpr uint8_t kSynthStep = 0xF0;
// Synth message 0xF5 kSynthArpChord chord (a CHD value): the track's ARP cycles that chord's notes.
constexpr uint8_t kSynthArpChord = 0xF1;

struct FxSlot {
  Fx cmd = Fx::None;
  uint8_t val = 0;
};

constexpr int kFxSlots = 6;

// Melodic tracks: note = kNoteEmpty / kNoteOff / a MIDI note, vel = kVelDefault (track's) or 1..127.
// Drum tracks (the track's instrument is a KIT, see Project::trackIsDrum), same layout:
//   note: kNoteEmpty = empty, kNoteOff = note off (releases every lane), else 0..127 = the step's
//         velocity for all its lanes (0 = the track's defVel); hasNote() still means note < 128.
//   vel:  8-bit lane mask, bit k = lane k + 1 hits. A note step with mask 0 plays nothing (its fx
//         still run). Melodic tracks' steps never carry bit 7 in vel.
struct Step {
  uint8_t note = kNoteEmpty;
  uint8_t vel = kVelDefault;
  FxSlot fx[kFxSlots];

  bool hasFx() const {
    for (const FxSlot& f : fx)
      if (f.cmd != Fx::None) return true;
    return false;
  }
  bool isEmpty() const { return note == kNoteEmpty && vel == kVelDefault && !hasFx(); }
  bool hasNote() const { return note < 128; }
  // The first slot with f.
  const FxSlot* find(Fx f) const {
    for (const FxSlot& s : fx)
      if (s.cmd == f) return &s;
    return nullptr;
  }
};
// Files store a step as note, vel, then cmd / val per slot (older files: 2 slots, see readPatn).
static_assert(sizeof(Step) == 2 + 2 * kFxSlots, "Step layout (file format)");

enum class Resolution : uint8_t {
  Quarter, Eighth, Sixteenth, ThirtySecond, EighthTriplet, SixteenthTriplet, Count
};

inline uint16_t ticksPerStep(Resolution r) {
  switch (r) {
    case Resolution::Quarter: return 96;
    case Resolution::Eighth: return 48;
    case Resolution::Sixteenth: return 24;
    case Resolution::ThirtySecond: return 12;
    case Resolution::EighthTriplet: return 32;
    case Resolution::SixteenthTriplet: return 16;
    default: return 24;
  }
}

// GAT value: 1..100 -> 1..100 %, 101..200 -> 107..800 % (7 % per unit).
inline uint16_t gatePercent(uint8_t v) {
  if (v == 0) return 1;
  if (v <= 100) return v;
  if (v > 200) v = 200;
  return 100 + (v - 100) * 7;
}

// NDG and PBN keep a signed value in the uint8 slot.
inline int8_t fxSigned(uint8_t v) { return static_cast<int8_t>(v); }

struct Pattern {
  uint8_t length = kDefaultSteps;
  Resolution res = Resolution::Sixteenth;
  uint8_t swing = 50;  // 50..75 %
  uint8_t groove = 0;  // groove template (grooveAt), 0 = OFF: swing applies
  uint8_t trackLen[kTracks] = {0};  // 0 = length, else 1..length: the track loops on its own (polymeter)
  Step steps[kTracks][kMaxSteps];

  void clear();
  bool isEmpty() const;  // steps only (trackLen ignored)
  void fitTrackLen();    // after a length change: track lengths past it become the length
};

struct TrackCfg {
  char name[9] = {0};
  uint8_t channel = 0;  // 0..15
  uint8_t defVel = 100;
  uint8_t defGate = 50;  // GAT encoding (gatePercent)
  uint8_t ccA = 74;
  uint8_t ccB = 71;
  uint8_t program = kNoProgram;
  bool mute = false;
  bool solo = false;
  TrackOut out = TrackOut::Int;  // files without TOUT load as MIDI (see loadProject)
  uint8_t instr = 0;  // 0..kInstruments-1, INT tracks
  uint8_t vol = 100;  // 0..127, INT tracks
  uint8_t humanize = 0;  // 0..100: random timing (up to +-10 % of a step) and velocity (+-20) per step
};

struct Project {
  char name[17] = {0};
  uint16_t bpm = 120;
  uint8_t scaleRoot = 0;
  uint8_t scaleType = 0;  // ScaleType (scale.h), 0 = Chromatic
  TrackCfg tracks[kTracks];
  Pattern patterns[kPatterns];
  uint8_t chain[kChainMax] = {0};
  uint8_t chainLen = 0;
  // Per chain item: transpose of melodic tracks (semitones, -kChainTrMax..kChainTrMax), passes before
  // advancing (1..kChainRepMax), mute scene recalled when the item starts (0 = none, 1..kScenes).
  // Rows move together: chainInsert / chainDelete (edit_ops).
  int8_t chainTr[kChainMax] = {0};
  uint8_t chainRep[kChainMax] = {0};  // reset() sets 1
  uint8_t chainScene[kChainMax] = {0};
  bool songMode = false;
  uint16_t scenes[kScenes];  // bit t = track t muted; kSceneEmpty = nothing stored (reset())
  Instrument instruments[kInstruments];
  uint8_t masterVol = 40;  // 0..kMasterVolMax %
  bool preview = true;     // GRID note entry sounds on INT tracks
  // Send delay (INT tracks): time 1..kDlyTimeMax sixteenths, feedback / tone / return level 0..127.
  uint8_t dlyTime = 3;
  uint8_t dlyFb = 50;
  uint8_t dlyTone = 90;
  uint8_t dlyLevel = 100;
  // Reverb (INT, send per instrument): size, damping, return level, 0..127.
  uint8_t rvbSize = 60;
  uint8_t rvbDamp = 70;
  uint8_t rvbLevel = 80;
  // Master compressor: amount 0..127 (0 = off), release 0..127; sidechain key = track scTrack (1..kTracks,
  // 0 = none) at depth scDepth 0..127.
  uint8_t compAmt = 0;
  uint8_t compRel = 50;
  uint8_t scTrack = 0;
  uint8_t scDepth = 64;
  // Master DJ filter on the internal sound: -64..-1 low-pass (closing towards -64), 0 off, 1..63
  // high-pass (opening towards 63).
  int8_t djFilter = 0;
  // PERF: the effect of track button 1..8 (PerfFx).
  uint8_t perfMap[kPerfButtons] = {1, 2, 3, 4, 5, 6, 7, 8};
  ProjSample samples[kProjSamples];
  uint8_t sampleCount = 0;  // names unique ignoring case
  ProjWavetable wavetables[kProjWavetables];
  uint8_t wavetableCount = 0;  // names unique ignoring case
  // Not saved: the loaded file had a sample list (SMPL chunk). False for files older than the list,
  // whose instrument sample names still have to be migrated (and after reset()).
  bool hasSampleList = false;

  Project() { reset(); }
  void reset();
  bool anySolo() const;
  bool trackInternal(int t) const { return tracks[t].out == TrackOut::Int; }
  bool trackAudible(int t) const { return !tracks[t].mute && (!anySolo() || tracks[t].solo); }
  // A drum track: its instrument is a KIT (steps hold lane masks, see Step). Any Out.
  bool trackIsDrum(int t) const { return kitOf(t) != nullptr; }
  // The KIT of track t, nullptr when its instrument is another type (or t out of range).
  const Instrument* kitOf(int t) const {
    if (t < 0 || t >= kTracks) return nullptr;
    const Instrument& m = instruments[tracks[t].instr % kInstruments];
    return m.type == InstrType::Kit ? &m : nullptr;
  }
};

}  // namespace mt
