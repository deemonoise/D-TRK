#include "perf_fx.h"
#include <stdio.h>

namespace mt {

namespace {

constexpr int kN = static_cast<int>(PerfFx::Count);

// Indexed by PerfFx - 1 (None has no entry).
const PerfRange kRanges[] = {
    {0, 6, 4, false},      // Repeat
    {1, 4, 2, false},      // Roll
    {0, 3, 2, false},      // Reverse
    {0, 3, 1, false},      // Scratch
    {0, 4, 2, false},      // TapeStop
    {0, 5, 3, false},      // Gate
    {-64, 63, 0, true},    // Filter
    {2, 12, 6, false},     // Crush
    {0, 5, 1, false},      // Downsample
    {0, 100, 60, true},    // Drive
    {0, 127, 100, true},   // DelayThrow
    {0, 127, 100, true},   // ReverbThrow
    {0, 100, 50, true},    // Freeze
};
const char* const kNames[] = {"REPEAT", "ROLL",  "REVERSE",     "SCRATCH",      "TAPE STOP", "GATE",  "FILTER",
                              "CRUSH",  "DOWNSAMPLE", "DRIVE", "DELAY THROW", "REVERB THROW", "FREEZE"};
const char* const kShorts[] = {"RPT", "ROL", "REV", "SCR", "TPS", "GAT", "FLT", "CRS", "DWN", "DRV", "DLY", "RVB", "FRZ"};
static_assert(sizeof(kRanges) / sizeof(kRanges[0]) == kN - 1, "perf ranges");
static_assert(sizeof(kNames) / sizeof(kNames[0]) == kN - 1, "perf names");
static_assert(sizeof(kShorts) / sizeof(kShorts[0]) == kN - 1, "perf short names");

// List-valued depths: labels and lengths in steps, index = depth - lo.
const char* const kRepeatLbl[] = {"1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
const float kRepeatSteps[] = {8, 4, 2, 4.0f / 3, 1, 2.0f / 3, 0.5f};
const char* const kGateLbl[] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
const float kGateSteps[] = {4, 2, 4.0f / 3, 1, 2.0f / 3, 0.5f};
const char* const kReverseLbl[] = {"1/4 BAR", "1/2 BAR", "1 BAR", "2 BAR"};
const float kReverseSteps[] = {4, 8, 16, 32};
const char* const kScratchLbl[] = {"1/4", "1/8", "1/16", "1/32"};
const float kScratchSteps[] = {4, 2, 1, 0.5f};
const char* const kTapeLbl[] = {"1/16", "1/8", "1/4 BAR", "1/2 BAR", "1 BAR"};
const float kTapeSteps[] = {1, 2, 4, 8, 16};
const char* const kDownLbl[] = {"16 KHZ", "8 KHZ", "4 KHZ", "2 KHZ", "1 KHZ", "500 HZ"};
const int kDownHz[] = {16000, 8000, 4000, 2000, 1000, 500};
static_assert(sizeof(kRepeatLbl) / sizeof(kRepeatLbl[0]) == 7 && sizeof(kGateLbl) / sizeof(kGateLbl[0]) == 6 &&
                  sizeof(kReverseLbl) / sizeof(kReverseLbl[0]) == 4 && sizeof(kScratchLbl) / sizeof(kScratchLbl[0]) == 4 &&
                  sizeof(kTapeLbl) / sizeof(kTapeLbl[0]) == 5 && sizeof(kDownLbl) / sizeof(kDownLbl[0]) == 6,
              "perf lists match the ranges");

bool valid(PerfFx f) { return f > PerfFx::None && f < PerfFx::Count; }

// Depth clamped into f's range, as an index from lo.
int at(PerfFx f, int depth) {
  const PerfRange r = perfRange(f);
  return (depth < r.lo ? r.lo : (depth > r.hi ? r.hi : depth)) - r.lo;
}

}  // namespace

PerfRange perfRange(PerfFx f) { return valid(f) ? kRanges[static_cast<int>(f) - 1] : PerfRange{0, 0, 0, false}; }

const char* perfName(PerfFx f) { return valid(f) ? kNames[static_cast<int>(f) - 1] : ""; }

const char* perfShort(PerfFx f) { return valid(f) ? kShorts[static_cast<int>(f) - 1] : ""; }

void perfValue(PerfFx f, int depth, char* out, int n) {
  if (n <= 0) return;
  out[0] = 0;
  if (!valid(f)) return;
  const int i = at(f, depth);
  const int d = i + perfRange(f).lo;
  switch (f) {
    case PerfFx::Repeat: snprintf(out, n, "%s", kRepeatLbl[i]); break;
    case PerfFx::Roll: snprintf(out, n, "N %d", d); break;
    case PerfFx::Reverse: snprintf(out, n, "%s", kReverseLbl[i]); break;
    case PerfFx::Scratch: snprintf(out, n, "%s", kScratchLbl[i]); break;
    case PerfFx::TapeStop: snprintf(out, n, "%s", kTapeLbl[i]); break;
    case PerfFx::Gate: snprintf(out, n, "%s", kGateLbl[i]); break;
    case PerfFx::Filter:
      if (d == 0) snprintf(out, n, "OFF");
      else snprintf(out, n, "%s %d", d < 0 ? "LP" : "HP", d < 0 ? -d : d);
      break;
    case PerfFx::Crush: snprintf(out, n, "%d BIT", d); break;
    case PerfFx::Downsample: snprintf(out, n, "%s", kDownLbl[i]); break;
    case PerfFx::Freeze: snprintf(out, n, "DRY %d", d); break;
    default: snprintf(out, n, "%d", d); break;  // Drive, the throws
  }
}

PerfFx perfFromLegacy(uint8_t old) {
  // None Rat2 Rat4 FltLow FltHigh DlyMax Crush Fade Mute DecShort Rat3 Rat8 RatUp RvbMax Srr Drive
  static const PerfFx kMap[] = {PerfFx::None,   PerfFx::Repeat, PerfFx::Repeat,     PerfFx::Filter,
                                PerfFx::Filter, PerfFx::DelayThrow, PerfFx::Crush,  PerfFx::None,
                                PerfFx::None,   PerfFx::None,   PerfFx::Repeat,     PerfFx::Repeat,
                                PerfFx::Roll,   PerfFx::ReverbThrow, PerfFx::Downsample, PerfFx::Drive};
  return old < sizeof(kMap) / sizeof(kMap[0]) ? kMap[old] : PerfFx::None;
}

float perfRepeatSteps(int depth) { return kRepeatSteps[at(PerfFx::Repeat, depth)]; }
float perfGateSteps(int depth) { return kGateSteps[at(PerfFx::Gate, depth)]; }
float perfReverseSteps(int depth) { return kReverseSteps[at(PerfFx::Reverse, depth)]; }
float perfScratchSteps(int depth) { return kScratchSteps[at(PerfFx::Scratch, depth)]; }
float perfTapeSteps(int depth) { return kTapeSteps[at(PerfFx::TapeStop, depth)]; }
int perfDownHz(int depth) { return kDownHz[at(PerfFx::Downsample, depth)]; }

int PerfStack::find(PerfFx f) const {
  for (int i = 0; i < n_; ++i)
    if (slot_[i].fx == f) return i;
  return -1;
}

void PerfStack::press(PerfFx f) {
  if (!valid(f)) return;
  release(f);  // pressed again: back on top at the default
  if (n_ >= kPerfButtons) return;
  slot_[n_++] = Slot{f, perfRange(f).def};
}

void PerfStack::release(PerfFx f) {
  const int i = find(f);
  if (i < 0) return;
  for (int j = i; j < n_ - 1; ++j) slot_[j] = slot_[j + 1];
  --n_;
}

void PerfStack::turn(int delta) {
  if (!n_) return;
  Slot& s = slot_[n_ - 1];
  const PerfRange r = perfRange(s.fx);
  const int d = s.depth + delta;
  s.depth = static_cast<int8_t>(d < r.lo ? r.lo : (d > r.hi ? r.hi : d));
}

bool PerfStack::held(PerfFx f) const { return find(f) >= 0; }

int PerfStack::depth(PerfFx f) const {
  const int i = find(f);
  return i < 0 ? perfRange(f).def : slot_[i].depth;
}

PerfFx PerfStack::buffered() const {
  for (int i = n_ - 1; i >= 0; --i)
    if (perfBuffered(slot_[i].fx)) return slot_[i].fx;
  return PerfFx::None;
}

}  // namespace mt
