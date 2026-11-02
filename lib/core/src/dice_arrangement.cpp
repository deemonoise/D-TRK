#include "dice_arrangement.h"
#include <algorithm>
#include <cstring>
#include "automation.h"
#include "dice_music.h"
#include "gen_util.h"

namespace mt {
namespace {
enum class Movement { Peak, Broken, Footwork, Ebm, Space, Ambient };
enum class Transition { Rise, Hit, Tail, Texture, Percussion };

bool arranging(const DiceOpts& o, const DiceSnap& s) {
  return o.form == DiceForm::Arrange && o.change == DiceChange::Wild &&
         (o.targetMask & 0x3f) == 0x3f && s.patCount == kDiceMaxPats;
}
uint8_t writable(const DiceOpts& o, const DiceSnap& s) {
  return s.patMask & o.targetMask & static_cast<uint8_t>(~o.lockMask) & 0x3f;
}
Movement movement(const DiceOpts& o, const DiceArrangeSpec& s) {
  if (s.genre == 7) return Movement::Ambient;
  if ((s.genre == 1 && o.style <= 1) || (s.genre == 4 && o.style == 3)) return Movement::Space;
  if (s.genre == 1 && o.style == 6) return Movement::Ebm;
  if (s.genre == 6 && o.style == 3) return Movement::Footwork;
  return s.genre == 3 || s.genre == 4 || s.genre == 5 || s.genre == 6
             ? Movement::Broken : Movement::Peak;
}
bool named(const Instrument& sound, const char* name) { return std::strcmp(sound.name, name) == 0; }
Transition transition(const Instrument& sound) {
  if (named(sound, "RISER") || named(sound, "UPLIFTER") || named(sound, "SWEEPUP") || named(sound, "NOISESWP"))
    return Transition::Rise;
  if (named(sound, "IMPACT") || named(sound, "LASER") || named(sound, "COIN") ||
      (sound.type == InstrType::Synth && named(sound, "ZAP"))) return Transition::Hit;
  if (named(sound, "DOWNLIFT") || named(sound, "WHOOSH")) return Transition::Tail;
  if (named(sound, "WIND") || named(sound, "SPACEDRN") || named(sound, "SCIFI") ||
      named(sound, "ROBOT") || named(sound, "SIREN")) return Transition::Texture;
  return Transition::Percussion;
}
int attacks(const Step* row, int len) {
  int n = 0;
  for (int i = 0; i < len; ++i) n += row[i].hasNote();
  return n;
}
void silence(Step* row) {
  for (int i = 0; i < kMaxSteps; ++i) row[i] = Step();
  row[0].note = kNoteOff;
}
void stopAt(Step* row, int len, int cutoff) {
  cutoff = std::max(0, std::min(len - 1, cutoff));
  for (int i = cutoff; i < len; ++i) row[i] = Step();
  row[cutoff].note = kNoteOff;
}
uint16_t laneBit(int lane) { return lane >= 0 ? static_cast<uint16_t>(1u << lane) : 0; }
uint16_t anchorMask(const DiceArrangeSpec& spec) {
  return laneBit(spec.lanes[kDiceKick]) | laneBit(spec.lanes[kDiceSnare]) | laneBit(spec.lanes[kDiceClap]);
}
uint16_t topMask(const DiceArrangeSpec& spec) {
  return (laneBit(spec.lanes[kDiceHat]) | laneBit(spec.lanes[kDiceOpen]) | laneBit(spec.lanes[kDicePerc])) &
         static_cast<uint16_t>(~anchorMask(spec));
}
void retainLanes(Step& step, uint16_t keep) {
  if (!step.hasNote()) return;
  setLaneMask(step, laneMask(step) & keep);
  if (!laneMask(step)) step = Step();
}
void thinTops(DicePat& pat, uint16_t tops, int every, int from = 0, int to = -1) {
  int seen[kKitLanes] = {};
  for (int i = from; i < (to < 0 ? pat.length : to); ++i) {
    Step& step = pat.steps[0][i];
    if (!step.hasNote()) continue;
    uint16_t keep = laneMask(step);
    for (int lane = 0; lane < kKitLanes; ++lane)
      if ((tops & (1u << lane)) && laneOn(step, lane) && seen[lane]++ % every) keep &= ~(1u << lane);
    retainLanes(step, keep);
  }
}
void extendBuild(DiceSnap& snap, const DiceOpts& o, uint8_t mask) {
  if (o.len || snap.preserveHeaders) return;
  DicePat& build = snap.parts.pats[kArrangeBuild];
  const int old = std::max(1, static_cast<int>(build.length));
  const int length = std::min(kMaxSteps, 8 * 4 * kPpqn / ticksPerStep(build.res));
  if (length <= old) return;
  for (int role = 0; role < kDiceRoles; ++role) if (mask & (1u << role)) {
    std::memcpy(snap.work.midiSteps, build.steps[role], sizeof(snap.work.midiSteps));
    for (int i = 0; i < length; ++i) build.steps[role][i] = snap.work.midiSteps[i % old];
    build.trackLen[role] = 0;
  }
  build.length = static_cast<uint8_t>(length);
}
void stagedCounter(DiceSnap& snap, int role) {
  DicePat& build = snap.parts.pats[kArrangeBuild];
  const int half = build.length / 2;
  if (half < 1) return;
  Step* row = build.steps[role];
  std::memcpy(snap.work.midiSteps, row, sizeof(snap.work.midiSteps));
  dice_detail::thinPhrase(snap.work.midiSteps, half, std::max(1, attacks(row, half) / 2));
  for (int i = 0; i < half; ++i)
    if (row[i].hasNote() && !snap.work.midiSteps[i].hasNote()) snap.work.midiSteps[i].note = kNoteOff;
  dice_detail::retainAttacks(row, build.length, snap.work.midiSteps);
}
void melodicSections(DiceSnap& snap, Movement kind, uint8_t mask, uint8_t nativeRoles) {
  auto& d = snap.parts;
  int counter = -1;
  for (int role : {4, 3, 2})
    if ((nativeRoles & mask & (1u << role)) && attacks(d.pats[kArrangeDrop].steps[role], d.pats[kArrangeDrop].length) >= 2) {
      counter = role; break;
    }
  for (int role = 1; role < kDiceRoles - 1; ++role) if (mask & (1u << role)) {
    DicePat& intro = d.pats[kArrangeIntro];
    DicePat& bridge = d.pats[kArrangeBreak];
    DicePat& build = d.pats[kArrangeBuild];
    DicePat& outro = d.pats[kArrangeOutro];
    if (role == 1) {
      // INTRO reserves the first half; BREAK removes the low end; BUILD leaves a real pre-drop gap.
      for (int i = 0; i < intro.length / 2; ++i) intro.steps[role][i] = Step();
      intro.steps[role][0].note = kNoteOff;
      silence(bridge.steps[role]);
      const int beat = std::max(1, kPpqn / ticksPerStep(build.res));
      if (kind == Movement::Space || kind == Movement::Ambient) silence(build.steps[role]);
      else stopAt(build.steps[role], build.length,
                  kind == Movement::Ebm ? build.length - beat : build.length / 2);
      // The final beat also carries an explicit release, regardless of earlier withdrawal.
      stopAt(build.steps[role], build.length, build.length - beat);
    } else {
      if (!(nativeRoles & (1u << role))) silence(intro.steps[role]);
      else dice_detail::thinPhrase(intro.steps[role], intro.length,
                                   std::max(1, attacks(intro.steps[role], intro.length) / 4));
      if (role != 2 && kind != Movement::Ambient && kind != Movement::Space) silence(bridge.steps[role]);
      else dice_detail::thinPhrase(bridge.steps[role], bridge.length,
                                   std::max(1, attacks(bridge.steps[role], bridge.length) / 4));
    }
    dice_detail::endPhrase(outro.steps[role], outro.length, std::max(1, static_cast<int>(outro.length) / 2));
  }
  if (counter >= 0) {
    DicePat& main = d.pats[kArrangeMain];
    dice_detail::thinPhrase(main.steps[counter], main.length,
                            std::max(1, attacks(main.steps[counter], main.length) / 2));
    stagedCounter(snap, counter);
  }
}
bool beatHasRoll(const Step* row, int len, int at, int beat) {
  const int from = at / beat * beat;
  for (int i = from; i < std::min(len, from + beat); ++i)
    if (row[i].find(Fx::RAT)) return true;
  return false;
}
void drumSections(DiceParts& d, const DiceArrangeSpec& spec, Movement kind) {
  const uint16_t kick = laneBit(spec.lanes[kDiceKick]), tops = topMask(spec);
  DicePat& intro = d.pats[kArrangeIntro];
  int lastBar = -1;
  const int barSteps = std::max(1, 4 * kPpqn / ticksPerStep(intro.res));
  for (int i = 0; i < intro.length; ++i) {
    uint16_t keep = i >= intro.length / 2 ? tops : 0;
    if (intro.steps[0][i].hasNote() && (laneMask(intro.steps[0][i]) & kick) && i / barSteps != lastBar) {
      keep |= kick; lastBar = i / barSteps;
    }
    retainLanes(intro.steps[0][i], keep);
  }
  thinTops(intro, tops, 2);
  thinTops(d.pats[kArrangeMain], tops, 2);
  DicePat& bridge = d.pats[kArrangeBreak];
  for (int i = 0; i < bridge.length; ++i) retainLanes(bridge.steps[0][i], tops);
  thinTops(bridge, tops, kind == Movement::Ambient ? 8 : 4);
  DicePat& build = d.pats[kArrangeBuild];
  thinTops(build, tops, 2, 0, build.length / 2);
  const int beat = std::max(1, kPpqn / ticksPerStep(build.res));
  for (int i = build.length - beat; i < build.length; ++i)
    retainLanes(build.steps[0][i], static_cast<uint16_t>(~kick));
  // Local punctuation belongs to energetic styles; space/ambient never receive a compulsory roll.
  if (!(spec.midiMask & 1) && kind != Movement::Space && kind != Movement::Ambient)
    for (int i = build.length * 3 / 4; i < build.length - beat; ++i) {
      Step& st = build.steps[0][i];
      const uint16_t lanes = st.hasNote() ? laneMask(st) : 0;
      if (lanes && !(lanes & (lanes - 1)) && (lanes & tops) &&
          !beatHasRoll(build.steps[0], build.length, i, beat))
        putFx(st, Fx::RAT, static_cast<uint8_t>((kRatUp << 4) | 2));
    }
  DicePat& outro = d.pats[kArrangeOutro];
  for (int i = outro.length / 2; i < outro.length; ++i) retainLanes(outro.steps[0][i], kick);
}
bool automationRoom(const Step& step, Fx fx) {
  if (step.find(fx)) return false;  // preserve authored parameter locks as well as note articulation
  for (const auto& slot : step.fx) if (slot.cmd == Fx::None) return true;
  return false;
}
void buildAutomation(DiceParts& d, const DiceArrangeSpec& spec, Movement kind, uint8_t mask) {
  for (int role : {4, 3, 2}) {
    if (!(mask & spec.nativeRoles & (1u << role))) continue;
    const TrackCfg& track = d.tracks[role];
    const Instrument& sound = d.instruments[track.instr % kInstruments];
    if (track.out != TrackOut::Int || sound.type == InstrType::Kit) continue;
    DicePat& build = d.pats[kArrangeBuild];
    Step& arrival = d.pats[kArrangeDrop].steps[role][0];
    for (Fx fx : {kind == Movement::Space ? Fx::DLY : kind == Movement::Ambient ? Fx::RVB : Fx::FLT,
                  Fx::RVB}) {
      if (fx == Fx::FLT && sound.fltMode == static_cast<uint8_t>(FltMode::Off)) continue;
      const int baseline = automationValue(track, sound, fx);
      if (baseline < 0 || !automationRoom(arrival, fx)) continue;
      const int from = fx == Fx::FLT ? std::max(0, baseline - 24) : baseline;
      const int to = std::min(127, baseline + (fx == Fx::FLT ? 12 : 28));
      if (from == to) continue;
      bool room = true;
      for (int i = 0; i < build.length; ++i) room &= automationRoom(build.steps[role][i], fx);
      if (!room) continue;
      if (automationRamp(build.steps[role], build.length, fx, 0, build.length - 1, from, to)) {
        automationSet(arrival, fx, baseline);
        return;
      }
    }
  }
}
void fxEvent(Step* row, int len, const Step& source, int end, bool held, int start = 0) {
  for (int i = 0; i < kMaxSteps; ++i) row[i] = Step();
  if (!source.hasNote()) return;
  row[start].note = source.note; row[start].vel = source.vel;
  putFx(row[start], Fx::GAT, gateValue(held ? 800 : 100));
  if (held && end > start) putFx(row[start], Fx::TIE, 0);
  if (end > 0 && end < len) row[end].note = kNoteOff;
}
void semanticFx(DiceParts& d, Transition kind, bool native) {
  Step source;
  const DicePat& drop = d.pats[kArrangeDrop];
  for (int i = 0; i < drop.length; ++i) if (drop.steps[5][i].hasNote()) { source = drop.steps[5][i]; break; }
  // Legacy generators intentionally leave FX-category instruments unscheduled. ARRANGE owns their
  // semantic placement; a percussion source still requires an actual rhythmic phrase.
  if (!source.hasNote() && native && kind != Transition::Percussion) {
    source.note = static_cast<uint8_t>(60 + d.scaleRoot % 12);
    source.vel = 90;
  }
  if (!source.hasNote()) return;
  if (kind == Transition::Percussion) {
    for (int section : {kArrangeIntro, kArrangeBreak}) {
      auto& pat = d.pats[section];
      dice_detail::thinPhrase(pat.steps[5], pat.length, std::max(1, attacks(pat.steps[5], pat.length) / 4));
    }
    auto& outro = d.pats[kArrangeOutro];
    dice_detail::endPhrase(outro.steps[5], outro.length, std::max(1, static_cast<int>(outro.length) / 2));
    return;
  }
  for (auto& pat : d.pats) silence(pat.steps[5]);
  if (kind == Transition::Rise) {
    auto& build = d.pats[kArrangeBuild];
    const int barSteps = 4 * kPpqn / ticksPerStep(build.res);
    // Preset envelope times are fixed: cue the one-shot near arrival instead of holding a long plateau.
    const int onset = std::max(0, static_cast<int>(build.length) - barSteps);
    fxEvent(build.steps[5], build.length, source, build.length - 1, true, onset);
  } else if (kind == Transition::Hit) {
    auto& arrival = d.pats[kArrangeDrop];
    fxEvent(arrival.steps[5], arrival.length, source, -1, false);
  } else if (kind == Transition::Tail) {
    auto& outro = d.pats[kArrangeOutro];
    fxEvent(outro.steps[5], outro.length, source, std::max(1, static_cast<int>(outro.length) / 2), true);
  } else for (int section : {kArrangeIntro, kArrangeBreak, kArrangeOutro}) {
    auto& pat = d.pats[section];
    const int end = section == kArrangeOutro ? std::max(1, static_cast<int>(pat.length) / 2) : pat.length - 1;
    fxEvent(pat.steps[5], pat.length, source, end, true);
  }
}
}  // namespace

void diceArrangeDraft(const Project&, const DiceOpts& options, const DiceArrangeSpec& spec,
                      uint32_t seed, DiceSnap& snap) {
  (void)seed;  // section decisions deliberately reuse the chosen theme, rather than rerolling melodies
  if (!arranging(options, snap)) return;
  const uint8_t mask = writable(options, snap);
  const Movement kind = movement(options, spec);
  extendBuild(snap, options, mask);
  melodicSections(snap, kind, mask, spec.nativeRoles);
  if ((mask & spec.nativeRoles & 1)) drumSections(snap.parts, spec, kind);
  buildAutomation(snap.parts, spec, kind, mask);
  if (mask & 32) {
    const Instrument& sound = snap.parts.instruments[snap.parts.tracks[5].instr % kInstruments];
    semanticFx(snap.parts, transition(sound), (spec.nativeRoles & 32) != 0);
  }
}

void diceArrangeSong(const Project& original, const DiceOpts& options, const DiceArrangeSpec&,
                     DiceSnap& snap) {
  if (!arranging(options, snap)) return;
  const uint16_t managed = writable(options, snap);
  uint16_t protectedMute = 0;
  for (int track = 0; track < kTracks; ++track)
    if (!(managed & (1u << track)) && original.tracks[track].mute) protectedMute |= 1u << track;
  auto& song = snap.parts.song;
  int slot = 0;
  const int targetBars[kDiceMaxPats] = {8, 16, 8, 0, 16, 8};
  for (int section = 0; section < kDiceMaxPats; ++section) {
    const auto& pat = snap.parts.pats[section];
    const int ticks = std::max(1, static_cast<int>(pat.length) * ticksPerStep(pat.res));
    const int repeats = section == kArrangeBuild ? 1 :
        std::min(16, std::max(1, (targetBars[section] * 4 * kPpqn + ticks - 1) / ticks));
    for (int repeat = 0; repeat < repeats && slot < kSongMax; ++repeat, ++slot) {
      song.song[slot] = snap.first + section;
      song.songMute[slot] = (original.songMute[slot] & ~managed) | protectedMute;
      song.songFol[slot] = SongFollow();
      song.songTrn[slot] = 0;
      song.songBpm[slot] = 0;
    }
  }
  song.songLen = static_cast<uint8_t>(slot);
}
}  // namespace mt
