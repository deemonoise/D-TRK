#include "dice_dub.h"
#include "gen_util.h"
#include <algorithm>
#include <cstdio>

namespace mt::dice_detail {
namespace {

struct Voice {
  const char* name;
  SynOsc primary, upper;
  int8_t interval;
  uint8_t blend, detune, attack, decay, sustain, release, cutoff, level;
  uint8_t delay, reverb, volume, pan, cutoffRate, cutoffDepth;
};

constexpr Voice motif{"MOTIF", SynOsc::Tri, SynOsc::Square, 12, 25, 65, 27, 61, 45, 59, 65, 66,
                      59, 38, 111, 64, 10, 8};
constexpr Voice bed{"BED", SynOsc::Tri, SynOsc::Tri, 0, 56, 72, 70, 79, 76, 77, 52, 54,
                    25, 68, 76, 64, 11, 9};
constexpr Voice figure{"FIGURE", SynOsc::Sine, SynOsc::Tri, 12, 31, 64, 14, 55, 37, 58, 83, 70,
                       71, 55, 68, 82, 9, 10};

void voice(DiceParts& parts, int track, const Voice& spec) {
  auto& config = parts.tracks[track];
  auto& sound = parts.instruments[config.instr];
  sound = Instrument{};
  instrSetType(sound, InstrType::Synth);
  sound.synOsc[0] = uint8_t(spec.primary);
  sound.synOsc[1] = uint8_t(spec.upper);
  sound.synSemi = spec.interval;
  sound.synPhaseMode = uint8_t(SynPhaseMode::Free);
  sound.macro[kMacMix] = spec.blend;
  sound.macro[kMacShp2] = 36;
  sound.macro[kMacDet] = spec.detune;
  sound.attack = spec.attack;
  sound.decay = spec.decay;
  sound.sustain = spec.sustain;
  sound.release = spec.release;
  sound.fltMode = uint8_t(FltMode::Lp);
  sound.cutoff = spec.cutoff;
  sound.reso = 19;
  sound.send = spec.delay;
  sound.rsend = spec.reverb;
  sound.vol = spec.level;
  sound.lfoWave = uint8_t(LfoWave::Sine);
  sound.lfoDest = uint8_t(LfoDest::Cutoff);
  sound.lfoSync = kLfoTempo | kLfoFree;
  sound.lfoRate = spec.cutoffRate;
  sound.lfoDepth = spec.cutoffDepth;
  if (spec.attack > 50)
    sound.lfo[0] = {uint8_t(LfoWave::Tri), 10, 15, uint8_t(LfoDest::Pan), kLfoTempo | kLfoFree};
  std::snprintf(sound.name, sizeof(sound.name), "%s", spec.name);
  std::snprintf(config.name, sizeof(config.name), "%s", spec.name);
  config.vol = spec.volume;
  config.pan = spec.pan;
  config.drive = 0;
}

int firstAttack(const Step* row, int start, int end) {
  for (int at = start; at < end; ++at)
    if (row[at].hasNote()) return at;
  return -1;
}

Step motifStep(const Step& guide, int bar, bool accent, bool phraseEnd) {
  constexpr int colour[] = {65, 61, 68, 63};
  Step result;
  result.note = guide.note >= 64 ? guide.note - 12 : guide.note;
  result.vel = std::clamp(int(guide.vel) + (accent ? -3 : -25), 52, 111);
  putFx(result, Fx::CHD, accent ? kChordAdd9 : chordValue(kChordTriad, 1));
  putFx(result, Fx::GAT, gateValue(accent ? 174 : 93));
  putFx(result, Fx::FLT, colour[bar % 4] - (accent ? 0 : 5));
  putFx(result, Fx::DLY, phraseEnd ? 100 : accent ? 61 : 46);
  if (const auto* nudge = guide.find(Fx::NDG)) putFx(result, Fx::NDG, nudge->val);
  return result;
}

void voiceMotif(Step* row, Step* harmony, int length, int barSteps, uint32_t seed,
                DiceDensity density, bool lift) {
  for (int start = 0, bar = 0; start < length; start += barSteps, ++bar) {
    const int end = std::min(start + barSteps, length);
    const int first = firstAttack(row, start, end);
    if (first < 0) continue;
    const int second = firstAttack(row, std::min(end, first + (barSteps + 2) / 3), end);
    const bool answer = second >= 0 && density != DiceDensity::Sparse &&
                        ((bar + (seed >> 8) + lift) % 2 == 0 || density == DiceDensity::Busy);
    const bool phraseEnd = end == length || (bar % 4 == 3);
    const auto note = motifStep(row[first], bar, true, phraseEnd && !answer);
    const auto reply = answer ? motifStep(row[second], bar, false, phraseEnd) : Step{};
    std::fill(row + start, row + end, Step{});
    row[first] = note;
    if (answer) row[second] = reply;

    // The held voicing comes from this bar's own chord, with a quieter
    // entrance before or around the offbeat motif. Long gates leave room
    // for the filtered attack to bloom.
    auto& held = harmony[start];
    held.note = note.note >= 54 ? note.note - 12 : note.note;
    held.vel = lift ? 72 : 62;
    putFx(held, Fx::GAT, gateValue(lift ? 750 : 610));
    putFx(held, Fx::CHD, kChordSeventh);
    putFx(held, Fx::FLT, (lift ? 55 : 50) + bar % 4);
  }
}

void voiceFigure(Step* row, int length, bool lift, bool quiet) {
  for (int at = 0; at < length; ++at) {
    auto& step = row[at];
    if (quiet || (!lift && at < length / 2)) {
      step = Step{};
      continue;
    }
    if (!step.hasNote()) continue;
    const int note = step.note;
    const int velocity = step.vel;
    step = Step{};
    step.note = note;
    step.vel = std::clamp(velocity + 8, 58, 97);
    putFx(step, Fx::GAT, gateValue(122));
    putFx(step, Fx::PAN, at < length / 2 ? 39 : 88);
  }
}

}  // namespace

void produceDubChords(DiceParts& parts, const DiceLayout& layout, const DiceVariant& variant,
                      int sections, uint32_t seed, DiceDensity density) {
  int chord = -1, harmony = -1, counter = -1;
  uint8_t quiet = 0;
  for (int t = 0; t < kDiceRoles; ++t) {
    const int source = layout.source[t];
    if (diceSupplement(source)) {
      const auto part = dicePartKind(source);
      if (part == DicePart::Lead) counter = t;
      else if (part != DicePart::Bass) quiet |= 1u << t;
    }
    if (source < 0 || diceVoice(variant, source).kind != uint8_t(DiceVoiceKind::Stab)) continue;
    if (chord < 0) chord = t;
    else if (harmony < 0) harmony = t;
  }
  if (harmony < 0 || parts.tracks[chord].instr >= kInstruments ||
      parts.tracks[harmony].instr >= kInstruments) return;
  if (counter >= 0 && parts.tracks[counter].instr >= kInstruments) counter = -1;
  voice(parts, chord, motif);
  voice(parts, harmony, bed);
  if (counter >= 0) voice(parts, counter, figure);
  for (int t = 0; t < kDiceRoles; ++t)
    if (quiet & (1u << t)) parts.tracks[t].mute = true;

  for (int section = 0; section < sections; ++section) {
    auto& pat = parts.pats[section];
    const int barSteps = std::max(1, 4 * kPpqn / ticksPerStep(pat.res));
    const bool lift = sections == kDicePats ? section == 2 : section == kArrangeBuild || section == kArrangeDrop;
    std::fill_n(pat.steps[harmony], kMaxSteps, Step{});
    voiceMotif(pat.steps[chord], pat.steps[harmony], pat.length, barSteps, seed, density, lift);
    if (counter >= 0) voiceFigure(pat.steps[counter], pat.length, lift, section == 0 || section == sections-1);
    for (int t = 0; t < kDiceRoles; ++t)
      if (quiet & (1u << t)) std::fill_n(pat.steps[t], kMaxSteps, Step{});
  }
}

}  // namespace mt::dice_detail
