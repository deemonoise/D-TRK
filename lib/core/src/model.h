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

enum class Fx : uint8_t {
  None = 0, CHN, RAT, PRB, GAT, TIE, NDG, CHD, STR, CND, VRN, NRN, CCA, CCB, PBN, PGM, Count
};

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

  Project() { reset(); }
  void reset();
  bool anySolo() const;
  bool trackAudible(int t) const { return !tracks[t].mute && (!anySolo() || tracks[t].solo); }
};

}  // namespace mt
