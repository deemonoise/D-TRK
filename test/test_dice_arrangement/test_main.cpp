#include <cstring>
#include <initializer_list>
#include <unity.h>
#include "automation.h"
#include "dice_arrangement.h"
#include "templates.h"

using namespace mt;
static Project original;
static DiceSnap snap;

static void fixture(int length = 32, Resolution resolution = Resolution::Sixteenth) {
  templateBuild(0, original);
  snap = DiceSnap();
  snap.patCount = kDiceMaxPats;
  snap.patMask = snap.cfgMask = 0x3f;
  auto& d = snap.parts;
  d.bpm = 140;
  for (int role = 0; role < kDiceRoles; ++role) {
    d.tracks[role].instr = role;
    d.instruments[role].type = role ? InstrType::Synth : InstrType::Kit;
    d.instruments[role].fltMode = static_cast<uint8_t>(FltMode::Lp);
    d.instruments[role].cutoff = 96;
    d.instruments[role].send = 20;
    d.instruments[role].rsend = 30;
    std::strcpy(d.instruments[role].name, role == 5 ? "RISER" : "THEME");
  }
  for (auto& pat : d.pats) {
    pat = DicePat(); pat.length = length; pat.res = resolution; pat.swing = 50;
    for (int i = 0; i < length; ++i) {
      auto& drum = pat.steps[0][i];
      if (!(i % 2)) {
        drum.note = 100;
        setLaneMask(drum, (1u << kDiceKitLane[kDiceHat]) | (!(i % 4) ? 1u : 0u));
      }
      for (int role = 1; role < kDiceRoles; ++role) {
        if (role == 1 ? i % 4 != 1 : role == 2 ? i % 8 != 0 : i % 2 != 0) continue;
        auto& note = pat.steps[role][i]; note.note = role == 1 ? 36 : 60 + role;
        note.vel = 90; note.fx[0] = {Fx::GAT, gateValue(role == 2 ? 700 : 50)};
        if (role == 2) note.fx[1] = {Fx::CHD, kChordTriad};
      }
    }
  }
}
static DiceArrangeSpec spec(int genre = 2, int style = 1, uint8_t roles = 0x3f) {
  return {*diceRecipe(genre, style), diceMusicProfile(genre, style), genre, roles, 0, {0, 1, 4, 2, 3, 5}};
}
static int attacks(int section, int role, int from = 0, int to = -1) {
  const auto& pat = snap.parts.pats[section];
  int count = 0;
  for (int i = from; i < (to < 0 ? pat.length : to); ++i) count += pat.steps[role][i].hasNote();
  return count;
}
void setUp() { fixture(); }
void tearDown() {}

void test_break_build_drop_and_outro_are_related_but_distinct() {
  DiceOpts opts; opts.form = DiceForm::Arrange; opts.len = 32;
  diceArrangeDraft(original, opts, spec(), 777, snap);
  TEST_ASSERT_EQUAL(0, attacks(kArrangeBreak, 1));
  TEST_ASSERT_EQUAL(kNoteOff, snap.parts.pats[kArrangeBreak].steps[1][0].note);
  for (int i = 0; i < 32; ++i)
    TEST_ASSERT_FALSE(snap.parts.pats[kArrangeBreak].steps[0][i].hasNote() && laneOn(snap.parts.pats[kArrangeBreak].steps[0][i], 0));
  TEST_ASSERT_EQUAL(0, attacks(kArrangeBuild, 1, 28));
  TEST_ASSERT_EQUAL(kNoteOff, snap.parts.pats[kArrangeBuild].steps[1][28].note);
  TEST_ASSERT_TRUE(snap.parts.pats[kArrangeDrop].steps[1][1].hasNote());
  TEST_ASSERT_FALSE(snap.parts.pats[kArrangeDrop].steps[1][0].hasNote());
  TEST_ASSERT_LESS_THAN(attacks(kArrangeDrop, 4), attacks(kArrangeMain, 4));
  TEST_ASSERT_EQUAL(0, attacks(kArrangeOutro, 3, 16));
  TEST_ASSERT_EQUAL(kNoteOff, snap.parts.pats[kArrangeOutro].steps[3][16].note);
}
void test_auto_build_is_one_eight_bar_phrase_and_explicit_headers_win() {
  DiceOpts opts; opts.form = DiceForm::Arrange;
  diceArrangeDraft(original, opts, spec(), 777, snap);
  TEST_ASSERT_EQUAL(128, snap.parts.pats[kArrangeBuild].length);
  fixture(16, Resolution::Eighth);
  diceArrangeDraft(original, opts, spec(7, 0), 777, snap);
  TEST_ASSERT_EQUAL(64, snap.parts.pats[kArrangeBuild].length);
  fixture(16); opts.len = 16;
  diceArrangeDraft(original, opts, spec(), 777, snap);
  TEST_ASSERT_EQUAL(16, snap.parts.pats[kArrangeBuild].length);
  fixture(7); opts.len = 0; snap.preserveHeaders = true; opts.lockMask = 4;
  Step locked[kMaxSteps]; std::memcpy(locked, snap.parts.pats[kArrangeBuild].steps[2], sizeof(locked));
  diceArrangeDraft(original, opts, spec(), 777, snap);
  TEST_ASSERT_EQUAL(7, snap.parts.pats[kArrangeBuild].length);
  TEST_ASSERT_EQUAL_MEMORY(locked, snap.parts.pats[kArrangeBuild].steps[2], sizeof(locked));
}
void test_song_is_ordered_build_once_and_preserves_unmanaged_mutes() {
  DiceOpts opts; opts.form = DiceForm::Arrange; opts.lockMask = 4;
  snap.first = 0; original.tracks[2].mute = true; original.tracks[7].mute = true;
  for (auto& mute : original.songMute) mute = 1u << 11;
  diceArrangeSong(original, opts, spec(), snap);
  const auto& song = snap.parts.song;
  TEST_ASSERT_EQUAL(29, song.songLen);
  int builds = 0, prior = 0;
  for (int i = 0; i < song.songLen; ++i) {
    const int section = song.song[i] - snap.first;
    TEST_ASSERT_GREATER_OR_EQUAL(prior, section); prior = section;
    builds += section == kArrangeBuild;
    TEST_ASSERT_BITS_HIGH((1u << 2) | (1u << 7) | (1u << 11), song.songMute[i]);
  }
  TEST_ASSERT_EQUAL(1, builds);
}
void test_semantic_fx_seed_empty_builtin_bodies_and_use_their_actual_function() {
  struct Case { const char* name; int section; };
  const Case cases[] = {{"RISER", kArrangeBuild}, {"IMPACT", kArrangeDrop}, {"LASER", kArrangeDrop}, {"ZAP", kArrangeDrop},
                        {"DOWNLIFT", kArrangeOutro}, {"WIND", kArrangeBreak}};
  for (const auto& c : cases) {
    fixture();
    for (auto& pat : snap.parts.pats) for (auto& step : pat.steps[5]) step = Step();
    std::strcpy(snap.parts.instruments[5].name, c.name);
    DiceOpts opts; opts.form = DiceForm::Arrange; opts.len = 32;
    diceArrangeDraft(original, opts, spec(), 777, snap);
    TEST_ASSERT_EQUAL_MESSAGE(1, attacks(c.section, 5), c.name);
    if (c.section != kArrangeDrop) TEST_ASSERT_EQUAL(0, attacks(kArrangeDrop, 5));
    else TEST_ASSERT_EQUAL(0, attacks(kArrangeBuild, 5));
  }
}
void test_build_parameter_ramp_restores_drop_and_filter_off_falls_back() {
  DiceOpts opts; opts.form = DiceForm::Arrange; opts.len = 32;
  diceArrangeDraft(original, opts, spec(), 777, snap);
  TEST_ASSERT_EQUAL(72, automationGet(snap.parts.pats[kArrangeBuild].steps[4][0], Fx::FLT));
  TEST_ASSERT_EQUAL(108, automationGet(snap.parts.pats[kArrangeBuild].steps[4][31], Fx::FLT));
  TEST_ASSERT_EQUAL(96, automationGet(snap.parts.pats[kArrangeDrop].steps[4][0], Fx::FLT));
  TEST_ASSERT_EQUAL(50, gatePercent(snap.parts.pats[kArrangeDrop].steps[4][0].find(Fx::GAT)->val));
  fixture();
  for (int role : {2, 3, 4}) snap.parts.instruments[role].fltMode = static_cast<uint8_t>(FltMode::Off);
  diceArrangeDraft(original, opts, spec(), 777, snap);
  TEST_ASSERT_NULL(snap.parts.pats[kArrangeBuild].steps[4][0].find(Fx::FLT));
  TEST_ASSERT_EQUAL(30, automationGet(snap.parts.pats[kArrangeBuild].steps[4][0], Fx::RVB));
  TEST_ASSERT_EQUAL(30, automationGet(snap.parts.pats[kArrangeDrop].steps[4][0], Fx::RVB));
}
void test_full_fx_slots_and_locks_preserve_articulation_and_headers() {
  for (int role : {2, 3, 4}) for (auto& pat : snap.parts.pats) for (auto& step : pat.steps[role]) {
    step.fx[0] = {Fx::GAT, gateValue(75)}; step.fx[1] = {Fx::CHD, kChordTriad};
    step.fx[2] = {Fx::NDG, 7}; step.fx[3] = {Fx::SLD, 8};
    step.fx[4] = {Fx::PRB, 100}; step.fx[5] = {Fx::OFF, 12};
  }
  Step kept = snap.parts.pats[kArrangeDrop].steps[4][0];
  Step locked[kMaxSteps]; std::memcpy(locked, snap.parts.pats[kArrangeBreak].steps[2], sizeof(locked));
  snap.preserveHeaders = true;
  DiceOpts opts; opts.form = DiceForm::Arrange; opts.lockMask = 4;
  diceArrangeDraft(original, opts, spec(), 777, snap);
  TEST_ASSERT_EQUAL_MEMORY(&kept, &snap.parts.pats[kArrangeDrop].steps[4][0], sizeof(Step));
  TEST_ASSERT_EQUAL_MEMORY(locked, snap.parts.pats[kArrangeBreak].steps[2], sizeof(locked));
  TEST_ASSERT_EQUAL(32, snap.parts.pats[kArrangeBuild].length);
}
void test_builtin_riser_is_scheduled_in_real_arrange_roll() {
  templateBuild(0, original);
  DiceOpts opts; opts.form = DiceForm::Arrange; opts.genre = 2; opts.style = 0;
  opts.root = 0; opts.variety = 0; opts.len = 32;
  diceRoll(original, opts, 0, 777, snap);
  TEST_ASSERT_EQUAL_STRING("RISER", original.instruments[original.tracks[5].instr].name);
  int rise = 0, impact = 0;
  for (int i = 0; i < original.patterns[kArrangeBuild].length; ++i) rise += original.patterns[kArrangeBuild].steps[5][i].hasNote();
  for (int i = 0; i < original.patterns[kArrangeDrop].length; ++i) impact += original.patterns[kArrangeDrop].steps[5][i].hasNote();
  TEST_ASSERT_EQUAL(1, rise);
  TEST_ASSERT_EQUAL(0, impact);
}
struct ArrangeMidi : DicePhraseSource {
  bool pick(int, DiceRole role, uint32_t, DicePhrase& phrase) const override {
    phrase = DicePhrase(); phrase.count = 4;
    for (int i = 0; i < 4; ++i)
      phrase.events[i] = {static_cast<uint16_t>(i * 96), 24,
                          static_cast<uint8_t>(role == DiceRole::Bass ? 36 : 60), 90,
                          static_cast<uint8_t>(role == DiceRole::Drums ? (i == 2 ? 1 : 0) : 255)};
    return true;
  }
};
void test_all_styles_and_sources_keep_arrange_silence_and_native_arrival() {
  ArrangeMidi midi;
  for (int genre = 0; genre < diceGenreCount(); ++genre)
    for (int style = 0; style < diceStyleCount(genre); ++style)
      for (DiceSource mode : {DiceSource::Builtin, DiceSource::Midi, DiceSource::Hybrid}) {
        templateBuild(0, original);
        DiceOpts opts; opts.form = DiceForm::Arrange; opts.genre = genre; opts.style = style;
        opts.source = mode; opts.len = 16; opts.root = 0; opts.variety = 0;
        const auto result = diceRoll(original, opts, 0, 777, snap, 0, &midi);
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, result.written, diceStyleName(genre, style));
        const auto& recipe = *diceRecipe(genre, style);
        for (int i = 0; i < 16; ++i) {
          TEST_ASSERT_FALSE(original.patterns[kArrangeBreak].steps[1][i].hasNote());
          const auto& drum = original.patterns[kArrangeBreak].steps[0][i];
          TEST_ASSERT_FALSE(drum.hasNote() && laneOn(drum, 0));
          for (int section = 0; section < kDiceMaxPats; ++section)
            if (!(recipe.roles & 1)) TEST_ASSERT_FALSE(original.patterns[section].steps[0][i].hasNote());
        }
        if (recipe.roles & 2) {
          int bass = 0;
          for (int i = 0; i < 16; ++i) bass += original.patterns[kArrangeDrop].steps[1][i].hasNote();
          TEST_ASSERT_GREATER_THAN_MESSAGE(0, bass, recipe.style);
        }
      }
}
void test_build_has_two_rhythmic_stages_and_ambient_never_invents_drums() {
  DiceOpts opts; opts.form = DiceForm::Arrange; opts.len = 32;
  auto arrangement = spec(); arrangement.midiMask = 1;
  for (auto& step : snap.parts.pats[kArrangeBuild].steps[0])
    if (step.hasNote()) setLaneMask(step, 1u << kDiceKitLane[kDiceHat]);
  diceArrangeDraft(original, opts, arrangement, 777, snap);
  int early = 0, late = 0;
  for (int i = 0; i < 32; ++i) {
    const auto& step = snap.parts.pats[kArrangeBuild].steps[0][i];
    if (step.hasNote() && laneOn(step, 2)) (i < 16 ? early : late)++;
    TEST_ASSERT_NULL(step.find(Fx::RAT));  // MIDI keeps its authored local articulation
  }
  TEST_ASSERT_GREATER_THAN(early, late);
  fixture(16, Resolution::Eighth);
  for (auto& pat : snap.parts.pats) for (int role : {0, 1}) for (auto& step : pat.steps[role]) step = Step();
  opts.len = 0;
  diceArrangeDraft(original, opts, spec(7, 0, 0x3c), 777, snap);
  for (int section = 0; section < kDiceMaxPats; ++section)
    TEST_ASSERT_EQUAL(0, attacks(section, 0));
  TEST_ASSERT_NOT_NULL(snap.parts.pats[kArrangeBuild].steps[4][0].find(Fx::RVB));
}
void test_short_song_headers_are_bounded_and_build_never_repeats() {
  fixture(1); snap.preserveHeaders = true;
  DiceOpts opts; opts.form = DiceForm::Arrange;
  diceArrangeSong(original, opts, spec(), snap);
  TEST_ASSERT_EQUAL(81, snap.parts.song.songLen);
  int builds = 0;
  for (int slot = 0; slot < snap.parts.song.songLen; ++slot)
    builds += snap.parts.song.song[slot] == kArrangeBuild;
  TEST_ASSERT_EQUAL(1, builds);
}
void test_canonical_trance_and_ebm_builds_roll_only_pure_tops_once_per_beat() {
  const int genres[] = {2, 2, 2, 2, 2, 1};
  const int styles[] = {0, 1, 2, 3, 4, 6};
  for (int c = 0; c < 6; ++c) {
    templateBuild(0, original);
    DiceOpts opts; opts.form = DiceForm::Arrange; opts.genre = genres[c]; opts.style = styles[c];
    opts.variety = 0; opts.root = 0; opts.groove = 0;
    diceRoll(original, opts, 0, 777, snap);
    const auto& build = original.patterns[kArrangeBuild];
    const auto& source = original.patterns[kArrangeDrop];
    const int beat = kPpqn / ticksPerStep(build.res);
    const uint16_t anchors = (1u << 0) | (1u << 1) | (1u << 4);
    int rolls[kMaxSteps] = {}, added = 0;
    for (int i = build.length * 3 / 4; i < build.length - beat; ++i) {
      const auto& step = build.steps[0][i];
      const auto& prior = source.steps[0][i % source.length];
      const uint16_t lanes = step.hasNote() ? laneMask(step) : 0;
      TEST_ASSERT_EQUAL((prior.hasNote() ? laneMask(prior) : 0) & anchors, lanes & anchors);
      if (!step.find(Fx::RAT) || prior.find(Fx::RAT)) continue;
      ++added; ++rolls[i / beat];
      TEST_ASSERT_EQUAL(0, lanes & anchors);
      TEST_ASSERT_EQUAL(0, lanes & (lanes - 1));
      TEST_ASSERT_LESS_OR_EQUAL(1, rolls[i / beat]);
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, added, diceStyleName(genres[c], styles[c]));
  }
}
void test_authored_offbeat_roll_prevents_an_additional_roll_in_that_beat() {
  for (auto& step : snap.parts.pats[kArrangeBuild].steps[0]) {
    step = Step(); step.note = 90; setLaneMask(step, 1u << 2);
  }
  const uint8_t authored = static_cast<uint8_t>((kRatDown << 4) | 3);
  snap.parts.pats[kArrangeBuild].steps[0][27].fx[0] = {Fx::RAT, authored};
  DiceOpts opts; opts.form = DiceForm::Arrange; opts.len = 32;
  diceArrangeDraft(original, opts, spec(), 777, snap);
  int rolls = 0;
  for (int i = 24; i < 28; ++i) rolls += snap.parts.pats[kArrangeBuild].steps[0][i].find(Fx::RAT) != nullptr;
  TEST_ASSERT_EQUAL(1, rolls);
  TEST_ASSERT_EQUAL(authored, snap.parts.pats[kArrangeBuild].steps[0][27].find(Fx::RAT)->val);
}
void test_rise_fx_enters_only_in_the_last_physical_build_bar() {
  for (Resolution resolution : {Resolution::Sixteenth, Resolution::Eighth}) {
    fixture(16, resolution);
    DiceOpts opts; opts.form = DiceForm::Arrange;
    diceArrangeDraft(original, opts, spec(), 777, snap);
    const auto& build = snap.parts.pats[kArrangeBuild];
    const int onset = build.length - 4 * kPpqn / ticksPerStep(build.res);
    TEST_ASSERT_EQUAL(1, attacks(kArrangeBuild, 5));
    TEST_ASSERT_TRUE(build.steps[5][onset].hasNote());
    TEST_ASSERT_NOT_NULL(build.steps[5][onset].find(Fx::TIE));
    TEST_ASSERT_EQUAL(kNoteOff, build.steps[5][build.length - 1].note);
  }
  fixture(7); snap.preserveHeaders = true;
  DiceOpts opts; opts.form = DiceForm::Arrange;
  diceArrangeDraft(original, opts, spec(), 777, snap);
  TEST_ASSERT_TRUE(snap.parts.pats[kArrangeBuild].steps[5][0].hasNote());
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_break_build_drop_and_outro_are_related_but_distinct);
  RUN_TEST(test_auto_build_is_one_eight_bar_phrase_and_explicit_headers_win);
  RUN_TEST(test_song_is_ordered_build_once_and_preserves_unmanaged_mutes);
  RUN_TEST(test_semantic_fx_seed_empty_builtin_bodies_and_use_their_actual_function);
  RUN_TEST(test_build_parameter_ramp_restores_drop_and_filter_off_falls_back);
  RUN_TEST(test_full_fx_slots_and_locks_preserve_articulation_and_headers);
  RUN_TEST(test_builtin_riser_is_scheduled_in_real_arrange_roll);
  RUN_TEST(test_all_styles_and_sources_keep_arrange_silence_and_native_arrival);
  RUN_TEST(test_build_has_two_rhythmic_stages_and_ambient_never_invents_drums);
  RUN_TEST(test_short_song_headers_are_bounded_and_build_never_repeats);
  RUN_TEST(test_canonical_trance_and_ebm_builds_roll_only_pure_tops_once_per_beat);
  RUN_TEST(test_authored_offbeat_roll_prevents_an_additional_roll_in_that_beat);
  RUN_TEST(test_rise_fx_enters_only_in_the_last_physical_build_bar);
  return UNITY_END();
}
