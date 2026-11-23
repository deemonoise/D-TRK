#include "dice_arrangement.h"

#include <algorithm>
#include <cstring>

#include "dice_hybrid.h"
#include "gen_util.h"
#include "rng.h"

namespace mt {
namespace dice_detail {
namespace {

bool isArp(int source, const DiceVariant& variant) {
  if (source < 0) return source == dicePartSource(DicePart::Arp);
  const auto& voice = diceVoice(variant, source);
  return !std::strcmp(voice.name, "Arp") || !std::strcmp(voice.name, "Seq");
}

int transpose(const DiceParts& parts, int t) {
  const auto& cfg = parts.tracks[t];
  return cfg.out == TrackOut::Int && cfg.instr < kInstruments ? parts.instruments[cfg.instr].transpose : 0;
}

bool hasMelodicOutput(const DiceParts& parts, int track) {
  const auto& cfg = parts.tracks[track];
  const auto* sound = cfg.instr < kInstruments ? &parts.instruments[cfg.instr] : nullptr;
  if (cfg.mute || (sound && sound->type == InstrType::Kit)) return false;
  if (cfg.out == TrackOut::Midi) return true;
  return cfg.vol && sound && sound->vol && companionPlayable(cfg, sound, DicePart::Lead);
}

bool isBass(int source, const DiceVariant& variant) {
  return source >= 0 ? diceVoice(variant, source).kind == uint8_t(DiceVoiceKind::Bass)
                     : source == dicePartSource(DicePart::Bass);
}

// Read untouched bass parts from the project. Only notes inside the track's
// own loop contribute, and a high-register line doesn't occupy the bass range.
int bassFloor(const DicePat& dst, const Pattern& prior, const DiceParts& parts, const DiceLayout& layout,
              const DiceVariant& variant, uint8_t written) {
  int total = 0, count = 0;
  for (int t = 0; t < kDiceRoles; ++t) {
    const int source = layout.source[t];
    if (!isBass(source, variant) || !hasMelodicOutput(parts, t)) continue;
    const bool generated = written & (1u << t);
    const auto* row = generated ? dst.steps[t] : prior.steps[t];
    const int loop = generated ? dst.trackLen[t] : prior.trackLen[t];
    const int length = loop ? std::min<int>(dst.length, loop) : dst.length;
    const int offset = transpose(parts, t);
    for (int at = 0; at < length; ++at)
      if (row[at].hasNote()) {
        const int pitch = row[at].note + offset;
        if (pitch >= 0 && pitch < 60) {
          total += pitch;
          ++count;
        }
      }
  }
  return count ? std::max(55, total / count + 10) : 0;
}

int exchangePriority(int source, const DiceVariant& v, const PhrasePlan& plan) {
  if (source >= 0 && (plan.melodicMask & (1u << source)) &&
      diceVoice(v, source).kind != uint8_t(DiceVoiceKind::Bass) && !isArp(source, v))
    return 0;
  if (source == dicePartSource(DicePart::Lead) || source == dicePartSource(DicePart::Counter)) return 1;
  if (source == dicePartSource(DicePart::Fx) || source == dicePartSource(DicePart::Pulse)) return 2;
  return 3;
}

// Compress the existing accents into one half of the bar. Read both lines
// before replacing either one, so an empty bar stays a rest.
struct Line {
  int count = 0, at[3] = {};
  Step notes[3];
};
Line halfPhrase(const Step* row, int start, int end, bool answer) {
  uint8_t attacks[kMaxSteps];
  int found = 0;
  for (int at = start; at < end; ++at)
    if (row[at].hasNote()) attacks[found++] = at;
  const int middle = (start + end) / 2, begin = answer ? middle : start, limit = answer ? end : middle;
  Line line;
  line.count = std::min({found, answer ? 3 : 2, limit - begin});
  int next = begin;
  for (int i = 0; i < line.count; ++i) {
    const int src = attacks[i * (found - 1) / std::max(1, line.count - 1)];
    const int beat = begin + (src - start) * (limit - begin) / (end - start);
    line.at[i] = std::max(next, std::min(beat, limit - (line.count - i)));
    line.notes[i] = row[src];
    next = line.at[i] + 1;
  }
  return line;
}

void writeLine(Step* row, const Line& line, int limit, int anchor, int offset, bool answer,
               const DiceParts& parts, const DiceVariant& variant, const DiceRecipe& recipe,
               const PhrasePlan& plan, int ticks) {
  // Each figure shares a call, a response and a landing tone across its bars.
  static constexpr uint8_t figures[][5] = {{2, 1, 0, 1, 0}, {0, 1, 2, 1, 0}, {1, 0, 1, 2, 0}, {0, 2, 2, 0, 1}};
  const auto* figure = figures[(plan.seed ^ (plan.seed >> 16)) % 4];
  for (int i = 0; i < line.count; ++i) {
    const int at = line.at[i], next = i + 1 < line.count ? line.at[i + 1] : limit;
    DiceChord chord;
    if (!harmonyAt(variant, recipe, at * ticks, parts.scaleRoot, parts.scaleType, plan, chord))
      chord = {static_cast<uint8_t>(60 + parts.scaleRoot), kChordTriad};
    // Place this chord's root in the octave nearest the original call.
    const int pc = chord.root % 12;
    const int root = ((anchor + 5 - pc) / 12) * 12 + pc;
    uint8_t tones[4];
    const int n =
        chordNotes(root, chord.chord, parts.scaleRoot, static_cast<ScaleType>(parts.scaleType), tones);
    const int degreeIndex = answer && i == line.count - 1 ? 4 : (answer ? 2 : 0) + i;
    Step& note = row[at];
    note.note = tones[figure[degreeIndex] % n] - offset;
    note.vel = (line.notes[i].vel + (i == 0 ? 92 : answer ? 70 : 78)) / 2;
    const int gate = std::min(80 * (next - at), (answer ? 125 : 165) * (kPpqn / 4) / ticks);
    putFx(note, Fx::GAT, gateValue(std::max(1, gate)));
    for (const auto& fx : line.notes[i].fx)
      if (fx.cmd == Fx::FLT || fx.cmd == Fx::RES || fx.cmd == Fx::DLY || fx.cmd == Fx::RVB)
        putFx(note, fx.cmd, fx.val);
  }
}
// Look for a bass attack in the first eighth of the return, on its own clock.
bool earlyBass(const DicePat& next, const Pattern& prior, const DiceParts& parts,
               const DiceLayout& layout, const DiceVariant& variant, uint8_t written) {
  for (int t = 0; t < kDiceRoles; ++t) {
    if (!isBass(layout.source[t], variant) || !hasMelodicOutput(parts, t)) continue;
    const bool generated = written & (1u << t);
    const auto speed = toSpeed(generated ? next.trackSpeed[t] : prior.trackSpeed[t]);
    const int ticks = ticksPerStep(next.res) * speedDiv(speed) / speedMul(speed);
    const int loop = generated ? next.trackLen[t] : prior.trackLen[t];
    const Step* row = generated ? next.steps[t] : prior.steps[t];
    for (int at = 0; at < (loop ? loop : next.length) && at * ticks < kPpqn / 2; ++at)
      if (row[at].hasNote()) return true;
  }
  return false;
}

struct FillLanes {
  uint16_t hats = 0, snare = 0, perc = 0;
};

FillLanes fillLanes(const Instrument& kit, uint16_t played, const DiceLayout& layout, const DiceVariant& variant) {
  FillLanes lanes;
  for (int i = 0; i < kDiceRoles; ++i) {
    const int lane = layout.lane[i];
    if (lane < 0) continue;
    const uint16_t mask = 1u << lane;
    const auto& sound = kit.kit[lane];
    if (!(played & mask) || !sound.vol || (sound.instr >= kInstruments && !sound.sample[0])) continue;
    switch (static_cast<DiceVoiceKind>(diceVoice(variant, i).kind)) {
      case DiceVoiceKind::Hat: case DiceVoiceKind::Shaker: case DiceVoiceKind::OpenHat:
        lanes.hats |= mask; break;
      case DiceVoiceKind::Snare: if (!lanes.snare) lanes.snare = mask; break;
      case DiceVoiceKind::Perc: case DiceVoiceKind::Rim: if (!lanes.perc) lanes.perc = mask; break;
      default: break;
    }
  }
  return lanes;
}

// Timing and accents belong to the outgoing hit. A ratchet, condition or sound lock
// belongs to the authored phrase, so leave the whole step alone when one is present.
bool canReplace(const Step& step, uint16_t allowed) {
  if (step.isEmpty()) return true;
  if (!step.hasNote() || (laneMask(step) & ~allowed)) return false;
  for (const auto& fx : step.fx)
    if (fx.cmd != Fx::None && fx.cmd != Fx::ACC && fx.cmd != Fx::AC2 && fx.cmd != Fx::NDG) return false;
  return true;
}

void fillHit(Step& step, uint16_t lane, int velocity) {
  step = Step{};
  step.note = velocity;
  setLaneMask(step, lane);
}

void snarePickup(Step* row, int end, int beat, int last, const FillLanes& lanes, int peak) {
  // A shared KIT ratchet must land on a single snare. Keep the opening ghost
  // optional if the previous offbeat already carries a kick or a locked hit.
  for (int at = last; at > end - beat; --at) {
    if (!(at % beat) || !canReplace(row[at], lanes.snare | lanes.hats)) continue;
    fillHit(row[at], lanes.snare, std::max(2, peak * 72 / 100));
    putFx(row[at], Fx::RAT, (kRatUp << 4) | 2);
    const int ghost = at - beat;
    if (ghost >= end / 2 && canReplace(row[ghost], lanes.hats))
      fillHit(row[ghost], lanes.snare, std::max(1, peak * 38 / 100));
    return;
  }
}

void percussionAnswer(Step* row, int end, int beat, int sixteenth, const FillLanes& lanes, int peak) {
  const int last = end - sixteenth;
  const int first = end - beat + sixteenth, second = end - std::max(1, beat / 2);
  if (first >= second || first % beat == 0 || second % beat == 0 ||
      !canReplace(row[first], lanes.hats | lanes.perc) || !canReplace(row[second], lanes.hats | lanes.perc)) return;
  fillHit(row[first], lanes.perc, std::max(1, peak * 56 / 100));
  fillHit(row[second], lanes.perc, std::max(2, peak * 78 / 100));
  if (last > second && last % beat && canReplace(row[last], lanes.hats)) row[last] = Step{};
}

}  // namespace

void drumFill(DicePat& dst, const DicePat& next, const Pattern& priorNext, const DiceParts& parts,
              const DiceLayout& layout, const DiceVariant& variant, const PhrasePlan& plan, uint8_t written) {
  const int beat = kPpqn / ticksPerStep(dst.res);
  const int end = dst.length, bar = std::min(end, 4 * beat);
  if (beat < 2 || end < 2 * beat) return;
  Step* row = dst.steps[0];
  uint16_t played = 0;
  int hits = 0, peak = 0;
  for (int at = end - bar; at < end; ++at) {
    if (!row[at].hasNote()) continue;
    ++hits;
    played |= laneMask(row[at]);
    peak = std::max(peak, int(row[at].note));
  }
  if (hits * 4 < bar || !played) return;
  const auto lanes = fillLanes(parts.instruments[parts.tracks[0].instr], played, layout, variant);
  const bool bass = earlyBass(next, priorNext, parts, layout, variant, written);
  const bool busy = hits * 4 >= bar * 3;
  Rng rng(plan.seed);
  const int choice = rng.below(100);
  const int keep = (plan.feel == DiceFeel::Air || plan.feel == DiceFeel::Echo ? 36 : 12)
                   + std::max(0, 25 - plan.amount);
  if (choice < keep) return;

  const int sixteenth = std::max(1, (beat + 2) / 4);
  const int last = end - sixteenth;
  if (choice < keep + (busy && bass ? 30 : 10)) {
    // A breath before the downbeat. An anchored kick or backbeat still plays.
    for (int at = end - std::max(1, beat / 2); at < end; ++at)
      if (at % beat && canReplace(row[at], lanes.hats)) row[at] = Step{};
    return;
  }
  if (lanes.snare && (!lanes.perc || (busy && bass) || rng.below(2)))
    snarePickup(row, end, beat, last, lanes, peak);
  else if (lanes.perc)
    percussionAnswer(row, end, beat, sixteenth, lanes, peak);
}

void voiceArpeggios(DicePat& dst, const Pattern& prior, const DiceParts& parts, const DiceLayout& layout,
                    const DiceVariant& variant, uint8_t written) {
  for (int t = 0; t < kDiceRoles; ++t) {
    if (!(written & (1u << t)) || !isArp(layout.source[t], variant)) continue;
    const int floor = bassFloor(dst, prior, parts, layout, variant, written);
    if (!floor) return;
    const int threshold = floor - transpose(parts, t);
    for (int at = 0; at < dst.length; ++at) {
      auto& step = dst.steps[t][at];
      if (!step.hasNote()) continue;
      while (step.note < threshold) step.note += 12;
    }
  }
}

void phraseExchange(DicePat& dst, const DiceParts& parts, const DiceLayout& layout, const DiceVariant& variant,
                    const DiceRecipe& recipe, const PhrasePlan& plan, uint8_t written) {
  const int ticks = ticksPerStep(dst.res), bar = 4 * kPpqn / ticks;
  const int start = std::max(dst.length / 2, dst.length - bar * (plan.amount < 25 ? 1 : 2));
  int tracks[2], count = 0;
  for (int rank = 0; rank < 3 && count < 2; ++rank)
    for (int t = 0; t < kDiceRoles && count < 2; ++t) {
      if (!(written & (1u << t)) || !hasMelodicOutput(parts, t) || dst.trackSpeed[t] ||
          (dst.trackLen[t] && dst.trackLen[t] < dst.length))
        continue;
      if (exchangePriority(layout.source[t], variant, plan) != rank) continue;
      if (std::any_of(dst.steps[t] + start, dst.steps[t] + dst.length,
                      [](const Step& s) { return s.hasNote(); }))
        tracks[count++] = t;
    }
  if (count < 2) return;
  const int caller = tracks[0], reply = tracks[1];
  const int callOffset = transpose(parts, caller), replyOffset = transpose(parts, reply);
  for (int begin = start; begin < dst.length;) {
    const int end = std::min<int>(dst.length, (begin / bar + 1) * bar);
    auto* callRow = dst.steps[caller];
    auto* replyRow = dst.steps[reply];
    const auto call = halfPhrase(callRow, begin, end, false), answer = halfPhrase(replyRow, begin, end, true);
    if (call.count && answer.count) {
      const int anchor = std::max(48, std::min(84, int(call.notes[0].note) + callOffset));
      std::fill(callRow + begin, callRow + end, Step{});
      std::fill(replyRow + begin, replyRow + end, Step{});
      replyRow[begin].note = kNoteOff;
      writeLine(callRow, call, (begin + end) / 2, anchor, callOffset, false, parts, variant, recipe, plan,
                ticks);
      writeLine(replyRow, answer, end, anchor, replyOffset, true, parts, variant, recipe, plan, ticks);
    }
    begin = end;
  }
}
}  // namespace dice_detail
}  // namespace mt
