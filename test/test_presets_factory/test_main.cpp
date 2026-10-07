#include <string.h>
#include <unity.h>
#include "inst_codec.h"
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

void test_counts() {
  TEST_ASSERT_TRUE(factoryCount(InstrType::Chip) >= 30);
  TEST_ASSERT_TRUE(factoryCount(InstrType::Fm) >= 30);
  TEST_ASSERT_TRUE(factoryCount(InstrType::Drum) >= 30);
  TEST_ASSERT_TRUE(factoryCount(InstrType::Synth) >= 30);
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
      factoryBuild(ty, i, p.instruments[0]);
      Synth s(p);
      s.setWavetables(&wt);
      const uint8_t on[3] = {0x90, 60, 127};
      s.event(0, 0, on, 3);
      int16_t buf[Synth::kBlock];
      int peak = 0;
      for (int k = 0; k < 40; ++k) {  // 160 ms
        s.render(buf);
        for (int16_t x : buf) peak = x > peak ? x : (-x > peak ? -x : peak);
      }
      TEST_ASSERT_TRUE_MESSAGE(peak > 300, factoryPreset(ty, i).name);
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_counts);
  RUN_TEST(test_names_unique_and_short);
  RUN_TEST(test_categories_grouped);
  RUN_TEST(test_valid_values);
  RUN_TEST(test_build_resets_target);
  RUN_TEST(test_synth_presets_builtin_tables);
  RUN_TEST(test_chord_preset_is_maj7);
  RUN_TEST(test_every_preset_sounds);
  return UNITY_END();
}
