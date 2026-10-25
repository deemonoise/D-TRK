#include <string.h>
#include <unity.h>
#include "inst_codec.h"
#include "bytebeat_lib.h"
#include "presets_factory.h"
#include "synth.h"
#include "synth_fm_machines.h"
#include "wt_builtin.h"
#include <vector>

using namespace mt;

// Field by field: Instrument has padding, which copies do not keep.
static void assertSameInst(const Instrument& a, const Instrument& b, const char* msg) {
  TEST_ASSERT_EQUAL_STRING_MESSAGE(a.name, b.name, msg);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(a.sample, b.sample, msg);
#define SAME(f) TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(a.f), static_cast<int>(b.f), msg)
  SAME(type); SAME(vol); SAME(transpose); SAME(fine);
  SAME(attack); SAME(decay); SAME(sustain); SAME(release); SAME(mono); SAME(glide);
  SAME(wave); SAME(duty); SAME(pwmRate); SAME(pwmDepth);
  SAME(root); SAME(start); SAME(end); SAME(loop); SAME(loopStart); SAME(reverse);
  SAME(machine);
  for (int k = 0; k < kFmMacros; ++k) SAME(macro[k]);
  SAME(lfoWave); SAME(lfoRate); SAME(lfoDepth); SAME(lfoDest);
  SAME(fltMode); SAME(cutoff); SAME(reso); SAME(fenv); SAME(fAtk); SAME(fDec); SAME(keytrack);
  for (int k = 0; k < 2; ++k) {
    SAME(synOsc[k]);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(a.synWt[k], b.synWt[k], msg);
  }
  SAME(synSemi); SAME(synSync); SAME(synSub); SAME(synSubOct); SAME(synNoise); SAME(synEAtk); SAME(synEDec);
#undef SAME
}

void setUp() {}
void tearDown() {}

// Two factory DRUM presets per machine BDA .. HHR, unique short names; the ANALOG KIT plays them.
void test_drum2_presets() {
  int per[static_cast<int>(DrumMachine::Count)] = {};
  const int n = factoryCount(InstrType::Drum);
  for (int i = 0; i < n; ++i) {
    const FactoryPreset& f = factoryPreset(InstrType::Drum, i);
    TEST_ASSERT_TRUE_MESSAGE(strlen(f.name) <= 8, f.name);
    for (int j = 0; j < i; ++j) TEST_ASSERT_TRUE_MESSAGE(strcmp(f.name, factoryPreset(InstrType::Drum, j).name), f.name);
    Instrument m;
    factoryBuild(InstrType::Drum, i, m);
    TEST_ASSERT_TRUE(m.machine < static_cast<int>(DrumMachine::Count));
    ++per[m.machine];
  }
  for (int mc = kDrum2First; mc < static_cast<int>(DrumMachine::Count); ++mc) TEST_ASSERT_EQUAL(2, per[mc]);
  bool found = false;
  for (int i = 0; i < factoryCount(InstrType::Kit); ++i) {
    const FactoryPreset& k = factoryPreset(InstrType::Kit, i);
    if (strcmp(k.name, "ANALOG")) continue;
    found = true;
    TEST_ASSERT_EQUAL(8, k.drumCount);
    for (int l = 0; l < k.drumCount; ++l) {
      bool exists = false;
      for (int j = 0; j < n && !exists; ++j) exists = !strcmp(k.drums[l].name, factoryPreset(InstrType::Drum, j).name);
      TEST_ASSERT_TRUE_MESSAGE(exists, k.drums[l].name);
    }
  }
  TEST_ASSERT_TRUE(found);
}

void test_counts() {
  TEST_ASSERT_TRUE(factoryCount(InstrType::Chip) >= 30);
  TEST_ASSERT_TRUE(factoryCount(InstrType::Fm) >= 30);
  TEST_ASSERT_TRUE(factoryCount(InstrType::Drum) >= 30);
  TEST_ASSERT_TRUE(factoryCount(InstrType::Synth) >= 30);
  TEST_ASSERT_TRUE(factoryCount(InstrType::Kit) >= 12);
  TEST_ASSERT_TRUE(factoryCount(InstrType::Lofi) >= 12);
  TEST_ASSERT_EQUAL(0, factoryCount(InstrType::Sample));
}

void test_names_unique_and_short() {
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    const InstrType ty = static_cast<InstrType>(t);
    for (int i = 0; i < factoryCount(ty); ++i) {
      const FactoryPreset& a = factoryPreset(ty, i);
      TEST_ASSERT_TRUE(a.type == ty);
      TEST_ASSERT_TRUE(strlen(a.name) >= 1 && strlen(a.name) < sizeof(Instrument::name));
      TEST_ASSERT_TRUE(strlen(a.category) >= 1 && strlen(a.category) <= 16);
      for (int j = i + 1; j < factoryCount(ty); ++j)
        TEST_ASSERT_TRUE_MESSAGE(strcmp(a.name, factoryPreset(ty, j).name) != 0, a.name);
    }
  }
}

// The browser shows a category's presets together: each category is one run in the table.
void test_categories_grouped() {
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    const InstrType ty = static_cast<InstrType>(t);
    for (int i = 0; i < factoryCount(ty); ++i)
      for (int j = i + 2; j < factoryCount(ty); ++j)
        if (strcmp(factoryPreset(ty, i).category, factoryPreset(ty, j).category) == 0)
          TEST_ASSERT_EQUAL_STRING_MESSAGE(factoryPreset(ty, i).category, factoryPreset(ty, j - 1).category,
                                           factoryPreset(ty, j).name);
  }
}

void test_valid_values() {
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    const InstrType ty = static_cast<InstrType>(t);
    for (int i = 0; i < factoryCount(ty); ++i) {
      Instrument m;
      factoryBuild(ty, i, m);
      TEST_ASSERT_TRUE_MESSAGE(m.type == ty, factoryPreset(ty, i).name);
      TEST_ASSERT_EQUAL_STRING(factoryPreset(ty, i).name, m.name);
      // Clamped values survive pack -> unpack unchanged: everything is in range.
      uint8_t a[kInstRecSize], f[kFmRecSize], l[kFltRecSize], y[kSynRecSize];
      packInst(m, a);
      packFm(m, f);
      packFlt(m, l);
      packSyn(m, y);
      Instrument r;
      unpackInst(a, r);
      unpackFm(f, r);
      unpackFlt(l, r);
      unpackSyn(y, r);
      fixInstrument(r);
      assertSameInst(m, r, m.name);
    }
  }
}

// A preset's drive / crush are the track insert it recommends (copied to the track on load): the
// factory set keeps the values these presets had before the insert moved to the track.
void test_recommended_insert() {
  struct R { InstrType t; const char* name; uint8_t drive, bits, rate; };
  const R want[] = {
      {InstrType::Chip, "GRIT BAS", 40, 70, 0},  {InstrType::Fm, "KICK PNC", 40, 0, 0},
      {InstrType::Fm, "CHRD 5TH", 25, 0, 0},     {InstrType::Drum, "BD808 D", 45, 0, 0},
      {InstrType::Drum, "BD909 H", 30, 0, 0},    {InstrType::Drum, "BD DUSTY", 0, 85, 30},
      {InstrType::Drum, "SD DUSTY", 0, 90, 40},  {InstrType::Drum, "HH DUSTY", 0, 80, 50},
      {InstrType::Drum, "CP DUSTY", 0, 0, 60},   {InstrType::Drum, "RIM BIT", 0, 110, 0},
      {InstrType::Drum, "BD DIST", 60, 0, 0},    {InstrType::Drum, "BD RUMBL", 35, 0, 0},
      {InstrType::Drum, "SD NOISE", 60, 0, 0},   {InstrType::Drum, "HH HARSH", 60, 0, 0},
      {InstrType::Synth, "REESE", 25, 0, 0},     {InstrType::Synth, "FM GROWL", 45, 0, 0},
      {InstrType::Synth, "CHIPPLK", 0, 60, 0},   {InstrType::Synth, "ROBOT", 0, 60, 60},
      {InstrType::Chip, "SUB SINE", 0, 0, 0},
  };
  for (const R& r : want) {
    bool found = false;
    for (int i = 0; i < factoryCount(r.t); ++i) {
      if (strcmp(factoryPreset(r.t, i).name, r.name) != 0) continue;
      Instrument m;
      factoryBuild(r.t, i, m);
      TEST_ASSERT_EQUAL_MESSAGE(r.drive, m.drive, r.name);
      TEST_ASSERT_EQUAL_MESSAGE(r.bits, m.crushBits, r.name);
      TEST_ASSERT_EQUAL_MESSAGE(r.rate, m.crushRate, r.name);
      TEST_ASSERT_EQUAL_MESSAGE(0, m.driveModel, r.name);
      found = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(found, r.name);
  }
}

// A factory KIT's drums sit in project instruments behind KIT lanes: no track insert takes them, so
// they keep no insert (BD DIST's drive is dropped there).
void test_kit_drums_no_insert() {
  static Project p;
  for (int i = 0; i < factoryCount(InstrType::Kit); ++i) {
    p.reset();
    factoryBuildKit(i, p, 0);
    for (const Instrument& m : p.instruments) {
      TEST_ASSERT_EQUAL_MESSAGE(0, m.drive, m.name);
      TEST_ASSERT_EQUAL_MESSAGE(0, m.crushBits, m.name);
      TEST_ASSERT_EQUAL_MESSAGE(0, m.crushRate, m.name);
      TEST_ASSERT_EQUAL_MESSAGE(0, m.driveModel, m.name);
    }
  }
}

void test_build_resets_target() {
  Instrument m;
  strcpy(m.sample, "JUNK");
  m.reso = 99;
  factoryBuild(InstrType::Chip, 0, m);
  TEST_ASSERT_EQUAL_STRING("", m.sample);
}

// Factory SYNTH presets use built-in tables only: no project list needed.
void test_synth_presets_builtin_tables() {
  for (int i = 0; i < factoryCount(InstrType::Synth); ++i) {
    Instrument m;
    factoryBuild(InstrType::Synth, i, m);
    for (int k = 0; k < 2; ++k) {
      if (m.synOsc[k] == static_cast<uint8_t>(SynOsc::Wt))
        TEST_ASSERT_TRUE_MESSAGE(m.synWt[k][0] != 0, m.name);
      if (m.synWt[k][0]) TEST_ASSERT_TRUE_MESSAGE(wtBuiltinFind(m.synWt[k]) >= 0, m.name);
    }
  }
}

// LOFI: BYTE and APC presets, machine and formula in range (the formula survives its record).
void test_lofi_presets_valid() {
  int byteN = 0, apcN = 0;
  for (int i = 0; i < factoryCount(InstrType::Lofi); ++i) {
    Instrument m;
    factoryBuild(InstrType::Lofi, i, m);
    TEST_ASSERT_TRUE_MESSAGE(m.type == InstrType::Lofi, m.name);
    TEST_ASSERT_TRUE_MESSAGE(m.machine < static_cast<int>(LofiMachine::Count), m.name);
    TEST_ASSERT_TRUE_MESSAGE(m.lofiFormula < byteFormulaCount(), m.name);
    for (int k = 0; k < kFmMacros; ++k) TEST_ASSERT_TRUE_MESSAGE(m.macro[k] <= 127, m.name);
    byteN += m.machine == static_cast<uint8_t>(LofiMachine::Byte);
    apcN += m.machine == static_cast<uint8_t>(LofiMachine::Apc);
    if (m.machine == static_cast<uint8_t>(LofiMachine::Apc)) TEST_ASSERT_EQUAL_MESSAGE(0, m.macro[4], m.name);
    uint8_t b[kLofiRecSize];
    packLofi(m, b);
    Instrument r;
    unpackLofi(b, sizeof(b), r);
    fixInstrument(r);
    TEST_ASSERT_EQUAL_MESSAGE(m.lofiFormula, r.lofiFormula, m.name);
  }
  TEST_ASSERT_TRUE(byteN >= 8);
  TEST_ASSERT_TRUE(apcN >= 4);
}

// LOFI BYTE: A B C are the formula's defaults (else the formula sounds other than its original),
// except the presets built on other values on purpose. A few play in SYNC.
void test_lofi_byte_presets_use_formula_defaults() {
  static const char* const kOwnAbc[] = {"MASK 27"};  // 42 melody with mask 27 instead of 42
  int syncN = 0;
  for (int i = 0; i < factoryCount(InstrType::Lofi); ++i) {
    Instrument m;
    factoryBuild(InstrType::Lofi, i, m);
    TEST_ASSERT_TRUE_MESSAGE(m.lofiSync <= 1, m.name);
    if (m.machine != static_cast<uint8_t>(LofiMachine::Byte)) {
      TEST_ASSERT_EQUAL_MESSAGE(0, m.lofiSync, m.name);
      continue;
    }
    syncN += m.lofiSync;
    bool own = false;
    for (const char* n : kOwnAbc) own = own || strcmp(n, m.name) == 0;
    if (own) continue;
    const ByteFormula& f = byteFormula(m.lofiFormula);
    const uint8_t want[3] = {f.a, f.b, f.c};
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(want, m.macro, 3, m.name);
  }
  TEST_ASSERT_TRUE(syncN >= 3);
}

void test_chord_preset_is_maj7() {
  for (int i = 0; i < factoryCount(InstrType::Fm); ++i) {
    if (strcmp(factoryPreset(InstrType::Fm, i).name, "CHORD M7") != 0) continue;
    Instrument m;
    factoryBuild(InstrType::Fm, i, m);
    TEST_ASSERT_EQUAL_STRING("MAJ7", fmChordName(m.macro[kMacShp]));
    return;
  }
  TEST_FAIL_MESSAGE("no CHORD M7");
}

// Built-in wavetables, built on first use.
struct BuiltinWt : WtSource {
  mutable std::vector<int16_t> t[kWtBuiltins];
  const int16_t* findWt(const char* name) const override {
    const int i = wtBuiltinFind(name);
    if (i < 0) return nullptr;
    if (t[i].empty()) {
      t[i].resize(kWtTableSamples);
      WtBuiltinSrc src(i);
      wtBuild(src, t[i].data());
    }
    return t[i].data();
  }
};

void test_every_preset_sounds() {
  static BuiltinWt wt;
  static Project p;
  for (int t = 0; t < static_cast<int>(InstrType::Count); ++t) {
    const InstrType ty = static_cast<InstrType>(t);
    for (int i = 0; i < factoryCount(ty); ++i) {
      p.reset();
      p.masterVol = 100;
      p.tracks[0].out = TrackOut::Int;
      p.tracks[0].vol = 127;
      if (ty == InstrType::Kit) factoryBuildKit(i, p, 0);  // note 60 = lane 1
      else factoryBuild(ty, i, p.instruments[0]);
      Synth s(p);
      s.setWavetables(&wt);
      const uint8_t on[3] = {0x90, 60, 127};
      s.event(0, 0, on, 3);
      int16_t buf[Synth::kBlock], bufR[Synth::kBlock];
      int peak = 0;
      // 116 ms; LOFI 348 ms: a bytebeat song may start with a rest (42 MELDY: 256 ms at C-4).
      const int blocks = ty == InstrType::Lofi ? 120 : 40;
      for (int k = 0; k < blocks; ++k) {
        s.render(buf, bufR);
        for (int16_t x : buf) peak = x > peak ? x : (-x > peak ? -x : peak);
      }
      TEST_ASSERT_TRUE_MESSAGE(peak > 300, factoryPreset(ty, i).name);
    }
  }
}

// Type 7 is a former type: no factory presets.
void test_unused7_has_no_presets() { TEST_ASSERT_EQUAL(0, factoryCount(InstrType::Unused7)); }

// STRING: 3 STRING + 3 MODAL presets, generic names.
void test_string_presets() {
  TEST_ASSERT_EQUAL(6, factoryCount(InstrType::String));
  int perModel[static_cast<int>(StringModel::Count)] = {};
  for (int i = 0; i < factoryCount(InstrType::String); ++i) {
    const FactoryPreset& a = factoryPreset(InstrType::String, i);
    Instrument m;
    factoryBuild(InstrType::String, i, m);
    TEST_ASSERT_TRUE_MESSAGE(m.type == InstrType::String, a.name);
    TEST_ASSERT_TRUE_MESSAGE(m.machine < static_cast<int>(StringModel::Count), a.name);
    ++perModel[m.machine];
    TEST_ASSERT_TRUE_MESSAGE(strlen(a.name) >= 1 && strlen(a.name) <= 8, a.name);
    for (int j = i + 1; j < factoryCount(InstrType::String); ++j)
      TEST_ASSERT_TRUE_MESSAGE(strcmp(a.name, factoryPreset(InstrType::String, j).name) != 0, a.name);
  }
  TEST_ASSERT_EQUAL(3, perModel[static_cast<int>(StringModel::String)]);
  TEST_ASSERT_EQUAL(3, perModel[static_cast<int>(StringModel::Modal)]);
}

void test_kit_block() {
  TEST_ASSERT_EQUAL(24, factoryKitBlock(0));
  TEST_ASSERT_EQUAL(24, factoryKitBlock(23));
  TEST_ASSERT_EQUAL(16, factoryKitBlock(24));
  TEST_ASSERT_EQUAL(16, factoryKitBlock(31));
}

// Every lane's drum is a factory preset; it lands in the block, transposed to sound on note 60 + k.
void test_kit_lanes() {
  static Project p;
  for (int slot : {0, 30}) {
    for (int i = 0; i < factoryCount(InstrType::Kit); ++i) {
      const FactoryPreset& f = factoryPreset(InstrType::Kit, i);
      TEST_ASSERT_TRUE_MESSAGE(f.drumCount >= 1 && f.drumCount <= kKitLanes, f.name);
      p.reset();
      strcpy(p.instruments[factoryKitBlock(slot) + kKitLanes - 1].name, "KEEP");
      factoryBuildKit(i, p, slot);
      const Instrument& k = p.instruments[slot];
      TEST_ASSERT_TRUE(k.type == InstrType::Kit);
      TEST_ASSERT_EQUAL_STRING(f.name, k.name);
      const int first = factoryKitBlock(slot);
      for (int l = 0; l < kKitLanes; ++l) {
        TEST_ASSERT_EQUAL(60 + l, k.kit[l].note);
        if (l >= f.drumCount) {
          TEST_ASSERT_EQUAL_MESSAGE(kNoInstr, k.kit[l].instr, f.name);
          continue;
        }
        TEST_ASSERT_EQUAL_MESSAGE(first + l, k.kit[l].instr, f.drums[l].name);
        TEST_ASSERT_EQUAL(f.drums[l].vol, k.kit[l].vol);
        const Instrument& d = p.instruments[first + l];
        TEST_ASSERT_TRUE_MESSAGE(d.type == f.drums[l].type, f.drums[l].name);
        TEST_ASSERT_EQUAL_STRING(f.drums[l].name, d.name);
        Instrument ref;
        for (int j = 0; j < factoryCount(d.type); ++j)
          if (strcmp(factoryPreset(d.type, j).name, d.name) == 0) factoryBuild(d.type, j, ref);
        TEST_ASSERT_EQUAL(ref.transpose - l, d.transpose);
      }
      if (f.drumCount < kKitLanes) TEST_ASSERT_EQUAL_STRING("KEEP", p.instruments[first + kKitLanes - 1].name);
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_counts);
  RUN_TEST(test_drum2_presets);
  RUN_TEST(test_names_unique_and_short);
  RUN_TEST(test_categories_grouped);
  RUN_TEST(test_valid_values);
  RUN_TEST(test_recommended_insert);
  RUN_TEST(test_kit_drums_no_insert);
  RUN_TEST(test_build_resets_target);
  RUN_TEST(test_synth_presets_builtin_tables);
  RUN_TEST(test_lofi_presets_valid);
  RUN_TEST(test_lofi_byte_presets_use_formula_defaults);
  RUN_TEST(test_chord_preset_is_maj7);
  RUN_TEST(test_every_preset_sounds);
  RUN_TEST(test_unused7_has_no_presets);
  RUN_TEST(test_string_presets);
  RUN_TEST(test_kit_block);
  RUN_TEST(test_kit_lanes);
  return UNITY_END();
}
