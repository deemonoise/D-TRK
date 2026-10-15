// Feature 5 integration (master / send fx inside Synth): MOD bus, delay modes, shimmer, master EQ.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "synth.h"
#include "synth_shimmer.h"

using namespace mt;

static Project* p;
static Synth* s;
static int16_t modA[ModFx::kLineLen], modB[ModFx::kLineLen];
static float wetA[Synth::kBlock], wetB[Synth::kBlock];  // the shimmer feedback blocks  // the MOD lines of s / r
static int16_t lr[2 * Synth::kBlock];
static int16_t dline[64000];
static float rvbBuf[Reverb::kBufLen];

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
    m.vol = 100;
    m.attack = 0;
    m.decay = 0;
    m.sustain = 127;
    m.release = 20;
  }
  s = new Synth(*p);
  s->setModBuffer(modA, ModFx::kLineLen);
}
void tearDown() {
  delete s;
  delete p;
}

static void send(Synth& x, int off, uint8_t track, uint8_t a, uint8_t b = 0, uint8_t c = 0, uint8_t len = 3) {
  const uint8_t m[3] = {a, b, c};
  TEST_ASSERT_TRUE(x.event(off, track, m, len));
}

// FNV-1a over the stereo output.
static uint32_t fnv(uint32_t h, const int16_t* d, int n) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(d);
  for (int i = 0; i < 2 * n; ++i) h = (h ^ b[i]) * 16777619u;
  return h;
}

// A mix with every master stage before feature 5 busy: 4 tracks panned, delay and reverb sends,
// compressor with a sidechain key, notes on and off. The hash of 300 blocks.
static uint32_t referenceScene(Synth& x) {
  x.setDelayBuffer(dline, 64000);
  x.setReverbBuffer(rvbBuf, Reverb::kBufLen);
  p->compAmt = 60;
  p->scTrack = 4;
  p->tracks[1].pan = 20;
  p->tracks[2].pan = 110;
  p->instruments[0].send = 70;
  p->instruments[1].rsend = 100;
  p->instruments[2].send = 40;
  p->instruments[2].rsend = 40;
  p->instruments[3].wave = static_cast<uint8_t>(Wave::Pulse);
  uint32_t h = 2166136261u;
  const uint8_t notes[4] = {48, 60, 67, 36};
  for (int k = 0; k < 300; ++k) {
    if (k % 25 == 0)
      for (int t = 0; t < 4; ++t) send(x, (t * 17) % Synth::kBlock, static_cast<uint8_t>(t), 0x90, notes[t] + (k / 25) % 3, 100);
    if (k % 25 == 12)
      for (int t = 0; t < 4; ++t) send(x, 5, static_cast<uint8_t>(t), 0x80, notes[t] + (k / 25) % 3, 0);
    x.renderStereo(lr);
    h = fnv(h, lr, Synth::kBlock);
  }
  return h;
}

// Measured on the firmware before feature 5's integration (2026-10-14).
static constexpr uint32_t kRefHash = 0x93E77752;

void test_default_render_bit_exact_to_before() {
  const uint32_t h = referenceScene(*s);
  printf("reference hash 0x%08X\n", h);
  TEST_ASSERT_EQUAL_HEX32(kRefHash, h);
}

// ---- A / B renders: the same events into two synths, compared ----

static Synth* r;  // the reference synth (its own buffers)
static int16_t dlineR[64000];
static float rvbBufR[Reverb::kBufLen];
static int16_t shimA[PitchShift::kLen], shimB[PitchShift::kLen];
static int16_t la[2 * Synth::kBlock], lb[2 * Synth::kBlock];

static void both(int off, uint8_t track, uint8_t a, uint8_t b = 0, uint8_t c = 0) {
  send(*s, off, track, a, b, c);
  send(*r, off, track, a, b, c);
}

// Renders n blocks of both; returns the summed |a - b| and, through lrDiff, the summed |L - R| of s.
static double renderAB(int n, double* lrDiff = nullptr) {
  double d = 0, w = 0;
  for (int k = 0; k < n; ++k) {
    s->renderStereo(la);
    r->renderStereo(lb);
    for (int i = 0; i < 2 * Synth::kBlock; ++i) d += abs(la[i] - lb[i]);
    for (int i = 0; i < Synth::kBlock; ++i) w += abs(la[2 * i] - la[2 * i + 1]);
  }
  if (lrDiff) *lrDiff = w;
  return d;
}

static void makeRef() {
  r = new Synth(*p);
  r->setModBuffer(modB, ModFx::kLineLen);
  s->setDelayBuffer(dline, 64000);
  r->setDelayBuffer(dlineR, 64000);
  s->setReverbBuffer(rvbBuf, Reverb::kBufLen);
  r->setReverbBuffer(rvbBufR, Reverb::kBufLen);
}

// MOD bus: a track's modSend feeds the selected effect, its return is stereo. Type OFF or send 0:
// bit-exact to no send.
void test_mod_send_reaches_mod_bus() {
  p->modType = static_cast<uint8_t>(ModType::Chorus);
  p->tracks[0].modSend = 127;
  Project q = *p;  // r renders the same project but without the send
  q.tracks[0].modSend = 0;
  r = new Synth(q);
  r->setModBuffer(modB, ModFx::kLineLen);
  both(0, 0, 0x90, 60, 100);
  double lrd = 0;
  TEST_ASSERT_TRUE(renderAB(40, &lrd) > 1000);
  TEST_ASSERT_TRUE(lrd > 1000);  // a centred track: only the stereo return makes L != R
  delete r;
}

void test_mod_off_or_zero_send_bit_exact() {
  p->tracks[0].modSend = 127;  // type OFF
  Project q = *p;
  q.tracks[0].modSend = 0;
  r = new Synth(q);
  r->setModBuffer(modB, ModFx::kLineLen);
  both(0, 0, 0x90, 60, 100);
  double lrd = 0;
  TEST_ASSERT_TRUE(renderAB(40, &lrd) == 0);
  TEST_ASSERT_TRUE(lrd == 0);
  delete r;
}

// The MOD lock (Fx MOD) and the LFO target ModSend move the send per step / per voice.
void test_mod_lock_and_lfo_target() {
  p->modType = static_cast<uint8_t>(ModType::Ensemble);
  makeRef();
  send(*s, 0, 0, 0xF5, static_cast<uint8_t>(Fx::MOD), 127);
  both(0, 0, 0x90, 60, 100);
  double lrd = 0;
  TEST_ASSERT_TRUE(renderAB(40, &lrd) > 1000);
  TEST_ASSERT_TRUE(lrd > 1000);
  delete r;
  delete s;
  s = new Synth(*p);
  s->setModBuffer(modA, ModFx::kLineLen);
  p->instruments[1].lfoDest = static_cast<uint8_t>(LfoDest::ModSend);
  p->instruments[1].lfoDepth = 63;
  p->instruments[1].lfoRate = 100;
  p->instruments[1].lfoWave = static_cast<uint8_t>(LfoWave::Square);
  Project q = *p;
  q.instruments[1].lfoDepth = 0;
  r = new Synth(q);
  r->setModBuffer(modB, ModFx::kLineLen);
  both(0, 1, 0x90, 60, 100);  // track 1 plays instrument 1
  TEST_ASSERT_TRUE(renderAB(80, &lrd) > 1000);
  delete r;
}

// The envelope follower reaches ModSend too.
void test_follower_moves_mod_send() {
  p->modType = static_cast<uint8_t>(ModType::Flanger);
  p->tracks[0].folSrc = 1;
  p->tracks[0].folDest = static_cast<uint8_t>(LfoDest::ModSend);
  p->tracks[0].folAmt = 63;
  Project q = *p;
  q.tracks[0].folAmt = 0;
  r = new Synth(q);
  r->setModBuffer(modB, ModFx::kLineLen);
  both(0, 0, 0x90, 60, 100);
  TEST_ASSERT_TRUE(renderAB(60) > 1000);
  delete r;
}

// Delay modes: TAPE differs from DIGITAL; PING-PONG puts the first echo on the left only.
void test_delay_tape_mode() {
  p->instruments[0].send = 127;
  p->dlyLevel = 127;
  p->dlyMode = static_cast<uint8_t>(DlyMode::Tape);
  p->dlyChar = 127;
  Project q = *p;
  q.dlyMode = 0;
  r = new Synth(q);
  r->setModBuffer(modB, ModFx::kLineLen);
  s->setDelayBuffer(dline, 64000);
  r->setDelayBuffer(dlineR, 64000);
  both(0, 0, 0x90, 60, 100);
  renderAB(5);
  both(0, 0, 0x80, 60, 0);
  TEST_ASSERT_TRUE(renderAB(200) > 1000);
  delete r;
}

void test_delay_ping_pong_mode() {
  p->instruments[0].send = 127;
  p->instruments[0].release = 0;
  p->dlyLevel = 127;
  p->dlyTime = 4;
  p->dlyMode = static_cast<uint8_t>(DlyMode::PingPong);
  p->dlyChar = 127;
  s->setDelayBuffer(dline, 64000);
  for (int k = 0; k < 20; ++k) s->renderStereo(lr);  // into PING-PONG: the line is wiped first (muted)
  send(*s, 0, 0, 0x90, 60, 100);
  s->renderStereo(lr);
  send(*s, 0, 0, 0x80, 60, 0);
  double l = 0, rr = 0;
  // 4 sixteenths at 120 BPM = 0.5 s = 125 blocks: the first echo, left only.
  for (int k = 1; k < 160; ++k) {
    s->renderStereo(lr);
    if (k > 20)
      for (int i = 0; i < Synth::kBlock; ++i) {
        l += abs(lr[2 * i]);
        rr += abs(lr[2 * i + 1]);
      }
  }
  TEST_ASSERT_TRUE(l > 1000);
  TEST_ASSERT_TRUE(rr < l * 0.01);
}

// Shimmer: with its buffer it adds an octave climb to the tail; without the buffer, or amount 0, it
// is the plain reverb bit for bit.
static double shimmerDiff(bool buffer, uint8_t amount) {
  p->instruments[0].rsend = 127;
  p->rvbLevel = 127;
  p->rvbSize = 110;
  p->rvbMode = static_cast<uint8_t>(RvbMode::Shimmer);
  p->rvbShim = amount;
  Project q = *p;
  q.rvbMode = 0;
  r = new Synth(q);
  r->setModBuffer(modB, ModFx::kLineLen);
  s->setReverbBuffer(rvbBuf, Reverb::kBufLen);
  r->setReverbBuffer(rvbBufR, Reverb::kBufLen);
  if (buffer) s->setShimmerBuffer(shimA, PitchShift::kLen, wetA);
  r->setShimmerBuffer(shimB, PitchShift::kLen, wetB);
  both(0, 0, 0x90, 60, 100);
  double d = renderAB(10);
  both(0, 0, 0x80, 60, 0);
  d += renderAB(200);
  delete r;
  return d;
}

void test_shimmer_mode() { TEST_ASSERT_TRUE(shimmerDiff(true, 127) > 1000); }
void test_shimmer_no_buffer_is_plain() { TEST_ASSERT_TRUE(shimmerDiff(false, 127) == 0); }
void test_shimmer_amount_zero_is_plain() { TEST_ASSERT_TRUE(shimmerDiff(true, 0) == 0); }

// Master EQ: a low boost changes the mix.
void test_master_eq() {
  p->eqLow = 127;
  Project q = *p;
  q.eqLow = 64;
  r = new Synth(q);
  r->setModBuffer(modB, ModFx::kLineLen);
  both(0, 0, 0x90, 40, 100);
  TEST_ASSERT_TRUE(renderAB(20) > 1000);
  delete r;
}

// PERF MOD THROW: the whole mix into the MOD bus while held, sounding at modLevel 0 (return floor).
void test_perf_mod_throw() {
  p->modType = static_cast<uint8_t>(ModType::Chorus);
  p->modLevel = 0;
  makeRef();
  both(0, 0, 0x90, 60, 100);
  send(*s, 0, kPreviewTrack, 0xF6, 0, static_cast<uint8_t>(PerfFx::ModThrow));
  double lrd = 0;
  TEST_ASSERT_TRUE(renderAB(40, &lrd) > 1000);
  TEST_ASSERT_TRUE(lrd > 1000);
  delete r;
}

// PERF TAPE WOBBLE: the delay runs TAPE at full wear while held.
void test_perf_tape_wobble() {
  p->instruments[0].send = 127;
  p->dlyLevel = 127;
  makeRef();
  both(0, 0, 0x90, 60, 100);
  send(*s, 0, kPreviewTrack, 0xF6, 0, static_cast<uint8_t>(PerfFx::TapeWobble));
  renderAB(5);
  both(0, 0, 0x80, 60, 0);
  TEST_ASSERT_TRUE(renderAB(200) > 1000);
  delete r;
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_render_bit_exact_to_before);
  RUN_TEST(test_mod_send_reaches_mod_bus);
  RUN_TEST(test_mod_off_or_zero_send_bit_exact);
  RUN_TEST(test_mod_lock_and_lfo_target);
  RUN_TEST(test_follower_moves_mod_send);
  RUN_TEST(test_delay_tape_mode);
  RUN_TEST(test_delay_ping_pong_mode);
  RUN_TEST(test_shimmer_mode);
  RUN_TEST(test_shimmer_no_buffer_is_plain);
  RUN_TEST(test_shimmer_amount_zero_is_plain);
  RUN_TEST(test_master_eq);
  RUN_TEST(test_perf_mod_throw);
  RUN_TEST(test_perf_tape_wobble);
  return UNITY_END();
}
