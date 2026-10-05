#pragma once
#include <stdint.h>

namespace mt {

constexpr int kTracks = 8;
constexpr int kMaxSteps = 128;
constexpr int kMinSteps = 4;
constexpr int kDefaultSteps = 16;
constexpr int kPatterns = 16;
constexpr int kChainMax = 64;
constexpr int kPpqn = 96;

constexpr uint8_t kNoteEmpty = 0xFF;
constexpr uint8_t kNoteOff = 0xFE;
constexpr uint8_t kVelDefault = 0;
constexpr uint8_t kNoProgram = 0xFF;

constexpr int kInstruments = 16;
constexpr int kWavetables = 16;
constexpr int kSampleNameMax = 16;

enum class TrackOut : uint8_t { Midi, Int, Count };
// Wt1..Wt16 follow Metal: wave = Wave::Wt1 + n.
enum class Wave : uint8_t { Pulse, Triangle, Saw, Noise, Metal, Wt1 };
constexpr int kWaveCount = static_cast<int>(Wave::Wt1) + kWavetables;
enum class LoopMode : uint8_t { Off, Forward, PingPong, Count };
enum class InstrType : uint8_t { Chip, Sample, Fm, Count };
// FM machines (Model:Cycles style). Stored in files: new machines go before Count only.
enum class FmMachine : uint8_t { Kick, Snare, Metal, Perc, Tone, Chord, Clap, Hat, Count };
// FM macros: Instrument::macro index, fx DEC..CON = Fx::DCY + index.
enum FmMacro : uint8_t { kMacDec, kMacCol, kMacShp, kMacSwp, kMacCon, kFmMacros };
enum class LfoWave : uint8_t { Sine, Tri, Saw, Square, Random, Count };
// Dec..Con = macro index + 1.
enum class LfoDest : uint8_t { Pitch, Dec, Col, Shp, Swp, Con, Vol, Count };

// Internal synth instrument. One-byte fields: the audio task reads them without a lock.
constexpr uint8_t kMasterVolMax = 200;  // master volume %, above 100 = up to +6 dB

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
  // FM: machine and its macros (see fmSetMachine), LFO.
  uint8_t machine = 0;                                // FmMachine
  uint8_t macro[kFmMacros] = {85, 40, 32, 70, 50};    // DECAY..CONTOUR 0..127, Kick defaults
  uint8_t lfoWave = 0;   // LfoWave
  uint8_t lfoRate = 64;  // 0..127, see lfoHz
  int8_t lfoDepth = 0;   // -64..63, 0 = off
  uint8_t lfoDest = 0;   // LfoDest
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

// Values are stored in project files: new commands go before Count only.
// SLD..CON act on INT tracks only (synth fx, see fxSynthOnly). DCY..CON lock FM macros
// (Fx::DCY + FmMacro). DCY is shown as "DEC": Arduino's Print.h #defines DEC.
enum class Fx : uint8_t {
  None = 0, CHN, RAT, PRB, GAT, TIE, NDG, CHD, STR, CND, VRN, NRN, CCA, CCB, PBN, PGM,
  SLD, VIB, ARP, VSL, OFS, CUT, DCY, COL, SHP, SWP, CON, Count
};

// Synth message 0xF5 cmd val: cmd is an Fx (synth fx) or kSynthStep, a step start on the INT
// track with val = ticks per step | 0x80 if the step has a note (sent by the sequencer).
constexpr uint8_t kSynthStep = 0xF0;

struct FxSlot {
  Fx cmd = Fx::None;
  uint8_t val = 0;
};

struct Step {
  uint8_t note = kNoteEmpty;
  uint8_t vel = kVelDefault;
  FxSlot fx[2];

  bool isEmpty() const {
    return note == kNoteEmpty && vel == kVelDefault &&
           fx[0].cmd == Fx::None && fx[1].cmd == Fx::None;
  }
  bool hasNote() const { return note < 128; }
  const FxSlot* find(Fx f) const {
    if (fx[0].cmd == f) return &fx[0];
    if (fx[1].cmd == f) return &fx[1];
    return nullptr;
  }
};
static_assert(sizeof(Step) == 6, "Step must stay 6 bytes (file format)");

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
  Step steps[kTracks][kMaxSteps];

  void clear();
  bool isEmpty() const;
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
  TrackOut out = TrackOut::Midi;
  uint8_t instr = 0;  // 0..kInstruments-1, INT tracks
  uint8_t vol = 100;  // 0..127, INT tracks
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
  bool songMode = false;
  Instrument instruments[kInstruments];
  uint8_t masterVol = 40;  // 0..kMasterVolMax %
  bool preview = true;     // GRID note entry sounds on INT tracks

  Project() { reset(); }
  void reset();
  bool anySolo() const;
  bool trackInternal(int t) const { return tracks[t].out == TrackOut::Int; }
  bool trackAudible(int t) const { return !tracks[t].mute && (!anySolo() || tracks[t].solo); }
};

}  // namespace mt
