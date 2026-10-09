#include <string.h>
#include <unity.h>
#include "arp_gen.h"
#include "model.h"

using namespace mt;

void setUp() {}
void tearDown() {}

void test_parse_tokens() {
  ArpPattern p;
  TEST_ASSERT_TRUE(parseArpPattern("Xs~ xl o xr^ xpv . - ?z x", p));
  TEST_ASSERT_EQUAL(8, p.len);  // "?z" skipped
  TEST_ASSERT_EQUAL(ArpKind::Note, p.steps[0].kind);
  TEST_ASSERT_EQUAL(ArpAcc::Accent, p.steps[0].acc);
  TEST_ASSERT_EQUAL(ArpLen::Short, p.steps[0].len);
  TEST_ASSERT_TRUE(p.steps[0].slide);
  TEST_ASSERT_EQUAL(ArpLen::Long, p.steps[1].len);
  TEST_ASSERT_EQUAL(ArpAcc::Ghost, p.steps[2].acc);
  TEST_ASSERT_EQUAL(ArpPitch::Repeat, p.steps[3].pitch);
  TEST_ASSERT_EQUAL(1, p.steps[3].oct);
  TEST_ASSERT_EQUAL(ArpPitch::Root, p.steps[4].pitch);
  TEST_ASSERT_EQUAL(-1, p.steps[4].oct);
  TEST_ASSERT_EQUAL(ArpKind::Rest, p.steps[5].kind);
  TEST_ASSERT_EQUAL(ArpKind::Tie, p.steps[6].kind);
  TEST_ASSERT_FALSE(parseArpPattern("  ", p));
}

void test_format_round_trip() {
  ArpPattern p, q;
  TEST_ASSERT_TRUE(parseArpPattern("Xs~ xl o xr^ xpv . -", p));
  char buf[256];
  TEST_ASSERT_TRUE(formatArpPattern(p, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("Xs~ xl o xr^ xpv . -", buf);
  TEST_ASSERT_TRUE(parseArpPattern(buf, q));
  TEST_ASSERT_EQUAL(0, memcmp(&p, &q, sizeof(p)));
  TEST_ASSERT_FALSE(formatArpPattern(p, buf, 4));
}

void test_parse_caps_at_32() {
  char text[200] = "";
  for (int i = 0; i < 40; ++i) strcat(text, "x ");
  ArpPattern p;
  TEST_ASSERT_TRUE(parseArpPattern(text, p));
  TEST_ASSERT_EQUAL(kArpPatMax, p.len);
}

void test_factory_patterns_parse() {
  TEST_ASSERT_TRUE(arpFactoryCount() >= 40);
  for (int i = 0; i < arpFactoryCount(); ++i) {
    ArpPattern p;
    TEST_ASSERT_TRUE_MESSAGE(parseArpPattern(arpFactoryText(i), p), arpFactoryName(i));
    TEST_ASSERT_TRUE(strlen(arpFactoryName(i)) <= 10);
    bool note = false;
    for (int k = 0; k < p.len; ++k) note |= p.steps[k].kind == ArpKind::Note;
    TEST_ASSERT_TRUE_MESSAGE(note, arpFactoryName(i));
    char back[256];
    TEST_ASSERT_TRUE(formatArpPattern(p, back, sizeof(back)));
  }
  TEST_ASSERT_EQUAL_STRING("16THS", arpFactoryName(0));
  TEST_ASSERT_NULL(arpFactoryName(arpFactoryCount()));
}

static Pattern S, D;

static void reset() {
  S.clear();
  S.length = 16;
  D = S;
}

static ArpPattern pat(const char* t) {
  ArpPattern p;
  parseArpPattern(t, p);
  return p;
}

// Notes of dest track 0 on the first n steps into out (kNoteEmpty kept).
static void notes(int n, int* out) {
  for (int i = 0; i < n; ++i) out[i] = D.steps[0][i].note;
}

static void expectNotes(ArpMode m, uint8_t chord, int oct, const int* want, int n) {
  reset();
  ArpSpec a;
  a.mode = m;
  a.chord = chord;
  a.octaves = static_cast<uint8_t>(oct);
  TEST_ASSERT_TRUE(applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x x x x x x x x x x x x x x x"), 0,
                            ScaleType::Major));
  int got[16];
  notes(n, got);
  TEST_ASSERT_EQUAL_INT_ARRAY(want, got, n);
}

void test_modes_triad() {
  const int up[] = {60, 64, 67, 60};
  expectNotes(ArpMode::Up, kChordTriad, 1, up, 4);
  const int down[] = {67, 64, 60, 67};
  expectNotes(ArpMode::Down, kChordTriad, 1, down, 4);
  const int ud[] = {60, 64, 67, 64, 60, 64};
  expectNotes(ArpMode::UpDown, kChordTriad, 1, ud, 6);
  const int du[] = {67, 64, 60, 64, 67, 64};
  expectNotes(ArpMode::DownUp, kChordTriad, 1, du, 6);
  const int ped[] = {60, 64, 60, 67, 60, 64};
  expectNotes(ArpMode::Pedal, kChordTriad, 1, ped, 6);
  const int oct2[] = {60, 64, 67, 72, 76, 79, 60};
  expectNotes(ArpMode::Up, kChordTriad, 2, oct2, 7);
}

void test_modes_seventh() {
  const int conv[] = {60, 71, 64, 67, 60};
  expectNotes(ArpMode::Converge, kChordSeventh, 1, conv, 5);
  const int div[] = {67, 64, 71, 60, 67};
  expectNotes(ArpMode::Diverge, kChordSeventh, 1, div, 5);
}

void test_chord_mode_writes_chd() {
  reset();
  ArpSpec a;
  a.mode = ArpMode::Chord;
  a.chord = kChordSeventh;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(60, D.steps[0][1].note);
  const FxSlot* c = D.steps[0][0].find(Fx::CHD);
  TEST_ASSERT_NOT_NULL(c);
  TEST_ASSERT_EQUAL(kChordSeventh, c->val);
}

void test_random_mode_deterministic() {
  reset();
  ArpSpec a;
  a.mode = ArpMode::Random;
  a.seed = 5;
  const ArpPattern p = pat("x x x x x x x x x x x x x x x x");
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  Pattern first = D;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(0, memcmp(first.steps[0], D.steps[0], sizeof(D.steps[0])));
  for (int i = 0; i < 16; ++i) {
    const int n = D.steps[0][i].note;
    TEST_ASSERT_TRUE(n == 60 || n == 64 || n == 67);
  }
}

static ArpSpec spec() { return ArpSpec{}; }

void test_tie_then_rest_writes_off() {
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat("x - - . x"), 0, ScaleType::Major);
  TEST_ASSERT_NOT_NULL(D.steps[0][0].find(Fx::TIE));
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(kNoteOff, D.steps[0][3].note);
  TEST_ASSERT_EQUAL(64, D.steps[0][4].note);
}

void test_tie_after_rest_is_silent() {
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat(". - x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(60, D.steps[0][2].note);
}

void test_slide_and_gates() {
  reset();
  ArpSpec a = spec();
  a.slide = 20;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x xs xl x~"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(50, D.steps[0][0].find(Fx::GAT)->val);
  TEST_ASSERT_EQUAL(25, D.steps[0][1].find(Fx::GAT)->val);
  TEST_ASSERT_EQUAL(gateValue(110), D.steps[0][2].find(Fx::GAT)->val);  // LONG raised by the slide
  TEST_ASSERT_EQUAL(20, D.steps[0][3].find(Fx::SLD)->val);
  TEST_ASSERT_NULL(D.steps[0][2].find(Fx::SLD));
}

void test_rate_spreads_and_scales_gate() {
  reset();
  ArpSpec a = spec();
  a.rate = 2;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(64, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(100, D.steps[0][0].find(Fx::GAT)->val);
  TEST_ASSERT_EQUAL(67, D.steps[0][4].note);  // the pattern repeats, the note cycle goes on
}

void test_velocity_levels() {
  reset();
  ArpSpec a = spec();
  a.velLo = 40;
  a.velHi = 120;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("o x X"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(40, D.steps[0][0].vel);
  TEST_ASSERT_EQUAL(80, D.steps[0][1].vel);
  TEST_ASSERT_EQUAL(120, D.steps[0][2].vel);
}

void test_keeps_other_fx_replaces_own() {
  reset();
  D.steps[0][0].fx[0] = FxSlot{Fx::FLT, 33};
  D.steps[0][0].fx[1] = FxSlot{Fx::RAT, 4};
  D.steps[0][1].note = 50;  // a rest step of the arp loses its old note
  applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat("x ."), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(33, D.steps[0][0].find(Fx::FLT)->val);
  TEST_ASSERT_NULL(D.steps[0][0].find(Fx::RAT));
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
}

void test_range_and_pattern_length() {
  reset();
  D.length = 8;
  applyArp(S, D, makeSel(0, 2, 0, 15), spec(), pat("x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(60, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(67, D.steps[0][4].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][8].note);  // past the pattern length
}

void test_drum_dest_untouched() {
  reset();
  bool drum[kTracks] = {};
  drum[0] = true;
  D.steps[0][0].note = 0;
  D.steps[0][0].vel = 1;
  TEST_ASSERT_FALSE(applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat("x"), 0, ScaleType::Major, drum));
  TEST_ASSERT_EQUAL(0, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(1, D.steps[0][0].vel);
}

void test_selection_played_order_into_dest() {
  reset();
  S.steps[0][0].note = 67;
  S.steps[1][0].note = 60;
  S.steps[2][0].note = 64;
  D = S;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.mode = ArpMode::Played;
  a.dest = 3;
  applyArp(S, D, makeSel(0, 0, 2, 15), a, pat("x x x x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(67, D.steps[3][0].note);
  TEST_ASSERT_EQUAL(60, D.steps[3][1].note);
  TEST_ASSERT_EQUAL(64, D.steps[3][2].note);
  TEST_ASSERT_EQUAL(67, D.steps[3][3].note);
  TEST_ASSERT_EQUAL(67, D.steps[0][0].note);  // sources untouched
}

void test_selection_chord_change_and_off() {
  reset();
  S.steps[0][0].note = 60;
  S.steps[0][0].fx[0] = FxSlot{Fx::CHD, kChordTriad};  // C E G
  S.steps[0][8].note = 65;
  S.steps[0][8].fx[0] = FxSlot{Fx::CHD, kChordTriad};  // F A C
  S.steps[0][12].note = kNoteOff;
  D = S;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 1;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x x x x x x x x x x x x x x x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[1][0].note);
  TEST_ASSERT_EQUAL(67, D.steps[1][2].note);
  // step 8: cycle position 8 over F A C (65 69 72) -> 8 % 3 = 2 -> 72
  TEST_ASSERT_EQUAL(72, D.steps[1][8].note);
  TEST_ASSERT_EQUAL(65, D.steps[1][9].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[1][12].note);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[1][15].note);
}

void test_selection_dest_inside_reads_snapshot() {
  reset();
  S.steps[0][0].note = 60;
  S.steps[1][0].note = 64;
  D = S;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 0;
  applyArp(S, D, makeSel(0, 0, 1, 15), a, pat("x x x x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(64, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(60, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(64, D.steps[1][0].note);  // other source track untouched
}

void test_selection_skips_drum_sources() {
  reset();
  S.steps[0][0].note = 0;  // drum velocity field, not a note
  S.steps[0][0].vel = 1;
  S.steps[1][0].note = 62;
  D = S;
  bool drum[kTracks] = {};
  drum[0] = true;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 2;
  applyArp(S, D, makeSel(0, 0, 1, 15), a, pat("x x"), 0, ScaleType::Major, drum);
  TEST_ASSERT_EQUAL(62, D.steps[2][0].note);
  TEST_ASSERT_EQUAL(62, D.steps[2][1].note);
}

void test_swing_on_odd_arp_steps() {
  reset();
  ArpSpec a = spec();
  a.swing = 40;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x x x"), 0, ScaleType::Major);
  TEST_ASSERT_NULL(D.steps[0][0].find(Fx::NDG));
  TEST_ASSERT_EQUAL(20, fxSigned(D.steps[0][1].find(Fx::NDG)->val));
  TEST_ASSERT_NULL(D.steps[0][2].find(Fx::NDG));
}

void test_ghost_prb_and_roll() {
  reset();
  ArpSpec a = spec();
  a.ghostPrb = 30;
  a.roll = 100;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("o x x~"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(30, D.steps[0][0].find(Fx::PRB)->val);
  TEST_ASSERT_NULL(D.steps[0][1].find(Fx::PRB));
  const FxSlot* r = D.steps[0][1].find(Fx::RAT);
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_TRUE(r->val >= 2 && r->val <= 4);
  TEST_ASSERT_NULL(D.steps[0][2].find(Fx::RAT));  // no roll on a slide
}

void test_mutate_seeded() {
  const ArpPattern p = pat("x x x x x x x x x x x x x x x x");
  ArpSpec a = spec();
  a.mutate = 100;
  a.seed = 9;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  Pattern first = D;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(0, memcmp(first.steps[0], D.steps[0], sizeof(D.steps[0])));
  int notesN = 0;
  for (int i = 0; i < 16; ++i) notesN += D.steps[0][i].hasNote();
  TEST_ASSERT_TRUE(notesN < 16);  // mutate 100 % drops some hits
  a.mutate = 0;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  for (int i = 0; i < 16; ++i) TEST_ASSERT_TRUE(D.steps[0][i].hasNote());
}

void test_roll_keeps_random_notes() {
  const ArpPattern p = pat("x x x x x x x x x x x x x x x x");
  ArpSpec a = spec();
  a.mode = ArpMode::Random;
  a.seed = 3;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  int before[16];
  notes(16, before);
  a.roll = 50;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  int after[16];
  notes(16, after);
  TEST_ASSERT_EQUAL_INT_ARRAY(before, after, 16);
}

void test_capture_round_trip_rhythm() {
  reset();
  ArpSpec a = spec();
  a.velLo = 40;
  a.velHi = 120;
  const ArpPattern p = pat("X xs o xl . x - . x");
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  ArpPattern c;
  TEST_ASSERT_TRUE(captureArp(D, 0, 0, 8, c));
  TEST_ASSERT_EQUAL(9, c.len);
  for (int i = 0; i < 9; ++i) {
    TEST_ASSERT_EQUAL_MESSAGE(p.steps[i].kind, c.steps[i].kind, "kind");
    if (p.steps[i].kind != ArpKind::Note) continue;
    TEST_ASSERT_EQUAL_MESSAGE(p.steps[i].acc, c.steps[i].acc, "acc");
    TEST_ASSERT_EQUAL_MESSAGE(p.steps[i].len, c.steps[i].len, "len");
  }
}

void test_capture_pitch_and_slide() {
  reset();
  const int n[] = {60, 60, 67, 60, 64};
  for (int i = 0; i < 5; ++i) D.steps[2][i].note = static_cast<uint8_t>(n[i]);
  D.steps[2][4].fx[0] = FxSlot{Fx::SLD, 16};
  ArpPattern c;
  TEST_ASSERT_TRUE(captureArp(D, 2, 0, 4, c));
  TEST_ASSERT_EQUAL(ArpPitch::Next, c.steps[0].pitch);
  TEST_ASSERT_EQUAL(ArpPitch::Repeat, c.steps[1].pitch);
  TEST_ASSERT_EQUAL(ArpPitch::Next, c.steps[2].pitch);
  TEST_ASSERT_EQUAL(ArpPitch::Root, c.steps[3].pitch);
  TEST_ASSERT_TRUE(c.steps[4].slide);
  TEST_ASSERT_EQUAL(ArpAcc::Norm, c.steps[0].acc);  // default velocity
}

void test_capture_empty_and_cap() {
  reset();
  ArpPattern c;
  TEST_ASSERT_FALSE(captureArp(D, 0, 0, 15, c));
  D.length = 64;
  D.steps[0][0].note = 60;
  TEST_ASSERT_TRUE(captureArp(D, 0, 0, 63, c));
  TEST_ASSERT_EQUAL(kArpPatMax, c.len);
}

void test_tie_to_range_end_becomes_gate() {
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 3), spec(), pat("x - - -"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[0][0].note);
  TEST_ASSERT_NULL(D.steps[0][0].find(Fx::TIE));  // no hanging note past the range
  TEST_ASSERT_EQUAL(gateValue(400), D.steps[0][0].find(Fx::GAT)->val);
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat("x . . . . . . . . . . . . x - -"), 0, ScaleType::Major);
  TEST_ASSERT_NULL(D.steps[0][13].find(Fx::TIE));
  TEST_ASSERT_EQUAL(gateValue(300), D.steps[0][13].find(Fx::GAT)->val);
}

void test_selection_off_ends_tie() {
  reset();
  S.steps[0][0].note = 60;
  S.steps[0][0].fx[0] = FxSlot{Fx::CHD, kChordTriad};
  S.steps[0][4].note = kNoteOff;
  D = S;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 1;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x - - - - - - -"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[1][0].note);
  TEST_ASSERT_NOT_NULL(D.steps[1][0].find(Fx::TIE));
  TEST_ASSERT_EQUAL(kNoteOff, D.steps[1][4].note);
  for (int i = 5; i < 16; ++i) TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[1][i].note);
}

void test_selection_reads_chord_before_range() {
  reset();
  S.steps[0][0].note = 60;
  S.steps[0][0].fx[0] = FxSlot{Fx::CHD, kChordTriad};
  D = S;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 1;
  applyArp(S, D, makeSel(0, 4, 0, 15), a, pat("x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[1][3].note);
  TEST_ASSERT_EQUAL(60, D.steps[1][4].note);
  TEST_ASSERT_EQUAL(64, D.steps[1][5].note);
}

void test_selection_many_held_notes() {
  reset();
  for (int t = 0; t < 5; ++t) {  // 5 seventh chords, 2 octaves apart... 20 different notes
    S.steps[t][0].note = static_cast<uint8_t>(36 + 12 * t);
    S.steps[t][0].fx[0] = FxSlot{Fx::CHD, kChordSeventh};
  }
  D = S;
  D.length = 32;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 5;
  applyArp(S, D, makeSel(0, 0, 4, 31), a, pat("x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(36, D.steps[5][0].note);
  TEST_ASSERT_EQUAL(84, D.steps[5][16].note);
  TEST_ASSERT_EQUAL(95, D.steps[5][19].note);
  TEST_ASSERT_EQUAL(36, D.steps[5][20].note);
}

void test_repeat_uses_base_pitch() {
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat("x xr^ xr^ xr"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(72, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(72, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(60, D.steps[0][3].note);
}

void test_slide_after_rest_keeps_sld() {
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat("x . x~"), 0, ScaleType::Major);
  TEST_ASSERT_NOT_NULL(D.steps[0][2].find(Fx::SLD));
  TEST_ASSERT_EQUAL(50, D.steps[0][0].find(Fx::GAT)->val);  // the gate is not stretched over a rest
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), spec(), pat("x~"), 0, ScaleType::Major);
  TEST_ASSERT_NULL(D.steps[0][0].find(Fx::SLD));  // nothing to glide from
}

void test_random_differs_from_up() {
  const ArpPattern p = pat("x x x x x x x x x x x x x x x x");
  ArpSpec a = spec();
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  int up[16];
  notes(16, up);
  a.mode = ArpMode::Random;
  a.seed = 5;
  reset();
  applyArp(S, D, makeSel(0, 0, 0, 15), a, p, 0, ScaleType::Major);
  int rnd[16];
  notes(16, rnd);
  TEST_ASSERT_TRUE(memcmp(up, rnd, sizeof(up)) != 0);
}

void test_octaves_clamp_at_127() {
  reset();
  ArpSpec a = spec();
  a.root = 120;  // C E G = 120 124 127; the upper octaves do not fit
  a.octaves = 4;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x x x x^"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(120, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(124, D.steps[0][1].note);
  TEST_ASSERT_EQUAL(127, D.steps[0][2].note);
  TEST_ASSERT_EQUAL(120, D.steps[0][3].note);
  TEST_ASSERT_EQUAL(127, D.steps[0][4].note);  // 124 + 12 clamped
}

void test_fx_slots_full() {
  reset();
  const Fx other[kFxSlots] = {Fx::FLT, Fx::RES, Fx::DLY, Fx::CCA, Fx::CCB, Fx::VIB};
  for (int s = 0; s < 2; ++s)
    for (int i = 0; i < kFxSlots; ++i) D.steps[0][s].fx[i] = FxSlot{other[i], static_cast<uint8_t>(10 + i)};
  ArpSpec a = spec();
  a.swing = 40;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x x~ - ."), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[0][0].note);
  TEST_ASSERT_EQUAL(64, D.steps[0][1].note);
  for (int s = 0; s < 2; ++s)
    for (int i = 0; i < kFxSlots; ++i) TEST_ASSERT_EQUAL(other[i], D.steps[0][s].fx[i].cmd);
  TEST_ASSERT_EQUAL(kNoteEmpty, D.steps[0][3].note);  // no TIE written, so no OFF
}

void test_selection_change_on_odd_step_with_rate() {
  reset();
  S.steps[0][0].note = 60;
  S.steps[0][3].note = 65;
  D = S;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 1;
  a.rate = 2;
  applyArp(S, D, makeSel(0, 0, 0, 15), a, pat("x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[1][0].note);
  TEST_ASSERT_EQUAL(60, D.steps[1][2].note);
  TEST_ASSERT_EQUAL(65, D.steps[1][4].note);
}

void test_selection_off_and_notes_same_step() {
  reset();
  S.steps[0][0].note = 60;
  S.steps[0][4].note = kNoteOff;
  S.steps[1][4].note = 64;
  D = S;
  ArpSpec a = spec();
  a.source = ArpSource::Selection;
  a.dest = 2;
  applyArp(S, D, makeSel(0, 0, 1, 15), a, pat("x"), 0, ScaleType::Major);
  TEST_ASSERT_EQUAL(60, D.steps[2][3].note);
  TEST_ASSERT_EQUAL(64, D.steps[2][4].note);
  TEST_ASSERT_EQUAL(64, D.steps[2][8].note);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_tokens);
  RUN_TEST(test_format_round_trip);
  RUN_TEST(test_parse_caps_at_32);
  RUN_TEST(test_factory_patterns_parse);
  RUN_TEST(test_modes_triad);
  RUN_TEST(test_modes_seventh);
  RUN_TEST(test_chord_mode_writes_chd);
  RUN_TEST(test_random_mode_deterministic);
  RUN_TEST(test_tie_then_rest_writes_off);
  RUN_TEST(test_tie_after_rest_is_silent);
  RUN_TEST(test_slide_and_gates);
  RUN_TEST(test_rate_spreads_and_scales_gate);
  RUN_TEST(test_velocity_levels);
  RUN_TEST(test_keeps_other_fx_replaces_own);
  RUN_TEST(test_range_and_pattern_length);
  RUN_TEST(test_drum_dest_untouched);
  RUN_TEST(test_selection_played_order_into_dest);
  RUN_TEST(test_selection_chord_change_and_off);
  RUN_TEST(test_selection_dest_inside_reads_snapshot);
  RUN_TEST(test_selection_skips_drum_sources);
  RUN_TEST(test_swing_on_odd_arp_steps);
  RUN_TEST(test_ghost_prb_and_roll);
  RUN_TEST(test_mutate_seeded);
  RUN_TEST(test_roll_keeps_random_notes);
  RUN_TEST(test_capture_round_trip_rhythm);
  RUN_TEST(test_capture_pitch_and_slide);
  RUN_TEST(test_capture_empty_and_cap);
  RUN_TEST(test_tie_to_range_end_becomes_gate);
  RUN_TEST(test_selection_off_ends_tie);
  RUN_TEST(test_selection_reads_chord_before_range);
  RUN_TEST(test_selection_many_held_notes);
  RUN_TEST(test_repeat_uses_base_pitch);
  RUN_TEST(test_slide_after_rest_keeps_sld);
  RUN_TEST(test_random_differs_from_up);
  RUN_TEST(test_octaves_clamp_at_127);
  RUN_TEST(test_fx_slots_full);
  RUN_TEST(test_selection_change_on_odd_step_with_rate);
  RUN_TEST(test_selection_off_and_notes_same_step);
  return UNITY_END();
}
