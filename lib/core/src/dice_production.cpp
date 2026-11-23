#include "dice_production.h"
#include "gen_util.h"
#include <algorithm>
#include <cstring>

namespace mt::dice_detail {
namespace {

enum class Role { Kit, Bass, Hook, Bed, Answer, Other };

Role roleOf(int source, const DiceVariant& variant, const PhrasePlan& plan, const Instrument& sound) {
  if (source == kDiceKitTrack) return Role::Kit;
  if (diceSupplement(source)) {
    switch (dicePartKind(source)) {
      case DicePart::Bass: return Role::Bass;
      case DicePart::Lead: return plan.leadSource < 0 ? Role::Hook : Role::Answer;
      case DicePart::Chords: case DicePart::Pad: case DicePart::Texture: return Role::Bed;
      default: return Role::Answer;
    }
  }
  if (source < 0) return Role::Other;
  const auto& voice = diceVoice(variant, source);
  const auto kind = static_cast<DiceVoiceKind>(voice.kind);
  if (kind == DiceVoiceKind::Bass) return Role::Bass;
  if (!std::strcmp(voice.name, "Arp")) return Role::Answer;
  if (source == plan.leadSource || !std::strcmp(sound.name, "303")) return Role::Hook;
  if (kind == DiceVoiceKind::Pad || diceChord(voice) != 255) return Role::Bed;
  return Role::Answer;
}

uint16_t kickLanes(const DiceLayout& layout, const DiceVariant& variant) {
  uint16_t lanes = 0;
  for (int source = 0; source < kDiceRoles; ++source)
    if ((layout.drums & (1u << source)) && diceVoice(variant, source).kind == uint8_t(DiceVoiceKind::Kick) &&
        diceEventCount(diceVoice(variant, source))) lanes |= 1u << layout.lane[source];
  return lanes;
}

void fader(TrackCfg& cfg, int percent) {
  cfg.vol = std::min(127, (cfg.vol * percent + 50) / 100);
}

void dry(Instrument& sound) {
  sound.send = 0;
  sound.rsend = 0;
}

struct BassTone {
  SynOsc main, colour;
  int8_t semitones;
  uint8_t blend, cutoff, resonance;
  int8_t filterDepth;
  uint8_t filterDecay, attack, decay, sustain, release, level;
};

void focusedBass(Instrument& sound, TrackCfg& cfg, TimbreProfile profile) {
  // The sine carries the played note. A quieter upper oscillator makes
  // the pitch followable on small speakers without a wide saw spectrum.
  static constexpr BassTone jump{SynOsc::Sine, SynOsc::Square, 12, 32, 65, 40, 18, 34, 14, 54, 86, 38, 103};
  static constexpr BassTone psy{SynOsc::Sine, SynOsc::Tri, 12, 35, 68, 12, 5, 24, 10, 47, 82, 16, 88};
  static constexpr BassTone dub{SynOsc::Sine, SynOsc::Tri, 12, 36, 67, 18, 7, 44, 21, 64, 93, 52, 101};
  static constexpr BassTone liquid{SynOsc::Sine, SynOsc::Tri, 0, 28, 57, 12, 8, 37, 14, 54, 86, 38, 96};
  const auto& tone = profile == TimbreProfile::JumpUp ? jump : profile == TimbreProfile::DarkPsy ? psy
                   : profile == TimbreProfile::DubTechno ? dub : liquid;
  sound.synOsc[0] = uint8_t(tone.main);
  sound.synOsc[1] = uint8_t(tone.colour);
  sound.synSemi = tone.semitones;
  sound.synSub = 0;  // The fundamental is already in the bass register.
  sound.synRing = sound.synFold = sound.synNoise = 0;
  sound.synPhaseMode = uint8_t(SynPhaseMode::Retrig);
  if (profile == TimbreProfile::DarkPsy || profile == TimbreProfile::DubTechno)
    sound.synPhase[0] = sound.synPhase[1] = 0;
  sound.macro[kMacDet] = 64;
  sound.macro[kMacMix] = tone.blend;
  sound.synEnvDest = uint8_t(SynEnvDest::Mix);
  sound.macro[kMacSenv] = 64;
  sound.fltMode = uint8_t(FltMode::Lp);
  sound.cutoff = tone.cutoff;
  sound.reso = tone.resonance;
  sound.fAtk = 0;
  sound.fenv = tone.filterDepth;
  sound.fDec = tone.filterDecay;
  sound.velCut = 4;
  sound.keytrack = 0;
  sound.attack = tone.attack;
  sound.decay = tone.decay;
  sound.sustain = tone.sustain;
  sound.release = tone.release;
  sound.glide = 0;
  sound.lfoDepth = 0;
  sound.synSatType = uint8_t(SynSatType::Off);
  sound.vol = tone.level;
  cfg.drive = 0;
  cfg.pan = 64;
  dry(sound);
}

void jumpVowel(Instrument& sound, TrackCfg& cfg) {
  sound.synOsc[0] = uint8_t(SynOsc::Saw);
  sound.synOsc[1] = uint8_t(SynOsc::Saw);
  sound.synSemi = 12;
  sound.macro[kMacMix] = 41;
  sound.macro[kMacDet] = 64;
  sound.synEnvDest = uint8_t(SynEnvDest::Shape);
  sound.macro[kMacSenv] = 73;
  sound.fltMode = uint8_t(FltMode::Formant);
  sound.cutoff = 61;
  sound.reso = 35;
  sound.fenv = 9;
  sound.fDec = 35;
  sound.velCut = 0;
  sound.attack = 6;
  sound.decay = 49;
  sound.sustain = 65;
  sound.release = 33;
  sound.lfoDest = uint8_t(LfoDest::Cutoff);
  sound.lfoWave = uint8_t(LfoWave::Tri);
  sound.lfoRate = 7;
  sound.lfoDepth = 31;
  sound.lfoSync = kLfoTempo | kLfoFree;
  sound.vol = 120;
  sound.send = 8;
  sound.rsend = 13;
  cfg.drive = 0;
  fader(cfg, 130);
}

void movingPsyFx(Instrument& sound, TrackCfg& cfg) {
  if (sound.type == InstrType::Synth) {
    sound.synOsc[0] = uint8_t(SynOsc::Square);
    sound.synOsc[1] = uint8_t(SynOsc::Saw);
    sound.synSemi = 7;
    sound.synRing = 24;
  }
  sound.fltMode = uint8_t(FltMode::Bp);
  sound.cutoff = 91;
  sound.reso = 57;
  sound.fenv = -18;
  sound.fDec = 48;
  sound.lfoDest = uint8_t(LfoDest::Cutoff);
  sound.lfoWave = uint8_t(LfoWave::Saw);
  sound.lfoRate = 6;
  sound.lfoDepth = 28;
  sound.lfoSync = kLfoTempo;
  sound.lfo[0] = {uint8_t(LfoWave::Tri), 8, 64, uint8_t(LfoDest::Pan), kLfoTempo | kLfoFree};
  sound.send = 74;
  sound.rsend = 53;
  sound.vol = 103;
  cfg.pan = 64;
  fader(cfg, 136);
}

void roundKick(Instrument& sound, TimbreProfile profile) {
  const bool psy = profile == TimbreProfile::DarkPsy;
  const int transpose = std::clamp(int(sound.transpose), -2, 2);
  // The soft 808 body sits around 57 Hz. The synthetic kick gives a shorter
  // handoff to the sixteenth-note bass; dirt stays off on that model.
  constexpr uint8_t dub[] = {58, 10, 0, 26, 44};
  constexpr uint8_t dark[] = {84, 33, 0, 17, 27};
  instrSetType(sound, InstrType::Drum);
  drumSetMachine(sound, uint8_t(psy ? DrumMachine::Bds : DrumMachine::Bd8));
  std::copy_n(psy ? dark : dub, kFmMacros, sound.macro);
  sound.transpose = transpose + (psy ? 2 : 4);
  sound.fltMode = uint8_t(FltMode::Lp);
  sound.cutoff = psy ? 92 : 86;
  sound.reso = 0;
  sound.fenv = 0;
  sound.lfoDepth = 0;
  sound.keytrack = 0;
  sound.velCut = 0;
  if (!psy) sound.vol = std::min(127, sound.vol * 110 / 100);
}

void psyAnswer(Instrument& sound, TrackCfg& cfg, bool arp) {
  sound.fltMode = uint8_t(FltMode::Bp);
  sound.cutoff = arp ? 92 : 83;
  sound.reso = arp ? 31 : 47;
  sound.fenv = arp ? -12 : 16;
  sound.fDec = arp ? 28 : 51;
  sound.send = arp ? 44 : 28;
  sound.rsend = arp ? 36 : 24;
  sound.release = arp ? 41 : 31;
  cfg.pan = arp ? 88 : 40;
  fader(cfg, arp ? 114 : 131);
}

void focusDrums(DiceParts& parts, Instrument& kit, const DiceLayout& layout,
                const DiceVariant& variant, TimbreProfile profile) {
  kit.send = profile == TimbreProfile::MinimalTechno ? 0 : std::min<int>(kit.send, 8);
  kit.rsend = std::min<int>(kit.rsend, profile == TimbreProfile::MinimalTechno ? 6 : 14);
  for (int source = 0; source < kDiceRoles; ++source) {
    if (!(layout.drums & (1u << source))) continue;
    auto& lane = kit.kit[layout.lane[source]];
    if (lane.instr >= kInstruments) continue;
    const auto kind = static_cast<DiceVoiceKind>(diceVoice(variant, source).kind);
    auto& sound = parts.instruments[lane.instr];
    if (profile == TimbreProfile::JumpUp && kind == DiceVoiceKind::Snare)
      sound.vol = std::min(127, sound.vol * 116 / 100);
    if (profile == TimbreProfile::DarkPsy && (kind == DiceVoiceKind::Hat || kind == DiceVoiceKind::OpenHat))
      lane.vol = std::min(127, lane.vol * 150 / 100);
    if (kind == DiceVoiceKind::Kick &&
        (profile == TimbreProfile::DarkPsy || profile == TimbreProfile::DubTechno))
      roundKick(sound, profile);
  }
}

void minimalTone(Instrument& sound, Role role) {
  dry(sound);
  if (role == Role::Bass) {
    sound.release = std::min<int>(sound.release, 30);
    sound.reso = std::min<int>(sound.reso, 28);
  }
}

void dubChord(Instrument& sound, TrackCfg& cfg, bool first) {
  sound.fltMode = uint8_t(FltMode::Lp);
  sound.cutoff = std::clamp(int(sound.cutoff), 54, 72);
  sound.reso = std::clamp(int(sound.reso), 22, 38);
  sound.release = std::min<int>(sound.release, 37);
  sound.send = first ? 87 : 76;
  sound.rsend = first ? 37 : 29;
  sound.lfo[0] = {uint8_t(LfoWave::Tri), uint8_t(first ? 8 : 9), 13,
                  uint8_t(LfoDest::Cutoff), kLfoTempo | kLfoFree};
  fader(cfg, first ? 98 : 72);
}

void duckBass(TrackCfg& cfg) {
  cfg.folSrc = 1;
  cfg.folDest = uint8_t(LfoDest::Vol);
  cfg.folAmt = -26;
  cfg.folAtk = 0;
  cfg.folRel = 41;
}

void acidMix(Instrument& sound, TrackCfg& cfg) {
  sound.vol = std::max<int>(100, sound.vol);
  sound.fenv = std::clamp(int(sound.fenv) + 12, 24, 62);
  sound.reso = std::clamp(int(sound.reso), 61, 93);
  sound.send = std::min<int>(sound.send, 18);
  sound.rsend = std::min<int>(sound.rsend, 12);
  sound.glide = 15;
  cfg.pan = 64;
  fader(cfg, 116);
}

void industrialMix(Instrument& sound, TrackCfg& cfg, Role role) {
  if (role == Role::Kit) {
    cfg.driveModel = uint8_t(DriveModel::Asym);
    cfg.drive = std::max<int>(34, cfg.drive);
    sound.send = std::min<int>(sound.send, 8);
    sound.rsend = std::min<int>(sound.rsend, 21);
  } else if (role == Role::Bass || role == Role::Hook) {
    cfg.driveModel = uint8_t(DriveModel::Clip);
    cfg.drive = role == Role::Bass ? 20 : 27;
    if (role == Role::Hook) cfg.crushRate = 17;
  }
}

enum class Stage { Intro, Main, Turn, Outro };

Stage stageOf(DiceForm form, int section) {
  if (form == DiceForm::Classic) {
    if (section == kDiceBreak) return Stage::Intro;
    return section == kDiceFill ? Stage::Turn : Stage::Main;
  }
  if (section == 0 || (form == DiceForm::Arrange && section == kArrangeBreak)) return Stage::Intro;
  if (section == (form == DiceForm::Arrange ? kArrangeOutro : 3)) return Stage::Outro;
  return section == 1 || (form == DiceForm::Arrange && section == kArrangeDrop) ? Stage::Main : Stage::Turn;
}

void acidPhrase(Step* row, int length, int stepTicks, int base, Stage stage) {
  constexpr int sweep[] = {-8, -3, 3, 0};
  const int sectionLift = stage == Stage::Turn ? 10 : stage == Stage::Main ? 0 : -5;
  for (int at = 0; at < length; ++at) {
    auto& step = row[at];
    if (!step.hasNote()) continue;
    const int tick = at * stepTicks;
    const bool accent = step.vel >= 104 || tick % kPpqn == kPpqn / 2;
    const auto* authored = step.find(Fx::FLT);
    const int cutoff = (authored ? authored->val : base) + sweep[tick / (4 * kPpqn) % 4] + sectionLift;
    putFx(step, Fx::FLT, std::clamp(cutoff + (accent ? 7 : 0), 24, 112));
    if (accent) step.vel = std::min(127, step.vel + 9);
    // The following note starts while the mono voice still sounds, which
    // engages the synth's portamento. Gate the final note normally.
    if (stepTicks != kPpqn / 4 || tick % kPpqn != kPpqn / 2 || at + 1 == length) continue;
    const auto& next = row[at + 1];
    if (next.hasNote() && next.note != step.note && !next.find(Fx::PRB) &&
        !next.find(Fx::CND) && !step.find(Fx::RAT) && !next.find(Fx::RAT))
      putFx(step, Fx::TIE, 1);
  }
}

int melodicOctave(const Step* row, int length) {
  int sum = 0, count = 0, high = 0;
  for (int at = 0; at < length; ++at) {
    if (!row[at].hasNote()) continue;
    sum += row[at].note;
    high = std::max(high, int(row[at].note));
    ++count;
  }
  return count && sum < 59 * count && high <= 76 ? 12 : 0;
}

void liftHook(Step* row, int length, int stepTicks, Stage stage) {
  const int octave = melodicOctave(row, length);
  for (int at = 0; at < length; ++at) {
    auto& step = row[at];
    if (!step.hasNote()) continue;
    step.note += octave;
    const int tick = at * stepTicks;
    if (tick % kPpqn == 0) {
      step.vel = std::min(127, step.vel + 8);
      if (at + 1 < length && row[at + 1].isEmpty() && !step.find(Fx::TIE))
        putFx(step, Fx::GAT, gateValue(125));
    }
    if (stage == Stage::Intro) step.vel = (step.vel * 85 + 50) / 100;
  }
}

void seatTechnoBass(Step* row, const Step* kit, int length, uint16_t kicks) {
  for (int at = 0; at < length; ++at) {
    if (!row[at].hasNote() || !kit[at].hasNote() || !(laneMask(kit[at]) & kicks)) continue;
    const auto* nudge = row[at].find(Fx::NDG);
    const int delay = nudge ? static_cast<int8_t>(nudge->val) : 0;
    putFx(row[at], Fx::NDG, std::max(delay, 29));
  }
}

void rollBassFilter(Step* row, int length, int stepTicks, int base, Stage stage) {
  constexpr int contour[] = {-8, -4, 4, 0};
  const int lift = stage == Stage::Turn ? 9 : stage == Stage::Intro ? -9 : 0;
  for (int at = 0; at < length; ++at) {
    auto& step = row[at];
    if (!step.hasNote() || step.find(Fx::FLT)) continue;
    const int bar = at * stepTicks / (4 * kPpqn);
    putFx(step, Fx::FLT, std::clamp(base + contour[bar % 4] + lift, 24, 114));
  }
}

void separateDrones(Step* row, const Step* anchor, int length, const DiceParts& parts,
                    int transpose, int anchorTranspose) {
  int pitchClass = -1;
  for (int at = 0; at < length; ++at) {
    if (anchor[at].hasNote()) pitchClass = (anchor[at].note + anchorTranspose + 24) % 12;
    if (anchor[at].note == kNoteOff) pitchClass = -1;
    if (!row[at].hasNote()) continue;
    const int note = row[at].note + transpose;
    if ((note + 24) % 12 != pitchClass) continue;
    const auto mode = static_cast<ScaleType>(parts.scaleType);
    const int harmony = mode == ScaleType::Chromatic ? note - 5 : moveDegrees(note, -3, parts.scaleRoot, mode);
    row[at].note = std::clamp(harmony - transpose, 0, 127);
  }
}

bool layBack(DiceFamily family, int bar, Stage stage) {
  if (stage == Stage::Intro) return bar % 2 == 0;
  if (family == DiceFamily::Idm) return stage == Stage::Turn ? bar % 2 != 0 : bar % 2 == 0;
  return bar % 4 < (stage == Stage::Turn ? 1 : 2);
}

void leaveSpace(Step* row, int length, int stepTicks, DiceFamily family, Stage stage, DiceDensity density) {
  const int barTicks = 4 * kPpqn;
  if (length * stepTicks < 2 * barTicks || density == DiceDensity::Busy) return;
  // Ghost the answering rhythm in alternate bars, retaining its pickup and
  // the original step FX. On the turn it answers in a different bar.
  for (int at = 0; at < length; ++at) {
    const int tick = at * stepTicks;
    if (row[at].hasNote() && tick % barTicks < 3 * kPpqn && layBack(family, tick / barTicks, stage))
      row[at].vel = std::max(1, row[at].vel * 52 / 100);
  }
}

}  // namespace

TimbreProfile timbreProfile(const DiceRecipe& recipe) {
  static constexpr struct { const char* style; TimbreProfile profile; } styles[] = {
    {"Jump Up", TimbreProfile::JumpUp}, {"Dark Psy", TimbreProfile::DarkPsy},
    {"Minimal Techno", TimbreProfile::MinimalTechno}, {"Dub Techno", TimbreProfile::DubTechno},
    {"Liquid DnB", TimbreProfile::Liquid},
  };
  for (const auto& style : styles)
    if (!std::strcmp(recipe.style, style.style)) return style.profile;
  return TimbreProfile::Family;
}

bool usesFamilyProduction(TimbreProfile profile) {
  return profile == TimbreProfile::Family || profile == TimbreProfile::DubTechno || profile == TimbreProfile::Liquid;
}

void produceTimbres(DiceParts& parts, const DiceLayout& layout, const DiceVariant& variant,
                    const PhrasePlan& plan, TimbreProfile profile) {
  bool firstChord = true;
  for (int t = 0; t < kDiceRoles; ++t) {
    const int source = layout.source[t];
    auto& cfg = parts.tracks[t];
    if (cfg.instr >= kInstruments || source == kDiceUnusedTrack) continue;
    auto& sound = parts.instruments[cfg.instr];
    const auto role = roleOf(source, variant, plan, sound);
    const auto kind = source >= 0 ? static_cast<DiceVoiceKind>(diceVoice(variant, source).kind) : DiceVoiceKind::Fx;
    switch (profile) {
      case TimbreProfile::JumpUp:
        if (role == Role::Kit) focusDrums(parts, sound, layout, variant, profile);
        else if (role == Role::Bass && sound.type == InstrType::Synth) focusedBass(sound, cfg, profile);
        else if (source >= 0 && !std::strcmp(diceVoice(variant, source).name, "Pluck") && sound.type == InstrType::Synth)
          jumpVowel(sound, cfg);
        else fader(cfg, 82);
        break;
      case TimbreProfile::DarkPsy:
        if (role == Role::Kit) focusDrums(parts, sound, layout, variant, profile);
        else if (role == Role::Bass && sound.type == InstrType::Synth) {
          focusedBass(sound, cfg, profile);
          fader(cfg, 70);
          cfg.folAmt = 0;
        } else if (source == dicePartSource(DicePart::Fx)) movingPsyFx(sound, cfg);
        else if (source == dicePartSource(DicePart::Arp) || source == dicePartSource(DicePart::Lead))
          psyAnswer(sound, cfg, source == dicePartSource(DicePart::Arp));
        else if (role == Role::Bed) {
          sound.rsend = std::max<int>(sound.rsend, 38);
          fader(cfg, 80);
        }
        break;
      case TimbreProfile::MinimalTechno:
        if (role == Role::Kit) focusDrums(parts, sound, layout, variant, profile);
        else {
          minimalTone(sound, role);
          if (diceSupplement(source) && role != Role::Bass) fader(cfg, 55);
        }
        break;
      case TimbreProfile::DubTechno:
        if (role == Role::Kit) focusDrums(parts, sound, layout, variant, profile);
        else if (kind == DiceVoiceKind::Stab || role == Role::Bed) { dubChord(sound, cfg, firstChord); firstChord = false; }
        else if (role == Role::Bass && sound.type == InstrType::Synth) focusedBass(sound, cfg, profile);
        else fader(cfg, 76);
        break;
      case TimbreProfile::Liquid:
        if (role == Role::Kit) { sound.send = std::min<int>(sound.send, 9); sound.rsend = std::min<int>(sound.rsend, 20); }
        else if (role == Role::Bass && sound.type == InstrType::Synth) focusedBass(sound, cfg, profile);
        else if (role == Role::Bed || kind == DiceVoiceKind::Stab) {
          sound.rsend = std::clamp(int(sound.rsend) + 18, 36, 61);
          sound.send = std::min<int>(sound.send, 22);
          fader(cfg, 114);
        }
        break;
      case TimbreProfile::Family:
        break;
    }
  }
}

void produceDarkPsySection(DicePat& dst, const DiceLayout& layout, const DiceVariant& variant,
                           DiceForm form, int section, DiceDensity density) {
  if (dst.res != Resolution::Sixteenth) return;
  int bass = -1, fx = -1, arp = -1;
  for (int t = 1; t < kDiceRoles; ++t) {
    const int source = layout.source[t];
    if (source >= 0 && diceVoice(variant, source).kind == uint8_t(DiceVoiceKind::Bass)) bass = t;
    if (source == dicePartSource(DicePart::Fx)) fx = t;
    if (source == dicePartSource(DicePart::Arp)) arp = t;
  }
  const auto kicks = kickLanes(layout, variant);
  if (bass < 0 || !kicks || dst.trackSpeed[0] || dst.trackSpeed[bass] ||
      dst.trackLen[0] || dst.trackLen[bass]) return;
  auto* kit = dst.steps[0];
  auto* roll = dst.steps[bass];
  const bool turn = form == DiceForm::Develop ? section == 2 : section == kArrangeBuild;
  const bool drive = section == 1 || turn || (form == DiceForm::Arrange && section == kArrangeDrop);
  if (!drive) return;
  for (int at = 0; at + 3 < dst.length; at += 4) {
    if (!kit[at].hasNote() || !(laneMask(kit[at]) & kicks) ||
        !roll[at + 1].hasNote() || !roll[at + 2].hasNote()) continue;
    auto& pickup = roll[at + 3];
    if (pickup.isEmpty() && density != DiceDensity::Sparse) {
      pickup.note = roll[at + 2].note;
      pickup.vel = std::max(1, roll[at + 2].vel - 5);
      putFx(pickup, Fx::GAT, 65);
    }
    putFx(roll[at + 1], Fx::GAT, 74);
    putFx(roll[at + 2], Fx::GAT, 76);
    if (pickup.hasNote()) putFx(pickup, Fx::GAT, 65);
  }
  if (arp >= 0) {
    auto* answer = dst.steps[arp];
    for (int at = 0; at < dst.length; ++at) {
      if (!answer[at].hasNote()) continue;
      const bool exposed = (at / 16 % 2 == int(turn)) && at % 16 >= 4;
      if (exposed) putFx(answer[at], Fx::FLT, 91);
      else answer[at].vel = std::max(1, answer[at].vel * 66 / 100);
    }
  }
  if (!turn || dst.length < 32) return;
  // Let a moving tail finish the phrase over one beat of space before the drop.
  const int lastBeat = dst.length - 4;
  const uint16_t lanes = laneMask(kit[lastBeat]) & ~kicks;
  if (kit[lastBeat].hasNote()) {
    if (lanes) setLaneMask(kit[lastBeat], lanes);
    else clearNote(kit[lastBeat]);
  }
  clearGenRange(roll, lastBeat, dst.length - 1);
  if (fx < 0) return;
  auto* tail = dst.steps[fx];
  for (int at = dst.length - 1; at >= 0; --at) {
    if (!tail[at].hasNote()) continue;
    if (at < lastBeat && tail[lastBeat].isEmpty()) {
      tail[lastBeat] = tail[at];
      tail[at] = Step();
      at = lastBeat;
    }
    putFx(tail[at], Fx::GAT, gateValue(260));
    break;
  }
}

void produceMix(DiceParts& parts, const DiceLayout& layout, const DiceVariant& variant,
                const PhrasePlan& plan, DiceFamily family) {
  const bool kick = kickLanes(layout, variant) != 0;
  for (int t = 0; t < kDiceRoles; ++t) {
    auto& cfg = parts.tracks[t];
    if (cfg.instr >= kInstruments || layout.source[t] == kDiceUnusedTrack) continue;
    auto& sound = parts.instruments[cfg.instr];
    const Role role = roleOf(layout.source[t], variant, plan, sound);
    if (family == DiceFamily::Acid && !std::strcmp(sound.name, "303") && role == Role::Bass) {
      acidMix(sound, cfg);
      continue;
    }
    if (family == DiceFamily::Industrial) industrialMix(sound, cfg, role);
    if (kick && role == Role::Bass &&
        (family == DiceFamily::Techno || family == DiceFamily::Trance || family == DiceFamily::Industrial))
      duckBass(cfg);
    if (family == DiceFamily::Trance && role == Role::Hook) fader(cfg, 117);
    if (family == DiceFamily::Hardcore && role == Role::Hook) {
      sound.vol = std::min(100, sound.vol * 5 / 4);
      fader(cfg, 118);
    }
    if (family == DiceFamily::Dnb && role == Role::Bass) fader(cfg, 113);
    if (family == DiceFamily::Ambient && role == Role::Bed) {
      fader(cfg, 114);
      sound.rsend = std::clamp(int(sound.rsend) + 7, 0, 62);
    }
    if (role == Role::Answer && family != DiceFamily::Ambient && family != DiceFamily::Dnb &&
        family != DiceFamily::UkGarage && family != DiceFamily::Rave)
      fader(cfg, family == DiceFamily::Trance ? 62 : 77);
  }
}

void produceSection(DicePat& dst, const DiceParts& parts, const DiceLayout& layout,
                    const DiceVariant& variant, const PhrasePlan& plan, DiceFamily family,
                    DiceForm form, int section, DiceDensity density) {
  const auto stage = stageOf(form, section);
  const uint16_t kicks = kickLanes(layout, variant);
  int droneAnchor = -1;
  for (int t = 0; t < kDiceRoles; ++t) {
    if (parts.tracks[t].instr >= kInstruments || layout.source[t] == kDiceUnusedTrack) continue;
    const auto& sound = parts.instruments[parts.tracks[t].instr];
    const auto role = roleOf(layout.source[t], variant, plan, sound);
    const int length = dst.trackLen[t] ? dst.trackLen[t] : dst.length;
    const auto speed = toSpeed(dst.trackSpeed[t]);
    const int stepTicks = ticksPerStep(dst.res) * speedDiv(speed) / speedMul(speed);
    if (family == DiceFamily::Acid && role == Role::Bass && !std::strcmp(sound.name, "303"))
      acidPhrase(dst.steps[t], length, stepTicks, sound.cutoff, stage);
    if (family == DiceFamily::Trance && role == Role::Hook)
      liftHook(dst.steps[t], length, stepTicks, stage);
    if (family == DiceFamily::Techno && role == Role::Bass && kicks && !dst.trackSpeed[t] && !dst.trackLen[t])
      seatTechnoBass(dst.steps[t], dst.steps[0], length, kicks);
    if (family == DiceFamily::Dnb && role == Role::Bass && sound.type == InstrType::Synth)
      rollBassFilter(dst.steps[t], length, stepTicks, sound.cutoff, stage);
    const int source = layout.source[t];
    if (family == DiceFamily::Idm && source >= 0 && !dst.trackSpeed[t] && !dst.trackLen[t] &&
        diceVoice(variant, source).kind == uint8_t(DiceVoiceKind::Pad)) {
      if (droneAnchor < 0) droneAnchor = t;
      else separateDrones(dst.steps[t], dst.steps[droneAnchor], length, parts, sound.transpose,
                          parts.instruments[parts.tracks[droneAnchor].instr].transpose);
    }
    if (role == Role::Answer && (family == DiceFamily::Acid || family == DiceFamily::Techno ||
         family == DiceFamily::Trance || family == DiceFamily::Idm || family == DiceFamily::Industrial))
      leaveSpace(dst.steps[t], length, stepTicks, family, stage, density);
  }
}

bool ambientTexture(const DiceLayout& layout, const DiceVariant& variant, DiceFamily family) {
  return family == DiceFamily::Ambient && !kickLanes(layout, variant);
}

void breatheTexture(Step* row, int length, int stepTicks, DiceDensity density) {
  const int spacing = (density == DiceDensity::Busy ? 1 : density == DiceDensity::Sparse ? 4 : 2) * kPpqn;
  const int window = std::max(1, spacing / stepTicks);
  for (int start = 0; start < length; start += window) {
    const int end = std::min(length, start + window);
    int strongest = -1;
    for (int i = start; i < end; ++i)
      if (row[i].hasNote() && (strongest < 0 || row[i].vel > row[strongest].vel)) strongest = i;
    for (int i = start; i < end; ++i)
      if (i != strongest && row[i].hasNote()) row[i] = Step();
  }
}

}  // namespace mt::dice_detail
