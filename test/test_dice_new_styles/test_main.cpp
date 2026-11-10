#include <unity.h>
#include <cstring>
#include <initializer_list>
#include "dice.h"
#include "templates.h"
using namespace mt;
static Project p, baseline;
static DiceSnap snap;
void setUp() { templateBuild(0, p); }
void tearDown() {}
static int find(int count, const char* (*name)(int), const char* wanted) {
  for (int i = 0; i < count; ++i) if (!strcmp(name(i), wanted)) return i;
  return -1;
}
static uint32_t prefixHash(int count, const char* (*name)(int), const char* (*text)(int)) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < count; ++i) for (const char* value : {name(i), text(i)})
    do { hash = (hash ^ static_cast<uint8_t>(*value)) * 16777619u; } while (*value++);
  return hash;
}
void test_old_factory_indices_and_assets_remain_unchanged() {
  // Captured from the real pre-extension native core, including all 64 VIRUS entries.
  TEST_ASSERT_GREATER_OR_EQUAL(48, bassFactoryCount());
  TEST_ASSERT_GREATER_OR_EQUAL(32, chordRhythmCount());
  TEST_ASSERT_GREATER_OR_EQUAL(106, arpFactoryCount());
  TEST_ASSERT_EQUAL_UINT32(3033957320u, prefixHash(48, bassFactoryName, bassFactoryText));
  TEST_ASSERT_EQUAL_UINT32(201960290u, prefixHash(32, chordRhythmName, chordRhythmText));
  TEST_ASSERT_EQUAL_UINT32(4209524424u, prefixHash(106, arpFactoryName, arpFactoryText));
  TEST_ASSERT_EQUAL_STRING("TUMBAO", bassFactoryName(47));
  TEST_ASSERT_EQUAL_STRING("TRESILLO", chordRhythmName(31));
  TEST_ASSERT_EQUAL_STRING("VIRUS 64", arpFactoryName(105));
}
void test_new_factory_assets_append_with_held_dub_and_syncopated_juke() {
  TEST_ASSERT_EQUAL(48, find(bassFactoryCount(), bassFactoryName, "DUB SPACE"));
  TEST_ASSERT_EQUAL(49, find(bassFactoryCount(), bassFactoryName, "JUKE SUB"));
  TEST_ASSERT_EQUAL(32, find(chordRhythmCount(), chordRhythmName, "JUKE STAB"));
  TEST_ASSERT_EQUAL(106, find(arpFactoryCount(), arpFactoryName, "JUKE HIT"));
  BassPattern dub, juke; ArpPattern stabs, hits;
  TEST_ASSERT_TRUE(parseBassPattern(bassFactoryText(48), dub));
  TEST_ASSERT_TRUE(parseBassPattern(bassFactoryText(49), juke));
  TEST_ASSERT_TRUE(parseArpPattern(chordRhythmText(32), stabs));
  TEST_ASSERT_TRUE(parseArpPattern(arpFactoryText(106), hits));
  int dubNotes = 0, dubRests = 0, jukeNotes = 0, syncopated = 0, stabNotes = 0, hitNotes = 0;
  for (int i = 0; i < 16; ++i) {
    dubNotes += dub.steps[i].kind == ArpKind::Note; dubRests += dub.steps[i].kind == ArpKind::Rest;
    if (juke.steps[i].kind == ArpKind::Note) { ++jukeNotes; syncopated += i % 4 != 0; }
    if (stabs.steps[i].kind == ArpKind::Note) { ++stabNotes; TEST_ASSERT_EQUAL(ArpLen::Short, stabs.steps[i].len); }
    if (hits.steps[i].kind == ArpKind::Note) {
      ++hitNotes; TEST_ASSERT_EQUAL(ArpLen::Short, hits.steps[i].len); TEST_ASSERT_EQUAL(ArpPitch::Root, hits.steps[i].pitch);
    }
  }
  TEST_ASSERT_EQUAL(2, dubNotes); TEST_ASSERT_GREATER_OR_EQUAL(6, dubRests);
  TEST_ASSERT_GREATER_OR_EQUAL(5, jukeNotes); TEST_ASSERT_LESS_OR_EQUAL(8, jukeNotes);
  TEST_ASSERT_GREATER_OR_EQUAL(4, syncopated);
  TEST_ASSERT_EQUAL(6, stabNotes); TEST_ASSERT_EQUAL(6, hitNotes);
}
void test_dub_space_generator_releases_the_sub_into_real_pauses() {
  const int index = find(bassFactoryCount(), bassFactoryName, "DUB SPACE");
  TEST_ASSERT_GREATER_OR_EQUAL(0, index);
  BassPattern pattern; TEST_ASSERT_TRUE(parseBassPattern(bassFactoryText(index), pattern));
  Pattern source, result; source.length = result.length = 16;
  BassSpec spec; spec.root = 36; spec.dest = 1;
  Sel selection; selection.t0 = selection.t1 = 1; selection.s0 = 0; selection.s1 = 15;
  TEST_ASSERT_TRUE(applyBass(source, result, selection, spec, pattern, 0, ScaleType::Minor, nullptr, 0));
  TEST_ASSERT_EQUAL(36, result.steps[1][0].note); TEST_ASSERT_NOT_NULL(result.steps[1][0].find(Fx::TIE));
  TEST_ASSERT_EQUAL(kNoteOff, result.steps[1][6].note);
  TEST_ASSERT_EQUAL(36, result.steps[1][10].note); TEST_ASSERT_NOT_NULL(result.steps[1][10].find(Fx::TIE));
  TEST_ASSERT_EQUAL(kNoteOff, result.steps[1][12].note);
  for (int i : {7, 8, 9, 13, 14, 15}) TEST_ASSERT_FALSE(result.steps[1][i].hasNote());
}
struct StylePair { int genre, style, nearestGenre, nearestStyle; };
static const StylePair additions[] = {{4, 3, 3, 4}, {6, 3, 6, 0}, {1, 6, 1, 3}};
static uint64_t onsetMask(const Project& project, int pattern, int role, int lane = -1) {
  uint64_t mask = 0;
  for (int i = 0; i < project.patterns[pattern].length && i < 64; ++i) {
    const auto& step = project.patterns[pattern].steps[role][i];
    if (step.hasNote() && (lane < 0 || laneOn(step, lane))) mask |= uint64_t(1) << i;
  }
  return mask;
}
static DiceOpts styleOptions(int genre, int style) {
  DiceOpts options; options.genre = genre; options.style = style; options.variety = 0;
  options.root = 0; options.len = 64; options.groove = 0; options.form = DiceForm::Classic;
  return options;
}
void test_canonical_new_styles_have_distinct_rhythm_foundations_without_using_bpm() {
  for (const auto& addition : additions) {
    TEST_ASSERT_NOT_NULL(diceRecipe(addition.genre, addition.style));
    templateBuild(0, p); auto options = styleOptions(addition.nearestGenre, addition.nearestStyle);
    diceRoll(p, options, 0, 777, snap); baseline = p;
    templateBuild(0, p); options = styleOptions(addition.genre, addition.style);
    diceRoll(p, options, 0, 777, snap);
    const auto kick = onsetMask(p, 0, 0, 0), snare = onsetMask(p, 0, 0, 1), clap = onsetMask(p, 0, 0, 4);
    TEST_ASSERT_TRUE_MESSAGE(kick != onsetMask(baseline, 0, 0, 0) || snare != onsetMask(baseline, 0, 0, 1) ||
                              clap != onsetMask(baseline, 0, 0, 4), diceStyleName(addition.genre, addition.style));
    TEST_ASSERT_NOT_EQUAL_MESSAGE(onsetMask(baseline, 0, 1), onsetMask(p, 0, 1), diceStyleName(addition.genre, addition.style));
    TEST_ASSERT_NOT_EQUAL_UINT64(0, kick);
    TEST_ASSERT_EQUAL_UINT64(0, onsetMask(p, 2, 0, 0));  // deliberate Classic BREAK
  }
}
void test_new_canonical_bass_articulation_and_anchor_counts() {
  for (const auto& addition : additions) {
    TEST_ASSERT_NOT_NULL(diceRecipe(addition.genre, addition.style));
    templateBuild(0, p); auto options = styleOptions(addition.genre, addition.style);
    diceRoll(p, options, 0, 777, snap);
    const auto& bass = p.patterns[0].steps[1];
    const char* name = addition.genre == 4 ? "DUB SPACE" : addition.genre == 6 ? "JUKE SUB" : "EBM";
    const int index = find(bassFactoryCount(), bassFactoryName, name); BassPattern expected;
    TEST_ASSERT_TRUE(parseBassPattern(bassFactoryText(index), expected));
    int attacks = 0;
    for (int i = 0; i < 64; ++i) {
      TEST_ASSERT_EQUAL(expected.steps[i % expected.len].kind == ArpKind::Note, bass[i].hasNote());
      attacks += bass[i].hasNote();
    }
    TEST_ASSERT_EQUAL(addition.genre == 4 ? 8 : addition.genre == 6 ? 28 : 64, attacks);
    if (addition.genre == 4) {
      for (int bar = 0; bar < 4; ++bar) {
        TEST_ASSERT_EQUAL(kNoteOff, bass[bar * 16 + 6].note);
        TEST_ASSERT_EQUAL(kNoteOff, bass[bar * 16 + 12].note);
        TEST_ASSERT_TRUE(laneOn(p.patterns[0].steps[0][bar * 16 + 8], 1) ||
                         laneOn(p.patterns[0].steps[0][bar * 16 + 8], 4));
      }
    } else if (addition.genre == 1) {
      const int root = bass[0].note;
      for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL(i % 4 == 2 ? (root + 7) % 12 : root % 12, bass[i].note % 12);
    }
  }
}
struct NewStyleMidi : DicePhraseSource {
  bool pick(int, DiceRole role, uint32_t, DicePhrase& phrase) const override {
    phrase = DicePhrase(); phrase.bars = 1;
    for (int i = 0; i < 16; ++i) {
      if (role == DiceRole::Chords && i % 8) continue;
      if (role == DiceRole::Fx && i) continue;
      auto& event = phrase.events[phrase.count++];
      event.tick = i * 24; event.duration = role == DiceRole::Chords ? 180 : 18;
      event.note = role == DiceRole::Bass ? 36 : 60 + i % 3 * 3; event.vel = 90;
      event.lane = role == DiceRole::Drums ? (i % 4 ? 2 : 0) : 255;
    }
    return true;
  }
};
void test_new_styles_all_sources_forms_and_seeds_are_playable_and_repeatable() {
  NewStyleMidi source;
  for (const auto& addition : additions)
    for (DiceSource mode : {DiceSource::Builtin, DiceSource::Midi, DiceSource::Hybrid})
      for (DiceForm form : {DiceForm::Classic, DiceForm::Develop}) for (uint32_t seed : {1u, 777u}) {
        TEST_ASSERT_NOT_NULL(diceRecipe(addition.genre, addition.style));
        auto options = styleOptions(addition.genre, addition.style); options.source = mode; options.form = form;
        templateBuild(0, p); diceRoll(p, options, 0, seed, snap, 0, &source); baseline = p;
        templateBuild(0, p); diceRoll(p, options, 0, seed, snap, 0, &source);
        TEST_ASSERT_EQUAL_MEMORY(&baseline, &p, sizeof(p));
        for (int pattern : {0, 1, 3}) {
          TEST_ASSERT_NOT_EQUAL_UINT64(0, onsetMask(p, pattern, 0, 0));
          TEST_ASSERT_NOT_EQUAL_UINT64(0, onsetMask(p, pattern, 1));
          for (int role = 0; role < 6; ++role)
            for (int i = p.patterns[pattern].length; i < kMaxSteps; ++i) TEST_ASSERT_TRUE(p.patterns[pattern].steps[role][i].isEmpty());
        }
        templateBuild(0, p); baseline = p;
        diceRoll(p, options, 0, seed, snap, 0, &source);
        diceUndo(p, snap); TEST_ASSERT_EQUAL_MEMORY(&baseline, &p, sizeof(p));
      }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_canonical_new_styles_have_distinct_rhythm_foundations_without_using_bpm);
  RUN_TEST(test_new_canonical_bass_articulation_and_anchor_counts);
  RUN_TEST(test_new_styles_all_sources_forms_and_seeds_are_playable_and_repeatable);
  RUN_TEST(test_old_factory_indices_and_assets_remain_unchanged);
  RUN_TEST(test_new_factory_assets_append_with_held_dub_and_syncopated_juke);
  RUN_TEST(test_dub_space_generator_releases_the_sub_into_real_pauses);
  return UNITY_END();
}
