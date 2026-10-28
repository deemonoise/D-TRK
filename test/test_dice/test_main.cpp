#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <vector>
#include <unity.h>
#include "arp_gen.h"
#include "bass_gen.h"
#include "chord_gen.h"
#include "demos.h"
#include "dice.h"
#include "groove.h"
#include "presets_factory.h"
#include "project_io.h"
#include "scale.h"
#include "templates.h"

using namespace mt;

// Large objects live in static storage.
static Project p, before, loaded;
static DiceSnap snap;
static Instrument tmp;

void setUp() {}
void tearDown() {}

namespace {

template <class T>
int listLen(const T* l) {
  int n = 0;
  if (l)
    while (l[n].w) ++n;
  return n;
}

int byName(int n, const char* (*nameOf)(int), const char* s) {
  for (int i = 0; i < n; ++i)
    if (nameOf(i) && strcmp(nameOf(i), s) == 0) return i;
  return -1;
}

int presetIndex(InstrType t, const char* name) {
  for (int i = 0; i < factoryCount(t); ++i)
    if (strcmp(factoryPreset(t, i).name, name) == 0) return i;
  return -1;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

DiceOpts opts(int g, int s, int variety = kDiceVarietyDefault, uint8_t layers = kDiceAll, int root = -1) {
  DiceOpts o;
  o.genre = static_cast<uint8_t>(g);
  o.style = static_cast<uint8_t>(s);
  o.variety = static_cast<uint8_t>(variety);
  o.layers = layers;
  o.root = static_cast<int8_t>(root);
  return o;
}

bool trackEmpty(const Pattern& pt, int t) {
  for (const Step& st : pt.steps[t])
    if (!st.isEmpty()) return false;
  return true;
}

int noteCount(const Pattern& pt, int t) {
  int n = 0;
  for (int i = 0; i < pt.length; ++i) n += pt.steps[t][i].hasNote();
  return n;
}

// A KIT on DRUMS: the factory kit index kit in INS1 (its drums in INS25..32).
void kitOnDrums(Project& q, int kit = 0) { factoryBuildKit(kit, q, 0); }

struct VecSink : ByteSink {
  std::vector<uint8_t> buf;
  bool write(const void* d, size_t n) override {
    const uint8_t* b = static_cast<const uint8_t*>(d);
    buf.insert(buf.end(), b, b + n);
    return true;
  }
};
struct VecSource : ByteSource {
  const std::vector<uint8_t>& buf;
  size_t pos = 0;
  explicit VecSource(const std::vector<uint8_t>& b) : buf(b) {}
  bool read(void* d, size_t n) override {
    if (pos + n > buf.size()) return false;
    memcpy(d, buf.data() + pos, n);
    pos += n;
    return true;
  }
  bool skip(size_t n) override {
    if (pos + n > buf.size()) return false;
    pos += n;
    return true;
  }
};

void checkValid(const Project& q) {
  TEST_ASSERT_TRUE(q.bpm >= 20 && q.bpm <= 300);
  TEST_ASSERT_TRUE(q.scaleRoot < 12 && q.scaleType < static_cast<uint8_t>(ScaleType::Count));
  for (const TrackCfg& c : q.tracks) TEST_ASSERT_TRUE(c.instr < kInstruments);
  for (const Instrument& m : q.instruments) {
    TEST_ASSERT_TRUE(m.type < InstrType::Count);
    if (m.type != InstrType::Kit) continue;
    for (int l = 0; l < kKitLanes; ++l) {
      const uint8_t i = m.kit[l].instr;
      TEST_ASSERT_TRUE(i == kNoInstr || (i < kInstruments && q.instruments[i].type != InstrType::Kit));
    }
  }
  for (const Pattern& pt : q.patterns) {
    TEST_ASSERT_TRUE(pt.length >= kMinSteps && pt.length <= kMaxSteps);
    TEST_ASSERT_TRUE(pt.groove < grooveCount());
    for (int t = 0; t < kTracks; ++t)
      for (const Step& st : pt.steps[t])
        for (const FxSlot& f : st.fx) TEST_ASSERT_TRUE(f.cmd < Fx::Count);
  }
}

void checkHits(const char* s, int len) {
  TEST_ASSERT_NOT_NULL(s);
  const int n = static_cast<int>(strlen(s));
  TEST_ASSERT_TRUE_MESSAGE(n == 16 || n == 32, s);
  TEST_ASSERT_TRUE_MESSAGE(len % n == 0, s);
  for (const char* c = s; *c; ++c) TEST_ASSERT_NOT_NULL_MESSAGE(strchr(".xXo234", *c), s);
}

void checkNames(const DiceName* l, int n, const char* (*nameOf)(int), const char* what) {
  TEST_ASSERT_TRUE_MESSAGE(listLen(l) > 0, what);
  for (int i = 0; l[i].w; ++i) TEST_ASSERT_TRUE_MESSAGE(byName(n, nameOf, l[i].name) >= 0, l[i].name);
}

const char* grooveName(int i) { return grooveAt(i).name; }

}  // namespace

void test_genres_and_styles() {
  static const char* const kGenres[] = {"HOUSE", "TECHNO", "TRANCE", "DNB", "BREAKS",
                                        "HIPHOP", "IDM", "AMBIENT", "SYNTHWAVE"};
  static const int kStyles[] = {5, 6, 5, 5, 3, 3, 3, 3, 3};
  TEST_ASSERT_EQUAL(9, diceGenreCount());
  for (int g = 0; g < 9; ++g) {
    TEST_ASSERT_EQUAL_STRING(kGenres[g], diceGenreName(g));
    TEST_ASSERT_EQUAL(kStyles[g], diceStyleCount(g));
    for (int s = 0; s < diceStyleCount(g); ++s) TEST_ASSERT_TRUE(strlen(diceStyleName(g, s)) <= 11);
  }
  TEST_ASSERT_EQUAL_STRING("FULLON", diceStyleName(2, 1));
  TEST_ASSERT_EQUAL_STRING("UKG 2STEP", diceStyleName(4, 1));
  TEST_ASSERT_NULL(diceGenreName(9));
  TEST_ASSERT_NULL(diceRecipe(0, 5));
  TEST_ASSERT_EQUAL(0, diceStyleCount(-1));
}

void test_every_recipe_valid() {
  for (int i = 0; i < diceMotifCount(); ++i) {
    const int n = diceMotifSteps(diceMotifText(i));
    TEST_ASSERT_TRUE_MESSAGE(n == 16 || n == 32, diceMotifName(i));
  }
  for (const DiceFill* f = diceFills(); f->w; ++f) {
    TEST_ASSERT_EQUAL(8, strlen(f->kick));
    TEST_ASSERT_EQUAL(8, strlen(f->snare));
    TEST_ASSERT_EQUAL(8, strlen(f->perc));
  }
  for (int g = 0; g < diceGenreCount(); ++g)
    for (int s = 0; s < diceStyleCount(g); ++s) {
      const DiceRecipe& r = *diceRecipe(g, s);
      char msg[40];
      snprintf(msg, sizeof(msg), "%s %s", diceGenreName(g), r.style);
      TEST_ASSERT_TRUE_MESSAGE(r.bpmLo >= 20 && r.bpmLo <= r.bpm && r.bpm <= r.bpmHi && r.bpmHi <= 300, msg);
      TEST_ASSERT_TRUE_MESSAGE(r.len == 32 || r.len == 64, msg);  // AUTO: groove 32, melodic 64
      TEST_ASSERT_TRUE_MESSAGE(r.res < Resolution::Count, msg);
      TEST_ASSERT_TRUE_MESSAGE(r.swing >= 50 && r.swing <= 75, msg);
      if (r.groove) TEST_ASSERT_TRUE_MESSAGE(byName(grooveCount(), grooveName, r.groove) > 0, r.groove);
      TEST_ASSERT_TRUE_MESSAGE(listLen(r.scales) > 0, msg);
      for (int i = 0; r.scales[i].w; ++i)
        TEST_ASSERT_EQUAL_MESSAGE(7, scaleDegrees(static_cast<ScaleType>(r.scales[i].v)), msg);
      TEST_ASSERT_TRUE_MESSAGE(r.roles != 0 && r.roles < (1u << kDiceRoles), msg);
      if (r.roles & diceRoleBit(DiceRole::Drums)) {
        // The kit: every lane a list of existing DRUM / FM / CHIP drums at a sane level.
        TEST_ASSERT_NOT_NULL_MESSAGE(r.kit, msg);
        for (int l = 0; l < kDiceLanes; ++l) {
          const DiceDrum* dl = r.kit->lane[l];
          TEST_ASSERT_TRUE_MESSAGE(listLen(dl) > 0, msg);
          for (int i = 0; dl[i].w; ++i) {
            const DiceDrum& d = dl[i];
            TEST_ASSERT_TRUE_MESSAGE(d.type == InstrType::Drum || d.type == InstrType::Fm || d.type == InstrType::Chip,
                                     d.name);
            TEST_ASSERT_TRUE_MESSAGE(presetIndex(d.type, d.name) >= 0, d.name);
            TEST_ASSERT_TRUE_MESSAGE(d.vol >= 30 && d.vol <= 127, d.name);
            TEST_ASSERT_TRUE_MESSAGE(d.dec >= -40 && d.dec <= 40 && d.tone >= -40 && d.tone <= 40, d.name);
            // The lane finder (PATTERNS on a kit already there) knows the drum by its name.
            const char* n = d.name;
            auto starts = [n](const char* x) { return strncmp(n, x, strlen(x)) == 0; };
            if (l == kDiceKick) TEST_ASSERT_TRUE_MESSAGE(starts("BD") || strstr(n, "KICK"), n);
            if (l == kDiceSnare) TEST_ASSERT_TRUE_MESSAGE(starts("SD") || strstr(n, "SN"), n);
            if (l == kDiceClap) TEST_ASSERT_TRUE_MESSAGE(starts("CP") || strstr(n, "CLAP"), n);
            if (l == kDiceHat)
              TEST_ASSERT_TRUE_MESSAGE((starts("CH") || starts("HH") || starts("HAT") || strstr(n, "HHR C") ||
                                        strstr(n, " HH")) &&
                                           !strstr(n, "HAT O") && n[strlen(n) - 1] != 'O',
                                       n);
            if (l == kDiceOpen) TEST_ASSERT_TRUE_MESSAGE(starts("OH") || strstr(n, "HAT O") || strstr(n, "HHR O"), n);
            if (l == kDicePerc)
              TEST_ASSERT_FALSE_MESSAGE(starts("BD") || strstr(n, "KICK") || starts("SD") || strstr(n, "SN") ||
                                            starts("CP") || strstr(n, "CLAP") || starts("CH") || starts("HH") ||
                                            starts("OH") || strstr(n, "HAT") || strstr(n, " HH") || strstr(n, "HHR"),
                                        n);
          }
        }
        // Balance: kick and snare / clap carry the groove, hats 4..12 dB under the kick, perc under the snare.
        const int kick = r.kit->lane[kDiceKick][0].vol, sn = r.kit->lane[kDiceSnare][0].vol;
        const int clap = r.kit->lane[kDiceClap][0].vol;
        TEST_ASSERT_TRUE_MESSAGE(kick >= sn && kick >= clap && kick >= 100, msg);
        for (int l : {kDiceHat, kDiceOpen}) {
          const float db = 20.f * log10f(static_cast<float>(r.kit->lane[l][0].vol) / kick);
          TEST_ASSERT_TRUE_MESSAGE(db <= -4.f && db >= -12.f, msg);
        }
        TEST_ASSERT_TRUE_MESSAGE(r.kit->lane[kDicePerc][0].vol < sn, msg);
        TEST_ASSERT_NOT_NULL_MESSAGE(r.feel, msg);
        TEST_ASSERT_TRUE_MESSAGE(r.feel->ghost < r.feel->vel[0] / 3 && r.feel->ghost > 0, msg);  // ghost < -9.5 dB
        for (int i = 0; i < 4; ++i) TEST_ASSERT_TRUE_MESSAGE(r.feel->vel[i] > r.feel->ghost, msg);
        TEST_ASSERT_TRUE_MESSAGE(r.feel->vel[0] >= r.feel->vel[1] && r.feel->accent >= r.feel->vel[0], msg);
        TEST_ASSERT_TRUE_MESSAGE(r.feel->human <= 100, msg);
        int lanes = 0;
        for (int l = 0; l < kDiceLanes; ++l) {
          if (!r.drums[l]) continue;
          ++lanes;
          TEST_ASSERT_TRUE_MESSAGE(listLen(r.drums[l]) > 0, msg);
          for (int i = 0; r.drums[l][i].w; ++i) checkHits(r.drums[l][i].name, r.len);
        }
        TEST_ASSERT_TRUE_MESSAGE(lanes > 0, msg);
      }
      for (int k = 1; k < kDiceRoles; ++k) {
        if (!(r.roles & (1u << k))) continue;
        const DiceSound* l = r.sounds[k];
        TEST_ASSERT_TRUE_MESSAGE(listLen(l) > 0, msg);
        for (int i = 0; l[i].w; ++i) {
          const int x = presetIndex(l[i].type, l[i].name);
          TEST_ASSERT_TRUE_MESSAGE(x >= 0, l[i].name);
          factoryBuild(l[i].type, x, tmp);
          // Melodic roles play notes as written (FM: TONE only); CHORDS needs polyphony.
          if (k != static_cast<int>(DiceRole::Fx) && tmp.type == InstrType::Fm)
            TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(FmMachine::Tone), tmp.machine, l[i].name);
          if (k == static_cast<int>(DiceRole::Chords)) TEST_ASSERT_FALSE_MESSAGE(tmp.mono, l[i].name);
        }
      }
      const bool melodic = r.roles & ~diceRoleBit(DiceRole::Drums) & ~diceRoleBit(DiceRole::Fx);
      if (melodic) {
        checkNames(r.progs, progFactoryCount(), progFactoryName, msg);
        Progression pr;
        for (int i = 0; r.progs[i].w; ++i)
          TEST_ASSERT_TRUE_MESSAGE(
              parseProgression(progFactoryText(byName(progFactoryCount(), progFactoryName, r.progs[i].name)), pr),
              r.progs[i].name);
        checkNames(r.rhythms, chordRhythmCount(), chordRhythmName, msg);
      }
      if (r.roles & diceRoleBit(DiceRole::Bass)) checkNames(r.basses, bassFactoryCount(), bassFactoryName, msg);
      if (r.roles & (diceRoleBit(DiceRole::Arp) | diceRoleBit(DiceRole::Lead))) {
        checkNames(r.arps, arpFactoryCount(), arpFactoryName, msg);
        TEST_ASSERT_TRUE_MESSAGE(listLen(r.arpModes) > 0, msg);
        for (int i = 0; r.arpModes[i].w; ++i) TEST_ASSERT_TRUE(r.arpModes[i].v < static_cast<uint8_t>(ArpMode::Count));
      }
      if (r.roles & diceRoleBit(DiceRole::Lead)) {
        TEST_ASSERT_TRUE_MESSAGE(listLen(r.leads) > 0, msg);
        for (int i = 0; r.leads[i].w; ++i) TEST_ASSERT_TRUE(r.leads[i].v < static_cast<uint8_t>(DiceLead::Count));
        checkNames(r.motifs, diceMotifCount(), diceMotifName, msg);
        for (int i = 0; r.motifs[i].w; ++i) {
          const int n = diceMotifSteps(diceMotifText(byName(diceMotifCount(), diceMotifName, r.motifs[i].name)));
          TEST_ASSERT_TRUE_MESSAGE(n > 0 && r.len % n == 0, r.motifs[i].name);
        }
      }
      if (r.fxHits)
        for (int i = 0; r.fxHits[i].w; ++i) checkHits(r.fxHits[i].name, r.len);
    }
}

void test_first_pattern_and_overwrites() {
  TEST_ASSERT_EQUAL(0, diceFirstPattern(0));
  TEST_ASSERT_EQUAL(5, diceFirstPattern(5));
  TEST_ASSERT_EQUAL(12, diceFirstPattern(12));
  TEST_ASSERT_EQUAL(12, diceFirstPattern(14));
  TEST_ASSERT_EQUAL(12, diceFirstPattern(15));
  p.reset();
  TEST_ASSERT_EQUAL(0, diceOverwrites(p, 14));
  p.patterns[13].steps[9][3].note = 60;  // any track counts
  p.patterns[11].steps[0][0].note = 60;  // outside P13..P16
  TEST_ASSERT_EQUAL(0x2, diceOverwrites(p, 14));
  TEST_ASSERT_EQUAL(0x5, diceOverwrites(p, 11));  // P12 and P14
}

// A roll at P15 writes P13..P16 only.
void test_other_patterns_untouched() {
  templateBuild(0, p);
  for (int i = 0; i < kPatterns; ++i) p.patterns[i].steps[1][i % 16].note = static_cast<uint8_t>(40 + i);
  memcpy(&before, &p, sizeof(Project));
  const DiceResult r = diceRoll(p, opts(0, 0), 14, 7, snap);
  TEST_ASSERT_EQUAL(12, r.first);
  for (int i = 0; i < 12; ++i) TEST_ASSERT_EQUAL_MEMORY(&before.patterns[i], &p.patterns[i], sizeof(Pattern));
  for (int i = 12; i < 16; ++i) TEST_ASSERT_TRUE(memcmp(&before.patterns[i], &p.patterns[i], sizeof(Pattern)) != 0);
}

void test_tracks_7_to_16_untouched() {
  for (int g = 0; g < diceGenreCount(); ++g) {
    demoBuild(g % demoCount(), p);
    for (int pt = 0; pt < kPatterns; ++pt)
      for (int t = 6; t < kTracks; ++t) p.patterns[pt].steps[t][(pt + t) % 16].note = static_cast<uint8_t>(30 + t);
    memcpy(&before, &p, sizeof(Project));
    diceRoll(p, opts(g, 0, 60, kDiceAll | kDiceSong), 0, 100u + static_cast<uint32_t>(g), snap);
    for (int t = 6; t < kTracks; ++t) {
      TEST_ASSERT_EQUAL_MEMORY(&before.tracks[t], &p.tracks[t], sizeof(TrackCfg));
      TEST_ASSERT_EQUAL_MEMORY(&before.gen[t], &p.gen[t], sizeof(GenCfg));
      TEST_ASSERT_EQUAL_MEMORY(&before.var[t], &p.var[t], sizeof(VarCfg));
      for (int pt = 0; pt < kPatterns; ++pt) {
        TEST_ASSERT_EQUAL_MEMORY(before.patterns[pt].steps[t], p.patterns[pt].steps[t], sizeof(p.patterns[pt].steps[t]));
        TEST_ASSERT_EQUAL(before.patterns[pt].trackLen[t], p.patterns[pt].trackLen[t]);
        TEST_ASSERT_EQUAL(before.patterns[pt].trackSpeed[t], p.patterns[pt].trackSpeed[t]);
      }
    }
    // Instruments the roll did not write stay (INS7..16: no role slot on a fresh demo / reset project).
  }
}

void test_same_seed_same_roll() {
  for (int g = 0; g < diceGenreCount(); ++g) {
    p.reset();
    diceRoll(p, opts(g, 0, 50, kDiceAll | kDiceSong), 2, 99, snap);
    memcpy(&before, &p, sizeof(Project));
    p.reset();
    diceRoll(p, opts(g, 0, 50, kDiceAll | kDiceSong), 2, 99, snap);
    TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
  }
  // Seeds differ (Variety 30, root drawn).
  p.reset();
  diceRoll(p, opts(2, 0), 0, 99, snap);
  memcpy(&before, &p, sizeof(Project));
  int differ = 0;
  for (uint32_t seed = 100; seed < 110; ++seed) {
    p.reset();
    diceRoll(p, opts(2, 0), 0, seed, snap);
    differ += memcmp(&before.patterns[0], &p.patterns[0], sizeof(Pattern)) != 0;
  }
  TEST_ASSERT_TRUE(differ >= 9);
}

// The kit is built lane by lane (canon drums at their levels and macros), the roles take the style's mix
// and tone caps, drum hits follow the feel.
void test_kit_mix_and_feel() {
  for (int g = 0; g < diceGenreCount(); ++g)
    for (int s = 0; s < diceStyleCount(g); ++s) {
      const DiceRecipe& rec = *diceRecipe(g, s);
      p.reset();
      diceRoll(p, opts(g, s, 0, kDiceAll, 0), 0, 3, snap);
      for (int k = 0; k < kDiceRoles; ++k) {
        if (!(rec.roles & (1u << k))) continue;
        const TrackCfg& c = p.tracks[k];
        const DiceTone& t = rec.mix->role[k];
        TEST_ASSERT_EQUAL_MESSAGE(t.vol, c.vol, rec.style);
        TEST_ASSERT_EQUAL_MESSAGE(t.pan, c.pan, rec.style);
        TEST_ASSERT_TRUE_MESSAGE(t.drv == kDiceKeep || c.drive <= t.drv, rec.style);
        TEST_ASSERT_TRUE_MESSAGE(t.crush == kDiceKeep || (c.crushBits <= t.crush && c.crushRate <= t.crush), rec.style);
        const Instrument& m = p.instruments[c.instr];
        if (k == 0) {
          TEST_ASSERT_EQUAL(InstrType::Kit, m.type);
          TEST_ASSERT_EQUAL(0, c.humanize);  // Variety 0
          for (int l = 0; l < kDiceLanes; ++l) {
            const KitLane& ln = m.kit[kDiceKitLane[l]];
            const DiceDrum& d = rec.kit->lane[l][0];
            TEST_ASSERT_TRUE_MESSAGE(ln.instr < kInstruments, rec.style);
            const Instrument& dm = p.instruments[ln.instr];
            TEST_ASSERT_EQUAL_STRING(d.name, dm.name);
            TEST_ASSERT_EQUAL(d.vol, dm.vol);
            TEST_ASSERT_EQUAL(0, dm.drive);
            if (l == kDiceKick) TEST_ASSERT_EQUAL(0, dm.rsend);
            if (d.type == InstrType::Drum || d.type == InstrType::Fm) {
              Instrument ref;
              factoryBuild(d.type, presetIndex(d.type, d.name), ref);
              TEST_ASSERT_EQUAL(clampi(ref.macro[0] + d.dec, 0, 127), dm.macro[0]);
              TEST_ASSERT_EQUAL(clampi(ref.macro[1] + d.tone, 0, 127), dm.macro[1]);
            }
          }
          TEST_ASSERT_EQUAL(kNoInstr, m.kit[6].instr);
          continue;
        }
        if (k == static_cast<int>(DiceRole::Fx)) continue;
        // Melodic: no harsh leftovers.
        if (t.cut != kDiceKeep) {
          TEST_ASSERT_TRUE_MESSAGE(m.fltMode != static_cast<uint8_t>(FltMode::Off), m.name);
          if (m.fltMode == static_cast<uint8_t>(FltMode::Lp)) TEST_ASSERT_TRUE_MESSAGE(m.cutoff <= t.cut, m.name);
        }
        TEST_ASSERT_TRUE_MESSAGE(t.res == kDiceKeep || m.reso <= t.res, m.name);
        TEST_ASSERT_TRUE_MESSAGE(t.fenv == kDiceKeep || m.fenv <= t.fenv, m.name);
        TEST_ASSERT_TRUE_MESSAGE(m.attack >= t.atk && m.release >= t.rel, m.name);
        TEST_ASSERT_TRUE_MESSAGE(t.drv == kDiceKeep || (m.synFold <= t.drv && m.synRing <= t.drv), m.name);
        if (t.rvb != kDiceKeep) TEST_ASSERT_EQUAL(t.rvb, m.rsend);
        if (t.dly != kDiceKeep) TEST_ASSERT_EQUAL(t.dly, m.send);
      }
      if (!(rec.roles & 1)) continue;
      // Drum velocities: from the feel, never the track default.
      const Pattern& pt = p.patterns[0];
      for (int i = 0; i < pt.length; ++i) {
        const Step& st = pt.steps[0][i];
        if (!st.hasNote()) continue;
        TEST_ASSERT_TRUE_MESSAGE(st.note > 0, rec.style);
        TEST_ASSERT_TRUE_MESSAGE(st.note == rec.feel->accent || st.note == rec.feel->ghost ||
                                     st.note == rec.feel->vel[i & 3],
                                 rec.style);
      }
    }
  // Variety: humanize and a drum spread around the canon.
  p.reset();
  diceRoll(p, opts(0, 0, 100, kDiceAll, 0), 0, 3, snap);
  TEST_ASSERT_EQUAL(diceRecipe(0, 0)->feel->human, p.tracks[0].humanize);
}

// Variety 0: the heaviest options, no mutations, the canon BPM: only the tonic depends on the seed.
void test_variety_zero_is_canon() {
  for (int g = 0; g < diceGenreCount(); ++g)
    for (int s = 0; s < diceStyleCount(g); ++s) {
      p.reset();
      diceRoll(p, opts(g, s, 0, kDiceAll, 9), 0, 1, snap);
      memcpy(&before, &p, sizeof(Project));
      p.reset();
      diceRoll(p, opts(g, s, 0, kDiceAll, 9), 0, 123456, snap);
      TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
      TEST_ASSERT_EQUAL(diceRecipe(g, s)->bpm, p.bpm);
      TEST_ASSERT_EQUAL(9, p.scaleRoot);
      TEST_ASSERT_EQUAL(diceRecipe(g, s)->scales[0].v, p.scaleType);  // lists put the heaviest first
    }
  // Variety moves things: BPM within range, other picks.
  int differ = 0;
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    p.reset();
    diceRoll(p, opts(0, 0, 100, kDiceAll, 9), 0, seed, snap);
    TEST_ASSERT_TRUE(p.bpm >= diceRecipe(0, 0)->bpmLo && p.bpm <= diceRecipe(0, 0)->bpmHi);
    differ += memcmp(&before.patterns[0], &p.patterns[0], sizeof(Pattern)) != 0;
  }
  TEST_ASSERT_TRUE(differ >= 9);
}

// Roles the style lacks: empty in all four patterns and muted; the others unmuted, with notes.
void test_absent_roles_empty_and_muted() {
  for (int g = 0; g < diceGenreCount(); ++g)
    for (int s = 0; s < diceStyleCount(g); ++s) {
      p.reset();
      for (int t = 0; t < 6; ++t) {
        p.tracks[t].mute = (t % 2) == 0;
        p.patterns[3].steps[t][1].note = 50;
      }
      const DiceResult r = diceRoll(p, opts(g, s, 0, kDiceAll, 0), 3, 5, snap);
      const DiceRecipe& rec = *diceRecipe(g, s);
      for (int k = 0; k < kDiceRoles; ++k) {
        bool present = rec.roles & (1u << k);
        if (k == 3 && present) present = rec.leads[0].v != static_cast<uint8_t>(DiceLead::None);
        TEST_ASSERT_TRUE(r.written & (1u << k));
        TEST_ASSERT_EQUAL_MESSAGE(!present, p.tracks[k].mute, rec.style);
        int notes = 0;
        for (int i = 0; i < kDicePats; ++i) {
          const Pattern& pt = p.patterns[3 + i];
          TEST_ASSERT_EQUAL(rec.len, pt.length);
          if (!present) TEST_ASSERT_TRUE_MESSAGE(trackEmpty(pt, k), rec.style);
          notes += noteCount(pt, k);
        }
        if (present) TEST_ASSERT_TRUE_MESSAGE(notes > 0, rec.style);
      }
    }
}

// MAIN / FILL a simpler lead, FILL changes the drums' end, BREAK drops kick / bass / arp / lead, DROP has
// everything.
void test_four_patterns() {
  p.reset();
  diceRoll(p, opts(2, 0, 0, kDiceAll, 0), 0, 1, snap);  // TRANCE UPLIFT: every role, motif lead
  const Pattern &main = p.patterns[0], &fill = p.patterns[1], &brk = p.patterns[2], &drop = p.patterns[3];
  TEST_ASSERT_TRUE(noteCount(main, 3) > 0);
  TEST_ASSERT_EQUAL_MEMORY(main.steps[3], fill.steps[3], sizeof(main.steps[3]));
  TEST_ASSERT_TRUE(noteCount(drop, 3) > 0);
  TEST_ASSERT_TRUE(noteCount(main, 1) > 0 && noteCount(main, 4) > 0 && noteCount(main, 2) > 0);
  TEST_ASSERT_EQUAL_MEMORY(main.steps[0], fill.steps[0], sizeof(Step) * (main.length - 8));
  TEST_ASSERT_TRUE(memcmp(main.steps[0], fill.steps[0], sizeof(main.steps[0])) != 0);
  TEST_ASSERT_EQUAL_MEMORY(main.steps[0], drop.steps[0], sizeof(main.steps[0]));
  TEST_ASSERT_TRUE(trackEmpty(brk, 1));
  TEST_ASSERT_TRUE(trackEmpty(brk, 3));
  TEST_ASSERT_TRUE(trackEmpty(brk, 4));
  TEST_ASSERT_TRUE(noteCount(brk, 2) > 0);
  // The kit's kick lane (909 KIT: lane 1) never plays in BREAK; MAIN has it on the beat.
  TEST_ASSERT_TRUE(main.steps[0][0].vel & 1);
  for (int i = 0; i < brk.length; ++i) TEST_ASSERT_FALSE(brk.steps[0][i].hasNote() && (brk.steps[0][i].vel & 1));
  TEST_ASSERT_TRUE(noteCount(brk, 0) > 0);
  // FX riser in FILL's last 8 steps.
  TEST_ASSERT_TRUE(fill.steps[5][fill.length - 8].hasNote());
}

void test_undo_bit_exact() {
  int n = 0;
  for (int src = 0; src < templateCount() + demoCount(); ++src)
    for (int pat : {0, 14}) {
      if (src < templateCount()) templateBuild(src, p);
      else demoBuild(src - templateCount(), p);
      p.songLen = 3;
      p.songTrn[1] = 5;
      p.songBpm[2] = 140;
      p.tracks[2].mute = true;
      p.tracks[4].solo = true;
      memcpy(&before, &p, sizeof(Project));
      const int g = n % diceGenreCount();
      const DiceOpts o = opts(g, n % diceStyleCount(g), 30 + n * 7 % 70, kDiceAll | kDiceSong);
      ++n;
      diceRoll(p, o, pat, 1000u + static_cast<uint32_t>(src), snap);
      checkValid(p);
      TEST_ASSERT_TRUE(memcmp(&before, &p, sizeof(Project)) != 0);
      TEST_ASSERT_EQUAL(kDiceAll | kDiceSong, diceUndo(p, snap));
      TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
      TEST_ASSERT_EQUAL(0, diceUndo(p, snap));
    }
}

void test_song_write() {
  p.reset();
  for (int i = 0; i < 20; ++i) {
    p.song[i] = 9;
    p.songTrn[i] = 3;
    p.songBpm[i] = 150;
  }
  p.songLen = 20;
  memcpy(&before, &p, sizeof(Project));
  diceRoll(p, opts(0, 0), 5, 3, snap);  // Song OFF: untouched
  TEST_ASSERT_EQUAL_MEMORY(before.song, p.song, sizeof(p.song));
  TEST_ASSERT_EQUAL(20, p.songLen);
  TEST_ASSERT_EQUAL_MEMORY(before.songTrn, p.songTrn, sizeof(p.songTrn));
  diceRoll(p, opts(0, 0, 30, kDiceAll | kDiceSong), 5, 3, snap);
  static const uint8_t kOrder[] = {5, 5, 6, 7, 8, 8, 6};
  TEST_ASSERT_EQUAL(kDiceSongLen, p.songLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kOrder, p.song, 7);
  for (int i = 0; i < kDiceSongLen; ++i) {
    TEST_ASSERT_EQUAL(0, p.songTrn[i]);
    TEST_ASSERT_EQUAL(0, p.songBpm[i]);
    TEST_ASSERT_EQUAL(0, p.songMute[i]);
  }
  diceUndo(p, snap);
  TEST_ASSERT_EQUAL(20, p.songLen);
  TEST_ASSERT_EQUAL_MEMORY(before.songBpm, p.songBpm, sizeof(p.songBpm));
  // SONG without PATTERN does nothing.
  diceRoll(p, opts(0, 0, 30, kDiceTempo | kDiceSong), 5, 3, snap);
  TEST_ASSERT_EQUAL(20, p.songLen);
}

void test_scopes() {
  // TEMPO: BPM and key only.
  p.reset();
  memcpy(&before, &p, sizeof(Project));
  diceRoll(p, opts(3, 0, 0, kDiceTempo, 2), 0, 1, snap);
  TEST_ASSERT_EQUAL(174, p.bpm);
  TEST_ASSERT_EQUAL(2, p.scaleRoot);
  p.bpm = before.bpm;
  p.scaleRoot = before.scaleRoot;
  p.scaleType = before.scaleType;
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
  // SOUNDS: the role tracks' sounds, no steps, no mute.
  p.reset();
  memcpy(&before, &p, sizeof(Project));
  const DiceResult r = diceRoll(p, opts(0, 0, 0, kDiceSounds), 0, 1, snap);
  TEST_ASSERT_EQUAL(0x3F, r.written);  // HOUSE DEEP: every role
  TEST_ASSERT_TRUE(p.instruments[p.tracks[0].instr].type == InstrType::Kit);
  TEST_ASSERT_EQUAL_STRING("DEEP", p.instruments[p.tracks[0].instr].name);  // built for the style
  TEST_ASSERT_EQUAL_STRING("BASS", p.tracks[1].name);
  TEST_ASSERT_EQUAL_STRING("FX", p.tracks[5].name);
  TEST_ASSERT_EQUAL_MEMORY(before.patterns, p.patterns, sizeof(p.patterns));
  for (int t = 0; t < kTracks; ++t) TEST_ASSERT_EQUAL(before.tracks[t].mute, p.tracks[t].mute);
  // PATTERN without a KIT on track 1: DRUMS is left alone, the rest is written.
  p.reset();
  p.patterns[0].steps[0][2].note = 61;
  const DiceResult q = diceRoll(p, opts(0, 0, 0, kDicePattern), 0, 1, snap);
  TEST_ASSERT_EQUAL(0x3E, q.written);
  TEST_ASSERT_EQUAL(61, p.patterns[0].steps[0][2].note);
  TEST_ASSERT_TRUE(noteCount(p.patterns[0], 1) > 0);
  // With a KIT there, the drums are written on its lanes.
  p.reset();
  kitOnDrums(p, 0);
  diceRoll(p, opts(0, 0, 0, kDicePattern), 0, 1, snap);
  TEST_ASSERT_TRUE(noteCount(p.patterns[0], 0) > 0);
}

void test_draft_then_apply() {
  p.reset();
  memcpy(&before, &p, sizeof(Project));
  diceDraft(p, opts(1, 2), 0, 11, snap);
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));  // a draft only reads
  TEST_ASSERT_EQUAL(0, diceUndo(p, snap));
  TEST_ASSERT_EQUAL(kDiceAll, diceApply(p, snap));
  TEST_ASSERT_EQUAL(0, diceApply(p, snap));  // applied once
  TEST_ASSERT_EQUAL(kDiceAll, diceUndo(p, snap));
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
}

// A song slot's BPM holds the live tempo: the undo puts back the project BPM.
void test_undo_keeps_project_bpm() {
  p.reset();
  p.bpm = 150;  // live (a slot's)
  diceRoll(p, opts(0, 0, 0, kDiceTempo), 0, 1, snap, 120);
  TEST_ASSERT_EQUAL(122, p.bpm);
  diceUndo(p, snap, 122);
  TEST_ASSERT_EQUAL(120, p.bpm);
}

void test_slots() {
  p.reset();
  uint8_t s[kDiceRoles];
  diceSlots(p, s);
  for (int k = 0; k < kDiceRoles; ++k) TEST_ASSERT_EQUAL(k, s[k]);
  p.tracks[8].instr = 26;  // inside the kit block INS25..32: DRUMS skipped
  diceSlots(p, s);
  TEST_ASSERT_EQUAL(kNoInstr, s[0]);
  memcpy(&before, &p, sizeof(Project));
  diceRoll(p, opts(0, 0, 0, kDiceSounds), 0, 5, snap);
  TEST_ASSERT_EQUAL_MEMORY(&before.tracks[0], &p.tracks[0], sizeof(TrackCfg));
  TEST_ASSERT_EQUAL_MEMORY(&before.instruments[24], &p.instruments[24], sizeof(Instrument) * kKitLanes);
  p.reset();
  p.tracks[9].instr = 1;  // track 10 shares BASS' instrument: BASS moves to a free slot
  diceSlots(p, s);
  TEST_ASSERT_EQUAL(9, s[1]);  // INS10, now played by no track
}

void test_rolls_valid_and_save() {
  for (int g = 0; g < diceGenreCount(); ++g)
    for (int s = 0; s < diceStyleCount(g); ++s) {
      p.reset();
      diceRoll(p, opts(g, s, 80, kDiceAll | kDiceSong), 0, 7u + static_cast<uint32_t>(g * 10 + s), snap);
      checkValid(p);
      VecSink out;
      TEST_ASSERT_TRUE(saveProject(p, out));
      VecSource in(out.buf);
      TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, loaded)));
      TEST_ASSERT_EQUAL(p.bpm, loaded.bpm);
      for (int i = 0; i < kDicePats; ++i)
        TEST_ASSERT_EQUAL_MEMORY(p.patterns[i].steps, loaded.patterns[i].steps, sizeof(p.patterns[i].steps));
    }
}

void test_snapshot_size() {
  char msg[64];
  snprintf(msg, sizeof(msg), "sizeof(DiceSnap) = %u bytes", static_cast<unsigned>(sizeof(DiceSnap)));
  TEST_MESSAGE(msg);
  TEST_ASSERT_TRUE(sizeof(DiceSnap) < 160 * 1024);
}

// ---- Phrases -------------------------------------------------------------------------------------
namespace {
DiceOpts lenOpts(int g, int s, int variety, int len, int root = 0) {
  DiceOpts o = opts(g, s, variety, kDiceAll, root);
  o.len = static_cast<uint8_t>(len);
  return o;
}
int unitOf(int len) { return len <= 16 ? 8 : 16; }
bool unitsDiffer(const Pattern& pt, int t, int a, int b) {
  const int u = unitOf(pt.length);
  return memcmp(&pt.steps[t][a * u], &pt.steps[t][b * u], sizeof(Step) * u) != 0;
}
// Offset of unit j's last note step, -1 = none.
int lastOnset(const Pattern& pt, int t, int j) {
  const int u = unitOf(pt.length);
  for (int o = u - 1; o >= 0; --o)
    if (pt.steps[t][j * u + o].hasNote()) return o;
  return -1;
}
}  // namespace

// AUTO: melodic styles 64 steps, groove styles 32; an explicit length wins, others are AUTO.
void test_length_auto_and_explicit() {
  struct {
    int g, s, len;
  } const kAuto[] = {{2, 0, 64}, {2, 1, 64}, {2, 2, 32}, {1, 0, 32}, {1, 5, 64}, {0, 0, 64}, {0, 1, 32},
                     {3, 0, 64}, {3, 2, 32}, {4, 0, 32}, {5, 1, 64}, {5, 2, 32}, {7, 2, 64}, {8, 0, 64}};
  for (const auto& a : kAuto) {
    p.reset();
    diceRoll(p, opts(a.g, a.s, 0, kDiceAll, 0), 0, 1, snap);
    TEST_ASSERT_EQUAL_MESSAGE(a.len, p.patterns[0].length, diceRecipe(a.g, a.s)->style);
  }
  for (int len : {16, 32, 64, 128, 48}) {
    p.reset();
    diceRoll(p, lenOpts(2, 0, 50, len), 0, 3, snap);
    checkValid(p);
    for (int i = 0; i < kDicePats; ++i) TEST_ASSERT_EQUAL(len == 48 ? 64 : len, p.patterns[i].length);
    TEST_ASSERT_TRUE(noteCount(p.patterns[3], 3) > 0);
    TEST_ASSERT_TRUE(p.patterns[1].steps[5][p.patterns[1].length - 8].hasNote());  // the riser fits
  }
}

// Variety 0: A' and B differ from bar 1 in every role (DROP); 32 steps: A B.
void test_phrase_bars_differ() {
  p.reset();
  diceRoll(p, opts(2, 0, 0, kDiceAll, 0), 0, 1, snap);  // TRANCE UPLIFT, 64
  const Pattern& drop = p.patterns[3];
  for (int t : {0, 3})
    for (int j : {1, 3}) TEST_ASSERT_TRUE_MESSAGE(unitsDiffer(drop, t, 0, j), t ? "lead" : "drums");
  for (int t : {1, 2, 4}) TEST_ASSERT_TRUE(unitsDiffer(drop, t, 0, 3));
  // MAIN's lead: A / A' only, quieter.
  const Pattern& main = p.patterns[0];
  TEST_ASSERT_TRUE(memcmp(main.steps[3], drop.steps[3], sizeof(Step) * 64) != 0);
  for (int i = 0; i < 64; ++i)
    if (main.steps[3][i].hasNote()) TEST_ASSERT_EQUAL(80, main.steps[3][i].vel);
  TEST_ASSERT_TRUE(trackEmpty(p.patterns[2], 3));
  p.reset();
  diceRoll(p, opts(1, 2, 0, kDiceAll, 0), 0, 1, snap);  // TECHNO PEAK, 32
  TEST_ASSERT_TRUE(unitsDiffer(p.patterns[0], 0, 0, 1));
}

// Every style with drums, every length: the B bars' drums are not bar 1's (MAIN, Variety 0 and 100).
void test_drum_b_bars_differ() {
  for (int g = 0; g < diceGenreCount(); ++g)
    for (int s = 0; s < diceStyleCount(g); ++s) {
      if (!(diceRecipe(g, s)->roles & diceRoleBit(DiceRole::Drums))) continue;
      for (int len : {16, 32, 64, 128})
        for (int v : {0, 100}) {
          p.reset();
          diceRoll(p, lenOpts(g, s, v, len), 0, 5, snap);
          const Pattern& pt = p.patterns[0];
          const int n = len / unitOf(len);
          for (int j = n == 2 ? 1 : 3; j < n; j += 4)
            TEST_ASSERT_TRUE_MESSAGE(unitsDiffer(pt, 0, 0, j), diceRecipe(g, s)->style);
        }
    }
}

// The lead's B bar ends on the tonic or the fifth; the bass's last note before a bar's root approaches it.
void test_cadence_and_approach() {
  int leads = 0, basses = 0;
  for (int g = 0; g < diceGenreCount(); ++g)
    for (int s = 0; s < diceStyleCount(g); ++s) {
      const DiceRecipe& r = *diceRecipe(g, s);
      for (int root : {0, 5}) {
        p.reset();
        diceRoll(p, opts(g, s, 0, kDiceAll, root), 0, 1, snap);
        const Pattern& drop = p.patterns[3];
        const int n = drop.length / unitOf(drop.length);
        const ScaleType sc = static_cast<ScaleType>(p.scaleType);
        if ((r.roles & diceRoleBit(DiceRole::Lead)) && r.leads[0].v == static_cast<uint8_t>(DiceLead::Motif)) {
          const int o = lastOnset(drop, 3, n - 1);
          TEST_ASSERT_TRUE_MESSAGE(o >= 0, r.style);
          const int pc = (drop.steps[3][(n - 1) * unitOf(drop.length) + o].note - root + 120) % 12;
          const int fifth = (moveDegrees(60 + root, 4, static_cast<uint8_t>(root), sc) - root + 120) % 12;
          TEST_ASSERT_TRUE_MESSAGE(pc == 0 || pc == fifth, r.style);
          ++leads;
        }
        if ((r.roles & diceRoleBit(DiceRole::Bass)) && drop.steps[1][0].hasNote()) {
          const int u = unitOf(drop.length), o = lastOnset(drop, 1, n - 1);
          TEST_ASSERT_TRUE_MESSAGE(o >= u - 4, r.style);
          const int d = (drop.steps[1][(n - 1) * u + o].note - drop.steps[1][0].note + 120) % 12;
          TEST_ASSERT_TRUE_MESSAGE(d == 1 || d == 2 || d == 10 || d == 11, r.style);
          ++basses;
        }
      }
    }
  TEST_ASSERT_TRUE(leads >= 10 && basses >= 30);
}

// Variety 100: leads differ seed to seed, the third bar varies now and then.
void test_variety_100_diverse() {
  static Step leads[10][64];
  int third = 0, distinct = 0;
  for (int seed = 0; seed < 10; ++seed) {
    p.reset();
    diceRoll(p, lenOpts(2, 0, 100, 64), 0, 100u + static_cast<uint32_t>(seed), snap);
    memcpy(leads[seed], p.patterns[3].steps[3], sizeof(leads[seed]));
    third += unitsDiffer(p.patterns[0], 0, 0, 2);
    bool fresh = true;
    for (int k = 0; k < seed; ++k) fresh &= memcmp(leads[k], leads[seed], sizeof(leads[seed])) != 0;
    distinct += fresh;
  }
  TEST_ASSERT_TRUE(distinct >= 9);
  TEST_ASSERT_TRUE(third >= 2);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_genres_and_styles);
  RUN_TEST(test_every_recipe_valid);
  RUN_TEST(test_kit_mix_and_feel);
  RUN_TEST(test_first_pattern_and_overwrites);
  RUN_TEST(test_other_patterns_untouched);
  RUN_TEST(test_tracks_7_to_16_untouched);
  RUN_TEST(test_same_seed_same_roll);
  RUN_TEST(test_variety_zero_is_canon);
  RUN_TEST(test_absent_roles_empty_and_muted);
  RUN_TEST(test_four_patterns);
  RUN_TEST(test_undo_bit_exact);
  RUN_TEST(test_song_write);
  RUN_TEST(test_scopes);
  RUN_TEST(test_draft_then_apply);
  RUN_TEST(test_undo_keeps_project_bpm);
  RUN_TEST(test_slots);
  RUN_TEST(test_rolls_valid_and_save);
  RUN_TEST(test_snapshot_size);
  RUN_TEST(test_length_auto_and_explicit);
  RUN_TEST(test_phrase_bars_differ);
  RUN_TEST(test_drum_b_bars_differ);
  RUN_TEST(test_cadence_and_approach);
  RUN_TEST(test_variety_100_diverse);
  return UNITY_END();
}
