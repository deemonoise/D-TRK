#include "midi_router.h"
#include "perf_fx.h"

namespace mt {

namespace {

constexpr int kModes = static_cast<int>(PadMode::Count);
constexpr int kPat = static_cast<int>(PadMode::Pat),
              kPerf = static_cast<int>(PadMode::Perf), kMute = static_cast<int>(PadMode::Mute);

Action act(ActKind k, int a = 0, int b = 0) { return Action{k, static_cast<uint8_t>(a), static_cast<int8_t>(b)}; }

// The mode whose learned bank holds (ch, note), first in enum order; -1 if none. A bank near 127 just
// has fewer pads: notes never exceed 127, so the range test needs no clamp.
int padOf(const PadLearn& pads, int ch, int note, int& pad) {
  for (int m = 1; m < kModes; ++m) {
    if (pads.base[m] == 0xFF || pads.ch[m] != ch) continue;
    const int d = note - pads.base[m];
    if (d >= 0 && d < (m == kPerf ? kPerfButtons : kPads)) {
      pad = d;
      return m;
    }
  }
  return -1;
}

// Switch on int: -1 (no bank) is not a PadMode value.
Action press(const PadLearn& pads, int ch, int note, int vel) {
  int pad = 0;
  switch (padOf(pads, ch, note, pad)) {
    case kPat: return act(ActKind::Pat, pad);
    case kMute: return act(ActKind::Mute, pad);
    case kPerf: return pad < kPerfButtons ? act(ActKind::PerfOn, pad) : act(ActKind::None);
    default: return act(ActKind::NoteOn, note, vel);
  }
}

// PAT / MUTE act on the press only; a release there does nothing.
Action release(const PadLearn& pads, int ch, int note) {
  int pad = 0;
  switch (padOf(pads, ch, note, pad)) {
    case kPerf: return act(ActKind::PerfOff, pad);
    case kPat:
    case kMute: return act(ActKind::None);
    default: return act(ActKind::NoteOff, note);
  }
}

}  // namespace

const char* padModeName(PadMode m) {
  static const char* const kNames[] = {"NOTE", "PAT", "PERF", "MUTE"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == kModes, "pad modes");
  const int k = static_cast<int>(m);
  return k < kModes ? kNames[k] : "?";
}

void PadLearn::clear() {
  for (int m = 0; m < kModes; ++m) {
    ch[m] = 0;
    base[m] = 0xFF;
  }
}

void MidiRouter::reset() {
  learn_ = -1;
  for (int i = 0; i < kKnobBinds; ++i) {
    last_[i] = 0xFF;
    lastCh_[i] = lastCc_[i] = 0;
  }
}

Action MidiRouter::route(const MidiMsg& m, PadLearn& pads, const Project& p) {
  const int type = m.status & 0xF0, ch = m.status & 0x0F;
  const bool noteOn = type == 0x90 && m.d2 > 0;
  const bool noteOff = type == 0x80 || (type == 0x90 && m.d2 == 0);
  if (noteOn && learn_ >= 0) {
    // Only a press teaches; releases and knobs route as usual so a held pad still lets go.
    const int mode = learn_;
    pads.ch[mode] = static_cast<uint8_t>(ch);
    pads.base[mode] = m.d1;
    learn_ = -1;
    return act(ActKind::LearnPad, mode);
  }
  if (noteOn) return press(pads, ch, m.d1, m.d2);
  if (noteOff) return release(pads, ch, m.d1);
  if (type != 0xB0) return act(ActKind::None);
  for (int i = 0; i < kKnobBinds; ++i) {
    const KnobBind& k = p.knobs[i];
    if (k.screen == 0 || k.ch != ch || k.cc != m.d1) continue;
    const int v = m.d2;
    int d;
    if (k.rel == 1) {
      d = v == 64 ? 0 : (v < 64 ? v : v - 128);  // 64 (-64) is no step a knob sends
    } else if (k.rel == 2) {
      d = v - 64;
    } else {
      // Absolute: the first value after connect or a rebind only sets the reference, so a knob far
      // from where it was does not jump the parameter. 0..127 values keep the delta within int8.
      const bool known = last_[i] != 0xFF && lastCh_[i] == k.ch && lastCc_[i] == k.cc;
      d = known ? v - last_[i] : 0;
      last_[i] = static_cast<uint8_t>(v);
      lastCh_[i] = k.ch;
      lastCc_[i] = k.cc;
    }
    return d != 0 ? act(ActKind::Knob, i, d) : act(ActKind::None);
  }
  return act(ActKind::KnobUnbound, m.d1, ch);
}

}  // namespace mt
