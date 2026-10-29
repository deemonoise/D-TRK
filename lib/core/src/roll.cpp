#include "roll.h"

namespace mt {

uint8_t rollGateVal(const Step& s, uint8_t defGate) {
  const FxSlot* g = s.find(Fx::GAT);
  return g ? g->val : defGate;
}

RollLen rollSetGate(Step& s, uint8_t v, uint8_t defGate) {
  if (v == defGate) {
    for (FxSlot& f : s.fx)
      if (f.cmd == Fx::GAT) f = FxSlot();
    return RollLen::Ok;
  }
  FxSlot* gat = nullptr;
  FxSlot* empty = nullptr;
  for (FxSlot& f : s.fx) {
    if (f.cmd == Fx::GAT && !gat) gat = &f;
    if (f.cmd == Fx::None && !empty) empty = &f;
  }
  FxSlot* to = gat ? gat : empty;
  if (!to) return RollLen::NoSlot;
  to->cmd = Fx::GAT;
  to->val = v;
  return RollLen::Ok;
}

int rollSpan(const Step* steps, int count, int i, uint8_t defGate) {
  if (i < 0 || i >= count || !steps[i].hasNote()) return 0;
  const FxSlot* g = steps[i].find(Fx::GAT);
  int span = gatePercent(g ? g->val : defGate);
  int end = count;  // the next note or OFF, else the track's end
  for (int j = i + 1; j < count; ++j)
    if (steps[j].hasNote() || steps[j].note == kNoteOff) {
      end = j;
      break;
    }
  const int room = (end - i) * 100;
  return span > room ? room : span;
}

int rollChordExtra(const Step& s, uint8_t root, ScaleType t, uint8_t out[4]) {
  const FxSlot* c = s.find(Fx::CHD);
  if (!c || !s.hasNote()) return 0;
  uint8_t all[4];
  const int n = chordNotes(s.note, c->val, root, t, all);
  int k = 0;
  for (int i = 0; i < n; ++i)
    if (all[i] != s.note) out[k++] = all[i];
  return k;
}

RollKeyRow rollKeyRow(int note) {
  const int pc = note % 12;
  const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
  return {black, pc == 0 || pc == 5, black ? note + 1 : note, black ? note - 1 : note};
}

RollKey rollKey(int note, uint8_t root, ScaleType t) {
  if (((note - root) % 12 + 12) % 12 == 0) return RollKey::Root;
  if (t == ScaleType::Chromatic) return RollKey::Chromatic;
  return inScale(note, root, t) ? RollKey::In : RollKey::Out;
}

bool rollToggleLane(Step& s, int lane) {
  if (lane < 0 || lane >= kKitLanes) return false;
  if (!s.hasNote()) {
    s.note = 0;
    setLaneMask(s, 0);
  }
  const bool on = !laneOn(s, lane);
  setLane(s, lane, on);
  return on;
}

int rollFollow(int first, int cur, int visible, int total) {
  if (total <= visible) return 0;
  if (cur < first) first = cur;
  if (cur >= first + visible) first = cur - visible + 1;
  if (first > total - visible) first = total - visible;
  return first < 0 ? 0 : first;
}

uint16_t rollMix565(uint16_t a, uint16_t b, int t) {
  if (t <= 0) return a;
  if (t >= 256) return b;
  auto ch = [&](int shift, int mask) {
    const int x = (a >> shift) & mask, y = (b >> shift) & mask;
    return ((x + (y - x) * t / 256) & mask) << shift;
  };
  return static_cast<uint16_t>(ch(11, 31) | ch(5, 63) | ch(0, 31));
}

}  // namespace mt
