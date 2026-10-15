#pragma once
#include <stdint.h>
#include "hot.h"
#include "perf_fx.h"

namespace mt {

constexpr int kTracks = 16;
constexpr int kMaxSteps = 128;
constexpr int kMinSteps = 4;
constexpr int kDefaultSteps = 16;
constexpr int kPatterns = 16;
constexpr int kSongMax = 128;  // song slots
constexpr int kPpqn = 96;

constexpr uint8_t kNoteEmpty = 0xFF;
constexpr uint8_t kNoteOff = 0xFE;
constexpr uint8_t kVelDefault = 0;
constexpr uint8_t kNoProgram = 0xFF;

constexpr int kInstruments = 32;
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
// Stored in files (INST byte 8): new types before Count only.
enum class InstrType : uint8_t { Chip, Sample, Fm, Drum, Synth, Kit, Macro, String, Count };
// UI order of the types (FM, SYNTH, MACRO, STRING, DRUM, SAMPLE, CHIP, KIT): position k -> type, type -> position.
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
// MACRO oscillator models (Instrument::machine). Stored in files: new models before Count only.
enum class MacroModel : uint8_t { SawPlus, Morph, SawSqr, Fold, Vosim, Vowel, Fm, FbFm, Count };
// MACRO macros: same slots as FM / DRUM (fx DCY..SHP, LFO Dec..Shp); SWP / CON unused.
enum MacroMacro : uint8_t { kMacTimbre = kMacDec, kMacColor = kMacCol, kMacEnvTmb = kMacShp };
const char* macroModelName(uint8_t model);  // "SAW+"; out of range -> model 0
// STRING resonator models (Instrument::machine). Stored in files: new models before Count only.
enum class StringModel : uint8_t { String, Modal, Count };
// STRING macros: same slots as FM / DRUM (fx DCY..CON, LFO Dec..Con).
enum StringMacro : uint8_t { kMacDamp = kMacDec, kMacBright = kMacCol, kMacStruct = kMacShp, kMacPos = kMacSwp,
                             kMacExc = kMacCon };
const char* stringModelName(uint8_t model);  // "STRING"; out of range -> model 0
// Instrument::machine of any type fits below this.
constexpr int kMachineMax = 16;
static_assert(static_cast<int>(FmMachine::Count) <= kMachineMax, "machine field");
static_assert(static_cast<int>(DrumMachine::Count) <= kMachineMax, "machine field");
static_assert(static_cast<int>(MacroModel::Count) <= kMachineMax, "machine field");
static_assert(static_cast<int>(StringModel::Count) <= kMachineMax, "machine field");
// Stored in files (Instrument::fltMode): new values go before Count only; loaders clamp unknown ones to Off.
enum class FltMode : uint8_t { Off, Lp, Bp, Hp, Ladder, Ms20, Comb, Formant, Count };
// Lock bits (Voice / TrackRt lockMask, lock[]): FM / DRUM macros 0..4, then the filter, the delay
// send, the drive, the reverb send, the bit / sample-rate reduction, the pan, the drive model, the
// trance gate's depth, the MOD send (the last bit: lockMask is full).
enum LockBit : uint8_t {
  kLockFlt = kFmMacros, kLockRes, kLockDly, kLockDrv, kLockRvb, kLockBit, kLockSrr, kLockPan, kLockDty, kLockGte,
  kLockMod, kLocks
};
static_assert(kLocks <= 16, "lockMask is a uint16_t");
enum class LfoWave : uint8_t { Sine, Tri, Saw, Square, Random, Count };
// TrackCfg::driveModel, fx DTY: the curve of the track insert's drive (see ModelDrive, synth_drive.h).
// Stored in files: new models before Count only; a value >= Count loads as Tanh.
enum class DriveModel : uint8_t { Tanh, Clip, Fold, Rect, Asym, Count };
// Dec..Con = macro index + 1 (FM / DRUM / SYNTH). Duty: CHIP; Sub, Noise, Semi2, Fold, Ring: SYNTH;
// RateN / DepthN / RtrgN: LFO N's rate, depth, phase restart (see Synth::control). Pan: the track's
// pan (+-64 at full depth). ModSend: the track's MOD send (TrackCfg::modSend). Stored in files: new targets before Count only (the Dest list shows them
// in its own order, see lfoDestStep).
enum class LfoDest : uint8_t {
  Pitch, Dec, Col, Shp, Swp, Con, Vol, Cutoff, Drive,
  Reso, Fenv, Dly, Rvb, Bit, Srr, Fine, Duty, Sub, Noise, Semi2,
  Rate1, Rate2, Rate3, Rate4, Depth1, Depth2, Depth3, Depth4, Rtrg1, Rtrg2, Rtrg3, Rtrg4, Pan, ModSend,
  Fold, Ring,
  Count
};

// LFO 2..4 of an instrument (LFO 1 keeps its own fields: older files). depth 0 = off.
constexpr int kLfos = 4;
static_assert(static_cast<int>(LfoDest::Rtrg1) - static_cast<int>(LfoDest::Rate1) == 2 * kLfos, "LFO targets");
// LFO sync byte (LfoCfg::sync, Instrument::lfoSync): bit 0 TEMPO (rate is a division, lfoSyncHz),
// bit 1 Retrig OFF (one phase per instrument, not restarted by notes; see Synth).
constexpr uint8_t kLfoTempo = 1, kLfoFree = 2;
inline bool lfoTempo(uint8_t s) { return s & kLfoTempo; }
inline bool lfoFree(uint8_t s) { return s & kLfoFree; }
struct LfoCfg {
  uint8_t wave = 0;   // LfoWave
  uint8_t rate = 64;  // 0..127 (lfoHz), or a division index when TEMPO (lfoSyncHz)
  int8_t depth = 0;   // -64..63
  uint8_t dest = 0;   // LfoDest
  uint8_t sync = 0;   // kLfoTempo | kLfoFree bits
};
static_assert(sizeof(LfoCfg) == 5, "LfoCfg layout (file format)");
// LFO fx locks (Fx::LFD .. LFT in this order): bit LFO x kLfoLocks + LfoLock of a lock mask.
enum LfoLock : uint8_t { kLfoLkDepth, kLfoLkRate, kLfoLkWave, kLfoLkDest, kLfoLocks };
static_assert(kLfos * kLfoLocks <= 16, "LFO lock mask is a uint16_t");

// Internal synth instrument. One-byte fields: the audio task reads them without a lock.
constexpr uint8_t kMasterVolMax = 200;  // master volume %, above 100 = up to +6 dB
constexpr uint8_t kDlyTimeMax = 16;     // delay time, sixteenths
// Master / send fx modes (SynthModel::dlyMode, rvbMode, modType, shimInt). Stored in files: new values
// before Count only. DlyMode = DelayMode (synth_delay.h), ModType = ModFxType (synth_modfx.h),
// ShimInt = PitchShift::Interval (synth_shimmer.h).
enum class DlyMode : uint8_t { Digital, Tape, PingPong, Count };
enum class RvbMode : uint8_t { Plain, Shimmer, Count };
enum class ModType : uint8_t { Off, Chorus, Ensemble, Flanger, Phaser, Count };
enum class ShimInt : uint8_t { Oct, Fifth, TwoOct, Count };

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
  uint8_t lfoSync = 0;   // LFO 1 kLfoTempo | kLfoFree bits (TEMPO: lfoRate is a division)
  // Filter, every type: see cutoffHz, resoQ, filterEnv.
  uint8_t fltMode = 0;          // FltMode
  uint8_t cutoff = 127;         // 0..127
  uint8_t reso = 0;             // 0..127
  int8_t fenv = 0;              // -64..63: +-6 octaves at the envelope's peak
  uint8_t fAtk = 0, fDec = 40;  // envTimeMs; fDec 0 = hold
  uint8_t keytrack = 0;         // 0..127 = 0..100 %
  uint8_t send = 0;             // delay send 0..127 (every type)
  // Track insert recommendation. In a preset (.mti, factory): the insert it recommends, with
  // driveModel below; loading the preset onto a track copies them into the track's insert
  // (takePresetInsert, preset_io.h). In a project: unused and 0 (the track insert, TrackCfg::drive ..,
  // is what plays); a project file without TINS migrates them to its tracks (see loadProject).
  uint8_t drive = 0;
  uint8_t crushBits = 0;
  uint8_t crushRate = 0;
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
  LfoCfg lfo[kLfos - 1];   // LFO 2..4 (LFO 1 is lfoWave .. lfoSync above; see lfoRef)
  KitLane kit[kKitLanes];  // KIT: the lanes (see kitSetDefaults)
  uint8_t driveModel = 0;  // DriveModel of the recommended insert (see drive above); 0 in a project
  // SYNTH wavefolder and ring modulator on the oscillator mix (SynParams::fold, ring), 0..127, 0 = off.
  uint8_t synFold = 0, synRing = 0;
};
// Drops an instrument's insert recommendation (drive, crushBits, crushRate, driveModel).
MT_INLINE void clearInsert(Instrument& m) { m.drive = m.crushBits = m.crushRate = m.driveModel = 0; }

// Envelope stage time: 0 -> 0 ms, 1..127 -> 1..10000 ms exponentially.
uint16_t envTimeMs(uint8_t v);
// FM DECAY: 0..127 -> 5..4000 ms exponentially (time to -60 dB).
uint16_t fmDecayMs(uint8_t v);
// LFO rate: 0..127 -> 0.05..30 Hz exponentially.
// Synced LFO rate: division index 0..kLfoSyncSteps-1 (1/32 .. 8 bars) at bpm -> Hz, and its name.
constexpr int kLfoSyncSteps = 12;
float lfoSyncHz(uint8_t div, uint16_t bpm);
const char* lfoSyncName(uint8_t div);
// LFO i (0..kLfos-1) of an instrument, by reference: LFO 1 is the instrument's own lfo* fields.
struct LfoRef {
  uint8_t& wave;
  uint8_t& rate;
  int8_t& depth;
  uint8_t& dest;
  uint8_t& sync;
};
struct Instrument;
LfoRef lfoRef(Instrument& m, int i);
MT_INLINE LfoCfg lfoAt(const Instrument& m, int i) {
  if (i <= 0 || i >= kLfos) return {m.lfoWave, m.lfoRate, m.lfoDepth, m.lfoDest, m.lfoSync};
  return m.lfo[i - 1];
}
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
// things per type); SYNTH: its default macros; KIT: its default lanes. A LFO target the type lacks
// (lfoDestValid) becomes PITCH.
void instrSetType(Instrument& m, InstrType t);
// KIT: every lane a silent sampler (no sample, no instrument), notes 60..67.
void kitSetDefaults(Instrument& m);
// LFO target valid for the type (DEC..CON: FM / DRUM / SYNTH / STRING, DEC..SHP: MACRO; DUTY: CHIP; SUB, NOISE, SEMI2: SYNTH).
bool lfoDestValid(uint8_t dest, InstrType t);
// LFO target d steps from dest over the targets valid for t, in the display order (PAN after RVB;
// clamped at the ends); self (0..kLfos-1, -1 = none): that LFO's RTRG is skipped.
uint8_t lfoDestStep(uint8_t dest, int d, InstrType t, int self = -1);
// Display name of the target: SYNTH names its macros SHP1..SENV; out of range = PITCH.
const char* lfoDestName(uint8_t dest, InstrType t);
// Envelope follower target d steps from dest: lfoDestStep without L1..L4 RTRG (a restart is an edge,
// not a level).
uint8_t folDestStep(uint8_t dest, int d, InstrType t);
// Follower detector: a track's peak (1.0 = full scale at MAIN 100 % x 4, as the meters) -> 0..1 over
// -48..0 dBFS of pk x 0.25.
float folLevel(float pk);
// One-pole coefficient per follower step (folStepMs: one Synth control update, 32 samples) for
// time v (envTimeMs: the envelope reaches ~63 % in it); 0 = instant (1).
float folCoef(uint8_t v);
float folStepMs();  // 32 samples at kSynthRate: 1 ms at 32 kHz, 0.726 ms at 44.1 kHz

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
  SLD, VIB, ARP, VSL, OFS, CUT, DCY, COL, SHP, SWP, CON, FLT, RES, SLC, OFF, DLY, ACC, DRV, RVB, ARM, ARS, BIT, SRR,
  LFO, LFD, LFS, LFW, LFT, LFR, PAN, DTY, GTE, MOD, Count
};
// LFO (1..kLfos) picks the LFO this step's LFD .. LFR act on (LFO 1 without it; the sequencer sends it
// first, so the slot order does not matter). LFD / LFS / LFW / LFT lock its depth (signed), rate (raw:
// lfoHz, or a division clamped to kLfoSyncSteps - 1 when TEMPO), wave, dest, like FLT: this step's
// note-ons, or the sounding voices on a step without a note. LFR restarts its phase at val / 256: the
// instrument's (Retrig OFF; the rate lock does not apply to that shared phase), and the step's
// note-ons' or the sounding voices' (Retrig ON). INT tracks only.
// BIT, SRR lock the track insert's bit-depth / sample-rate reduction (kLockBit / kLockSrr, 0 = off),
// INT tracks only.
// DRV, RVB lock the track insert's drive / the reverb send (kLockDrv / kLockRvb), INT tracks only.
// PAN locks the track's pan (kLockPan): 0..127, 64 = centre, shown L64 .. C .. R63, INT tracks only.
// DTY locks the track insert's drive model (kLockDty, DriveModel: TNH CLP FLD RCT ASY), INT tracks only.
// GTE locks the trance gate's depth (kLockGte, TrackCfg::gateDepth: 0 = OFF, 1..127), INT tracks only.
// MOD locks the MOD bus send (kLockMod, TrackCfg::modSend), INT tracks only.
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

// Synth message 0xF5 cmd val: cmd is an Fx (synth fx) or kSynthStep, a step start on the INT
// track with val = ticks per step | 0x80 if the step has a note (sent by the sequencer).
constexpr uint8_t kSynthStep = 0xF0;
// Synth message 0xF5 kSynthArpChord chord (a CHD value): the track's ARP cycles that chord's notes.
constexpr uint8_t kSynthArpChord = 0xF1;
// Synth message 0xF4 note ofs (INT tracks, ahead of a SAMPLE step): where the track's next note-on
// will start (note, OFS 0 = none), so the synth board loads it in advance. Never reaches MIDI.
constexpr uint8_t kSynthHint = 0xF4;

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

// Per pattern, per track: x2 / x4 play 2 / 4 track steps per pattern step, 1/2 / 1/4 one every 2 / 4.
// 0 = x1, so zeroed (old) patterns play as before.
enum class TrackSpeed : uint8_t { X1, X2, X4, Half, Quarter, Count };
inline TrackSpeed toSpeed(uint8_t v) {
  return v < static_cast<uint8_t>(TrackSpeed::Count) ? static_cast<TrackSpeed>(v) : TrackSpeed::X1;
}
inline int speedMul(TrackSpeed s) { return s == TrackSpeed::X2 ? 2 : (s == TrackSpeed::X4 ? 4 : 1); }
inline int speedDiv(TrackSpeed s) { return s == TrackSpeed::Half ? 2 : (s == TrackSpeed::Quarter ? 4 : 1); }
const char* speedName(TrackSpeed s);  // "1/4", "1/2", "x1", "x2", "x4"

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
  uint8_t trackSpeed[kTracks] = {0};  // TrackSpeed, per track
  Step steps[kTracks][kMaxSteps];

  void clear();
  bool isEmpty() const;  // steps only (trackLen ignored)
  void fitTrackLen();    // after a length change: track lengths past it become the length
};

// The step of `track` at pattern step pos, sub-step sub (0..speedMul-1), pass = passes of the
// pattern since it started. -1: a slow track does not play on this pattern step. The track loops
// on its own length (polymeter); slow tracks count through pass ends.
inline int trackStepAt(const Pattern& pt, int track, int pos, uint32_t pass, int sub) {
  const uint8_t n = pt.trackLen[track];
  const int len = n && n < pt.length ? n : pt.length;
  if (len <= 0) return 0;
  const TrackSpeed s = toSpeed(pt.trackSpeed[track]);
  const int d = speedDiv(s);
  if (d > 1) {
    const uint64_t c = static_cast<uint64_t>(pass) * pt.length + pos;
    return c % d ? -1 : static_cast<int>((c / d) % len);
  }
  return (pos * speedMul(s) + sub) % len;
}

// Trance gate (TrackCfg::gate*). Rate: the length of a gate step (stored in files: new rates before
// Count only, a value >= Count loads as 1/16). gateSixteenths: that length in 1/16 steps.
enum class GateRate : uint8_t { R32, R16, R8, Count };
inline float gateSixteenths(uint8_t r) { return r == 0 ? 0.5f : (r == 2 ? 2.f : 1.f); }
// Attack / release 0..127 -> 0.5 .. 50 ms (exponential); the defaults are ~1 ms / ~4 ms.
constexpr uint8_t kGateAtkDefault = 19, kGateRelDefault = 57;
float gateMs(uint8_t v);
// The gate presets the TRACK page's Pattern row copies into gateMask (bit i = step i + 1).
struct GatePreset {
  const char* name;
  uint16_t mask;
};
constexpr int kGatePresets = 16;
extern const GatePreset kGatePreset[kGatePresets];
// The preset holding mask, -1 = none (USER).
int gatePresetOf(uint16_t mask);

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
  uint8_t pan = 64;   // 0..127, 64 = centre, INT tracks
  uint8_t humanize = 0;  // 0..100: random timing (up to +-10 % of a step) and velocity (+-20) per step
  // Insert chain of the INT track (Synth: the track's mono sum, before the fader and the pan):
  // drive, then bit crush and sample-rate reduction, 0..127 each (0 = off). Fx DRV / BIT / SRR
  // lock them per step, LFO targets Drive / Bit / Srr move them.
  uint8_t drive = 0;
  uint8_t crushBits = 0;
  uint8_t crushRate = 0;
  uint8_t driveModel = 0;  // DriveModel (fx DTY locks it); drive 0 is off for every model
  // Trance gate (insert, after the crush): a 16-step on / off mask (bit i = step i + 1) on the song
  // clock at gateRate (GateRate), the closed steps at 1 - depth / 127 (0 = off; fx GTE locks it),
  // open for gateLen % of a step, ramps gateAtk / gateRel (gateMs). Bypassed while stopped.
  uint16_t gateMask = 0x5555;
  uint8_t gateRate = 1;  // GateRate
  uint8_t gateDepth = 0;
  uint8_t gateLen = 100;  // 1..100 %
  uint8_t gateAtk = kGateAtkDefault;
  uint8_t gateRel = kGateRelDefault;
  // Envelope follower (Synth): the level of track folSrc (1..kTracks, 0 = off; after its insert,
  // before its fader) moves this track's target folDest (LfoDest, folDestStep) by folAmt (-64..63, LFO
  // depth units), envelope attack / release folAtk / folRel (envTimeMs, folCoef). INT tracks only.
  uint8_t folSrc = 0;
  uint8_t folDest = static_cast<uint8_t>(LfoDest::Cutoff);
  int8_t folAmt = 0;
  uint8_t folAtk = 0;
  uint8_t folRel = 70;
  // MOD send (Synth's MOD bus, SynthModel::mod*): 0..127, after the fader like the delay / reverb sends.
  // Fx MOD locks it, LFO / follower target ModSend moves it. INT tracks only; a KIT shares one.
  uint8_t modSend = 0;
};

// Everything the synth reads from the project: the sound state the ESP mirrors to the synth board
// (synth_model.h cuts it into chunks). Fields are grouped by chunk: master, tracks, instruments,
// samples, wavetables.
struct SynthModel {
  uint16_t bpm = 120;
  uint8_t scaleRoot = 0;
  uint8_t scaleType = 0;   // ScaleType (scale.h), 0 = Chromatic
  uint8_t masterVol = 100;  // 0..kMasterVolMax %
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
  // 0 = none) at depth scDepth 0..127. The key track itself bypasses the compressor (only the rest ducks).
  uint8_t compAmt = 0;
  uint8_t compRel = 50;
  uint8_t scTrack = 0;
  uint8_t scDepth = 64;
  // Master / send fx (feature 5), saved in AUDI bytes 14..25. Delay mode (DlyMode) and its character
  // dlyChar (TAPE: wear, PING-PONG: width); reverb mode (RvbMode), shimmer amount and interval
  // (ShimInt: +12 / +7 / +24); MOD bus effect (ModType), rate (0.05..8 Hz exp), depth, feedback
  // (FLANGER / PHASER), return level; master EQ low / high shelf (0..127, 64 = flat, -12..+12 dB).
  // The defaults (DIGITAL, PLAIN, OFF, flat) sound as before the feature.
  uint8_t dlyMode = 0;
  uint8_t dlyChar = 40;
  uint8_t rvbMode = 0;
  uint8_t rvbShim = 60;
  uint8_t modType = 0;
  uint8_t modRate = 30;
  uint8_t modDepth = 64;
  uint8_t modFb = 40;
  uint8_t modLevel = 100;
  uint8_t eqLow = 64;
  uint8_t eqHigh = 64;
  uint8_t shimInt = 0;
  TrackCfg tracks[kTracks];
  Instrument instruments[kInstruments];
  ProjSample samples[kProjSamples];
  uint8_t sampleCount = 0;  // names unique ignoring case
  ProjWavetable wavetables[kProjWavetables];
  uint8_t wavetableCount = 0;  // names unique ignoring case
};

struct Project : SynthModel {
  char name[17] = {0};
  Pattern patterns[kPatterns];
  // Song: slot i plays pattern song[i]; when it starts, track t is muted iff bit t of songMute[i].
  uint8_t song[kSongMax] = {0};
  uint16_t songMute[kSongMax] = {0};
  uint8_t songLen = 0;
  bool preview = true;     // GRID note entry sounds on INT tracks
  // PERF: the effect of track button 1..8 (PerfFx).
  uint8_t perfMap[kPerfButtons] = {kPerfDefaultMap[0], kPerfDefaultMap[1], kPerfDefaultMap[2], kPerfDefaultMap[3],
                                   kPerfDefaultMap[4], kPerfDefaultMap[5], kPerfDefaultMap[6], kPerfDefaultMap[7]};
  // Each button's start depth (PerfStack depth), within perfRange() of its effect; 0 for None.
  int8_t perfDepth[kPerfButtons] = {};
  // Not saved: the loaded file had a sample list (SMPL chunk). False for files older than the list,
  // whose instrument sample names still have to be migrated (and after reset()).
  bool hasSampleList = false;

  Project() { reset(); }
  void reset();
  void perfDefaultDepths();  // perfDepth = the default of each button's effect
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
