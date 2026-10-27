// Synth::setPreviewTap: the preview strip alone (insert, fader, pan; no sends, no master chain), per
// render() block, in mix units.
#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth.h"

using namespace mt;

static Project* p;
static Synth* s;
static int16_t buf[Synth::kBlock];
static float tapL[Synth::kBlock], tapR[Synth::kBlock];

void setUp() {
  p = new Project();
  p->masterVol = 100;
  for (int t = 0; t < kTracks; ++t) {
    p->tracks[t].out = TrackOut::Int;
    p->tracks[t].vol = 127;
  }
  for (auto& m : p->instruments) {
    instrSetType(m, InstrType::Chip);
    m.wave = static_cast<uint8_t>(Wave::Saw);
    m.vol = 60;
    m.attack = 0;
    m.decay = 0;
    m.sustain = 127;
    m.release = 0;
  }
  s = new Synth(*p);
}
void tearDown() {
  delete s;
  delete p;
}

static void send(int off, uint8_t track, uint8_t a, uint8_t b = 0, uint8_t c = 0, uint8_t len = 3) {
  const uint8_t m[3] = {a, b, c};
  TEST_ASSERT_TRUE(s->event(off, track, m, len));
}
static void noteOn(int off, uint8_t track, uint8_t note) { send(off, track, 0x90, note, 127); }

static float peak(const float* x) {
  float pk = 0;
  for (int i = 0; i < Synth::kBlock; ++i) pk = fmaxf(pk, fabsf(x[i]));
  return pk;
}

constexpr int kBlocks = 20;

// Renders kBlocks blocks of a preview note (through insert ins), the taps appended to l / r.
static void previewNote(float* l, float* r, uint8_t ins = kTracks) {
  s->setPreviewTap(tapL, tapR);
  send(0, kPreviewTrack, 0xF7, ins, 0, 2);
  noteOn(0, kPreviewTrack, 60);
  for (int k = 0; k < kBlocks; ++k) {
    s->render(buf);
    memcpy(l + k * Synth::kBlock, tapL, sizeof tapL);
    memcpy(r + k * Synth::kBlock, tapR, sizeof tapR);
  }
}

static float rl[kBlocks * Synth::kBlock], rr[kBlocks * Synth::kBlock];
static float ql[kBlocks * Synth::kBlock], qr[kBlocks * Synth::kBlock];

// Tap off (never set) and tap on: the same output, bit for bit.
void test_tap_does_not_change_output() {
  int16_t a[kBlocks][Synth::kBlock], b[kBlocks][Synth::kBlock];
  p->instruments[0].send = 60;
  p->instruments[0].rsend = 60;
  noteOn(0, kPreviewTrack, 60);
  noteOn(0, 2, 64);
  for (auto& x : a) s->render(x);
  delete s;
  s = new Synth(*p);
  s->setPreviewTap(tapL, tapR);
  noteOn(0, kPreviewTrack, 60);
  noteOn(0, 2, 64);
  for (auto& x : b) s->render(x);
  TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof a);
}

void test_preview_note_fills_tap() {
  previewNote(rl, rr);
  TEST_ASSERT_TRUE(peak(rl + 5 * Synth::kBlock) > 0.01f);
  TEST_ASSERT_EQUAL_FLOAT_ARRAY(rl, rr, kBlocks * Synth::kBlock);  // centre: L == R
}

// Only the preview strip: a pattern track's note leaves the tap at 0 (cleared every block).
void test_pattern_track_not_tapped() {
  for (auto& x : tapL) x = 9.f;
  for (auto& x : tapR) x = 9.f;
  s->setPreviewTap(tapL, tapR);
  noteOn(0, 0, 60);
  for (int k = 0; k < 4; ++k) {
    s->render(buf);
    TEST_ASSERT_EQUAL_FLOAT(0.f, peak(tapL));
    TEST_ASSERT_EQUAL_FLOAT(0.f, peak(tapR));
  }
  TEST_ASSERT_TRUE(buf[10] != 0 || buf[50] != 0 || buf[100] != 0);
}

// Off again: the buffers are no longer written.
void test_tap_off_leaves_buffers() {
  s->setPreviewTap(tapL, tapR);
  s->setPreviewTap(nullptr, nullptr);
  for (auto& x : tapL) x = 9.f;
  noteOn(0, kPreviewTrack, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL_FLOAT(9.f, tapL[50]);
}

// The master (volume, comp, EQ) and the sends do not reach the tap.
void test_tap_independent_of_master_and_sends() {
  previewNote(rl, rr);
  delete s;
  p->masterVol = 30;
  p->compAmt = 100;
  p->eqLow = 120;
  p->instruments[0].send = 100;
  p->instruments[0].rsend = 100;
  s = new Synth(*p);
  previewNote(ql, qr);
  for (int i = 0; i < kBlocks * Synth::kBlock; ++i) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, rl[i], ql[i]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, rr[i], qr[i]);
  }
}

// Through a track's insert (0xF7): the insert is in the tap.
void test_tap_includes_insert() {
  previewNote(rl, rr);
  delete s;
  p->tracks[3].drive = 120;
  p->tracks[3].crushBits = 100;
  s = new Synth(*p);
  previewNote(ql, qr, 3);
  float d = 0;
  for (int i = 0; i < kBlocks * Synth::kBlock; ++i) d = fmaxf(d, fabsf(rl[i] - ql[i]));
  TEST_ASSERT_TRUE(d > 0.01f);
}

// Units: the mix before the master gain. With no sends / fx, MAIN 100 %: mono out =
// softClip(tap x 0.25) x 32767 (truncated). kPreviewVol is in the tap (fader), the pan (centre: 1).
void test_tap_level_vs_mix() {
  s->setPreviewTap(tapL, tapR);
  noteOn(0, kPreviewTrack, 60);
  float pk = 0;
  for (int k = 0; k < kBlocks; ++k) {
    s->render(buf);
    for (int i = 0; i < Synth::kBlock; ++i) {
      const float x = tapL[i] * 0.25f;
      const float y = x * (1.5f - 0.5f * x * x) * 32767.f;
      TEST_ASSERT_FLOAT_WITHIN(1.01f, y, static_cast<float>(buf[i]));
      pk = fmaxf(pk, fabsf(tapL[i]));
    }
  }
  TEST_ASSERT_TRUE(pk > 0.05f && pk < 4.f);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tap_does_not_change_output);
  RUN_TEST(test_preview_note_fills_tap);
  RUN_TEST(test_pattern_track_not_tapped);
  RUN_TEST(test_tap_off_leaves_buffers);
  RUN_TEST(test_tap_independent_of_master_and_sends);
  RUN_TEST(test_tap_includes_insert);
  RUN_TEST(test_tap_level_vs_mix);
  return UNITY_END();
}
