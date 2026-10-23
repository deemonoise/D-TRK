#include <string.h>
#include <initializer_list>
#include <vector>
#include <unity.h>
#include "demos.h"
#include "dice.h"
#include "presets_factory.h"
#include "project_io.h"
#include "scale.h"
#include "templates.h"

using namespace mt;

// Large objects live in static storage.
static Project p, before;
static DiceSnap snap;

void setUp() {}
void tearDown() {}

void test_genres_table() {
  TEST_ASSERT_TRUE(diceGenreCount() >= 5);
  for (int i = 0; i < diceGenreCount(); ++i) {
    const DiceGenre& g = diceGenre(i);
    TEST_ASSERT_TRUE(strlen(g.name) <= 10);
    TEST_ASSERT_TRUE(g.bpmLo >= 20 && g.bpmLo <= g.bpmHi && g.bpmHi <= 300);
    for (ScaleType s : g.scales) TEST_ASSERT_EQUAL(7, scaleDegrees(s));
  }
  TEST_ASSERT_EQUAL_STRING(diceGenre(0).name, diceGenre(-5).name);  // clamped
}

void test_tempo_layer_in_ranges() {
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    p.reset();
    const DiceResult r = diceRoll(p, kDiceTempo, 0, seed, snap);
    TEST_ASSERT_TRUE(r.genre >= 0 && r.genre < diceGenreCount());
    const DiceGenre& g = diceGenre(r.genre);
    TEST_ASSERT_TRUE(p.bpm >= g.bpmLo && p.bpm <= g.bpmHi);
    TEST_ASSERT_TRUE(p.scaleRoot < 12);
    bool ok = false;
    for (ScaleType s : g.scales) ok |= static_cast<uint8_t>(s) == p.scaleType;
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(kDiceTempo, snap.layers);
  }
}

void test_tempo_touches_nothing_else() {
  templateBuild(1, p);
  memcpy(&before, &p, sizeof(Project));
  diceRoll(p, kDiceTempo, 0, 7, snap);
  p.bpm = before.bpm;
  p.scaleRoot = before.scaleRoot;
  p.scaleType = before.scaleType;
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
}

void test_undo_restores_tempo_one_step() {
  templateBuild(1, p);
  p.bpm = 77;
  memcpy(&before, &p, sizeof(Project));
  diceRoll(p, kDiceTempo, 0, 3, snap);
  TEST_ASSERT_TRUE(p.bpm != 77);
  TEST_ASSERT_EQUAL(kDiceTempo, diceUndo(p, snap));
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
  TEST_ASSERT_EQUAL(0, diceUndo(p, snap));  // one step
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
}

void test_nothing_rolled_nothing_to_undo() {
  p.reset();
  memcpy(&before, &p, sizeof(Project));
  const DiceResult r = diceRoll(p, 0, 0, 1, snap);
  TEST_ASSERT_EQUAL(-1, r.genre);
  TEST_ASSERT_EQUAL(0, r.written);
  TEST_ASSERT_EQUAL(0, diceUndo(p, snap));
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
}

// The two-phase roll of the UI: the draft (no lock) leaves the project alone, the apply writes it.
void test_draft_then_apply() {
  templateBuild(1, p);
  memcpy(&before, &p, sizeof(Project));
  const DiceResult r = diceDraft(p, kDiceTempo, 0, 3, snap);
  TEST_ASSERT_TRUE(r.genre >= 0);
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
  TEST_ASSERT_EQUAL(0, snap.layers);  // nothing to undo before the apply
  TEST_ASSERT_EQUAL(kDiceTempo, diceApply(p, snap));
  TEST_ASSERT_TRUE(p.bpm >= diceGenre(r.genre).bpmLo && p.bpm <= diceGenre(r.genre).bpmHi);
  TEST_ASSERT_EQUAL(0, diceApply(p, snap));  // applied once
  TEST_ASSERT_EQUAL(kDiceTempo, diceUndo(p, snap));
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
}

namespace {
// Factory category of an instrument built from a preset (by type and name), nullptr if none.
const char* categoryOf(const Instrument& m) {
  for (int i = 0; i < factoryCount(m.type); ++i)
    if (strcmp(factoryPreset(m.type, i).name, m.name) == 0) return factoryPreset(m.type, i).category;
  return nullptr;
}
bool isOneOf(const char* s, const char* a, const char* b = nullptr) {
  return s && (strcmp(s, a) == 0 || (b && strcmp(s, b) == 0));
}
}  // namespace

void test_slots_default_and_conflicts() {
  p.reset();
  uint8_t s[kDiceRoles];
  diceSlots(p, s);
  TEST_ASSERT_EQUAL(0, s[0]);
  TEST_ASSERT_EQUAL(1, s[1]);
  TEST_ASSERT_EQUAL(2, s[2]);
  TEST_ASSERT_EQUAL(3, s[3]);
  // KIT in INS28: drums in INS17..24. INS18 is inside that block, INS28 is the kit: both move. INS6
  // plays on track 6: LEAD moves too. The free slots skip the role tracks' own (INS1..4, INS28).
  p.tracks[0].instr = 27;
  p.tracks[1].instr = 17;
  p.tracks[2].instr = 27;
  p.tracks[3].instr = 5;
  diceSlots(p, s);
  TEST_ASSERT_EQUAL(27, s[0]);
  TEST_ASSERT_EQUAL(0, s[1]);
  TEST_ASSERT_EQUAL(1, s[2]);
  TEST_ASSERT_EQUAL(2, s[3]);
}

// An instrument another track plays (or a KIT lane of its) is never overwritten: the role moves to a
// free slot, else it is skipped.
void test_slots_spare_other_tracks() {
  p.reset();
  p.tracks[5].instr = 1;  // track 6 shares BASS' instrument
  uint8_t s[kDiceRoles];
  diceSlots(p, s);
  TEST_ASSERT_EQUAL(5, s[1]);  // the first one no track plays (INS25..32: the kit block)
  // Tracks 7 and 8 play KITs whose lanes take INS17..24 and INS6: no slot left for BASS.
  instrSetType(p.instruments[6], InstrType::Kit);
  for (int l = 0; l < kKitLanes; ++l) p.instruments[6].kit[l].instr = static_cast<uint8_t>(16 + l);
  instrSetType(p.instruments[7], InstrType::Kit);
  p.instruments[7].kit[0].instr = 5;
  diceSlots(p, s);
  TEST_ASSERT_EQUAL(kNoInstr, s[1]);
  TEST_ASSERT_EQUAL(0, s[0]);
  TEST_ASSERT_EQUAL(2, s[2]);
  TEST_ASSERT_EQUAL(3, s[3]);
  memcpy(&before, &p, sizeof(Project));
  const DiceResult r = diceRoll(p, kDiceSounds, 0, 5, snap);
  TEST_ASSERT_EQUAL(0x000D, r.written);
  TEST_ASSERT_EQUAL_MEMORY(&before.tracks[1], &p.tracks[1], sizeof(TrackCfg));
  for (int i = 1; i < 24; ++i)
    if (i != 2 && i != 3) TEST_ASSERT_EQUAL_MEMORY(&before.instruments[i], &p.instruments[i], sizeof(Instrument));
  // A MIDI track does not play its instrument.
  p.reset();
  p.tracks[5].instr = 1;
  p.tracks[5].out = TrackOut::Midi;
  diceSlots(p, s);
  TEST_ASSERT_EQUAL(1, s[1]);
}

// DRUMS is skipped when another track plays the kit or an instrument of the kit block.
void test_slots_kit_block_in_use() {
  p.reset();
  p.tracks[8].instr = 26;  // inside INS25..32
  uint8_t s[kDiceRoles];
  diceSlots(p, s);
  TEST_ASSERT_EQUAL(kNoInstr, s[0]);
  TEST_ASSERT_EQUAL(1, s[1]);
  memcpy(&before, &p, sizeof(Project));
  const DiceResult r = diceRoll(p, kDiceSounds, 0, 5, snap);
  TEST_ASSERT_EQUAL(0x000E, r.written);
  TEST_ASSERT_EQUAL_MEMORY(&before.tracks[0], &p.tracks[0], sizeof(TrackCfg));
  TEST_ASSERT_EQUAL_MEMORY(&before.instruments[0], &p.instruments[0], sizeof(Instrument));
  TEST_ASSERT_EQUAL_MEMORY(&before.instruments[24], &p.instruments[24], sizeof(Instrument) * kKitLanes);
  p.reset();
  p.tracks[8].instr = 0;  // shares DRUMS' instrument
  diceSlots(p, s);
  TEST_ASSERT_EQUAL(kNoInstr, s[0]);
}

// Mute / solo are the performer's (and the song's): a roll and its undo keep the live ones.
void test_sounds_keep_mute_solo() {
  p.reset();
  memcpy(&before, &p, sizeof(Project));
  diceDraft(p, kDiceSounds, 0, 9, snap);
  p.tracks[1].mute = true;  // after the draft, before the apply
  p.tracks[2].solo = true;
  diceApply(p, snap);
  TEST_ASSERT_TRUE(p.tracks[1].mute);
  TEST_ASSERT_TRUE(p.tracks[2].solo);
  TEST_ASSERT_EQUAL_STRING("BASS", p.tracks[1].name);
  p.tracks[1].mute = false;
  p.tracks[3].mute = true;
  TEST_ASSERT_EQUAL(kDiceSounds, diceUndo(p, snap));
  TEST_ASSERT_FALSE(p.tracks[1].mute);
  TEST_ASSERT_TRUE(p.tracks[2].solo);
  TEST_ASSERT_TRUE(p.tracks[3].mute);
  TEST_ASSERT_EQUAL_STRING(before.tracks[1].name, p.tracks[1].name);  // the rest is undone
}

void test_sounds_roles() {
  for (uint32_t seed = 1; seed <= 60; ++seed) {
    p.reset();
    p.tracks[1].out = TrackOut::Midi;
    memcpy(&before, &p, sizeof(Project));
    const DiceResult r = diceRoll(p, kDiceSounds, 0, seed, snap);
    TEST_ASSERT_EQUAL(0x000F, r.written);
    TEST_ASSERT_EQUAL(0x000F, snap.cfgMask);
    TEST_ASSERT_TRUE(p.trackIsDrum(0));
    const Instrument& kit = *p.kitOf(0);
    TEST_ASSERT_TRUE(categoryOf(kit) != nullptr);  // a factory KIT
    const int block = factoryKitBlock(p.tracks[0].instr);
    for (int l = 0; l < kKitLanes; ++l)
      if (kit.kit[l].instr != kNoInstr) {
        TEST_ASSERT_EQUAL(block + l, kit.kit[l].instr);
        TEST_ASSERT_TRUE(snap.instrMask & (1u << (block + l)));
      }
    for (int k = 0; k < kDiceRoles; ++k) {
      const TrackCfg& c = p.tracks[kDiceTrack[k]];
      TEST_ASSERT_TRUE(c.out == TrackOut::Int);
      TEST_ASSERT_EQUAL_STRING(diceRoleName(static_cast<DiceRole>(k)), c.name);
      TEST_ASSERT_TRUE(snap.instrMask & (1u << c.instr));
    }
    const Instrument& bass = p.instruments[p.tracks[1].instr];
    const Instrument& chords = p.instruments[p.tracks[2].instr];
    const Instrument& lead = p.instruments[p.tracks[3].instr];
    TEST_ASSERT_TRUE(isOneOf(categoryOf(bass), "BASS"));
    TEST_ASSERT_TRUE(isOneOf(categoryOf(chords), "PAD", "KEYS"));
    TEST_ASSERT_TRUE(isOneOf(categoryOf(lead), "LEAD", "PLUCK"));
    TEST_ASSERT_FALSE(chords.mono);
    for (const Instrument* m : {&bass, &chords, &lead}) {
      TEST_ASSERT_TRUE(m->type == InstrType::Chip || m->type == InstrType::Fm || m->type == InstrType::Synth);
      if (m->type == InstrType::Fm) TEST_ASSERT_EQUAL(static_cast<int>(FmMachine::Tone), m->machine);
    }
    // Patterns, other tracks, untouched instruments: as before.
    TEST_ASSERT_EQUAL_MEMORY(before.patterns, p.patterns, sizeof(p.patterns));
    TEST_ASSERT_EQUAL_MEMORY(&before.tracks[4], &p.tracks[4], sizeof(TrackCfg) * (kTracks - 4));
    for (int i = 0; i < kInstruments; ++i)
      if (!(snap.instrMask & (1u << i)))
        TEST_ASSERT_EQUAL_MEMORY(&before.instruments[i], &p.instruments[i], sizeof(Instrument));
    TEST_ASSERT_EQUAL(kDiceSounds, diceUndo(p, snap));
    TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
  }
}

void test_sounds_vary_with_seed() {
  char first[9] = {0};
  int differ = 0;
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    p.reset();
    diceRoll(p, kDiceSounds, 0, seed, snap);
    const char* n = p.instruments[p.tracks[3].instr].name;
    if (seed == 1) memcpy(first, n, sizeof(first));
    else differ += strcmp(first, n) != 0;
  }
  TEST_ASSERT_TRUE(differ >= 10);
}

// Every genre names factory kits; a roll with TEMPO mostly (~70 %) takes one of its genre's kits.
void test_genre_kits() {
  for (int g = 0; g < diceGenreCount(); ++g)
    for (const char* k : diceGenre(g).kits) {
      bool found = false;
      for (int i = 0; i < factoryCount(InstrType::Kit); ++i) found |= strcmp(factoryPreset(InstrType::Kit, i).name, k) == 0;
      TEST_ASSERT_TRUE_MESSAGE(found, k);
    }
  int match = 0;
  const int n = 200;
  for (uint32_t seed = 1; seed <= n; ++seed) {
    p.reset();
    const DiceResult r = diceRoll(p, kDiceTempo | kDiceSounds, 0, seed, snap);
    const char* kit = p.kitOf(0)->name;
    for (const char* k : diceGenre(r.genre).kits) match += strcmp(kit, k) == 0;
  }
  TEST_ASSERT_TRUE(match > n * 55 / 100 && match < n * 95 / 100);
  // SOUNDS alone: the genre of the project's BPM.
  TEST_ASSERT_EQUAL_STRING("TECHNO", diceGenre(diceGenreOfBpm(130)).name);
  TEST_ASSERT_EQUAL(-1, diceGenreOfBpm(150));
  match = 0;
  for (uint32_t seed = 1; seed <= n; ++seed) {
    p.reset();
    p.bpm = 130;
    diceRoll(p, kDiceSounds, 0, seed, snap);
    const char* kit = p.kitOf(0)->name;
    match += strcmp(kit, "909 KIT") == 0 || strcmp(kit, "909 HARD") == 0;
  }
  TEST_ASSERT_TRUE(match > n * 55 / 100);
}

void test_drums_euclid_on_kit_track() {
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    templateBuild(1, p);  // 808 SET: T1 KIT with 6 lanes (INS9..14), lanes 7, 8 silent
    p.patterns[2].steps[0][1].fx[0] = {Fx::PRB, 50};  // a user fx: the roll clears the track
    p.patterns[2].trackLen[0] = 5;
    memcpy(&before, &p, sizeof(Project));
    diceRoll(p, kDicePattern, 2, seed, snap);
    TEST_ASSERT_TRUE(snap.patMask & 1u);
    const Pattern& pt = p.patterns[2];
    TEST_ASSERT_EQUAL(0, pt.trackLen[0]);
    TEST_ASSERT_TRUE(pt.steps[0][0].hasNote());
    TEST_ASSERT_TRUE(pt.steps[0][0].vel & 1u);  // the kick on step 1
    int hits = 0;
    for (int s = 0; s < kMaxSteps; ++s) {
      const Step& st = pt.steps[0][s];
      TEST_ASSERT_FALSE(st.hasFx());
      if (s >= pt.length) TEST_ASSERT_TRUE(st.isEmpty());
      if (!st.hasNote()) continue;
      ++hits;
      TEST_ASSERT_EQUAL(0, st.vel & 0xC0);  // lanes 7, 8 play nothing in this kit
    }
    TEST_ASSERT_TRUE(hits >= 4);
    TEST_ASSERT_EQUAL_MEMORY(&before.instruments, &p.instruments, sizeof(p.instruments));
    TEST_ASSERT_EQUAL(kDicePattern, diceUndo(p, snap));
    TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
  }
}

void test_drums_skipped_without_kit() {
  p.reset();  // T1 plays INS1 (FM TONE): no drum track
  diceRoll(p, kDicePattern, 0, 5, snap);
  TEST_ASSERT_FALSE(snap.patMask & 1u);
  for (int s = 0; s < kMaxSteps; ++s) TEST_ASSERT_TRUE(p.patterns[0].steps[0][s].isEmpty());
}

namespace {
// Every note of melodic track t (and every CHD chord note) is in the project's scale; returns the notes.
int checkInScale(const Project& q, int pat, int t) {
  const ScaleType sc = static_cast<ScaleType>(q.scaleType);
  int notes = 0;
  for (int s = 0; s < kMaxSteps; ++s) {
    const Step& st = q.patterns[pat].steps[t][s];
    if (!st.hasNote()) continue;
    ++notes;
    TEST_ASSERT_TRUE_MESSAGE(inScale(st.note, q.scaleRoot, sc), "note");
    if (const FxSlot* f = st.find(Fx::CHD)) {
      uint8_t out[4];
      const int n = chordNotes(st.note, f->val, q.scaleRoot, sc, out);
      for (int i = 0; i < n; ++i) TEST_ASSERT_TRUE_MESSAGE(inScale(out[i], q.scaleRoot, sc), "chord");
    }
  }
  return notes;
}
}  // namespace

void test_pattern_notes_in_scale() {
  static const uint8_t kLens[] = {16, 32, 12, 64};
  for (uint32_t seed = 1; seed <= 80; ++seed) {
    p.reset();
    diceRoll(p, kDiceTempo | kDiceSounds, 0, seed * 7919u, snap);  // a KIT on T1, a genre scale
    p.patterns[1].length = kLens[seed % 4];
    memcpy(&before, &p, sizeof(Project));
    diceRoll(p, kDicePattern, 1, seed, snap);
    TEST_ASSERT_EQUAL(0x000F, snap.patMask);
    TEST_ASSERT_TRUE(checkInScale(p, 1, 1) > 0);  // bass
    TEST_ASSERT_TRUE(checkInScale(p, 1, 2) > 0);  // chords
    TEST_ASSERT_TRUE(checkInScale(p, 1, 3) > 0);  // lead
    bool chd = false;
    for (int s = 0; s < kMaxSteps; ++s) chd |= p.patterns[1].steps[2][s].find(Fx::CHD) != nullptr;
    TEST_ASSERT_TRUE(chd);
    // Tracks 5..16 and the other patterns: as before.
    for (int t = 4; t < kTracks; ++t)
      TEST_ASSERT_EQUAL_MEMORY(before.patterns[1].steps[t], p.patterns[1].steps[t], sizeof(p.patterns[1].steps[t]));
    TEST_ASSERT_EQUAL_MEMORY(&before.patterns[0], &p.patterns[0], sizeof(Pattern));
    TEST_ASSERT_EQUAL_MEMORY(&before.patterns[2], &p.patterns[2], sizeof(Pattern) * (kPatterns - 2));
    TEST_ASSERT_EQUAL(p.patterns[1].length, kLens[seed % 4]);
  }
}

void test_lead_without_chords_track() {
  templateBuild(1, p);  // INS1 is the 808 KIT
  p.scaleType = static_cast<uint8_t>(ScaleType::Dorian);
  p.scaleRoot = 9;
  p.tracks[2].instr = 0;  // T3 plays the KIT: no CHORDS role, the lead arps the tonic triad
  diceRoll(p, kDicePattern, 0, 11, snap);
  TEST_ASSERT_FALSE(snap.patMask & (1u << 2));
  TEST_ASSERT_TRUE(checkInScale(p, 0, 3) > 0);
  TEST_ASSERT_TRUE(checkInScale(p, 0, 1) > 0);  // the bass still follows the progression
}

void test_pattern_turns_gen_off() {
  templateBuild(1, p);
  p.gen[1].mode = static_cast<uint8_t>(GenMode::Pitch);
  p.gen[7].mode = static_cast<uint8_t>(GenMode::Pitch);
  diceRoll(p, kDicePattern, 0, 3, snap);
  TEST_ASSERT_EQUAL(static_cast<int>(GenMode::Off), p.gen[1].mode);
  TEST_ASSERT_EQUAL(static_cast<int>(GenMode::Pitch), p.gen[7].mode);
  diceUndo(p, snap);
  TEST_ASSERT_EQUAL(static_cast<int>(GenMode::Pitch), p.gen[1].mode);
}

void test_pattern_turns_var_off() {
  templateBuild(1, p);
  p.var[1].mode = static_cast<uint8_t>(VarMode::Ca1d);
  p.var[7].mode = static_cast<uint8_t>(VarMode::Markov);
  diceRoll(p, kDicePattern, 0, 3, snap);
  TEST_ASSERT_EQUAL(static_cast<int>(VarMode::Off), p.var[1].mode);
  TEST_ASSERT_EQUAL(static_cast<int>(VarMode::Markov), p.var[7].mode);
  diceUndo(p, snap);
  TEST_ASSERT_EQUAL(static_cast<int>(VarMode::Ca1d), p.var[1].mode);
}

namespace {
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
Project loaded;

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
      for (int k = l + 1; k < kKitLanes; ++k) TEST_ASSERT_TRUE(m.kit[l].note != m.kit[k].note);
    }
  }
  for (const Pattern& pt : q.patterns) {
    TEST_ASSERT_TRUE(pt.length >= kMinSteps && pt.length <= kMaxSteps);
    for (int t = 0; t < kTracks; ++t)
      for (const Step& st : pt.steps[t])
        for (const FxSlot& f : st.fx) TEST_ASSERT_TRUE(f.cmd < Fx::Count);
  }
}
}  // namespace

void test_roll_all_valid_and_saves() {
  for (uint32_t seed = 1; seed <= 12; ++seed) {
    p.reset();
    const DiceResult r = diceRoll(p, kDiceAll, 0, seed, snap);
    TEST_ASSERT_EQUAL(0x000F, r.written);
    checkValid(p);
    VecSink out;
    TEST_ASSERT_TRUE(saveProject(p, out));
    VecSource in(out.buf);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, loaded)));
    TEST_ASSERT_EQUAL(p.bpm, loaded.bpm);
    TEST_ASSERT_EQUAL(p.scaleRoot, loaded.scaleRoot);
    TEST_ASSERT_EQUAL(p.scaleType, loaded.scaleType);
    TEST_ASSERT_EQUAL_MEMORY(p.patterns[0].steps, loaded.patterns[0].steps, sizeof(p.patterns[0].steps));
    for (int k = 0; k < kDiceRoles; ++k) {
      const int t = kDiceTrack[k];
      TEST_ASSERT_EQUAL(p.tracks[t].instr, loaded.tracks[t].instr);
      TEST_ASSERT_TRUE(loaded.tracks[t].out == TrackOut::Int);
      const Instrument& a = p.instruments[p.tracks[t].instr];
      const Instrument& b = loaded.instruments[p.tracks[t].instr];
      TEST_ASSERT_TRUE(a.type == b.type);
      TEST_ASSERT_EQUAL_STRING(a.name, b.name);
      TEST_ASSERT_EQUAL_MEMORY(a.macro, b.macro, sizeof(a.macro));
    }
  }
}

// From every template and demo, on two patterns: an ALL roll undoes bit for bit.
void test_roll_all_undo_bit_exact() {
  for (int src = 0; src < templateCount() + demoCount(); ++src) {
    for (int pat : {0, 3}) {
      if (src < templateCount()) templateBuild(src, p);
      else demoBuild(src - templateCount(), p);
      memcpy(&before, &p, sizeof(Project));
      diceRoll(p, kDiceAll, pat, 1000u + static_cast<uint32_t>(src), snap);
      checkValid(p);
      TEST_ASSERT_EQUAL(kDiceAll, diceUndo(p, snap));
      TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
    }
  }
}

void test_unrelated_edits_survive_undo() {
  p.reset();
  diceRoll(p, kDiceAll, 0, 42, snap);
  p.instruments[10].vol = 33;            // not a role slot, not the kit block
  p.tracks[8].vol = 44;                  // not a role track
  p.patterns[0].steps[9][2].note = 61;   // not a role track
  p.patterns[5].steps[1][0].note = 50;   // another pattern
  diceUndo(p, snap);
  TEST_ASSERT_EQUAL(33, p.instruments[10].vol);
  TEST_ASSERT_EQUAL(44, p.tracks[8].vol);
  TEST_ASSERT_EQUAL(61, p.patterns[0].steps[9][2].note);
  TEST_ASSERT_EQUAL(50, p.patterns[5].steps[1][0].note);
}

void test_same_seed_same_roll_and_seeds_differ() {
  p.reset();
  diceRoll(p, kDiceAll, 0, 99, snap);
  memcpy(&before, &p, sizeof(Project));
  p.reset();
  diceRoll(p, kDiceAll, 0, 99, snap);
  TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof(Project));
  int differ = 0;
  for (uint32_t seed = 100; seed < 110; ++seed) {
    p.reset();
    diceRoll(p, kDiceAll, 0, seed, snap);
    differ += memcmp(before.patterns[0].steps, p.patterns[0].steps, sizeof(p.patterns[0].steps)) != 0;
  }
  TEST_ASSERT_EQUAL(10, differ);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_genres_table);
  RUN_TEST(test_tempo_layer_in_ranges);
  RUN_TEST(test_tempo_touches_nothing_else);
  RUN_TEST(test_undo_restores_tempo_one_step);
  RUN_TEST(test_nothing_rolled_nothing_to_undo);
  RUN_TEST(test_draft_then_apply);
  RUN_TEST(test_slots_default_and_conflicts);
  RUN_TEST(test_slots_spare_other_tracks);
  RUN_TEST(test_slots_kit_block_in_use);
  RUN_TEST(test_sounds_keep_mute_solo);
  RUN_TEST(test_sounds_roles);
  RUN_TEST(test_sounds_vary_with_seed);
  RUN_TEST(test_genre_kits);
  RUN_TEST(test_drums_euclid_on_kit_track);
  RUN_TEST(test_drums_skipped_without_kit);
  RUN_TEST(test_pattern_notes_in_scale);
  RUN_TEST(test_lead_without_chords_track);
  RUN_TEST(test_pattern_turns_gen_off);
  RUN_TEST(test_pattern_turns_var_off);
  RUN_TEST(test_roll_all_valid_and_saves);
  RUN_TEST(test_roll_all_undo_bit_exact);
  RUN_TEST(test_unrelated_edits_survive_undo);
  RUN_TEST(test_same_seed_same_roll_and_seeds_differ);
  return UNITY_END();
}
