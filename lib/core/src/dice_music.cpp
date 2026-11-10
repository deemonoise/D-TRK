#include "dice_music.h"
#include <algorithm>
#include "gen_util.h"

namespace mt { namespace dice_detail {
// A deleted attack must not leave a retained tie holding into silence indefinitely.
void endThinnedTie(Step& st, int stepsToNext) {
  if (!st.find(Fx::TIE)) return;
  for (auto& fx : st.fx) if (fx.cmd == Fx::TIE) fx = FxSlot();
  putFx(st, Fx::GAT, gateValue(std::min(800, 100 * stepsToNext)));
}

namespace {
void chooseAttacks(Step* row, int start, int end, int budget, bool support, bool* removed) {
  int attacks = 0;
  for (int i = start; i < end; ++i) attacks += row[i].hasNote();
  if (attacks <= budget && !support) return;
  int seen = 0;
  for (int i = start; i < end; ++i) {
    Step& st = row[i];
    if (!st.hasNote()) { if (support) st = Step(); continue; }
    // Preserve the phrase head and cadence, spacing retained interior attacks evenly.
    const bool edge = seen == 0 || (budget > 1 && seen == attacks - 1);
    const bool interior = budget > 2 && seen > 0 && seen < attacks - 1 &&
                          seen * (budget - 2) / (attacks - 2) != (seen - 1) * (budget - 2) / (attacks - 2);
    const bool keep = attacks <= budget || edge || interior;
    ++seen;
    if (!keep) { st = Step(); removed[i] = true; continue; }
    if (support) {
      for (auto& fx : st.fx)
        if (fx.cmd == Fx::TIE || fx.cmd == Fx::SLD || fx.cmd == Fx::RAT || fx.cmd == Fx::NDG) fx = FxSlot();
      putFx(st, Fx::GAT, gateValue(50));
      st.vel = static_cast<uint8_t>(std::min(72, st.vel ? static_cast<int>(st.vel) : 100));
    }
  }
}
void repairRemovedTies(Step* row, int len, const bool* removed) {
  for (int i = 0; i < len; ++i) if (row[i].hasNote() && row[i].find(Fx::TIE)) {
    int distance = 1;
    bool affected = false;
    for (; distance < len; ++distance) {
      const int at = (i + distance) % len;
      if (row[at].hasNote() || row[at].note == kNoteOff) break;
      affected |= removed[at];
    }
    if (affected) endThinnedTie(row[i], distance);
  }
}
}  // namespace

void limitAttacks(Step* row, int len, Resolution resolution, int perBar, bool support) {
  if (!perBar) return;
  const int q = ticksPerStep(resolution), barSteps = std::max(1, 4 * kPpqn / q);
  bool removed[kMaxSteps] = {};
  for (int start = 0; start < len; start += barSteps)
    chooseAttacks(row, start, std::min(len, start + barSteps), perBar, support, removed);
  if (!support) repairRemovedTies(row, len, removed);
}

void thinPhrase(Step* row, int len, int budget) {
  if (budget < 1) return;
  bool removed[kMaxSteps] = {};
  chooseAttacks(row, 0, len, budget, false, removed);
  repairRemovedTies(row, len, removed);
}

void retainAttacks(Step* row, int len, const Step* source) {
  bool removed[kMaxSteps] = {};
  int kept = 0, first = -1;
  for (int i = 0; i < len; ++i) {
    if (source[i].hasNote() && first < 0) first = i;
    removed[i] = source[i].note == kNoteOff;
    if (!row[i].hasNote()) continue;
    if (source[i].note != kNoteOff) ++kept;
    else row[i] = Step();
  }
  // Sparse edits may remove every preselected event. Retain one canonical source event, not a new note.
  if (!kept && first >= 0) row[first] = source[first];
  repairRemovedTies(row, len, removed);
}

void endPhrase(Step* row, int len, int cutoff) {
  if (cutoff < 1 || cutoff >= len) return;
  int first = -1;
  for (int i = 0; i < len; ++i) if (row[i].hasNote()) { first = i; break; }
  if (first < 0) return;
  // An imported late pickup still needs one source event before the withdrawal.
  if (first >= cutoff) row[0] = row[first];
  for (int i = cutoff; i < len; ++i) row[i] = Step();
  row[cutoff].note = kNoteOff;  // releases long GAT/TIE/chord voices, including across loop boundaries
}

}}  // namespace mt::dice_detail
