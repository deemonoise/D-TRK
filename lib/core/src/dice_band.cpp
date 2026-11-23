#include "dice_band.h"
#include "dice_sounds.h"
#include "gen_util.h"
#include <algorithm>
#include <cstring>

namespace mt {
namespace dice_detail {
namespace {
using Family = BandFamily;
struct Colours {
  const char *bass, *chord, *answer;
};
constexpr Colours rolling[] = {{"REESE", "STAB", "PULSE"},
                               {"SUB", "ORGAN", "VOX FORM"},
                               {"ANALOGBS", "PLUCK", "STAB"},
                               {"WOBBLE", "WARM PAD", "PULSE"}};
constexpr Colours minimal[] = {{"SUB", "PLUCK", "VOX FORM"},
                               {"808 SUB", "DRONE", "PULSE"},
                               {"ANALOGBS", "STAB", "PLUCK"},
                               {"SUB", "ORGAN", "PULSE"}};
constexpr Colours liquid[] = {{"SUB", "E PIANO", "ACOU GTR"},
                              {"ANALOGBS", "ORGAN", "WARM PAD"},
                              {"808 SUB", "ACOU GTR", "PLUCK"},
                              {"SUB", "WARM PAD", "E PIANO"}};
constexpr Colours dark[] = {{"REESE", "STAB", "VOX FORM"},
                            {"WOBBLE", "PULSE", "PLUCK"},
                            {"ANALOGBS", "VOX FORM", "STAB"},
                            {"REESE", "DRONE", "PULSE"}};
constexpr Colours hardcore[] = {{"ANALOGBS", "SAW LEAD", "STAB"},
                                {"REESE", "PULSE", "VOX FORM"},
                                {"ANALOGBS", "STAB", "SAW LEAD"},
                                {"WOBBLE", "SAW LEAD", "PULSE"}};
const Colours &colours(const PhrasePlan &plan) {
  const Colours *set = rolling;
  if (plan.band.family == Family::Gabber)
    set = hardcore;
  else
    switch (plan.production.voice) {
    case DiceVoicing::Minimal:
    case DiceVoicing::Sub:
      set = minimal;
      break;
    case DiceVoicing::Liquid:
    case DiceVoicing::Swing:
      set = liquid;
      break;
    case DiceVoicing::Reese:
      set = dark;
      break;
    default:
      break;
    }
  return set[plan.band.sound];
}
// Sixteenth positions in a two-bar question/answer. The bass and chord patterns
// are chosen separately from the instruments. Long notes and short doubles
// articulate different grooves even over a single held root.
struct BreakPhrase {
  uint16_t bass[2], comp[2];
  uint16_t gate;
};
constexpr BreakPhrase breaks[] = {
    {{0x0409, 0x4081}, {0x0040, 0x1000}, 290}, // downbeat weight, late pickup
    {{0x2244, 0x0884}, {0x0001, 0x4040}, 105}, // broken offbeats, short answers
    {{0x0081, 0x4801}, {0x0808, 0x0020}, 475}, // long sub, two clipped stabs
    {{0x1122, 0x2048}, {0x0100, 0x0804}, 165}, // rolling syncopation
    {{0x4201, 0x0412}, {0x0020, 0x2001}, 230},
    {{0x0844, 0x2210}, {0x4001, 0x0080}, 185}};
bool hit(uint16_t mask, int slot) { return (mask & (1u << slot)) != 0; }
int place(int note, int bottom) { return bottom + (note - bottom + 120) % 12; }
void writeNote(Step &step, int pitch, int velocity, int gate, int transpose) {
  step = Step();
  step.note = std::max(0, std::min(127, pitch - transpose));
  step.vel = velocity;
  putFx(step, Fx::GAT, gateValue(gate));
}
bool bassRole(const DiceVariant &v, int source) {
  return source == dicePartSource(DicePart::Bass) ||
         (source >= 0 &&
          diceVoice(v, source).kind == uint8_t(DiceVoiceKind::Bass));
}
void breakLine(Step *row, int length, int source, const DiceVariant &v,
               const DiceRecipe &recipe, const DiceParts &parts,
               const PhrasePlan &plan, int transpose, DiceDensity density) {
  const bool bass = bassRole(v, source);
  const auto &band = plan.band;
  const auto &phrase = breaks[band.rhythm];
  const auto &comp = breaks[(band.rhythm + band.turn + band.sound) % 6];
  const bool second = !bass && source != plan.harmonicSource;
  const auto &palette = colours(plan);
  const char *patch = second ? palette.answer : palette.chord;
  const bool bed =
      !bass && (!strcmp(patch, "WARM PAD") || !strcmp(patch, "DRONE"));
  const bool single =
      !bass && (!strcmp(patch, "PLUCK") || !strcmp(patch, "PULSE") ||
                !strcmp(patch, "VOX FORM") || !strcmp(patch, "DRONE"));
  for (int at = 0; at < length; ++at) {
    const int bar = at / 16, slot = at % 16, half = (bar + band.turn) % 2;
    uint16_t mask = bass ? phrase.bass[half] : comp.comp[half];
    // At high Variety, answer the first half with the roll's own motif.
    // Keep the opening groove so the listener can recognise its return.
    const bool answer = half && plan.hook.active(bar);
    if (answer && bass) {
      const auto& response = breaks[(band.rhythm + 1 + plan.hook.bassRhythm) % 6];
      mask = response.bass[1];
    }
    // A turnaround changes just the end of the fourth bar.
    if ((bar + band.turn) % 4 == 3 && plan.amount >= 65) {
      if (bass)
        mask = uint16_t((mask & 0x0fff) | (1u << (12 + (plan.hook.length ? plan.hook.accent(2, bar, true) : band.sound) % 4)));
      else if (!second)
        mask ^= 0x4000;
    }
    if (second)
      mask = uint16_t((mask << 8) | (mask >> 8));
    const bool sparse = density == DiceDensity::Sparse;
    if (sparse && !bass && second && length >= 32 && !half)
      mask = 0;
    if (bed)
      mask = length >= 32 && half ? 0 : (band.turn % 2 ? 0x0010 : 0x0001);
    if (bass && density == DiceDensity::Busy && (bar & 1))
      mask |= 0x8000;
    row[at] = Step();
    if (!hit(mask, slot))
      continue;
    DiceChord chord{static_cast<uint8_t>(60 + parts.scaleRoot), kChordTriad};
    harmonyAt(v, recipe, at * (kPpqn / 4), parts.scaleRoot, parts.scaleType,
              plan, chord);
    const int root = place(chord.root, bass ? 32 : second ? 62 : 54);
    int note = root, gate = bass  ? phrase.gate
                            : bed ? 780
                                  : 62 + (band.sound % 3) * 62;
    uint8_t tones[4];
    const int count =
        chordNotes(root, chord.chord, parts.scaleRoot,
                   static_cast<ScaleType>(parts.scaleType), tones);
    if (bass) {
      if (answer && slot >= 4 && slot < 10) {
        const int movement = plan.hook.degree(bar, slot / 4, true);
        note = tones[std::abs(movement) % count];
      }
      // At a phrase ending, touch the chord's fifth or octave, then resolve.
      if (slot >= 10 && ((bar + band.turn) & 1))
        note = band.sound % 2 ? root + 12 : tones[std::min(2, count - 1)];
      int next = slot + 1;
      while (next < 16 && !hit(mask, next))
        ++next;
      gate = std::min(gate, 100 * (next - slot) - 12);
    }
    if (single && slot >= 8 && count > 1)
      note = tones[(bar + band.sound) % count];
    writeNote(row[at], note, bass ? (slot < 4 ? 110 : 94) : (second ? 64 : 78),
              gate, transpose);
    if (!bass && !single && (!second || !sparse))
      putFx(row[at], Fx::CHD, chord.chord);
    if (slot % 4 && band.groove) {
      const int late = (1 + band.turn) * band.groove / 100;
      if (late)
        putFx(row[at], Fx::NDG, late);
    }
  }
}
void hardcoreLine(Step *row, int length, int source, const DiceVariant &v,
                  const DiceParts &parts, const PhrasePlan &plan, int transpose,
                  DiceDensity density) {
  const auto &band = plan.band;
  const bool bass = bassRole(v, source);
  const bool lead = source >= 0 && source == plan.leadSource;
  const bool answer = source >= 0 && !lead && !bass;
  const bool chord = source == dicePartSource(DicePart::Chords);
  // The 909 tail owns the downbeat. The mid-bass only punctuates its decay.
  const int bars = std::min(4, std::max(1, length / 16));
  constexpr uint16_t calls[] = {0x0444, 0x1081, 0x0221, 0x4840, 0x1101, 0x0422};
  constexpr uint16_t replies[] = {0x5000, 0x2200, 0x4400,
                                  0x2800, 0x0880, 0x2080};
  for (int at = 0; at < length; ++at) {
    const int bar = at / 16, slot = at % 16;
    const bool reply = (bar + band.turn) % 2;
    uint16_t mask = 0;
    if (bass) {
      constexpr uint16_t tails[] = {0x0040, 0x0400, 0x0004, 0x4000};
      mask = tails[(band.rhythm + bar + band.turn) % 4];
      // Psy Gabber has an authored bass role of its own. Keep its offbeat
      // pulse.
      if (source >= 0)
        mask |= reply ? 0x0440 : 0x4004;
    } else if (lead)
      mask = reply ? uint16_t(calls[band.rhythm] & 0x00ff) : calls[band.rhythm];
    else if (answer)
      mask = reply || length <= 16 ? replies[band.rhythm] : 0;
    else if (chord)
      mask = (bar + band.turn) % bars == 0 ? 0x0100 : 0;
    else
      mask = (bar + band.turn) % bars == bars - 1 ? 0x8000 : 0;
    if (bass && density == DiceDensity::Busy)
      mask |= 0x4000;
    row[at] = Step();
    if (!hit(mask, slot))
      continue;
    const int root = place(parts.scaleRoot, bass ? 44 : lead ? 53 : 59);
    // Repeated pitch is intentional; one change at the end makes the reply
    // legible.
    const int degree = bass       ? (source >= 0 && reply && slot >= 8 ? 4 : 0)
                       : slot < 8 ? 0
                       : answer   ? 4
                                  : (band.sound % 2 ? 2 : 3);
    const int note = moveDegrees(root, degree, parts.scaleRoot,
                                 static_cast<ScaleType>(parts.scaleType));
    writeNote(row[at], note,
              bass   ? 89
              : lead ? 101
                     : 73,
              bass   ? 52
              : lead ? 108
                     : 68,
              transpose);
    if (chord)
      putFx(row[at], Fx::CHD, kChordTriad);
    if (slot % 4 && band.groove >= 50)
      putFx(row[at], Fx::NDG, 2);
  }
}
} // namespace
BandPlan rollBand(const char *genre, const DiceRecipe &recipe, uint32_t seed,
                  int variety, int groove) {
  BandPlan band;
  Rng rng(seed ^ 0x9c86723bu);
  if (!variety || (variety < 35 && int(rng.below(35)) >= variety))
    return band;
  if (!strcmp(genre, "Drum & Bass") ||
      !strcmp(genre, "Atmospheric & Jazz DnB") ||
      !strcmp(genre, "Techstep & Dancefloor"))
    band.family = Family::Dnb;
  else if (!strcmp(recipe.style, "Gabber") ||
           !strcmp(recipe.style, "Nu-Style Gabber") ||
           !strcmp(recipe.style, "Psy Gabber"))
    band.family = Family::Gabber;
  else
    return band;
  // Halftime, Jump Up and Drumfunk retain their authored bass syncopation.
  band.rephraseBass = !strcmp(recipe.style, "Drum & Bass") ||
                      !strcmp(recipe.style, "Minimal DnB") ||
                      !strcmp(recipe.style, "Rolling DnB") ||
                      !strcmp(recipe.style, "Liquid DnB");
  band.groove = std::min(100, std::max(0, groove));
  band.sound = rng.below(4);
  band.rhythm = rng.below(6);
  band.turn = rng.below(4);
  return band;
}
const char *bandPatch(const DiceVariant &v, int source,
                      const PhrasePlan &plan) {
  const auto &band = plan.band;
  if (band.family == Family::Guide)
    return nullptr;
  const auto &c = colours(plan);
  if (bassRole(v, source)) {
    if (band.family == Family::Dnb && source >= 0 &&
        !strcmp(diceVoice(v, source).name, "Wobble")) {
      constexpr const char *jump[] = {"WOBBLE", "PLUCK BS", "WOBBLE",
                                      "ANALOGBS"};
      return jump[band.sound];
    }
    return c.bass;
  }
  if (source >= 0) {
    if (band.family == Family::Gabber)
      return source == plan.leadSource ? c.chord : c.answer;
    if (diceChord(diceVoice(v, source)) != 255)
      return source == plan.harmonicSource ? c.chord : c.answer;
    return nullptr;
  }
  if (!diceSupplement(source))
    return nullptr;
  switch (dicePartKind(source)) {
  case DicePart::Chords:
  case DicePart::Pad:
    return c.chord;
  case DicePart::Lead:
  case DicePart::Counter:
    return c.answer;
  case DicePart::Arp:
    return "PLUCK";
  default:
    return "PULSE";
  }
}
void buildGuideVoice(const DiceVoice &guide, const char *patch,
                     Instrument &sound, TrackCfg &track, bool clean, Rng &rng,
                     const PhrasePlan &plan) {
  auto voice = guide;
  if (plan.band.family != Family::Guide &&
      std::strcmp(diceTimbre(guide).name, patch)) {
    voice.timbre = diceVoiceForTimbre(patch, plan.seed).timbre;
    voice.lfo[3] = 0;
    voice.name = patch;
  }
  buildVoice(voice, sound, track, clean, plan.amount, rng, plan.feel,
             DicePart::Count, plan.production);
  if (plan.band.family == Family::Dnb && diceChord(guide) != 255) {
    // A timbre borrowed from a solo pluck may now play four voices at once.
    sound.vol = std::min<int>(sound.vol, 52);
  }
  if (plan.band.family != Family::Gabber ||
      guide.kind == uint8_t(DiceVoiceKind::Bass))
    return;
  sound.mono = true;
  sound.vol = std::max<int>(62, sound.vol);
  sound.attack = std::min<int>(8, sound.attack);
  sound.release = std::min<int>(31, sound.release);
  if (!std::strcmp(patch, "SAW LEAD") && sound.type == InstrType::Synth) {
    // One stable saw with an octave pulse falling into tune above it.
    sound.synOsc[1] = uint8_t(SynOsc::Square);
    sound.synSemi = 12;
    sound.macro[kMacShp2] = 37;
    sound.macro[kMacMix] = 33;
    sound.synEnvDest = uint8_t(SynEnvDest::Pitch2);
    sound.synEDec = 25;
    sound.macro[kMacSenv] = 83;
    sound.synPhaseMode = uint8_t(SynPhaseMode::Free);
  }
}
bool composeBandLine(Step *row, int length, int ticks, int source,
                     const DiceVariant &v, const DiceRecipe &recipe,
                     const DiceParts &parts, const PhrasePlan &plan,
                     int transpose, DiceDensity density) {
  if (plan.band.family == Family::Guide || ticks != kPpqn / 4)
    return false;
  if (source >= 0 &&
      (diceVoice(v, source).length || diceVoice(v, source).speed))
    return false;
  if (plan.band.family == Family::Gabber) {
    hardcoreLine(row, length, source, v, parts, plan, transpose, density);
    return true;
  }
  if (source < 0)
    return false;
  const bool bass = plan.band.rephraseBass && bassRole(v, source);
  if (!bass && diceChord(diceVoice(v, source)) == 255)
    return false;
  breakLine(row, length, source, v, recipe, parts, plan, transpose, density);
  return true;
}
void thinBreakSupport(Step *row, int length, DicePart part,
                      const PhrasePlan &plan, DiceDensity density) {
  if (plan.band.family != Family::Dnb)
    return;
  // The guide already has the bass and usually a chord part. Give the added
  // voices different bars to answer in; a one-bar loop still has an accent to
  // edit.
  const bool supporting = part == DicePart::Lead || part == DicePart::Arp ||
                          part == DicePart::Fx || part == DicePart::Counter;
  if (!supporting)
    return;
  const int period = length >= 64 ? 4 : length >= 32 ? 2 : 1;
  const int turn = part == DicePart::Lead ? 0 : part == DicePart::Arp ? 1 : 3;
  for (int bar = 0; bar < length; bar += 16) {
    const bool play = (bar / 16 + plan.band.turn) % period == turn % period;
    int budget = density == DiceDensity::Busy ? 3 : 1;
    for (int at = bar; at < std::min(length, bar + 16); ++at)
      if (row[at].hasNote() && (!play || budget-- <= 0))
        row[at] = Step();
  }
}
bool bandRide(const DiceVoice &voice, const DiceRecipe &recipe,
              const BandPlan &band) {
  return band.family == Family::Dnb &&
         voice.kind == uint8_t(DiceVoiceKind::OpenHat) &&
         !strcmp(voice.name, "Ride") &&
         diceEventCount(voice) >= recipe.length / 2;
}
bool rideAccent(const BandPlan &band, int tick) {
  const int slot = tick / (kPpqn / 4);
  constexpr uint16_t rides[] = {0x4444, 0x4040, 0x2222, 0x1010};
  return hit(rides[band.sound], slot % 16) ||
         ((slot / 16 + band.turn) % 4 == 3 && slot % 16 == 15);
}
} // namespace dice_detail
} // namespace mt
