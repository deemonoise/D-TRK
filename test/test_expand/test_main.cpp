#include <unity.h>
#include "step_expand.h"
#include "scale.h"

using namespace mt;

static const uint32_t kStepUs = 125000;  // 1/16 at 120 BPM
static TrackCfg track;
static Rng rng(7);
static const ExpandCtx ctx{kStepUs, 0, 0, ScaleType::Chromatic};

void setUp() {
  track = TrackCfg();
  track.out = TrackOut::Midi;
  track.channel = 2;
  track.defVel = 100;
  track.defGate = 50;
}
void tearDown() {}

static Step note(uint8_t n) { Step s; s.note = n; return s; }

void test_empty_and_off_produce_nothing() {
  ExpandOut out;
  TEST_ASSERT_FALSE(expandStep(Step(), track, ctx, rng, out));
  TEST_ASSERT_EQUAL(0, out.count);
  TEST_ASSERT_FALSE(expandStep(note(kNoteOff), track, ctx, rng, out));
}

void test_plain_note() {
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(note(60), track, ctx, rng, out));
  TEST_ASSERT_EQUAL(2, out.count);
  TEST_ASSERT_EQUAL(0, out.ev[0].offsetUs);
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::NoteOn);
  TEST_ASSERT_EQUAL(2, out.ev[0].ch);
  TEST_ASSERT_EQUAL(60, out.ev[0].note);
  TEST_ASSERT_EQUAL(100, out.ev[0].vel);
  TEST_ASSERT_TRUE(out.ev[1].kind == EvKind::NoteOff);
  TEST_ASSERT_EQUAL(62500, out.ev[1].offsetUs);
  TEST_ASSERT_FALSE(out.tie);
}

void test_step_velocity_overrides_default() {
  Step s = note(60);
  s.vel = 80;
  ExpandOut out;
  expandStep(s, track, ctx, rng, out);
  TEST_ASSERT_EQUAL(80, out.ev[0].vel);
}

void test_chn_overrides_channel() {
  Step s = note(60);
  s.fx[0] = {Fx::CHN, 10};
  ExpandOut out;
  expandStep(s, track, ctx, rng, out);
  TEST_ASSERT_EQUAL(9, out.ev[0].ch);
  TEST_ASSERT_EQUAL(9, out.ev[1].ch);
}

void test_ratchet_four() {
  Step s = note(60);
  s.fx[0] = {Fx::RAT, 4};
  ExpandOut out;
  expandStep(s, track, ctx, rng, out);
  TEST_ASSERT_EQUAL(8, out.count);
  const int32_t ons[] = {0, 31250, 62500, 93750};
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_TRUE(out.ev[i * 2].kind == EvKind::NoteOn);
    TEST_ASSERT_EQUAL(ons[i], out.ev[i * 2].offsetUs);
    TEST_ASSERT_EQUAL(ons[i] + 15625, out.ev[i * 2 + 1].offsetUs);
  }
}

void test_probability_bounds() {
  Step s = note(60);
  s.fx[0] = {Fx::PRB, 0};
  ExpandOut out;
  for (int i = 0; i < 100; ++i) TEST_ASSERT_FALSE(expandStep(s, track, ctx, rng, out));
  s.fx[0].val = 100;
  for (int i = 0; i < 100; ++i) TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  s.fx[0].val = 50;
  int hits = 0;
  for (int i = 0; i < 1000; ++i) hits += expandStep(s, track, ctx, rng, out);
  TEST_ASSERT_INT_WITHIN(100, 500, hits);
}

void test_negative_nudge() {
  Step s = note(60);
  s.fx[1] = {Fx::NDG, static_cast<uint8_t>(-25)};
  ExpandOut out;
  expandStep(s, track, ctx, rng, out);
  TEST_ASSERT_EQUAL(-31250, out.ev[0].offsetUs);
  TEST_ASSERT_EQUAL(-31250 + 62500, out.ev[1].offsetUs);
}

void test_long_gate() {
  Step s = note(60);
  s.fx[0] = {Fx::GAT, 150};  // 450 %
  ExpandOut out;
  expandStep(s, track, ctx, rng, out);
  TEST_ASSERT_EQUAL(562500, out.ev[1].offsetUs);
}

void test_tie_has_no_note_off() {
  Step s = note(60);
  s.fx[0] = {Fx::TIE, 0};
  ExpandOut out;
  expandStep(s, track, ctx, rng, out);
  TEST_ASSERT_EQUAL(1, out.count);
  TEST_ASSERT_TRUE(out.tie);
}

void test_ratchet_with_tie_holds_last() {
  Step s = note(60);
  s.fx[0] = {Fx::RAT, 4};
  s.fx[1] = {Fx::TIE, 0};
  ExpandOut out;
  expandStep(s, track, ctx, rng, out);
  TEST_ASSERT_EQUAL(7, out.count);
  TEST_ASSERT_TRUE(out.ev[6].kind == EvKind::NoteOn);
  TEST_ASSERT_TRUE(out.tie);
}

static ExpandCtx ctxLoop(uint32_t loop) { return ExpandCtx{kStepUs, loop, 0, ScaleType::Chromatic}; }
static const ExpandCtx cMajor{kStepUs, 0, 0, ScaleType::Major};

void test_cnd_one_of_two() {
  Step s = note(60);
  s.fx[0] = {Fx::CND, 0x12};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctxLoop(0), rng, out));
  TEST_ASSERT_FALSE(expandStep(s, track, ctxLoop(1), rng, out));
  TEST_ASSERT_EQUAL(0, out.count);
  TEST_ASSERT_TRUE(expandStep(s, track, ctxLoop(2), rng, out));
  TEST_ASSERT_FALSE(expandStep(s, track, ctxLoop(3), rng, out));
}

void test_cnd_two_of_four() {
  Step s = note(60);
  s.fx[0] = {Fx::CND, 0x24};
  ExpandOut out;
  for (uint32_t l = 0; l < 8; ++l) {
    TEST_ASSERT_EQUAL(l == 1 || l == 5, expandStep(s, track, ctxLoop(l), rng, out));
  }
}

void test_cnd_first() {
  Step s = note(60);
  s.fx[0] = {Fx::CND, 0};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctxLoop(0), rng, out));
  for (uint32_t l = 1; l < 5; ++l) TEST_ASSERT_FALSE(expandStep(s, track, ctxLoop(l), rng, out));
}

void test_cca_on_empty_step() {
  Step s;
  s.fx[0] = {Fx::CCA, 100};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(1, out.count);
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::Cc);
  TEST_ASSERT_EQUAL(2, out.ev[0].ch);
  TEST_ASSERT_EQUAL(74, out.ev[0].note);
  TEST_ASSERT_EQUAL(100, out.ev[0].vel);
}

void test_ccb_uses_track_controller_and_clamps() {
  Step s;
  s.fx[0] = {Fx::CCB, 200};
  ExpandOut out;
  expandStep(s, track, ctx, rng, out);
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::Cc);
  TEST_ASSERT_EQUAL(71, out.ev[0].note);
  TEST_ASSERT_EQUAL(127, out.ev[0].vel);
}

void test_control_on_off_step() {
  Step s = note(kNoteOff);
  s.fx[0] = {Fx::CCA, 5};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(1, out.count);
}

static void checkPbn(int v, uint8_t lsb, uint8_t msb) {
  Step s;
  s.fx[0] = {Fx::PBN, static_cast<uint8_t>(v)};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(1, out.count);
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::PitchBend);
  TEST_ASSERT_EQUAL(lsb, out.ev[0].note);
  TEST_ASSERT_EQUAL(msb, out.ev[0].vel);
}

void test_pitch_bend_values() {
  checkPbn(-64, 0, 0);
  checkPbn(0, 0, 64);
  checkPbn(63, 0, 127);
}

void test_program_comes_first() {
  Step s = note(60);
  s.fx[0] = {Fx::PGM, 5};
  s.fx[1] = {Fx::NDG, static_cast<uint8_t>(-10)};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(3, out.count);
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::Program);
  TEST_ASSERT_EQUAL(5, out.ev[0].note);
  TEST_ASSERT_EQUAL(-12500, out.ev[0].offsetUs);
  TEST_ASSERT_TRUE(out.ev[1].kind == EvKind::NoteOn);
  TEST_ASSERT_TRUE(out.ev[2].kind == EvKind::NoteOff);
}

void test_control_uses_chn() {
  Step s;
  s.fx[0] = {Fx::CCA, 1};
  s.fx[1] = {Fx::CHN, 16};
  ExpandOut out;
  expandStep(s, track, ctx, rng, out);
  TEST_ASSERT_EQUAL(15, out.ev[0].ch);
}

void test_chord_triad_c_major() {
  Step s = note(60);
  s.fx[0] = {Fx::CHD, kChordTriad};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, cMajor, rng, out));
  TEST_ASSERT_EQUAL(6, out.count);
  const uint8_t notes[] = {60, 64, 67};
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(out.ev[i * 2].kind == EvKind::NoteOn);
    TEST_ASSERT_TRUE(out.ev[i * 2 + 1].kind == EvKind::NoteOff);
    TEST_ASSERT_EQUAL(notes[i], out.ev[i * 2].note);
    TEST_ASSERT_EQUAL(notes[i], out.ev[i * 2 + 1].note);
    TEST_ASSERT_EQUAL(0, out.ev[i * 2].offsetUs);
  }
}

void test_chord_strum() {
  Step s = note(60);
  s.fx[0] = {Fx::CHD, kChordTriad};
  s.fx[1] = {Fx::STR, 10};
  ExpandOut out;
  expandStep(s, track, cMajor, rng, out);
  TEST_ASSERT_EQUAL(0, out.ev[0].offsetUs);
  TEST_ASSERT_EQUAL(12500, out.ev[2].offsetUs);
  TEST_ASSERT_EQUAL(25000, out.ev[4].offsetUs);
}

void test_seventh_chord_ratchet_eight_fits() {
  Step s = note(60);
  s.fx[0] = {Fx::CHD, kChordSeventh};
  s.fx[1] = {Fx::RAT, 8};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, cMajor, rng, out));
  TEST_ASSERT_EQUAL(64, out.count);
  TEST_ASSERT_TRUE(out.count <= kMaxStepEvents);
}

void test_note_random_in_scale() {
  Step s = note(60);
  s.fx[0] = {Fx::NRN, 1};
  ExpandOut out;
  bool seen59 = false, seen60 = false, seen62 = false;
  for (int i = 0; i < 1000; ++i) {
    TEST_ASSERT_TRUE(expandStep(s, track, cMajor, rng, out));
    const uint8_t n = out.ev[0].note;
    TEST_ASSERT_TRUE(n == 59 || n == 60 || n == 62);
    TEST_ASSERT_EQUAL(n, out.ev[1].note);
    seen59 |= n == 59;
    seen60 |= n == 60;
    seen62 |= n == 62;
  }
  TEST_ASSERT_TRUE(seen59 && seen60 && seen62);
}

void test_velocity_random() {
  Step s = note(60);
  s.vel = 100;
  s.fx[0] = {Fx::VRN, 64};
  ExpandOut out;
  int lo = 127, hi = 0;
  for (int i = 0; i < 1000; ++i) {
    expandStep(s, track, ctx, rng, out);
    const int v = out.ev[0].vel;
    TEST_ASSERT_TRUE(v >= 36 && v <= 127);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  TEST_ASSERT_TRUE(hi - lo > 40);
}

void test_default_gate_uses_gat_encoding() {
  track.defGate = 150;  // 450 %
  ExpandOut out;
  expandStep(note(60), track, ctx, rng, out);
  TEST_ASSERT_EQUAL(562500, out.ev[1].offsetUs);
}

void test_chord_ignores_tie() {
  Step s = note(60);
  s.fx[0] = {Fx::CHD, kChordTriad};
  s.fx[1] = {Fx::TIE, 0};
  ExpandOut out;
  expandStep(s, track, cMajor, rng, out);
  TEST_ASSERT_FALSE(out.tie);
  TEST_ASSERT_EQUAL(6, out.count);
  for (int i = 0; i < 3; ++i) TEST_ASSERT_TRUE(out.ev[i * 2 + 1].kind == EvKind::NoteOff);
}

void test_synth_fx_on_int_track() {
  track.out = TrackOut::Int;
  Step s = note(60);
  s.fx[0] = {Fx::VIB, 0x44};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(3, out.count);
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::SynthFx);
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::VIB), out.ev[0].note);
  TEST_ASSERT_EQUAL(0x44, out.ev[0].vel);
  TEST_ASSERT_EQUAL(0, out.ev[0].offsetUs);
  TEST_ASSERT_TRUE(out.ev[1].kind == EvKind::NoteOn);
}

void test_synth_fx_ignored_on_midi_track() {
  Step s = note(60);
  s.fx[0] = {Fx::VIB, 0x44};
  s.fx[1] = {Fx::CUT, 3};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(2, out.count);
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::NoteOn);
  Step e;
  e.fx[0] = {Fx::SLD, 10};
  TEST_ASSERT_FALSE(expandStep(e, track, ctx, rng, out));  // nothing plays
  track.out = TrackOut::Int;
  TEST_ASSERT_TRUE(expandStep(e, track, ctx, rng, out));  // slide without a note
  TEST_ASSERT_EQUAL(1, out.count);
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::SynthFx);
}

void test_fm_lock_fx_on_int_track() {
  track.out = TrackOut::Int;
  Step s = note(60);
  s.fx[0] = {Fx::COL, 99};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_TRUE(out.ev[0].kind == EvKind::SynthFx);
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::COL), out.ev[0].note);
  TEST_ASSERT_EQUAL(99, out.ev[0].vel);
}

// ---- OFF ----

static int countKind(const ExpandOut& o, EvKind k) {
  int n = 0;
  for (int i = 0; i < o.count; ++i) n += o.ev[i].kind == k;
  return n;
}

void test_off_on_empty_step() {
  Step s;
  s.fx[3] = {Fx::OFF, 0};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(0, out.count);
  TEST_ASSERT_EQUAL(0, out.offUs);
  s.fx[3].val = 12;  // half a 24-tick step
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(kStepUs / 2, out.offUs);
  // Without OFF there is none.
  TEST_ASSERT_TRUE(expandStep(note(60), track, ctx, rng, out));
  TEST_ASSERT_EQUAL(-1, out.offUs);
}

void test_off_uses_ticks_per_step() {
  ExpandCtx c = ctx;
  c.tps = 96;
  Step s;
  s.fx[0] = {Fx::OFF, 24};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, c, rng, out));
  TEST_ASSERT_EQUAL(kStepUs / 4, out.offUs);
}

void test_off_shortens_gate() {
  Step s = note(60);
  s.fx[0] = {Fx::GAT, 200};  // 800 %
  s.fx[1] = {Fx::OFF, 6};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(2, out.count);
  TEST_ASSERT_EQUAL(kStepUs / 4, out.ev[1].offsetUs);
  TEST_ASSERT_EQUAL(kStepUs / 4, out.offUs);
}

void test_off_zero_with_note_keeps_min_gate() {
  Step s = note(60);
  s.fx[0] = {Fx::OFF, 0};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(2, out.count);
  TEST_ASSERT_EQUAL(static_cast<int32_t>(kMinGateUs), out.ev[1].offsetUs);
  TEST_ASSERT_EQUAL(static_cast<int32_t>(kMinGateUs), out.offUs);
}

void test_off_drops_later_ratchets_and_tie() {
  Step s = note(60);
  s.fx[0] = {Fx::RAT, 4};
  s.fx[1] = {Fx::TIE, 0};
  s.fx[5] = {Fx::OFF, 12};  // hits at 0 and 6 ticks play
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(2, countKind(out, EvKind::NoteOn));
  TEST_ASSERT_EQUAL(2, countKind(out, EvKind::NoteOff));
  TEST_ASSERT_FALSE(out.tie);
  for (int i = 0; i < out.count; ++i) TEST_ASSERT_TRUE(out.ev[i].offsetUs <= out.offUs);
}

void test_off_follows_nudge() {
  Step s = note(60);
  s.fx[0] = {Fx::NDG, 20};  // +20 %
  s.fx[1] = {Fx::OFF, 12};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(static_cast<int32_t>(kStepUs / 5 + kStepUs / 2), out.offUs);
}

// Drum track (KIT instrument): vel = lane mask, note = step velocity.
static Instrument kit() { Instrument k; instrSetType(k, InstrType::Kit); return k; }  // notes 60..67
static Step drumStep(uint8_t mask, uint8_t vel = 0) { Step s; s.note = vel; s.vel = mask; return s; }

void test_drum_mask_to_lane_notes() {
  const Instrument k = kit();
  ExpandCtx c = ctx;
  c.kit = &k;
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(drumStep(0b00000101), track, c, rng, out));
  TEST_ASSERT_EQUAL(4, out.count);  // 2 lanes x (on, off); a lane without a sample still goes out (MIDI)
  TEST_ASSERT_EQUAL(60, out.ev[0].note);
  TEST_ASSERT_EQUAL(track.defVel, out.ev[0].vel);
  TEST_ASSERT_EQUAL(2, out.ev[0].ch);
  TEST_ASSERT_TRUE(out.ev[1].kind == EvKind::NoteOff);
  TEST_ASSERT_EQUAL(60, out.ev[1].note);
  TEST_ASSERT_EQUAL(62500, out.ev[1].offsetUs);
  TEST_ASSERT_EQUAL(62, out.ev[2].note);
  TEST_ASSERT_EQUAL(0, out.ev[2].offsetUs);
  TEST_ASSERT_FALSE(out.tie);
}

void test_drum_velocity_and_accent() {
  const Instrument k = kit();
  ExpandCtx c = ctx;
  c.kit = &k;
  Step s = drumStep(0b00000101, 100);
  s.fx[0] = {Fx::ACC, 0b00000001};  // lane 1 full, lane 3 at 60 %
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, c, rng, out));
  TEST_ASSERT_EQUAL(100, out.ev[0].vel);
  TEST_ASSERT_EQUAL(60, out.ev[2].vel);
}

void test_drum_ratchet_fits_and_tie_ignored() {
  const Instrument k = kit();
  ExpandCtx c = ctx;
  c.kit = &k;
  Step s = drumStep(0xFF);
  s.fx[0] = {Fx::RAT, 8};
  s.fx[1] = {Fx::TIE, 0};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, c, rng, out));
  TEST_ASSERT_EQUAL(128, out.count);
  TEST_ASSERT_TRUE(out.count <= kMaxStepEvents);
  TEST_ASSERT_EQUAL(128, countKind(out, EvKind::NoteOn) + countKind(out, EvKind::NoteOff));
  TEST_ASSERT_FALSE(out.tie);
  // Hits must not overlap: every NoteOff before the next ratchet's NoteOn.
  TEST_ASSERT_TRUE(out.ev[1].offsetUs < out.ev[16].offsetUs);
}

void test_drum_empty_mask_is_note_step() {
  const Instrument k = kit();
  ExpandCtx c = ctx;
  c.kit = &k;
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(drumStep(0), track, c, rng, out));
  TEST_ASSERT_EQUAL(0, out.count);
}

void test_drum_off_cuts_notes() {
  const Instrument k = kit();
  ExpandCtx c = ctx;
  c.kit = &k;
  Step s = drumStep(0b00000011);
  s.fx[0] = {Fx::GAT, 200};  // 800 %
  s.fx[1] = {Fx::OFF, 6};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, c, rng, out));
  TEST_ASSERT_EQUAL(4, out.count);
  TEST_ASSERT_EQUAL(kStepUs / 4, out.offUs);
  TEST_ASSERT_EQUAL(kStepUs / 4, out.ev[1].offsetUs);
  TEST_ASSERT_EQUAL(kStepUs / 4, out.ev[3].offsetUs);
}

void test_acc_ignored_on_melodic_track() {
  Step s = note(60);
  s.fx[0] = {Fx::ACC, 0};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(2, out.count);
  TEST_ASSERT_EQUAL(100, out.ev[0].vel);
}

void test_cnd_fill_values() {
  Step s;
  s.note = 60;
  s.fx[0] = {Fx::CND, kCndFill};
  TrackCfg t;
  Rng rng(1);
  ExpandOut out;
  ExpandCtx on = ctx;
  on.fill = true;
  ExpandCtx off = ctx;
  off.fill = false;
  TEST_ASSERT_TRUE(expandStep(s, t, on, rng, out));
  TEST_ASSERT_FALSE(expandStep(s, t, off, rng, out));
  s.fx[0].val = kCndNoFill;
  TEST_ASSERT_FALSE(expandStep(s, t, on, rng, out));
  TEST_ASSERT_TRUE(expandStep(s, t, off, rng, out));
  s.fx[0].val = 0;  // FST still on the first pass only, fill or not
  TEST_ASSERT_TRUE(expandStep(s, t, on, rng, out));
  on.loop = 1;
  TEST_ASSERT_FALSE(expandStep(s, t, on, rng, out));
}

// LFO (the selector) goes out before the step's other LFO fx, whatever its slot; INT only.
void test_lfo_select_goes_first() {
  TrackCfg t;
  t.out = TrackOut::Int;
  Step s = note(60);
  s.fx[0] = {Fx::LFD, 20};
  s.fx[5] = {Fx::LFO, 3};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  int sel = -1, dep = -1;
  for (int i = 0; i < out.count; ++i) {
    if (out.ev[i].kind != EvKind::SynthFx) continue;
    if (out.ev[i].note == static_cast<uint8_t>(Fx::LFO)) sel = i;
    if (out.ev[i].note == static_cast<uint8_t>(Fx::LFD)) dep = i;
  }
  TEST_ASSERT_TRUE(sel >= 0 && sel < dep);
  TEST_ASSERT_EQUAL(3, out.ev[sel].vel);
  TEST_ASSERT_EQUAL(2, countKind(out, EvKind::SynthFx));
  t.out = TrackOut::Midi;
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(0, countKind(out, EvKind::SynthFx));
}

// ---- ARP + CHD ----

void test_arp_chd_int_emits_root_and_arpchord() {
  TrackCfg t;
  t.out = TrackOut::Int;
  Step s = note(60);
  s.fx[0] = {Fx::CHD, kChordTriad};
  s.fx[1] = {Fx::ARP, 0x37};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(1, countKind(out, EvKind::NoteOn));
  int arpChord = -1, firstOn = -1;
  for (int i = 0; i < out.count; ++i) {
    if (out.ev[i].kind == EvKind::SynthFx && out.ev[i].note == kSynthArpChord) arpChord = i;
    if (out.ev[i].kind == EvKind::NoteOn && firstOn < 0) firstOn = i;
  }
  TEST_ASSERT_TRUE(arpChord >= 0 && arpChord < firstOn);  // before the note-on
  TEST_ASSERT_EQUAL(kChordTriad, out.ev[arpChord].vel);
  TEST_ASSERT_EQUAL(60, out.ev[firstOn].note);
}

void test_arp_chd_midi_full_chord() {
  TrackCfg t;
  t.out = TrackOut::Midi;
  Step s = note(60);
  s.fx[0] = {Fx::CHD, kChordTriad};
  s.fx[1] = {Fx::ARP, 0x37};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(3, countKind(out, EvKind::NoteOn));
  TEST_ASSERT_EQUAL(0, countKind(out, EvKind::SynthFx));
}

void test_arp_00_chd_normal_chord() {
  TrackCfg t;
  t.out = TrackOut::Int;
  Step s = note(60);
  s.fx[0] = {Fx::CHD, kChordTriad};
  s.fx[1] = {Fx::ARP, 0};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(3, countKind(out, EvKind::NoteOn));
}

// ---- ARS (step arp) ----

void test_ars_chord_plays_first_note_and_hands_over_the_arp() {
  TrackCfg t;
  t.out = TrackOut::Int;
  Step s = note(60);
  s.fx[0] = {Fx::CHD, kChordTriad};
  s.fx[1] = {Fx::ARP, 0x37};
  s.fx[2] = {Fx::ARS, 0x12};  // DOWN, every 2 steps
  s.fx[3] = {Fx::ARM, 0x05};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(1, countKind(out, EvKind::NoteOn));
  TEST_ASSERT_EQUAL(0, countKind(out, EvKind::SynthFx));  // no synth ARP / ARM / arp chord
  TEST_ASSERT_EQUAL(3, out.arp.n);
  TEST_ASSERT_EQUAL(60, out.arp.notes[0]);
  TEST_ASSERT_EQUAL(64, out.arp.notes[1]);
  TEST_ASSERT_EQUAL(67, out.arp.notes[2]);
  TEST_ASSERT_EQUAL(1, out.arp.mode);
  TEST_ASSERT_EQUAL(2, out.arp.div);
  for (int i = 0; i < out.count; ++i)
    if (out.ev[i].kind == EvKind::NoteOn) TEST_ASSERT_EQUAL(67, out.ev[i].note);  // DOWN: the top first
}

void test_ars_uses_arp_offsets_or_octave_on_midi() {
  TrackCfg t;
  t.out = TrackOut::Midi;
  t.channel = 3;
  t.defGate = 50;
  Step s = note(48);
  s.fx[0] = {Fx::ARS, kArsDefault};
  s.fx[1] = {Fx::TIE, 0};
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(2, out.arp.count());  // root and an octave up
  TEST_ASSERT_EQUAL(48, out.arp.note(0));
  TEST_ASSERT_EQUAL(60, out.arp.note(1));
  TEST_ASSERT_EQUAL(3, out.arp.ch);
  TEST_ASSERT_EQUAL(gatePercent(50), out.arp.gate);
  TEST_ASSERT_FALSE(out.tie);  // an arp ignores TIE
  TEST_ASSERT_EQUAL(48, out.ev[0].note);
  s.fx[1] = {Fx::ARP, 0x47};
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(3, out.arp.n);
  TEST_ASSERT_EQUAL(52, out.arp.notes[1]);
  TEST_ASSERT_EQUAL(55, out.arp.notes[2]);
}

void test_ars_octaves() {
  TrackCfg t;
  t.out = TrackOut::Midi;
  Step s = note(60);
  s.fx[0] = {Fx::CHD, kChordTriad};
  s.fx[1] = {Fx::ARS, 0x51};  // 2 octaves, DOWN, every step
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(3, out.arp.n);
  TEST_ASSERT_EQUAL(2, out.arp.oct);
  TEST_ASSERT_EQUAL(6, out.arp.count());
  TEST_ASSERT_EQUAL(79, out.arp.note(5));  // G an octave up
  TEST_ASSERT_EQUAL(79, out.ev[0].note);   // DOWN starts at the top of the range
  // The root alone: its octaves, at least two.
  s.fx[0] = {};
  s.fx[1] = {Fx::ARS, 0x81};  // 3 octaves
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(1, out.arp.n);
  TEST_ASSERT_EQUAL(3, out.arp.count());
  TEST_ASSERT_EQUAL(84, out.arp.note(2));
  s.fx[1] = {Fx::ARS, kArsDefault};
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(2, out.arp.count());
  // Above 127: an octave down.
  s.note = 120;
  s.fx[1] = {Fx::ARS, 0xC1};
  TEST_ASSERT_TRUE(expandStep(s, t, cMajor, rng, out));
  TEST_ASSERT_EQUAL(120, out.arp.note(1));
}

void test_ars_absent_without_fx() {
  ExpandOut out;
  TEST_ASSERT_TRUE(expandStep(note(60), track, ctx, rng, out));
  TEST_ASSERT_EQUAL(0, out.arp.n);
}

void test_arp_index_modes() {
  const int up[] = {0, 1, 2, 0}, down[] = {2, 1, 0, 2}, ud[] = {0, 1, 2, 1, 0, 1};
  for (int k = 0; k < 4; ++k) TEST_ASSERT_EQUAL(up[k], arpIndex(0, k, 3));
  for (int k = 0; k < 4; ++k) TEST_ASSERT_EQUAL(down[k], arpIndex(1, k, 3));
  for (int k = 0; k < 6; ++k) TEST_ASSERT_EQUAL(ud[k], arpIndex(2, k, 3));
  TEST_ASSERT_EQUAL(0, arpIndex(2, 5, 1));
}

// PRE / NEI read the context; CND A:B and PRB report their result for the next ones.
void test_cnd_pre_nei_and_result() {
  ExpandOut out;
  Step s = note(60);
  s.fx[0] = {Fx::CND, kCndPre};
  ExpandCtx c = ctx;
  c.pre = false;
  TEST_ASSERT_FALSE(expandStep(s, track, c, rng, out));
  TEST_ASSERT_EQUAL(-1, out.cond);  // PRE itself is not a result
  c.pre = true;
  TEST_ASSERT_TRUE(expandStep(s, track, c, rng, out));
  s.fx[0] = {Fx::CND, kCndNotNei};
  c.nei = true;
  TEST_ASSERT_FALSE(expandStep(s, track, c, rng, out));
  s.fx[0] = {Fx::CND, 0x12};  // 1:2, loop 0 passes
  TEST_ASSERT_TRUE(expandStep(s, track, c, rng, out));
  TEST_ASSERT_EQUAL(1, out.cond);
  c.loop = 1;
  TEST_ASSERT_FALSE(expandStep(s, track, c, rng, out));
  TEST_ASSERT_EQUAL(0, out.cond);
  s.fx[0] = {Fx::PRB, 0};
  TEST_ASSERT_FALSE(expandStep(s, track, ctx, rng, out));
  TEST_ASSERT_EQUAL(0, out.cond);
  TEST_ASSERT_TRUE(expandStep(note(60), track, ctx, rng, out));
  TEST_ASSERT_EQUAL(-1, out.cond);
}

void test_rat_velocity_ramp() {
  ExpandOut out;
  Step s = note(60);
  s.vel = 100;
  s.fx[0] = {Fx::RAT, 0x14};  // 4 hits rising
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  const int up[] = {25, 50, 75, 100};
  int k = 0;
  for (int i = 0; i < out.count; ++i)
    if (out.ev[i].kind == EvKind::NoteOn) TEST_ASSERT_EQUAL(up[k++], out.ev[i].vel);
  TEST_ASSERT_EQUAL(4, k);
  s.fx[0] = {Fx::RAT, 0x24};  // falling
  TEST_ASSERT_TRUE(expandStep(s, track, ctx, rng, out));
  k = 0;
  for (int i = 0; i < out.count; ++i)
    if (out.ev[i].kind == EvKind::NoteOn) TEST_ASSERT_EQUAL(up[3 - k++], out.ev[i].vel);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_and_off_produce_nothing);
  RUN_TEST(test_plain_note);
  RUN_TEST(test_off_on_empty_step);
  RUN_TEST(test_off_uses_ticks_per_step);
  RUN_TEST(test_off_shortens_gate);
  RUN_TEST(test_off_zero_with_note_keeps_min_gate);
  RUN_TEST(test_off_drops_later_ratchets_and_tie);
  RUN_TEST(test_off_follows_nudge);
  RUN_TEST(test_step_velocity_overrides_default);
  RUN_TEST(test_chn_overrides_channel);
  RUN_TEST(test_ratchet_four);
  RUN_TEST(test_probability_bounds);
  RUN_TEST(test_negative_nudge);
  RUN_TEST(test_long_gate);
  RUN_TEST(test_tie_has_no_note_off);
  RUN_TEST(test_ratchet_with_tie_holds_last);
  RUN_TEST(test_cnd_one_of_two);
  RUN_TEST(test_cnd_two_of_four);
  RUN_TEST(test_cnd_first);
  RUN_TEST(test_cca_on_empty_step);
  RUN_TEST(test_ccb_uses_track_controller_and_clamps);
  RUN_TEST(test_control_on_off_step);
  RUN_TEST(test_pitch_bend_values);
  RUN_TEST(test_program_comes_first);
  RUN_TEST(test_control_uses_chn);
  RUN_TEST(test_chord_triad_c_major);
  RUN_TEST(test_chord_strum);
  RUN_TEST(test_seventh_chord_ratchet_eight_fits);
  RUN_TEST(test_note_random_in_scale);
  RUN_TEST(test_velocity_random);
  RUN_TEST(test_chord_ignores_tie);
  RUN_TEST(test_default_gate_uses_gat_encoding);
  RUN_TEST(test_synth_fx_on_int_track);
  RUN_TEST(test_synth_fx_ignored_on_midi_track);
  RUN_TEST(test_fm_lock_fx_on_int_track);
  RUN_TEST(test_drum_mask_to_lane_notes);
  RUN_TEST(test_drum_velocity_and_accent);
  RUN_TEST(test_drum_ratchet_fits_and_tie_ignored);
  RUN_TEST(test_drum_empty_mask_is_note_step);
  RUN_TEST(test_drum_off_cuts_notes);
  RUN_TEST(test_acc_ignored_on_melodic_track);
  RUN_TEST(test_cnd_fill_values);
  RUN_TEST(test_arp_chd_int_emits_root_and_arpchord);
  RUN_TEST(test_arp_chd_midi_full_chord);
  RUN_TEST(test_arp_00_chd_normal_chord);
  RUN_TEST(test_ars_chord_plays_first_note_and_hands_over_the_arp);
  RUN_TEST(test_ars_uses_arp_offsets_or_octave_on_midi);
  RUN_TEST(test_ars_octaves);
  RUN_TEST(test_ars_absent_without_fx);
  RUN_TEST(test_arp_index_modes);
  RUN_TEST(test_cnd_pre_nei_and_result);
  RUN_TEST(test_rat_velocity_ramp);
  RUN_TEST(test_lfo_select_goes_first);
  return UNITY_END();
}
