#include "dice_reference.h"
#include "gen_util.h"
#include <algorithm>
#include <cstring>

namespace mt::dice_detail {
namespace {

struct VoiceMix {
  const char* style;
  const char* track;
  uint8_t percent;
  int8_t pan = -1, delay = -1, room = -1;
};

// Tracks are the source/companion labels in the recipes. The adjustments
// leave the catalog variants and their authored patches available in Classic.
constexpr VoiceMix mix[] = {
  {"Acid", "ARP", 50}, {"Acid", "Keys", 74},
  {"Acid House", "Stab", 78, 76, 27, 16},
  {"Acid Breaks", "Keys", 72}, {"Acid Breaks", "ARP", 63},
  {"Acid Techno", "Stab", 80}, {"Acid Techno", "ARP", 57},
  {"Warehouse Techno", "ARP", 53}, {"Warehouse Techno", "FX", 113, 80, 26, 23},
  {"Hypnotic Techno", "LEAD", 60}, {"Hypnotic Techno", "ARP", 61},
  {"Hypnotic Techno", "Stab", 114, 66, 24, 18},
  {"Detroit Techno", "Stab", 119, 76, 36, 26}, {"Detroit Techno", "LEAD", 62},
  {"Detroit Techno", "ARP", 63}, {"Detroit Techno", "Bass", 90},
  {"Hardgroove", "ARP", 51}, {"Hardgroove", "LEAD", 60},
  {"Minimal Techno", "LEAD", 64}, {"Minimal Techno", "ARP", 61},
  {"Liquid DnB", "Piano", 106, 56, 23, 55}, {"Liquid DnB", "ACOU GTR", 109, 83, 35, 48},
  {"Neurofunk", "Reese", 100, 64, 9, 10},
  {"Jungle", "ARP", 27}, {"Jungle", "LEAD", 70}, {"Jungle", "Sub", 97},
  {"Halftime DnB", "Pluck", 112, 76, 36, 27}, {"Halftime DnB", "FX", 106, 43, 34, 37},
  {"Jump Up", "Wobble", 79}, {"Jump Up", "Pluck", 108, 70, 12, 16}, {"Jump Up", "PLUCK", 69},
  {"UK Garage", "ARP", 52}, {"UK Garage", "Stab", 117, 73, 24, 15},
  {"2-Step Garage", "ARP", 111, 78, 19, 14},
  {"Speed Garage", "ARP", 47}, {"Speed Garage", "FX", 70},
  {"Future Garage", "Sub", 81}, {"Future Garage", "Pad", 123, 53, 30, 58},
  {"Future Garage", "ARP", 61, 79, 37, 29},
  {"UK Bassline", "Lead", 52}, {"UK Bassline", "ARP", 45}, {"UK Bassline", "Stab", 102, 78, 16, 10},
  {"Old Skool Rave", "Piano", 113, 58, 11, 21}, {"Old Skool Rave", "ARP", 51},
  {"Bleep & Bass", "Hoover", 41}, {"Bleep & Bass", "Bleep", 129, 76, 24, 12},
  {"Bleep & Bass", "CHORDS", 46}, {"Bleep & Bass", "ARP", 38},
  {"Rave Revival", "ARP", 54}, {"Rave Revival", "Stab", 107, 60, 19, 24},
  {"Rave Techno", "ARP", 48}, {"Rave Techno", "Lead", 123, 75, 40, 30},
  {"Classic Trance", "Lead", 109, 55, 34, 35}, {"Classic Trance", "Arp", 87, 82, 25, 19},
  {"Progressive Trance", "Pad", 111, 52, 30, 53}, {"Progressive Trance", "Pluck", 107, 78, 32, 22},
  {"Progressive Trance", "Bass", 90},
  {"Uplifting", "Lead", 117, 54, 36, 44}, {"Uplifting", "CHORDS", 117, 78, 27, 55},
  {"Uplifting", "ARP", 73, 83, 29, 21},
  {"Goa", "Acid", 117, 58, 30, 19}, {"Goa", "LEAD", 93, 84, 33, 32},
  {"Goa", "CHORDS", 67}, {"Goa", "FX", 106, 39, 42, 35},
  {"Full-On", "Bass", 90}, {"Full-On", "Lead", 121, 55, 23, 33},
  {"Full-On", "Pluck", 103, 85, 31, 26}, {"Full-On", "ARP", 66},
  {"Gabber", "Hoover", 79}, {"Industrial Hardcore", "ARP", 54}, {"Industrial Hardcore", "FX", 98, 82, 37, 23},
  {"Happy Hardcore", "Bass", 76}, {"Happy Hardcore", "Piano", 116, 73, 17, 31},
  {"Happy Hardcore", "Hoover", 94, 54, 31, 24}, {"Happy Hardcore", "ARP", 60},
  {"Breakbeat Hardcore", "Piano", 111, 56, 17, 32}, {"Breakbeat Hardcore", "Stab", 107, 77, 16, 24},
  {"Breakbeat Hardcore", "ARP", 55},
  {"IDM", "Pad", 80}, {"IDM", "Drone", 73}, {"IDM", "LEAD", 114, 73, 37, 34},
  {"Braindance", "Bass", 80}, {"Braindance", "Bell", 116, 83, 26, 33}, {"Braindance", "ARP", 50},
  {"Clicks & Cuts", "Bass", 63}, {"Clicks & Cuts", "Tone", 91, 78, 19, 10},
  {"Clicks & Cuts", "CHORDS", 32}, {"Clicks & Cuts", "ARP", 37},
  {"Polyrhythmic IDM", "Synth", 108, 49, 21, 29}, {"Polyrhythmic IDM", "Bell", 123, 85, 31, 25},
  {"Polyrhythmic IDM", "ARP", 58},
  {"Space Ambient", "Bass", 74}, {"Space Ambient", "Bell", 72, 86, 27, 63},
  {"Dark Ambient", "DRUMS", 35, 64, 6, 22}, {"Dark Ambient", "Pad", 47, 81, 6, 37},
  {"Dark Ambient", "Texture", 48, 41, 19, 40}, {"Dark Ambient", "TEXTURE", 42, 78, 18, 53},
  {"Generative Ambient", "Pulse", 47, 64, 20, 51}, {"Generative Ambient", "Bass", 72},
  {"Generative Ambient", "Pad", 100, 53, 9, 61},
  {"Berlin School", "DRUMS", 44}, {"Berlin School", "Bass", 69},
  {"Berlin School", "Seq", 119, 57, 26, 20}, {"Berlin School", "CHORDS", 115, 76, 16, 61},
  {"Electro-Industrial", "ARP", 57}, {"Electro-Industrial", "LEAD", 107, 76, 14, 17},
  {"Industrial Techno", "LEAD", 60}, {"Industrial Techno", "ARP", 52}, {"Industrial Techno", "FX", 68},
  {"Darkwave", "Bass", 75}, {"Darkwave", "ARP", 47}, {"Darkwave", "Synth", 151, 64, 32, 38},
};

bool is(const char* name, const char* target) { return std::strcmp(name, target) == 0; }

void adjustVoices(DiceParts& parts, const char* style, const DiceVariant& variant, const DiceLayout& layout) {
  bool mainDrone = true;
  for (auto& cfg : parts.tracks) {
    if (cfg.instr >= kInstruments) continue;
    auto& sound = parts.instruments[cfg.instr];
    for (const auto& entry : mix) {
      if (!is(entry.style, style) || !is(entry.track, cfg.name)) continue;
      cfg.vol = std::min(127, (cfg.vol * entry.percent + 50) / 100);
      if (entry.pan >= 0) cfg.pan = entry.pan;
      if (entry.delay >= 0) sound.send = entry.delay;
      if (entry.room >= 0) sound.rsend = entry.room;
      break;
    }
    if (is(style, "Dark Ambient") && is(cfg.name, "Drone")) {
      sound.transpose = std::max(-24, int(sound.transpose) - (mainDrone ? 24 : 12));
      sound.fltMode = uint8_t(FltMode::Lp);
      sound.cutoff = mainDrone ? 48 : 54;
      sound.reso = 15;
      sound.fenv = 4;
      sound.attack = 57;
      sound.release = mainDrone ? 87 : 74;
      sound.rsend = mainDrone ? 30 : 62;
      cfg.pan = mainDrone ? 64 : 49;
      if (!mainDrone) cfg.vol = cfg.vol * 55 / 100;
      mainDrone = false;
    }
    if (is(style, "Future Garage") && is(cfg.name, "Pad")) {
      sound.attack = 51;
      sound.release = 82;
    }
    if (is(style, "Speed Garage") && is(cfg.name, "Bass")) {
      sound.glide = 18;
      sound.release = std::min<int>(sound.release, 29);
    }
    if (is(style, "2-Step Garage") && is(cfg.name, "Bass"))
      sound.release = std::min<int>(sound.release, 24);
    if (is(style, "UK Bassline") && is(cfg.name, "Bass") && sound.type == InstrType::Synth) {
      sound.fenv = 23;
      sound.fDec = 45;
      sound.lfo[0] = {uint8_t(LfoWave::Tri), 7, 16, uint8_t(LfoDest::Cutoff), kLfoTempo};
    }
    if (is(style, "Darkwave") && is(cfg.name, "Synth") && sound.type == InstrType::Synth) {
      sound.fltMode = uint8_t(FltMode::Lp);
      sound.cutoff = std::max<int>(sound.cutoff, 78);
      sound.lfo[0] = {uint8_t(LfoWave::Sine), 6, 24, uint8_t(LfoDest::Pan), kLfoTempo | kLfoFree};
    }
    if (is(style, "Hypnotic Techno") && is(cfg.name, "Stab"))
      sound.lfo[0] = {uint8_t(LfoWave::Tri), 9, 23, uint8_t(LfoDest::Cutoff), kLfoTempo | kLfoFree};
    if (is(style, "Rolling DnB") && is(cfg.name, "Reese")) sound.release = 37;
    if (is(style, "Liquid DnB") && is(cfg.name, "Piano")) sound.release = std::max<int>(sound.release, 54);
    if (is(style, "Warehouse Techno") && is(cfg.name, "Stab")) {
      sound.rsend = std::max<int>(sound.rsend, 31);
      sound.lfo[0] = {uint8_t(LfoWave::Tri), 8, 11, uint8_t(LfoDest::Cutoff), kLfoTempo | kLfoFree};
    }
    if (sound.type != InstrType::Kit) continue;
    for (int source = 0; source < kDiceRoles; ++source) {
      if (!(layout.drums & (1u << source))) continue;
      auto& lane = sound.kit[layout.lane[source]];
      if (lane.instr >= kInstruments) continue;
      const auto kind = static_cast<DiceVoiceKind>(diceVoice(variant, source).kind);
      const bool hats = kind == DiceVoiceKind::Hat || kind == DiceVoiceKind::OpenHat || kind == DiceVoiceKind::Shaker;
      const bool snare = kind == DiceVoiceKind::Snare || kind == DiceVoiceKind::Clap;
      int percent = 100;
      if (hats && (is(style, "Hardgroove") || is(style, "Liquid DnB") || is(style, "Halftime DnB") ||
                   is(style, "Progressive Trance"))) percent = 130;
      if (hats && is(style, "Jump Up")) percent = 114;
      if (hats && (is(style, "Minimal Techno") || is(style, "Acid") || is(style, "UK Garage"))) percent = 78;
      if (snare && (is(style, "Darkwave") || is(style, "Electro-Industrial"))) percent = 123;
      if (kind == DiceVoiceKind::Kick && is(style, "Warehouse Techno")) {
        auto& kick = parts.instruments[lane.instr];
        kick.macro[kMacDec] = std::min(127, kick.macro[kMacDec] + 10);
      }
      if (kind == DiceVoiceKind::Perc && (is(style, "Hardgroove") || is(style, "Industrial Techno") ||
                                        is(style, "Industrial Hardcore") || is(style, "Clicks & Cuts"))) percent = 126;
      lane.vol = std::min(127, (lane.vol * percent + 50) / 100);
    }
  }
}

int kickLane(const DiceVariant& variant, const DiceLayout& layout) {
  for (int source = 0; source < kDiceRoles; ++source)
    if ((layout.drums & (1u << source)) && diceVoice(variant, source).kind == uint8_t(DiceVoiceKind::Kick))
      return layout.lane[source];
  return -1;
}

void setKick(Step& step, int lane, bool on) {
  const bool present = step.hasNote() && laneOn(step, lane);
  if (on == present) return;
  if (on && !step.hasNote()) {
    step.note = 106;
    setLaneMask(step, 0);
  }
  setLane(step, lane, on);
  if (on) {
    const Fx command = lane >= kKitLanesLo ? Fx::AC2 : Fx::ACC;
    if (const auto* acc = step.find(command))
      putFx(step, command, acc->val | (1u << (lane % kKitLanesLo)));
  } else if (!laneMask(step)) {
    clearNote(step);
  }
}

enum class KickShape { Guide, Garage, Speed, Rave, Clicks, Berlin };

KickShape kickShape(const char* style) {
  if (is(style, "UK Garage")) return KickShape::Garage;
  if (is(style, "Speed Garage")) return KickShape::Speed;
  if (is(style, "Old Skool Rave")) return KickShape::Rave;
  if (is(style, "Clicks & Cuts")) return KickShape::Clicks;
  if (is(style, "Berlin School")) return KickShape::Berlin;
  return KickShape::Guide;
}

void placeKicks(DicePat& pattern, int lane, KickShape shape, bool turn) {
  if (lane < 0 || shape == KickShape::Guide || pattern.trackSpeed[0] || pattern.trackLen[0]) return;
  auto* row = pattern.steps[0];
  for (int at = 0; at < pattern.length; ++at) {
    bool on = false;
    switch (shape) {
      case KickShape::Garage: on = at % 4 == 0; break;
      case KickShape::Speed: on = at % 4 == 0 || (turn && at % 32 == 31); break;
      case KickShape::Rave: {
        constexpr uint32_t breaks = (1u << 0) | (1u << 6) | (1u << 10) | (1u << 16) | (1u << 23) | (1u << 26);
        on = (breaks & (1u << (at % 32))) || (turn && at % 32 == 30);
        break;
      }
      case KickShape::Clicks:
        on = row[at].hasNote() && laneOn(row[at], lane) && at % 32 < 12 && at % 8 != 4;
        break;
      default: break; // Berlin School's sequencer takes over the pulse.
    }
    setKick(row[at], lane, on);
  }
}

void shapeAcid(Step* row, int length, bool breaks, bool turn) {
  for (int at = 0; at < length; ++at) {
    auto& note = row[at];
    if (!note.hasNote()) continue;
    const int pos = at % 32;
    if (breaks && ((pos >= 12 && pos < 16) || (pos >= 25 && pos < 28))) {
      clearGenRange(row, at, at);
      continue;
    }
    const int lift = turn ? 7 : 0;
    if (breaks) {
      if (pos % 8 == 0 || pos % 8 == 6)
        putFx(note, Fx::FLT, 72 + lift + pos / 4);
    } else {
      constexpr int opening[] = {64, 69, 77, 83};
      putFx(note, Fx::FLT, opening[at / 16 % 4] + lift + (pos % 4 == 2 ? 8 : 0));
      if (pos % 4 == 0 && row[at].find(Fx::TIE) == nullptr) putFx(note, Fx::GAT, 56);
    }
  }
}

void arrangeVoices(DicePat& pat, const DiceParts& parts, const char* style, bool turn, DiceDensity density) {
  int noteVoice = 0;
  bool firstStab = true;
  for (int track = 0; track < kDiceRoles; ++track) {
    if (pat.trackSpeed[track] || pat.trackLen[track]) continue;
    auto* row = pat.steps[track];
    const char* name = parts.tracks[track].name;
    const int length = pat.length;
    if ((is(style, "Acid Breaks") || is(style, "Acid Techno")) && is(name, "Acid"))
      shapeAcid(row, length, is(style, "Acid Breaks"), turn);
    if (is(style, "Acid House") && is(name, "Bass"))
      for (int at = 0; at < length; ++at) {
        if (!row[at].hasNote()) continue;
        if (at % 16 == 6 || at % 16 == 14)
          row[at].vel = std::min(127, row[at].vel + 13);
        if (at % 4 == 0 && !row[at].find(Fx::TIE)) putFx(row[at], Fx::GAT, 65);
      }
    if (is(style, "Bleep & Bass") && is(name, "Hoover"))
      clearGenRange(row, 0, turn ? length - 9 : length - 1);
    if (is(style, "Rave Revival") && is(name, "Stab")) {
      if (!firstStab)
        for (int start = turn ? 0 : 16; start < length; start += 32)
          clearGenRange(row, start, std::min(length, start + 12) - 1);
      firstStab = false;
    }
    if (is(style, "Space Ambient") && is(name, "Bass"))
      for (int at = 0; at < length; ++at)
        if (row[at].hasNote()) putFx(row[at], Fx::GAT, gateValue(420));
    if (is(style, "Dark Ambient") && is(name, "Drone"))
      for (int at = 0; at < length; ++at)
        if (row[at].hasNote()) putFx(row[at], Fx::GAT, gateValue(710));
    if (is(style, "Generative Ambient") && is(name, "Note")) {
      // Two pulse lengths will fall together and drift apart within the four-bar phrase.
      const int period = noteVoice == 0 ? 15 : 21;
      const int phase = (noteVoice++ * 8 + int(turn) * 5) % period;
      for (int at = 0; at < length; ++at) {
        if (!row[at].hasNote()) continue;
        const int slot = (at + period - phase) % period;
        if (slot > (density == DiceDensity::Busy ? 8 : 4)) clearGenRange(row, at, at);
        else putFx(row[at], Fx::GAT, gateValue(270));
      }
    }
    if (is(style, "Berlin School") && is(name, "Seq"))
      for (int at = 0; at < length; ++at) {
        if (!row[at].hasNote()) continue;
        putFx(row[at], Fx::FLT, 58 + (at / 16 % 4) * 7 + (at % 8 == 6 ? 10 : 0));
      }
    if (is(style, "Breakbeat Hardcore") && is(name, "Bass"))
      for (int at = 0; at < length; ++at) {
        if (!row[at].hasNote()) continue;
        if (at % 8 == 3 || (density != DiceDensity::Busy && at % 16 >= 12 && at % 16 < 15))
          clearGenRange(row, at, at);
        else putFx(row[at], Fx::GAT, 65);
      }
    if (is(style, "Polyrhythmic IDM") && (is(name, "Bell") || is(name, "Synth"))) {
      const bool bell = is(name, "Bell");
      const int period = bell ? 5 : 3;
      const int phase = bell ? 2 : 0;
      for (int at = 0; at < length; ++at) {
        if (!row[at].hasNote()) continue;
        if ((at + phase) % period == 0) {
          row[at].vel = std::min(127, row[at].vel + 20);
          putFx(row[at], Fx::FLT, bell ? 92 : 79);
        } else row[at].vel = std::max(1, row[at].vel * 65 / 100);
      }
    }
    if (is(style, "Neurofunk") && is(name, "Reese")) {
      constexpr int vowels[] = {53, 68, 59, 74};
      for (int at = 0; at < length; ++at)
        if (row[at].hasNote()) putFx(row[at], Fx::FLT, vowels[at / 4 % 4] + (turn ? 6 : 0));
    }
    if (is(style, "Jump Up") && density != DiceDensity::Busy && (is(name, "Wobble") || is(name, "Pluck"))) {
      // Alternating bass and vowel phrases leave the backbeat audible.
      const bool bass = is(name, "Wobble");
      for (int start = (bass == turn) ? 4 : 20; start < length; start += 32)
        clearGenRange(row, start, std::min(length, start + (bass ? 8 : 10)) - 1);
    }
    if ((is(style, "Jungle") && is(name, "Sub")) || (is(style, "Halftime DnB") && is(name, "SUB")))
      for (int at = 0; at < length; ++at)
        if (row[at].hasNote()) putFx(row[at], Fx::GAT, gateValue(at % 16 < 8 ? 250 : 155));
  }
}

}  // namespace

void applyReferenceEdits(DiceParts& parts, const DiceRecipe& recipe, const DiceVariant& variant,
                         const DiceLayout& layout, DiceForm form, int sections, DiceDensity density) {
  const char* style = recipe.style;
  adjustVoices(parts, style, variant, layout);
  const auto shape = kickShape(style);
  const int lane = shape == KickShape::Guide ? -1 : kickLane(variant, layout);
  for (int section = 0; section < sections; ++section) {
    auto& pattern = parts.pats[section];
    if (pattern.res != Resolution::Sixteenth) continue;
    const bool turn = form == DiceForm::Develop ? section == 2 : section == kArrangeDrop;
    if (section == 1 || turn || shape == KickShape::Berlin) placeKicks(pattern, lane, shape, turn);
    if (section == 1 || turn) arrangeVoices(pattern, parts, style, turn, density);
  }
}

}  // namespace mt::dice_detail
