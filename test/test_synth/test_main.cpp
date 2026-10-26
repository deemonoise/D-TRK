#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "bytebeat_lib.h"
#include "scale.h"
#include "scene.h"
#include "slices.h"
#include "synth.h"
#include "synth_drum_machines.h"
#include "scene.h"
#include "tuning_factory.h"
#include "wt_builtin.h"

using namespace mt;

static Project* p;
static Synth* s;
struct ArrayBank;
static ArrayBank* ab = nullptr;
static void freeBank();
static int16_t buf[Synth::kBlock];
static int16_t bufR[Synth::kBlock];  // R of the last render1()

// Renders one block: L into out, R into bufR.
static void render1(int16_t* out) { s->render(out, bufR); }

void setUp() {
  p = new Project();
  p->masterVol = 100;
  for (int t = 0; t < kTracks; ++t) {
    p->tracks[t].out = TrackOut::Int;
    p->tracks[t].vol = 127;
  }
  for (auto& m : p->instruments) {
    instrSetType(m, InstrType::Chip);  // new projects default to FM
    m.wave = static_cast<uint8_t>(Wave::Saw);
    m.vol = 127;
    m.attack = 0;
    m.decay = 0;
    m.sustain = 127;
    m.release = 0;
  }
  s = new Synth(*p);
}
void tearDown() {
  freeBank();
  delete s;
  delete p;
}

static void send(int off, uint8_t track, uint8_t a, uint8_t b = 0, uint8_t c = 0, uint8_t len = 3) {
  const uint8_t m[3] = {a, b, c};
  TEST_ASSERT_TRUE(s->event(off, track, m, len));
}
static void noteOn(int off, uint8_t track, uint8_t note, uint8_t vel = 127) { send(off, track, 0x90, note, vel); }
static void noteOff(int off, uint8_t track, uint8_t note) { send(off, track, 0x80, note, 0); }

static bool silentBlocks(int n) {
  for (int k = 0; k < n; ++k) {
    render1(buf);
    for (int i = 0; i < Synth::kBlock; ++i)
      if (buf[i] != 0) return false;
  }
  return true;
}

// Falling zero crossings over n blocks.
static int crossings(int n) {
  int c = 0;
  int16_t prev = 0;
  bool first = true;
  for (int k = 0; k < n; ++k) {
    render1(buf);
    for (int i = 0; i < Synth::kBlock; ++i) {
      if (!first && prev >= 0 && buf[i] < 0) ++c;
      prev = buf[i];
      first = false;
    }
  }
  return c;
}

constexpr int kBlocksPerSec = kSynthRate / Synth::kBlock;  // 344
// Blocks covering ms (rounded up); time of n samples in us (rounded).
constexpr int blocksMs(int ms) { return (ms * kSynthRate / 1000 + Synth::kBlock - 1) / Synth::kBlock; }
constexpr uint64_t usOf(int n) { return (n * 1000000ull + kSynthRate / 2) / kSynthRate; }

void test_event_offset() {
  const uint64_t bt = 1000000;
  TEST_ASSERT_EQUAL(0, eventOffset(bt, bt));
  TEST_ASSERT_EQUAL(10, eventOffset(bt + usOf(10), bt));
  TEST_ASSERT_EQUAL(0, eventOffset(bt - 100, bt));  // late: as soon as possible
  TEST_ASSERT_EQUAL(Synth::kBlock - 1, eventOffset(bt + usOf(Synth::kBlock - 1), bt));
  TEST_ASSERT_EQUAL(-1, eventOffset(bt + usOf(Synth::kBlock), bt));  // next block
  TEST_ASSERT_EQUAL(-1, eventOffset(bt + 100000, bt));
}

void test_silent_without_events() {
  TEST_ASSERT_TRUE(silentBlocks(3));
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

// ---- stereo, pan ----

static int peakOf(const int16_t* x) {
  int pk = 0;
  for (int i = 0; i < Synth::kBlock; ++i) pk = abs(x[i]) > pk ? abs(x[i]) : pk;
  return pk;
}

void test_centred_track_l_equals_r() {
  noteOn(0, 0, 60);
  noteOn(0, 3, 67);
  for (int k = 0; k < 8; ++k) {
    render1(buf);
    TEST_ASSERT_EQUAL_INT16_ARRAY(buf, bufR, Synth::kBlock);
  }
  TEST_ASSERT_TRUE(peakOf(buf) > 100);
}

void test_track_peaks() {
  float pk[kTracks];
  s->setMeters(true);
  s->takeTrackPeaks(pk);
  noteOn(0, 2, 60);
  render1(buf);
  s->takeTrackPeaks(pk);
  for (int t = 0; t < kTracks; ++t) {
    if (t == 2) TEST_ASSERT_TRUE(pk[t] > 0.01f && pk[t] <= 1.f);
    else TEST_ASSERT_EQUAL_FLOAT(0.f, pk[t]);
  }
  s->takeTrackPeaks(pk);  // cleared by the read
  TEST_ASSERT_EQUAL_FLOAT(0.f, pk[2]);
  s->setMeters(false);  // off: no peaks for a voice without sends
  render1(buf);
  s->takeTrackPeaks(pk);
  TEST_ASSERT_EQUAL_FLOAT(0.f, pk[2]);
}

// Profiler: a setProfiler while a block renders (another task) takes effect at the next block's
// start; the block in flight keeps its clock, so no stage gets a stale-mark delta.
static uint32_t profA = 0, profB = 0;
static int profACalls = 0;
static uint32_t clockB() { return profB += 1; }
static uint32_t clockA() {
  if (++profACalls == 3) s->setProfiler(clockB);  // re-armed mid-block
  return profA += 1;
}

void test_profiler_arms_at_block_start() {
  noteOn(0, 2, 60);
  s->render(buf);
  profB = 1000000000u;
  s->setProfiler(clockA);
  TEST_ASSERT_TRUE(s->profilerArmed());
  TEST_ASSERT_FALSE(s->profiling());  // not before the next block
  uint32_t c[Synth::kProfStages], n[Synth::kProfStages], blocks = 0;
  s->render(buf);  // clockA all through, though clockB was asked for inside it
  s->takeProfile(c, n, blocks);
  TEST_ASSERT_EQUAL_UINT32(1, blocks);
  uint32_t sum = 0;
  for (int i = 0; i < Synth::kProfStages; ++i) sum += c[i];
  TEST_ASSERT_TRUE(sum > 0 && sum <= static_cast<uint32_t>(profACalls));
  const int aCalls = profACalls;
  s->render(buf);  // clockB from this block's start: its sums start from 0
  TEST_ASSERT_EQUAL(aCalls, profACalls);
  s->takeProfile(c, n, blocks);
  TEST_ASSERT_EQUAL_UINT32(1, blocks);
  sum = 0;
  for (int i = 0; i < Synth::kProfStages; ++i) sum += c[i];
  TEST_ASSERT_TRUE(sum > 0 && sum < 10000u);
  s->setProfiler(nullptr);
  TEST_ASSERT_TRUE(s->profiling());  // still on until the next block
  s->render(buf);
  TEST_ASSERT_FALSE(s->profiling());
  s->takeProfile(c, n, blocks);
  TEST_ASSERT_EQUAL_UINT32(0, blocks);
}

void test_note_on_lands_on_its_sample() {
  noteOn(64, 0, 60);
  render1(buf);
  for (int i = 0; i < 64; ++i) TEST_ASSERT_EQUAL(0, buf[i]);
  TEST_ASSERT_NOT_EQUAL(0, buf[64]);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_events_sorted_by_offset() {
  noteOn(100, 0, 60);
  noteOff(110, 0, 60);
  noteOn(20, 1, 72);  // queued later, plays earlier
  render1(buf);
  for (int i = 0; i < 20; ++i) TEST_ASSERT_EQUAL(0, buf[i]);
  TEST_ASSERT_NOT_EQUAL(0, buf[20]);
}

void test_note_off_frees_voice_after_release() {
  p->instruments[0].release = 40;  // envTimeMs(40)
  const int relMs = envTimeMs(40);
  noteOn(0, 0, 60);
  render1(buf);
  noteOff(0, 0, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  const int blocks = relMs * kSynthRate / 1000 / Synth::kBlock + 2;
  for (int k = 0; k < blocks; ++k) render1(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
  TEST_ASSERT_TRUE(silentBlocks(1));
}

void test_velocity_zero_is_note_off() {
  noteOn(0, 0, 60);
  render1(buf);
  noteOn(0, 0, 60, 0);
  render1(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_track_volume_zero_is_silent() {
  p->tracks[2].vol = 0;
  noteOn(0, 2, 60);
  TEST_ASSERT_TRUE(silentBlocks(4));
}

void test_master_volume_zero_is_silent() {
  p->masterVol = 0;
  noteOn(0, 0, 60);
  TEST_ASSERT_TRUE(silentBlocks(4));
}

void test_instrument_volume_scales_output() {
  noteOn(0, 0, 60);
  render1(buf);
  int loud = 0;
  for (int i = 0; i < Synth::kBlock; ++i) loud = abs(buf[i]) > loud ? abs(buf[i]) : loud;
  delete s;
  s = new Synth(*p);
  p->instruments[0].vol = 32;
  noteOn(0, 0, 60);
  render1(buf);
  int quiet = 0;
  for (int i = 0; i < Synth::kBlock; ++i) quiet = abs(buf[i]) > quiet ? abs(buf[i]) : quiet;
  TEST_ASSERT_TRUE(quiet > 0);
  TEST_ASSERT_TRUE(quiet * 3 < loud);
}

// ---- CPU guard (Synth::setLoad) ----

// Every track holds 2 sustained CHIP notes: the pool is full.
static void fillVoices() {
  for (auto& m : p->instruments) {
    m.sustain = 127;
    m.release = 100;
    m.mono = false;
  }
  for (int t = 0; t < kTracks; ++t) {
    noteOn(0, static_cast<uint8_t>(t), static_cast<uint8_t>(40 + t));
    noteOn(0, static_cast<uint8_t>(t), static_cast<uint8_t>(70 + t));
  }
  render1(buf);
}
static int liveVoices() {
  int n = 0;
  for (int i = 0; i < kVoices; ++i) n += s->voice(i).on && !s->voice(i).stolen;
  return n;
}

void test_guard_sheds_one_voice_per_block_down_to_min() {
  fillVoices();
  const int full = liveVoices();
  TEST_ASSERT_EQUAL(kVoices < 2 * kTracks ? kVoices : 2 * kTracks, full);
  s->setLoad(0.5f);
  TEST_ASSERT_EQUAL(full, liveVoices());  // under the limit: nothing happens
  s->setLoad(1.2f);  // one block over budget: the DMA absorbs it
  TEST_ASSERT_EQUAL(full, liveVoices());
  s->setLoad(1.2f);  // the second in a row
  TEST_ASSERT_EQUAL(full - 1, liveVoices());
  for (int k = 0; k < 100; ++k) {
    s->setLoad(1.2f);
    render1(buf);
  }
  TEST_ASSERT_EQUAL(Synth::kMinVoices, liveVoices());
  TEST_ASSERT_TRUE(s->activeVoices() <= Synth::kMinVoices + 1);  // the faded ones are freed
}

void test_guard_sheds_releasing_first_then_oldest() {
  fillVoices();
  noteOff(0, 5, 75);  // a releasing voice
  render1(buf);
  s->setLoad(1.6f);
  for (int i = 0; i < kVoices; ++i) {
    const Voice& v = s->voice(i);
    if (v.stolen) TEST_ASSERT_TRUE(v.track == 5 && v.note == 75);
  }
  int oldest = -1;  // next: the oldest sounding one
  for (int i = 0; i < kVoices; ++i) {
    const Voice& v = s->voice(i);
    if (v.on && !v.stolen && (oldest < 0 || v.age < s->voice(oldest).age)) oldest = i;
  }
  s->setLoad(1.6f);
  TEST_ASSERT_TRUE(s->voice(oldest).stolen);
  int stolen = 0;
  for (int i = 0; i < kVoices; ++i) stolen += s->voice(i).stolen;
  TEST_ASSERT_EQUAL(2, stolen);
}

void test_guard_cap_limits_new_notes_and_recovers() {
  fillVoices();
  for (int k = 0; k < 6; ++k) s->setLoad(1.2f);
  const int cap = s->voiceCap();
  for (int k = 0; k < 4; ++k) render1(buf);  // the fades end
  for (int t = 0; t < kTracks; ++t) noteOn(0, static_cast<uint8_t>(t), static_cast<uint8_t>(90 + t));
  render1(buf);
  TEST_ASSERT_TRUE(s->activeVoices() <= cap);
  // Between the marks the cap holds (once the smoothed load has come down below the high mark).
  for (int k = 0; k < 20; ++k) s->setLoad(0.7f);
  const int held = s->voiceCap();
  TEST_ASSERT_TRUE(held <= cap);
  for (int k = 0; k < 200; ++k) s->setLoad(0.7f);
  TEST_ASSERT_EQUAL(held, s->voiceCap());
  // Light load: one voice back every kCapUpBlocks, up to the whole pool.
  for (int k = 0; k < Synth::kCapUpBlocks * (kVoices + 4); ++k) s->setLoad(0.2f);
  TEST_ASSERT_EQUAL(kVoices, s->voiceCap());
}

void test_guard_ignores_single_spikes() {
  fillVoices();
  const int full = liveVoices();
  for (int k = 0; k < 50; ++k) {
    s->setLoad(1.3f);  // a late block now and then, the average well under kLoadHigh
    s->setLoad(0.4f);
    s->setLoad(0.4f);
  }
  TEST_ASSERT_EQUAL(full, liveVoices());
  TEST_ASSERT_EQUAL(kVoices, s->voiceCap());
}

void test_guard_hard_spike_sheds_at_once() {
  fillVoices();
  const int full = liveVoices();
  s->setLoad(1.6f);  // over kLoadHard: no second block needed
  TEST_ASSERT_EQUAL(full - 1, liveVoices());
}

// ---- Voice pool by cost (setVoiceBudget) ----

static void poolInstr(Instrument& m) {
  m.sustain = 127;
  m.release = 100;
  m.mono = false;
}

void test_pool_budget_off_by_default() {
  TEST_ASSERT_EQUAL(kVoiceBudgetOff, s->voiceBudget());
  fillVoices();  // 32 CHIP notes on 16 tracks x2
  TEST_ASSERT_EQUAL(kVoices < 2 * kTracks ? kVoices : 2 * kTracks, liveVoices());
  for (int i = 0; i < kVoices; ++i)
    if (s->voice(i).on) TEST_ASSERT_EQUAL(kCostChip, s->voice(i).cost);
}

void test_pool_budget_steals_oldest_chip() {
  s->setVoiceBudget(500);  // four CHIP notes (119) fit, the fifth does not
  for (int t = 0; t < 5; ++t) {
    poolInstr(p->instruments[t]);
    p->tracks[t].instr = static_cast<uint8_t>(t);
  }
  for (int t = 0; t < 4; ++t) noteOn(0, static_cast<uint8_t>(t), 60);
  render1(buf);
  TEST_ASSERT_EQUAL(4, liveVoices());
  noteOn(0, 4, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(4, liveVoices());
  for (int i = 0; i < kVoices; ++i) {
    const Voice& v = s->voice(i);
    if (v.on && v.stolen) TEST_ASSERT_EQUAL(0, v.track);  // the oldest
    if (v.on && v.track == 4) TEST_ASSERT_FALSE(v.stolen);  // the new note plays
  }
}

// The note sequence of bench-max group 5 (plan A worst case): WT SYNTH fold + ring + LADDER on
// tracks 0..7 and a growing CHIP LADDER pool on tracks 8.., 16 .. 32 voices requested, the notes
// ended and started again each step. At every block, step changes included, the live voices stay
// within the budget (scaled to the native pool: kFmVoiceMax heavy voices here, not 16).
void test_pool_budget_holds_through_ladder_steps() {
  Instrument& w = p->instruments[1];
  instrSetType(w, InstrType::Synth);
  w.synOsc[0] = w.synOsc[1] = static_cast<uint8_t>(SynOsc::Wt);
  w.synFold = 100;
  w.synRing = 64;
  w.fltMode = static_cast<uint8_t>(FltMode::Ladder);
  Instrument& c = p->instruments[2];
  c.fltMode = static_cast<uint8_t>(FltMode::Ladder);
  w.sustain = c.sustain = 127;
  w.mono = c.mono = false;
  for (int t = 0; t < kTracks; ++t) p->tracks[t].instr = t < 8 ? 1 : 2;
  const int budget = kFmVoiceMax * (kCostSynWt + kCostFold + kCostRing + kCostLadder) + 3 * (kCostChip + kCostLadder) + 50;
  s->setVoiceBudget(budget);
  auto chord = [](int first, int voices, uint8_t on) {
    for (int i = 0; i < voices; ++i) {
      const int t = first + i / kPolyPerTrack, k = i % kPolyPerTrack;
      const uint8_t key = static_cast<uint8_t>(48 + (t % 4) * 3 + k * 5);
      if (on) noteOn(0, static_cast<uint8_t>(t), key, 100);
      else noteOff(0, static_cast<uint8_t>(t), key);
    }
  };
  int prev = 0, top = 0;
  for (int count = 0; count <= 16; count += 4) {
    if (count) {
      chord(0, 16, 0);
      chord(8, prev, 0);
      render1(buf);
    }
    chord(0, 16, 1);
    chord(8, count, 1);
    for (int b = 0; b < 40; ++b) {
      render1(buf);
      int used = 0;
      for (int i = 0; i < kVoices; ++i)
        if (s->voice(i).on && !s->voice(i).stolen) used += s->voice(i).cost;
      TEST_ASSERT_TRUE(used <= budget);
      if (used > top) top = used;
    }
    prev = count;
  }
  // The budget, not the pool, set the limit: no room left for one more WT voice.
  TEST_ASSERT_TRUE(top + kCostSynWt + kCostFold + kCostRing + kCostLadder > budget);
}

void test_pool_budget_wt_synth_ladder() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  poolInstr(m);
  m.synOsc[0] = static_cast<uint8_t>(SynOsc::Wt);
  m.fltMode = static_cast<uint8_t>(FltMode::Ladder);
  const int c = kCostSynWt + kCostLadder;
  s->setVoiceBudget(3 * c);  // three fit, the fourth steals one
  p->tracks[0].instr = 0;
  p->tracks[1].instr = 0;
  noteOn(0, 0, 60);
  noteOn(0, 1, 60);
  noteOn(0, 0, 64);
  render1(buf);
  TEST_ASSERT_EQUAL(3, liveVoices());
  for (int i = 0; i < kVoices; ++i)
    if (s->voice(i).on) TEST_ASSERT_EQUAL(c, s->voice(i).cost);
  noteOn(0, 1, 64);
  render1(buf);
  TEST_ASSERT_EQUAL(3, liveVoices());
}

void test_guard_panic_keeps_the_cap() {
  fillVoices();
  const int full = liveVoices();
  s->setLoad(1.2f);
  s->setLoad(1.2f);  // two in a row, the smoothed load still under kLoadHigh
  TEST_ASSERT_EQUAL(full - 1, liveVoices());
  TEST_ASSERT_EQUAL(kVoices, s->voiceCap());  // a new note still finds a free voice
}

void test_guard_smoothed_load_sheds_without_a_spike() {
  fillVoices();
  const int full = liveVoices();
  for (int k = 0; k < 20; ++k) s->setLoad(0.9f);  // never over 1.0, but above kLoadHigh
  TEST_ASSERT_TRUE(liveVoices() < full);
}

void test_guard_reset_restores_pool() {
  fillVoices();
  for (int k = 0; k < 6; ++k) s->setLoad(1.2f);
  TEST_ASSERT_TRUE(s->voiceCap() < kVoices);
  s->reset();
  TEST_ASSERT_EQUAL(kVoices, s->voiceCap());
}

void test_sixteen_voices_soft_clip() {
  for (auto& m : p->instruments) {
    m.wave = static_cast<uint8_t>(Wave::Pulse);
    m.duty = 50;
  }
  for (int t = 0; t < kTracks; ++t) {
    noteOn(0, static_cast<uint8_t>(t), static_cast<uint8_t>(40 + t * 3));
    noteOn(0, static_cast<uint8_t>(t), static_cast<uint8_t>(41 + t * 5));
  }
  int full = 0, total = 0, peak = 0;
  for (int k = 0; k < 50; ++k) {
    render1(buf);
    for (int i = 0; i < Synth::kBlock; ++i) {
      TEST_ASSERT_TRUE(buf[i] >= -32767 && buf[i] <= 32767);
      if (abs(buf[i]) == 32767) ++full;
      peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
      ++total;
    }
  }
  TEST_ASSERT_EQUAL(kVoices < 2 * kTracks ? kVoices : 2 * kTracks, s->activeVoices());
  TEST_ASSERT_TRUE(peak > 30000);
  TEST_ASSERT_TRUE(full < total);
}

void test_program_change_selects_instrument() {
  send(0, 3, 0xC0, 5, 0, 2);
  noteOn(0, 3, 60);
  render1(buf);
  const int v = s->trackVoice(3);
  TEST_ASSERT_TRUE(v >= 0);
  TEST_ASSERT_EQUAL(5, s->voiceInstr(v));
  send(0, 3, 0xC0, 100, 0, 2);  // out of range: last instrument
  noteOn(0, 3, 62);
  render1(buf);
  TEST_ASSERT_EQUAL(kInstruments - 1, s->voiceInstr(s->trackVoice(3)));
}

void test_default_instrument_from_track_cfg() {
  p->tracks[4].instr = 11;
  noteOn(0, 4, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(11, s->voiceInstr(s->trackVoice(4)));
}

void test_start_track_resets_program() {
  send(0, 3, 0xC0, 5, 0, 2);
  send(0, 3, 0xFE, 0, 0, 1);
  noteOn(0, 3, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(3, s->voiceInstr(s->trackVoice(3)));
}

void test_pitch_includes_transpose_and_fine() {
  p->instruments[0].transpose = -12;
  p->instruments[0].fine = 50;
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 48.5f, s->voice(s->trackVoice(0)).pitch);
}

void test_saw_frequency() {
  noteOn(0, 0, 69);
  TEST_ASSERT_INT_WITHIN(3, 440, crossings(kBlocksPerSec));
}

void test_pitch_bend_up_two_semitones() {
  send(0, 0, 0xE0, 0x7F, 0x7F);  // max: +2 semitones
  noteOn(0, 0, 69);
  const int c = crossings(kBlocksPerSec);
  TEST_ASSERT_INT_WITHIN(5, 494, c);  // 440 * 1.122
}

void test_pitch_bend_center_is_neutral() {
  send(0, 0, 0xE0, 0x00, 0x40);
  noteOn(0, 0, 69);
  TEST_ASSERT_INT_WITHIN(3, 440, crossings(kBlocksPerSec));
}

void test_all_off_releases_track() {
  noteOn(0, 0, 60);
  noteOn(0, 0, 64);
  noteOn(0, 1, 67);
  render1(buf);
  send(0, 0, 0xFF, 0, 0, 1);
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());  // release 0: track 0 voices are gone
  TEST_ASSERT_EQUAL(1, s->voice(s->trackVoice(1)).track);
}

void test_poly_limit_per_track() {
  for (int n = 0; n < 6; ++n) noteOn(0, 0, static_cast<uint8_t>(60 + n));
  render1(buf);
  TEST_ASSERT_EQUAL(kPolyPerTrack, s->activeVoices());
}

void test_mono_glide() {
  Instrument& m = p->instruments[0];
  m.mono = true;
  m.glide = 25;  // 100 ms
  noteOn(0, 0, 60);
  render1(buf);
  noteOn(0, 0, 72);
  for (int k = 0; k < blocksMs(48); ++k) render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  const float mid = s->voice(s->trackVoice(0)).pitch;
  TEST_ASSERT_TRUE(mid > 62.f && mid < 70.f);
  for (int k = 0; k < blocksMs(80); ++k) render1(buf);  // past 100 ms
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 72.f, s->voice(s->trackVoice(0)).pitch);
}

void test_mono_without_glide_jumps() {
  p->instruments[0].mono = true;
  noteOn(0, 0, 60);
  render1(buf);
  noteOn(0, 0, 72);
  noteOff(1, 0, 60);  // the old note's off must not cut the legato note
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 72.f, s->voice(s->trackVoice(0)).pitch);
  TEST_ASSERT_EQUAL(72, s->voice(s->trackVoice(0)).note);
}

void test_mono_glide_not_after_release() {
  Instrument& m = p->instruments[0];
  m.mono = true;
  m.glide = 25;     // 100 ms
  m.release = 100;  // the old voice is still releasing
  noteOn(0, 0, 60);
  render1(buf);
  noteOff(0, 0, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  noteOn(0, 0, 72);  // not overlapping: no glide
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 72.f, s->voice(s->trackVoice(0)).pitch);
}

void test_preview_track() {
  p->instruments[2].vol = 127;
  send(0, kPreviewTrack, 0xC0, 2, 0, 2);
  noteOn(0, kPreviewTrack, 60);
  render1(buf);
  TEST_ASSERT_NOT_EQUAL(0, buf[5]);
  TEST_ASSERT_EQUAL(2, s->voiceInstr(s->trackVoice(kPreviewTrack)));
  TEST_ASSERT_FALSE(s->event(0, kSynthTracks, reinterpret_cast<const uint8_t*>("\x90\x3C\x7F"), 3));
}

void test_queue_overflow_keeps_note_offs() {
  noteOn(0, 0, 60);
  render1(buf);
  for (int i = 0; i < Synth::kMaxEvents; ++i) send(10, 1, 0xE0, 0, 0x40);
  const uint8_t off[3] = {0x80, 60, 0};
  TEST_ASSERT_TRUE(s->event(5, 0, off, 3));  // evicts a control
  const uint8_t on[3] = {0x90, 61, 100};
  TEST_ASSERT_FALSE(s->event(5, 1, on, 3));  // full: dropped
  render1(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

struct FakeBank : SampleSource {
  mutable char asked[kSampleNameMax + 1] = {0};
  int16_t data[1000] = {0};
  bool find(const char* name, SampleData& o) const override {
    strncpy(asked, name, kSampleNameMax);
    if (strcmp(name, "kick") != 0) return false;
    o = SampleData();
    o.key = 1;
    o.frames = 1000;
    o.rate = 16000;
    o.whole = data;
    return true;
  }
};
void test_sample_without_bank_is_silent() {
  p->instruments[0].type = InstrType::Sample;
  strcpy(p->instruments[0].sample, "kick");
  noteOn(0, 0, 60);
  TEST_ASSERT_TRUE(silentBlocks(2));
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_sample_resolved_by_name() {
  FakeBank bank;
  s->setBank(&bank);
  p->instruments[0].type = InstrType::Sample;
  strcpy(p->instruments[0].sample, "kick");
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_EQUAL_STRING("kick", bank.asked);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_TRUE(v.sample);
  TEST_ASSERT_EQUAL_PTR(bank.data, v.smp);
  TEST_ASSERT_EQUAL(1000u, v.smpLen);
  TEST_ASSERT_EQUAL(16000u, v.smpRate);
  strcpy(p->instruments[0].sample, "snare");  // not in the bank
  noteOn(0, 0, 62);
  render1(buf);
  TEST_ASSERT_EQUAL_STRING("snare", bank.asked);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

static int blockPeak() {
  int peak = 0;
  for (int i = 0; i < Synth::kBlock; ++i) peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
  return peak;
}

// Chip waves sit at full scale while a peak-normalised sample is ~12 dB quieter on average:
// a sample at -12 dBFS (8192) must play as loud as a full-scale pulse.
void test_sample_gain_matches_chip() {
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Pulse);
  noteOn(0, 0, 60);
  render1(buf);
  render1(buf);
  const int chip = blockPeak();
  delete s;
  s = new Synth(*p);
  FakeBank bank;
  for (auto& x : bank.data) x = 8192;
  s->setBank(&bank);
  p->instruments[0].type = InstrType::Sample;
  strcpy(p->instruments[0].sample, "kick");
  noteOn(0, 0, 60);
  render1(buf);
  render1(buf);
  TEST_ASSERT_INT_WITHIN(chip / 50, chip, blockPeak());
}

// The LFSR is clocked 93x the note frequency: METAL (period 93) sounds at the played pitch.
void test_noise_clock_is_93x_note() {
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Metal);
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, noteHz(60) * 93.f / kSynthRate, s->voice(s->trackVoice(0)).inc);
  noteOff(0, 0, 60);
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Noise);
  noteOn(0, 0, 127);
  render1(buf);
  TEST_ASSERT_EQUAL_FLOAT(8.f, s->voice(s->trackVoice(0)).inc);  // capped: 8 LFSR steps per sample
}

// Volume above 100 % adds up to +6 dB (x2 before the soft clip).
void test_master_volume_200_is_louder() {
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Pulse);
  noteOn(0, 0, 60);
  render1(buf);
  render1(buf);
  const int at100 = blockPeak();
  p->masterVol = 200;
  render1(buf);
  const int at200 = blockPeak();
  TEST_ASSERT_TRUE(at200 > at100 * 18 / 10);
  TEST_ASSERT_TRUE(at200 < 32767);
}

// ---- synth fx (0xF5) ----

static void fx(int off, uint8_t track, Fx f, uint8_t v) { send(off, track, 0xF5, static_cast<uint8_t>(f), v); }
// Step start as the sequencer sends it: ticks per step, note flag.
static void stepStart(int off, uint8_t track, uint8_t tps, bool note) {
  send(off, track, 0xF5, kSynthStep, static_cast<uint8_t>(tps | (note ? 0x80 : 0)));
}
static float voiceHz(uint8_t track) { return s->voice(s->trackVoice(track)).inc * kSynthRate; }
static float voiceSemis(uint8_t track) { return 69.f + 12.f * log2f(voiceHz(track) / 440.f); }
static void renderMs(int ms) {
  for (int k = 0; k < blocksMs(ms); ++k) render1(buf);
}

void test_fx_sld_slides_to_next_note() {
  noteOn(0, 0, 60);
  render1(buf);
  noteOff(0, 0, 60);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::SLD, 25);  // 100 ms
  noteOn(0, 0, 72);
  renderMs(48);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  const float mid = voiceHz(0);
  TEST_ASSERT_TRUE(mid > noteHz(60) * 1.05f && mid < noteHz(72) * 0.95f);
  renderMs(64);  // 112 ms
  TEST_ASSERT_FLOAT_WITHIN(noteHz(72) * 0.01f, noteHz(72), voiceHz(0));
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  // The next note without SLD starts at once.
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(noteHz(60) * 0.01f, noteHz(60), voiceHz(0));
}

// SLD takes the held voice without allocVoice: the voice's cost follows the new note's instrument.
void test_pool_sld_held_voice_takes_new_cost() {
  p->tracks[0].instr = 0;
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(kCostChip, s->voice(0).cost);
  Instrument& w = p->instruments[1];
  instrSetType(w, InstrType::Synth);
  w.sustain = 127;
  w.synOsc[0] = static_cast<uint8_t>(SynOsc::Wt);
  w.fltMode = static_cast<uint8_t>(FltMode::Ladder);
  p->tracks[0].instr = 1;
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::SLD, 25);
  noteOn(0, 0, 72);
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_TRUE(s->voice(0).on);
  TEST_ASSERT_EQUAL(kCostSynWt + kCostLadder, s->voice(0).cost);
}

// SLD into an expensive note keeps the budget: the oldest other voice fades out.
void test_pool_sld_into_expensive_note_steals() {
  for (int t = 0; t < 3; ++t) {
    p->tracks[t].instr = 0;
    p->instruments[t].mono = false;
  }
  s->setVoiceBudget(kCostSynWt + kCostLadder + kCostChip + 6);  // three CHIP notes fit (357 <= 400)
  noteOn(0, 1, 50);  // the oldest
  noteOn(0, 2, 52);
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(3, liveVoices());
  Instrument& w = p->instruments[1];
  instrSetType(w, InstrType::Synth);
  w.sustain = 127;
  w.synOsc[0] = static_cast<uint8_t>(SynOsc::Wt);
  w.fltMode = static_cast<uint8_t>(FltMode::Ladder);
  p->tracks[0].instr = 1;
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::SLD, 25);
  noteOn(0, 0, 72);  // 275 + 2 x 119 > 400, 275 + 119 fits: only track 1's (oldest) voice goes
  render1(buf);
  TEST_ASSERT_EQUAL(2, liveVoices());
  for (int i = 0; i < kVoices; ++i) {
    const Voice& v = s->voice(i);
    if (!v.on) continue;
    if (v.track == 1) TEST_ASSERT_TRUE(v.stolen);
    else TEST_ASSERT_FALSE(v.stolen);
    if (v.track == 0) TEST_ASSERT_EQUAL(kCostSynWt + kCostLadder, v.cost);
  }
}

void test_fx_vib_depth_and_period() {
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::VIB, 0x8F);  // 4 Hz, +-2 semitones
  noteOn(0, 0, 60);
  float lo = 100, hi = 0, prev = 60;
  int firstUp = -1, lastUp = -1, ups = 0;
  for (int k = 0; k < kBlocksPerSec; ++k) {
    render1(buf);
    const float x = voiceSemis(0);
    lo = x < lo ? x : lo;
    hi = x > hi ? x : hi;
    if (k > 0 && prev < 60.f && x >= 60.f) {
      if (firstUp < 0) firstUp = k;
      lastUp = k;
      ++ups;
    }
    prev = x;
  }
  TEST_ASSERT_TRUE(hi <= 62.05f && hi >= 61.8f);
  TEST_ASSERT_TRUE(lo >= 57.95f && lo <= 58.2f);
  TEST_ASSERT_TRUE(ups >= 3);
  const float periodMs = (lastUp - firstUp) * (1000.f * Synth::kBlock / kSynthRate) / (ups - 1);
  TEST_ASSERT_FLOAT_WITHIN(250.f * 0.05f, 250.f, periodMs);
  // A new note without VIB ends it.
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  for (int k = 0; k < 40; ++k) {
    render1(buf);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.f, voiceSemis(0));
  }
}

void test_fx_arp_three_notes_per_step() {
  p->bpm = 125;  // tick 5000 us, 24 ticks = 120 ms step
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::ARP, 0x47);
  noteOn(0, 0, 60);
  renderMs(20);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 60.f, voiceSemis(0));
  renderMs(40);  // 60 ms
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 64.f, voiceSemis(0));
  renderMs(40);  // 100 ms
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 67.f, voiceSemis(0));
  renderMs(40);  // 140 ms: next round
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 60.f, voiceSemis(0));
}

void test_fx_vsl_fades_over_step() {
  p->bpm = 150;  // 24 ticks = 100 ms
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::VSL, static_cast<uint8_t>(-64));
  noteOn(0, 0, 60, 127);
  renderMs(52);
  TEST_ASSERT_FLOAT_WITHIN(0.06f, 0.5f, s->voice(s->trackVoice(0)).gain);
  renderMs(52);  // 104 ms
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, s->voice(s->trackVoice(0)).gain);
  TEST_ASSERT_TRUE(silentBlocks(1));
  // Kept until the next note-on.
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 62, 127);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.f, s->voice(s->trackVoice(0)).gain);
}

void test_fx_vsl_on_empty_step_fades_sounding_note() {
  p->bpm = 150;
  noteOn(0, 0, 60, 127);
  render1(buf);
  stepStart(0, 0, 24, false);
  fx(0, 0, Fx::VSL, static_cast<uint8_t>(-32));
  renderMs(120);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, s->voice(s->trackVoice(0)).gain);
}

void test_fx_cut_kills_after_ticks() {
  p->bpm = 120;  // tick 5208 us
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::CUT, 3);  // 15.6 ms
  noteOn(0, 0, 60);
  static int16_t all[8 * Synth::kBlock];
  for (int k = 0; k < 8; ++k) render1(all + k * Synth::kBlock);
  int last = -1;
  for (int i = 0; i < 8 * Synth::kBlock; ++i)
    if (all[i] != 0) last = i;
  const float ms = (last + 1) * 1000.f / kSynthRate;
  TEST_ASSERT_FLOAT_WITHIN(1.05f, 15.625f, ms);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_fx_ofs_stored_for_sample_note() {
  FakeBank bank;
  s->setBank(&bank);
  p->instruments[0].type = InstrType::Sample;
  strcpy(p->instruments[0].sample, "kick");
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::OFS, 128);
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(128, s->voice(s->trackVoice(0)).ofs);
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 62);
  render1(buf);
  TEST_ASSERT_EQUAL(0, s->voice(s->trackVoice(0)).ofs);
}

void test_start_track_clears_fx() {
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::VIB, 0x8F);
  noteOn(0, 0, 60);
  render1(buf);
  s->startTrack(0);
  for (int k = 0; k < 40; ++k) {
    render1(buf);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.f, voiceSemis(0));
  }
}

// ---- FM voice ----

static void useFm(int instr, FmMachine mc) {
  Instrument& m = p->instruments[instr];
  m.type = InstrType::Fm;
  fmSetMachine(m, static_cast<uint8_t>(mc));
}

static bool blockSilent() {
  render1(buf);
  for (int i = 0; i < Synth::kBlock; ++i)
    if (buf[i] != 0) return false;
  return true;
}

void test_fm_kick_sounds_and_ends_by_itself() {
  useFm(0, FmMachine::Kick);
  p->instruments[0].macro[kMacDec] = 20;  // ~14 ms
  noteOn(0, 0, 60);
  TEST_ASSERT_FALSE(blockSilent());
  for (int k = 0; k < 30; ++k) render1(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_fm_drum_ignores_note_off() {
  useFm(0, FmMachine::Kick);
  p->instruments[0].macro[kMacDec] = 110;
  noteOn(0, 0, 60);
  noteOff(10, 0, 60);
  for (int k = 0; k < 4; ++k) render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_FALSE(blockSilent());
}

void test_fm_drum_chokes_on_same_track() {
  useFm(0, FmMachine::Hat);
  p->instruments[0].mono = false;  // ignored: drums are mono
  noteOn(0, 0, 60);
  noteOn(50, 0, 64);
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_fm_tone_poly_and_gated() {
  useFm(0, FmMachine::Tone);
  p->instruments[0].mono = false;
  p->instruments[0].sustain = 127;
  noteOn(0, 0, 60);
  noteOn(0, 0, 64);
  noteOn(0, 0, 67);
  for (int k = 0; k < 50; ++k) render1(buf);
  TEST_ASSERT_EQUAL(3, s->activeVoices());  // held
  noteOff(0, 0, 60);
  noteOff(0, 0, 64);
  noteOff(0, 0, 67);
  render1(buf);
  render1(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());  // release 0
}

void test_fm_chord_is_one_voice() {
  useFm(0, FmMachine::Chord);
  p->instruments[0].mono = false;
  noteOn(0, 0, 60);
  noteOn(0, 0, 65);
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_fm_lock_on_note_step() {
  useFm(0, FmMachine::Tone);
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::COL, 99);
  noteOn(0, 0, 60);
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(1 << kMacCol, v.lockMask);
  TEST_ASSERT_EQUAL(99, v.lock[kMacCol]);
  stepStart(0, 0, 6, true);  // next step: no lock
  noteOn(0, 0, 62);
  render1(buf);
  TEST_ASSERT_EQUAL(0, s->voice(s->trackVoice(0)).lockMask);
}

void test_fm_lock_on_empty_step_hits_sounding_voice() {
  useFm(0, FmMachine::Tone);
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  render1(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::SHP, 127);
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(1 << kMacShp, v.lockMask);
  TEST_ASSERT_EQUAL(127, v.lock[kMacShp]);
}

void test_fm_lock_ignored_on_chip() {
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  render1(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::DCY, 1);
  render1(buf);
  TEST_ASSERT_EQUAL(0, s->voice(s->trackVoice(0)).lockMask);
}

void test_fm_lfo_on_pitch() {
  useFm(0, FmMachine::Tone);
  Instrument& m = p->instruments[0];
  m.macro[kMacCol] = 0;  // pure sine
  m.sustain = 127;
  noteOn(0, 0, 69);
  const int plain = crossings(250);
  s->reset();
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 60;
  m.lfoDepth = 63;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Pitch);
  noteOn(0, 0, 69);
  TEST_ASSERT_TRUE(crossings(250) > plain * 115 / 100);  // +12 / -12 st halves: ~1.25x
}

// Largest |difference| between neighbouring samples of a block, prev: last sample before it.
static int maxStep(const int16_t* a, int16_t prev) {
  int m = 0;
  for (int i = 0; i < Synth::kBlock; ++i) {
    const int d = abs(a[i] - prev);
    if (d > m) m = d;
    prev = a[i];
  }
  return m;
}

void test_fm_choke_mid_segment_has_no_jump() {
  useFm(0, FmMachine::Kick);
  Instrument& m = p->instruments[0];
  m.macro[kMacDec] = 100;  // ~1 s
  m.macro[kMacCol] = 0;    // pure sine, no sweep
  m.macro[kMacShp] = 0;
  m.macro[kMacSwp] = 0;
  noteOn(0, 0, 72);
  int peak = 0;
  for (int k = 0; k < 4; ++k) {
    render1(buf);
    peak = peakOf(buf) > peak ? peakOf(buf) : peak;
  }
  for (int k = 0; k < 60; ++k) render1(buf);  // decays to ~0.2
  const int16_t prev = buf[Synth::kBlock - 1];
  noteOn(50, 0, 72);  // choke inside a control segment
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_TRUE(peakOf(buf) > peak / 2);  // retriggered
  TEST_ASSERT_TRUE(maxStep(buf, prev) < peak / 8);
}

void test_fm_drum_all_off_fades() {
  useFm(0, FmMachine::Kick);
  p->instruments[0].macro[kMacDec] = 110;
  noteOn(0, 0, 60);
  for (int k = 0; k < 4; ++k) render1(buf);
  const int16_t prev = buf[Synth::kBlock - 1];
  send(0, 0, 0xFF, 0, 0, 1);  // all off (stop)
  render1(buf);
  TEST_ASSERT_TRUE(buf[0] != 0 || buf[1] != 0);  // a short fade, not a cut
  TEST_ASSERT_TRUE(maxStep(buf, prev) < 2000);
  render1(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

// PGM: instrument 1 on track 0.
static void pgm(int off, uint8_t instr) { send(off, 0, 0xC0, instr, 0, 2); }

void test_fm_legato_from_chip_starts_fm_voice() {
  Instrument& f = p->instruments[1];
  useFm(1, FmMachine::Tone);
  f.mono = true;
  f.lfoDepth = 20;
  f.lfoRate = 40;
  p->instruments[0].mono = true;
  pgm(0, 1);
  noteOn(0, 0, 60);
  for (int k = 0; k < 100; ++k) render1(buf);
  const float hz = lfoHz(f.lfoRate);
  const float fresh = hz * (Synth::kBlock - Synth::kControl) / kSynthRate;  // first update precedes the note
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).lfoPhase[0] > fresh * 4);
  pgm(0, 0);
  noteOn(0, 0, 62);  // CHIP legato on the same voice
  render1(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).fm);
  pgm(0, 1);
  noteOn(0, 0, 64);  // FM legato from a CHIP voice: a new FM note
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, fresh, s->voice(s->trackVoice(0)).lfoPhase[0]);
}

void test_fm_tone_after_drum_takes_its_own_env() {
  useFm(0, FmMachine::Kick);
  useFm(1, FmMachine::Tone);
  p->instruments[0].macro[kMacDec] = 127;
  Instrument& t = p->instruments[1];
  t.mono = true;
  t.sustain = 0;
  t.macro[kMacDec] = 0;  // 5 ms decay to nothing
  noteOn(0, 0, 60);
  render1(buf);
  pgm(0, 1);
  noteOn(0, 0, 60);  // legato into the sounding kick's voice
  for (int k = 0; k < 10; ++k) render1(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());  // TONE's env, not the drum gate
}

void test_fm_machine_switch_keeps_sounding_drum_one_shot() {
  useFm(0, FmMachine::Kick);
  p->instruments[0].macro[kMacDec] = 40;  // ~40 ms
  noteOn(0, 0, 60);
  render1(buf);
  fmSetMachine(p->instruments[0], static_cast<uint8_t>(FmMachine::Tone));  // UI edit while it sounds
  p->instruments[0].sustain = 127;
  for (int k = 0; k < 100; ++k) render1(buf);  // 400 ms
  TEST_ASSERT_EQUAL(0, s->activeVoices());  // no drone
}

void test_fm_lfo_random_starts_off_zero() {
  useFm(0, FmMachine::Tone);
  Instrument& m = p->instruments[0];
  m.macro[kMacCol] = 0;
  noteOn(0, 0, 69);
  const int plain = crossings(125);
  s->reset();
  m.lfoWave = static_cast<uint8_t>(LfoWave::Random);
  m.lfoRate = 0;  // 0.05 Hz: the first cycle covers the test
  m.lfoDepth = 63;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Pitch);
  noteOn(0, 0, 69);
  render1(buf);
  TEST_ASSERT_TRUE(fabsf(s->voice(s->trackVoice(0)).lfoRnd[0]) > 0.05f);
  const int c = crossings(124);
  TEST_ASSERT_TRUE(abs(c - plain) > plain / 30);
}

void test_fm_lfo_on_vol() {
  useFm(0, FmMachine::Tone);
  Instrument& m = p->instruments[0];
  m.macro[kMacCol] = 0;
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 46;  // ~0.5 Hz: first second loud, then quiet
  m.lfoDepth = 63;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Vol);
  noteOn(0, 0, 69);
  renderMs(200);
  const int loud = blockPeak();
  renderMs(1000);
  const int quiet = blockPeak();
  TEST_ASSERT_TRUE(loud > quiet * 4);
}

void test_fm_lfo_on_macro() {
  useFm(0, FmMachine::Tone);
  Instrument& m = p->instruments[0];
  m.macro[kMacCol] = 0;  // pure sine
  noteOn(0, 0, 69);
  static int16_t a[Synth::kBlock * 20];
  for (int k = 0; k < 20; ++k) render1(a + k * Synth::kBlock);
  s->reset();
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 30;
  m.lfoDepth = 63;  // COLOR 0 -> ~63 in the first half cycle
  m.lfoDest = static_cast<uint8_t>(LfoDest::Col);
  noteOn(0, 0, 69);
  long diff = 0, sum = 0;
  for (int k = 0; k < 20; ++k) {
    render1(buf);
    for (int i = 0; i < Synth::kBlock; ++i) {
      diff += abs(buf[i] - a[k * Synth::kBlock + i]);
      sum += abs(a[k * Synth::kBlock + i]);
    }
  }
  TEST_ASSERT_TRUE(diff > sum / 10);
}

// ---- filter, LFO on every type ----

// RMS over n blocks.
static float rmsBlocks(int n) {
  double acc = 0;
  for (int k = 0; k < n; ++k) {
    render1(buf);
    for (int i = 0; i < Synth::kBlock; ++i) acc += double(buf[i]) * buf[i];
  }
  return sqrtf(static_cast<float>(acc / (n * Synth::kBlock)));
}

void test_filter_off_is_bit_identical() {
  noteOn(0, 0, 72);
  int16_t ref[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) render1(ref + k * Synth::kBlock);
  s->reset();
  // Off ignores every filter field, and CHIP ignores an LFO on a macro.
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Off);
  m.cutoff = 0;
  m.reso = 127;
  m.fenv = -64;
  m.keytrack = 127;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Col);
  m.lfoRate = 127;
  m.lfoDepth = 63;
  noteOn(0, 0, 72);
  for (int k = 0; k < 4; ++k) {
    render1(buf);
    TEST_ASSERT_EQUAL_INT16_ARRAY(ref + k * Synth::kBlock, buf, Synth::kBlock);
  }
}

void test_lp_darkens_saw() {
  noteOn(0, 0, 84);
  render1(buf);
  const float open = rmsBlocks(8);
  s->reset();
  p->instruments[0].fltMode = static_cast<uint8_t>(FltMode::Lp);
  p->instruments[0].cutoff = 20;  // ~56 Hz, far below C6
  noteOn(0, 0, 84);
  render1(buf);
  TEST_ASSERT_TRUE(rmsBlocks(8) < open * 0.2f);
}

void test_filter_env_opens() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 20;
  m.fenv = 63;
  m.fAtk = 0;
  m.fDec = 70;  // ~160 ms
  noteOn(0, 0, 84);
  const float early = rmsBlocks(4);
  rmsBlocks(200);  // ~0.8 s
  const float late = rmsBlocks(4);
  TEST_ASSERT_TRUE(early > late * 3.f);
}

void test_max_reso_stays_finite() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Bp);
  m.reso = 127;
  m.cutoff = 127;
  m.fenv = 63;
  noteOn(0, 0, 100);
  for (int k = 0; k < 50; ++k) render1(buf);  // softClip keeps int16 in range; must not hang / NaN
  TEST_ASSERT_TRUE(s->activeVoices() == 1);
  VoiceFilter f = s->voice(s->trackVoice(0)).flt;  // a copy: its own state (BP: no line)
  float y = 0;
  f.processAdd(nullptr, &y, 1, nullptr);
  TEST_ASSERT_TRUE(isfinite(y));
}

void test_flt_lock_on_note_step() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 127;
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::FLT, 15);
  noteOn(0, 0, 84);
  render1(buf);
  const float locked = rmsBlocks(8);
  s->reset();
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 84);  // a new step drops the lock
  render1(buf);
  TEST_ASSERT_TRUE(locked < rmsBlocks(8) * 0.3f);
}

void test_flt_lock_without_note_hits_sounding_voice() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  noteOn(0, 0, 84);
  render1(buf);
  const float open = rmsBlocks(4);
  stepStart(0, 0, 24, false);
  fx(0, 0, Fx::FLT, 15);
  for (int k = 0; k < 8; ++k) render1(buf);  // the filter's state rings down from the open cutoff
  TEST_ASSERT_TRUE(rmsBlocks(4) < open * 0.3f);
}

// First block RMS after the filter turns on mid-note (LP at 20 Hz): ringing from an old state
// shows up, a fresh one passes next to nothing of C6.
static float rmsAfterFilterOn(bool ringFirst) {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(ringFirst ? FltMode::Bp : FltMode::Off);
  m.cutoff = 77;  // ~C6
  m.reso = 127;
  noteOn(0, 0, 84);
  for (int k = 0; k < 8; ++k) render1(buf);
  m.fltMode = static_cast<uint8_t>(FltMode::Off);
  for (int k = 0; k < 2; ++k) render1(buf);
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 0;
  m.reso = 0;
  return rmsBlocks(1);
}

void test_filter_turned_on_mid_note_starts_clean() {
  const float fresh = rmsAfterFilterOn(false);
  s->reset();
  TEST_ASSERT_FLOAT_WITHIN(fresh * 0.01f + 1.f, fresh, rmsAfterFilterOn(true));
}

void test_lfo_restarts_on_mono_retrigger_after_release() {
  Instrument& m = p->instruments[0];
  m.mono = true;
  m.release = 100;  // ~1.5 s: the voice is still releasing
  m.lfoDest = static_cast<uint8_t>(LfoDest::Pitch);
  m.lfoRate = 60;  // ~1 Hz
  m.lfoDepth = 10;
  noteOn(0, 0, 69);
  for (int k = 0; k < 50; ++k) render1(buf);  // ~0.2 cycle
  noteOff(0, 0, 69);
  render1(buf);
  noteOn(0, 0, 69);
  render1(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).lfoPhase[0] < 0.02f);
}

// Retrig OFF: one phase per instrument, running without notes; a note on another track joins it.
void test_lfo_retrig_off_shares_phase() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoRate = 60;  // ~1 Hz
  m.lfoDepth = 10;
  m.lfoSync = kLfoFree;
  p->tracks[1].instr = 0;
  for (int k = 0; k < 20; ++k) s->render(buf);  // no voice yet
  noteOn(0, 0, 60);
  for (int k = 0; k < 30; ++k) s->render(buf);
  noteOn(0, 1, 64);
  s->render(buf);
  const float inc = lfoHz(m.lfoRate) * Synth::kControl / kSynthRate;  // per control update
  const float want = 51 * (Synth::kBlock / Synth::kControl) * inc;
  const int a = s->trackVoice(0), b = s->trackVoice(1);
  TEST_ASSERT_TRUE(a >= 0 && b >= 0 && a != b);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, want, s->lfoPhase(a, 0));
  TEST_ASSERT_EQUAL_FLOAT(s->lfoPhase(a, 0), s->lfoPhase(b, 0));
}

// Retrig ON (the default): the later note starts its LFO at phase 0.
void test_lfo_retrig_on_restarts_per_note() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoRate = 60;
  m.lfoDepth = 10;
  p->tracks[1].instr = 0;
  noteOn(0, 0, 60);
  for (int k = 0; k < 40; ++k) s->render(buf);
  noteOn(0, 1, 64);
  s->render(buf);
  const float inc = lfoHz(m.lfoRate) * Synth::kControl / kSynthRate;
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3 * inc, s->lfoPhase(s->trackVoice(1), 0));  // updates after the note
  TEST_ASSERT_TRUE(s->lfoPhase(s->trackVoice(0), 0) > 0.1f);
}

// Transport start (0xFA): TEMPO + Retrig OFF back to phase 0, FREE + Retrig OFF keeps running.
void test_lfo_transport_start_resets_tempo_retrig_off() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoRate = 8;  // 1 bar
  m.lfoDepth = 10;
  m.lfoSync = kLfoTempo | kLfoFree;
  m.lfo[0] = {0, 60, 10, static_cast<uint8_t>(LfoDest::Vol), kLfoFree};
  noteOn(0, 0, 60);
  for (int k = 0; k < 40; ++k) s->render(buf);
  const int v = s->trackVoice(0);
  const float freeBefore = s->lfoPhase(v, 1);
  TEST_ASSERT_TRUE(s->lfoPhase(v, 0) > 0.05f);
  send(0, 3, 0xFA, 0, 0, 1);  // any track
  s->render(buf);
  const float tempoInc = lfoSyncHz(m.lfoRate, p->bpm) * Synth::kControl / kSynthRate;
  const float freeInc = lfoHz(m.lfo[0].rate) * Synth::kControl / kSynthRate;
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3 * tempoInc, s->lfoPhase(v, 0));  // updates after the event
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, freeBefore + 4 * freeInc, s->lfoPhase(v, 1));
}

// Transport pause (0xFC) holds TEMPO + Retrig OFF on the bar grid, FREE + Retrig OFF runs on;
// resume (0xFB) moves it on from where it stopped.
void test_lfo_transport_pause_holds_tempo_retrig_off() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoRate = 8;  // 1 bar
  m.lfoDepth = 10;
  m.lfoSync = kLfoTempo | kLfoFree;
  m.lfo[0] = {0, 60, 10, static_cast<uint8_t>(LfoDest::Vol), kLfoFree};
  noteOn(0, 0, 60);
  send(0, 0, 0xFA, 0, 0, 1);
  s->render(buf);
  send(0, 0, 0xFC, 0, 0, 1);
  s->render(buf);
  const int v = s->trackVoice(0);
  const float tempo = s->lfoPhase(v, 0), freeP = s->lfoPhase(v, 1);
  for (int k = 0; k < 20; ++k) s->render(buf);
  TEST_ASSERT_EQUAL_FLOAT(tempo, s->lfoPhase(v, 0));
  TEST_ASSERT_TRUE(s->lfoPhase(v, 1) > freeP + 0.05f);
  send(0, 0, 0xFB, 0, 0, 1);
  s->render(buf);
  const float tempoInc = lfoSyncHz(m.lfoRate, p->bpm) * Synth::kControl / kSynthRate;
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, tempo + 3 * tempoInc, s->lfoPhase(v, 0));  // updates after the event
}

// reset() (audio park during flash writes) leaves the shared LFO phases where they are.
void test_lfo_reset_keeps_retrig_off_phase() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoRate = 60;
  m.lfoDepth = 10;
  m.lfoSync = kLfoFree;
  for (int k = 0; k < 30; ++k) s->render(buf);
  s->reset();
  noteOn(0, 0, 60);
  s->render(buf);
  const float inc = lfoHz(m.lfoRate) * Synth::kControl / kSynthRate;
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 31 * 4 * inc, s->lfoPhase(s->trackVoice(0), 0));
}

// LFO -> LFO uses the previous control update's outputs (1 ms lag). L1 RATE from LFO 2 (square,
// depth 63): LFO 1 runs 2^(4 x 63/64) ~ 15x faster while LFO 2 is high.
void test_lfo_rate_mod() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoDepth = 10;
  m.lfoRate = 0;  // 0.05 Hz
  m.lfo[0] = {static_cast<uint8_t>(LfoWave::Square), 0, 63, static_cast<uint8_t>(LfoDest::Rate1), 0};
  noteOn(0, 0, 60);
  for (int k = 0; k < 10; ++k) s->render(buf);
  const float inc = lfoHz(0) * Synth::kControl / kSynthRate;
  // The note-on update sets LFO 2's output (+63/64), every later one steps LFO 1 sped up.
  const int steps = 10 * Synth::kBlock / Synth::kControl - 1;
  TEST_ASSERT_FLOAT_WITHIN(inc, steps * inc * exp2f(4.f * 63.f / 64.f), s->lfoPhase(s->trackVoice(0), 0));
}

// L1 DEPTH from LFO 2 (square, held high): -64 silences LFO 1, +63 about doubles it.
static float depthModSend(int8_t depth2) {
  Instrument& m = p->instruments[0];
  m.send = 64;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Dly);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 0;
  m.lfoDepth = 16;  // +16 send units
  m.lfo[0] = {static_cast<uint8_t>(LfoWave::Square), 0, depth2, static_cast<uint8_t>(LfoDest::Depth1), 0};
  noteOn(0, 0, 60);
  for (int k = 0; k < 4; ++k) s->render(buf);
  return s->voice(s->trackVoice(0)).send * 127.f;
}

void test_lfo_depth_mod() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 64.f, depthModSend(-64));
  tearDown();
  setUp();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 96.f, depthModSend(63));  // 64 + 16 x (1 + 63/64), rounded
  tearDown();
  setUp();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.f, depthModSend(0));  // depth 0 source: off
}

// L1 RTRG: every cycle of LFO 2 restarts LFO 1 (per-voice or shared, either side).
static void rtrgMod(uint8_t sync1, uint8_t sync2) {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoDepth = 10;
  m.lfoRate = 0;  // slow: without restarts its phase only grows
  m.lfoSync = sync1;
  m.lfo[0] = {static_cast<uint8_t>(LfoWave::Saw), 100, 1, static_cast<uint8_t>(LfoDest::Rtrg1), sync2};
  noteOn(0, 0, 60);
  const float inc2 = lfoHz(100) * Synth::kControl / kSynthRate;
  const int ticksPerCycle = static_cast<int>(1.f / inc2);
  const int blocks = (3 * ticksPerCycle) * Synth::kControl / Synth::kBlock + 1;
  for (int k = 0; k < blocks; ++k) s->render(buf);
  const float inc1 = lfoHz(0) * Synth::kControl / kSynthRate;
  TEST_ASSERT_TRUE(s->lfoPhase(s->trackVoice(0), 0) <= (ticksPerCycle + 2) * inc1);
  TEST_ASSERT_TRUE(s->lfoPhase(s->trackVoice(0), 0) > 0.f);
}

void test_lfo_rtrg_mod() { rtrgMod(0, 0); }
void test_lfo_rtrg_mod_shared_source() { rtrgMod(0, kLfoFree); }
void test_lfo_rtrg_mod_shared_target() { rtrgMod(kLfoFree, kLfoFree); }

// LFO i's BYTE value at phase ph: the formula at t = phase x 65536 (A = lfoByte, B / C its
// defaults), its mode's sample, x depth / 64.
static float byteLfoWant(const Instrument& m, int i, float ph, int8_t depth) {
  const int f = m.lfoByte[i][0];
  const ByteFormula& bf = byteFormula(f);
  const int32_t x = byteEval(byteCode(f), static_cast<uint32_t>(ph * 65536.f), m.lfoByte[i][1], bf.b, bf.c);
  return byteSample(x, bf.mode) * depth * (1.f / 64.f);
}

// LFO i (0 = LFO 1, else m.lfo[i - 1]) set to BYTE, depth 63 on Cutoff; checks every block that its
// output is the formula at the voice's phase, in [-1, 1] and on the 1/128 grid (steps). Returns the
// number of distinct values seen.
static int byteLfoRun(int i, uint8_t sync, int blocks, float* outs = nullptr) {
  Instrument& m = p->instruments[0];
  LfoCfg c = {static_cast<uint8_t>(LfoWave::Byte), 60, 63, static_cast<uint8_t>(LfoDest::Cutoff), sync};
  if (i == 0) {
    m.lfoWave = c.wave;
    m.lfoRate = c.rate;
    m.lfoDepth = c.depth;
    m.lfoDest = c.dest;
    m.lfoSync = c.sync;
  } else {
    m.lfo[i - 1] = c;
  }
  noteOn(0, 0, 60);
  int distinct = 0;
  float last = 2.f;
  for (int k = 0; k < blocks; ++k) {
    s->render(buf);
    const int v = s->trackVoice(0);
    const float o = s->voice(v).lfoOut[i];
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, byteLfoWant(m, i, s->lfoPhase(v, i), 63), o);
    TEST_ASSERT_TRUE(o >= -1.f && o <= 1.f);
    const float q = o * (64.f / 63.f) * 128.f;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, roundf(q), q);
    if (o != last) ++distinct;
    last = o;
    if (outs) outs[k] = o;
  }
  return distinct;
}

void test_lfo_byte_follows_formula() {
  p->instruments[0].lfoByte[0][0] = 0;  // 42MELODY: t*(a&t>>b)
  TEST_ASSERT_TRUE(byteLfoRun(0, 0, 200) > 20);
}

void test_lfo_byte_lfo2_own_formula_shared_phase() {
  Instrument& m = p->instruments[0];
  m.lfoByte[1][0] = static_cast<uint8_t>(byteFormulaCount() - 1);  // a signed one
  m.lfoByte[1][1] = 4;
  m.lfoByte[0][0] = 3;  // LFO 1's, unused
  TEST_ASSERT_TRUE(byteLfoRun(1, kLfoFree, 200) > 20);  // Retrig OFF: the instrument's phase
}

void test_lfo_byte_changes_with_a() {
  float x[100], y[100];
  p->instruments[0].lfoByte[0][1] = 42;
  byteLfoRun(0, 0, 100, x);
  s->reset();
  p->instruments[0].lfoByte[0][1] = 7;
  byteLfoRun(0, 0, 100, y);
  int diff = 0;
  for (int k = 0; k < 100; ++k) diff += x[k] != y[k];
  TEST_ASSERT_TRUE(diff > 10);
}

// TEMPO: the phase runs at the division's rate like the other waves.
void test_lfo_byte_tempo_period() {
  const int blocks = 150;
  byteLfoRun(0, kLfoTempo, blocks);
  const Instrument& m = p->instruments[0];
  float want = 0;  // the same steps as Synth::lfo
  for (int k = 0; k < blocks * Synth::kBlock / Synth::kControl - 1; ++k) {
    want += lfoSyncHz(m.lfoRate, p->bpm) * Synth::kControl * (1.f / kSynthRate);
    if (want >= 1.f) want -= static_cast<int>(want);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, want, s->lfoPhase(s->trackVoice(0), 0));
}

// RTRG on itself is ignored: LFO 1 -> L1 RTRG runs on like a plain LFO (no restart after its wraps).
void test_lfo_self_rtrg_ignored() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Rtrg1);  // not offered by the UI, set directly
  m.lfoWave = static_cast<uint8_t>(LfoWave::Saw);
  m.lfoRate = 100;
  m.lfoDepth = 63;
  noteOn(0, 0, 60);
  const int blocks = 150;
  for (int k = 0; k < blocks; ++k) s->render(buf);
  float want = 0;  // the same steps as Synth::lfo
  for (int k = 0; k < blocks * Synth::kBlock / Synth::kControl - 1; ++k) {
    want += lfoHz(100) * Synth::kControl * (1.f / kSynthRate);
    if (want >= 1.f) want -= static_cast<int>(want);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, want, s->lfoPhase(s->trackVoice(0), 0));
}

// A dt = 0 update between ticks (pitch bend, fx) keeps a pending wrap: the restart still comes.
void test_lfo_rtrg_survives_mid_tick_update() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoDepth = 10;
  m.lfoRate = 0;
  m.lfo[0] = {static_cast<uint8_t>(LfoWave::Saw), 100, 1, static_cast<uint8_t>(LfoDest::Rtrg1), 0};
  noteOn(0, 0, 60);
  const float inc2 = lfoHz(100) * Synth::kControl / kSynthRate;
  const int ticksPerCycle = static_cast<int>(1.f / inc2);
  const int blocks = (3 * ticksPerCycle) * Synth::kControl / Synth::kBlock + 1;
  for (int k = 0; k < blocks; ++k) {
    for (int t = 0; t < Synth::kBlock; t += Synth::kControl) send(t + 10, 0, 0xE0, 0, 0x40);  // after each tick
    s->render(buf);
  }
  const float inc1 = lfoHz(0) * Synth::kControl / kSynthRate;
  TEST_ASSERT_TRUE(s->lfoPhase(s->trackVoice(0), 0) <= (ticksPerCycle + 2) * inc1);
}

// A shared target ignores RTRG from a per-voice LFO (no single phase to follow).
void test_lfo_shared_target_ignores_voice_rtrg() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoDepth = 10;
  m.lfoRate = 0;
  m.lfoSync = kLfoFree;
  m.lfo[0] = {static_cast<uint8_t>(LfoWave::Saw), 100, 1, static_cast<uint8_t>(LfoDest::Rtrg1), 0};
  noteOn(0, 0, 60);
  const int ticks = 200 * Synth::kBlock / Synth::kControl;
  for (int k = 0; k < 200; ++k) s->render(buf);
  const float inc1 = lfoHz(0) * Synth::kControl / kSynthRate;
  TEST_ASSERT_FLOAT_WITHIN(inc1 * 2, ticks * inc1, s->lfoPhase(s->trackVoice(0), 0));
}

// A shared (Retrig OFF) LFO ignores RATE from a per-voice LFO, takes it from a shared one.
void test_lfo_shared_target_rate_sources() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoDepth = 10;
  m.lfoRate = 0;
  m.lfoSync = kLfoFree;
  m.lfo[0] = {static_cast<uint8_t>(LfoWave::Square), 0, 63, static_cast<uint8_t>(LfoDest::Rate1), 0};  // per voice
  noteOn(0, 0, 60);
  for (int k = 0; k < 10; ++k) s->render(buf);
  const float inc = lfoHz(0) * Synth::kControl / kSynthRate;
  const int ticks = 10 * Synth::kBlock / Synth::kControl;
  TEST_ASSERT_FLOAT_WITHIN(inc * 2, ticks * inc, s->lfoPhase(s->trackVoice(0), 0));  // not sped up
  m.lfo[0].sync = kLfoFree;  // now shared
  const float before = s->lfoPhase(s->trackVoice(0), 0);
  for (int k = 0; k < 10; ++k) s->render(buf);
  TEST_ASSERT_TRUE(s->lfoPhase(s->trackVoice(0), 0) - before > ticks * inc * 8);
}

// L1 DEPTH from a per-voice LFO scales a shared LFO 1 too (applied per voice).
void test_lfo_shared_target_depth_from_voice() {
  Instrument& m = p->instruments[0];
  m.lfoSync = kLfoFree;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 64.f, depthModSend(-64));
}

void test_filter_env_long_attack_not_dropped() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.fenv = 63;
  m.fAtk = 127;  // 10 s: the envelope starts below -60 dB
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).fenvDone);
}

void test_filter_env_tail_dropped() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.fenv = 63;
  m.fDec = 70;  // ~160 ms to -60 dB
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).fenvDone);
  for (int k = 0; k < 60; ++k) render1(buf);  // 240 ms
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).fenvDone);
}

void test_res_lock_on_chip_sets_lock_bit() {
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  render1(buf);
  stepStart(0, 0, 24, false);
  fx(0, 0, Fx::RES, 99);
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(1 << kLockRes, v.lockMask);
  TEST_ASSERT_EQUAL(99, v.lock[kLockRes]);
}

void test_lfo_pitch_on_chip() {
  Instrument& m = p->instruments[0];
  m.wave = static_cast<uint8_t>(Wave::Triangle);
  noteOn(0, 0, 69);
  const int still = crossings(40);
  s->reset();
  m.lfoDest = static_cast<uint8_t>(LfoDest::Pitch);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 0;    // 0.05 Hz: the first half cycle stays at +depth
  m.lfoDepth = 63;  // ~ +12 semitones
  noteOn(0, 0, 69);
  TEST_ASSERT_INT_WITHIN(still / 10, still * 2, crossings(40));
}

void test_lfo_cutoff_on_chip() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 127;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 0;
  m.lfoDepth = -64;  // square starts at +1: -64 units of cutoff
  noteOn(0, 0, 84);
  render1(buf);
  const float swept = rmsBlocks(8);
  s->reset();
  m.lfoDepth = 0;
  noteOn(0, 0, 84);
  render1(buf);
  TEST_ASSERT_TRUE(swept < rmsBlocks(8) * 0.7f);
}

void test_lfo_macro_dest_ignored_on_chip() {
  Instrument& m = p->instruments[0];
  noteOn(0, 0, 69);
  const float ref = rmsBlocks(8);
  s->reset();
  m.lfoDest = static_cast<uint8_t>(LfoDest::Col);
  m.lfoDepth = 63;
  noteOn(0, 0, 69);
  TEST_ASSERT_FLOAT_WITHIN(ref * 0.01f, ref, rmsBlocks(8));
}

// ---- sampler voice ----

// SampleSource over an array: one sample "s" of n frames at rate.
struct ArrayBank : SampleSource {
  int16_t data[400] = {0};
  uint32_t n = 100, rate = kSynthRate;
  bool find(const char* name, SampleData& o) const override {
    if (strcmp(name, "s") != 0) return false;
    o = SampleData();
    o.key = 2;
    o.frames = n;
    o.rate = rate;
    o.whole = data;
    return true;
  }
};static void freeBank() {
  delete ab;
  ab = nullptr;
}
// Output for a sample value at full gain: sample gain x4, masterVol 100 -> x0.25, soft clip
// ~x1.5 near zero (test ramps stay below 1% of full scale).
static float outOf(int16_t d) { return d * 1.5f; }

static void sampleInstr(uint32_t frames, uint32_t rate) {
  ab = new ArrayBank();
  ab->n = frames;
  ab->rate = rate;
  for (uint32_t i = 0; i < frames; ++i) ab->data[i] = static_cast<int16_t>(100 + i * 5);  // ramp, never 0
  s->setBank(ab);
  p->instruments[0].type = InstrType::Sample;
  strcpy(p->instruments[0].sample, "s");
  p->instruments[0].root = 60;
}
// Renders until silence (or max samples) into a, returns the count of non-zero samples.
static int renderSamples(int16_t* a, int max) {
  int n = 0;
  for (int k = 0; k < max / Synth::kBlock; ++k) {
    render1(a + k * Synth::kBlock);
    for (int i = 0; i < Synth::kBlock; ++i) n += a[k * Synth::kBlock + i] != 0;
  }
  return n;
}
static int16_t out[4096];

void test_sample_root_note_repeats_data() {
  sampleInstr(100, kSynthRate);
  noteOn(0, 0, 60);
  TEST_ASSERT_EQUAL(100, renderSamples(out, 512));
  for (int i = 0; i < 100; ++i) TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[i]), out[i]);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_sample_octave_up_is_twice_as_fast() {
  sampleInstr(200, kSynthRate);
  noteOn(0, 0, 72);
  TEST_ASSERT_INT_WITHIN(1, 100, renderSamples(out, 512));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[20]), out[10]);
}

void test_sample_half_rate_lasts_twice_as_long() {
  sampleInstr(100, kSynthRate / 2);
  noteOn(0, 0, 60);
  TEST_ASSERT_INT_WITHIN(1, 200, renderSamples(out, 512));
  // Halfway between frames: linear interpolation.
  TEST_ASSERT_FLOAT_WITHIN(2.f, (outOf(ab->data[10]) + outOf(ab->data[11])) / 2, out[21]);
}

void test_sample_start_end_region() {
  sampleInstr(100, kSynthRate);
  p->instruments[0].start = 0x8000;  // ~50 %
  p->instruments[0].end = 0xCCCC;    // 80 %
  noteOn(0, 0, 60);
  TEST_ASSERT_INT_WITHIN(1, 30, renderSamples(out, 512));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[50]), out[0]);
}

void test_sample_forward_loop_sounds_longer() {
  sampleInstr(100, kSynthRate);
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::Forward);
  p->instruments[0].loopStart = 0x8000;  // loop 50..99
  noteOn(0, 0, 60);
  TEST_ASSERT_EQUAL(1024, renderSamples(out, 1024));
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  // After the end it jumps back to the loop start.
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[99]), out[99]);
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[50]), out[100]);
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[50]), out[150]);
}

void test_sample_ping_pong_turns_back() {
  sampleInstr(100, kSynthRate);
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::PingPong);
  noteOn(0, 0, 60);
  TEST_ASSERT_EQUAL(1024, renderSamples(out, 1024));
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  for (int i = 101; i < 190; ++i) TEST_ASSERT_TRUE(out[i] < out[i - 1]);  // reverse order
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[79]), out[119]);  // 98, 97, ... no repeated end
  TEST_ASSERT_TRUE(out[205] > out[204]);  // and forward again from the loop start
}

void test_sample_reverse_starts_at_the_end() {
  sampleInstr(100, kSynthRate);
  p->instruments[0].reverse = true;
  noteOn(0, 0, 60);
  TEST_ASSERT_EQUAL(100, renderSamples(out, 512));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[99]), out[0]);
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[0]), out[99]);
}

void test_sample_ofs_starts_from_the_middle() {
  sampleInstr(100, kSynthRate);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::OFS, 128);
  noteOn(0, 0, 60);
  TEST_ASSERT_EQUAL(50, renderSamples(out, 512));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[50]), out[0]);
}

void test_sample_mono_retrigger_after_release_restarts() {
  sampleInstr(400, kSynthRate);
  p->instruments[0].mono = true;
  p->instruments[0].release = 100;
  noteOn(0, 0, 60);
  render1(buf);
  noteOff(0, 0, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  noteOn(0, 0, 60);
  render1(out);
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[0]), out[0]);
}

void test_sample_missing_is_silent() {
  sampleInstr(100, kSynthRate);
  strcpy(p->instruments[0].sample, "nope");
  noteOn(0, 0, 60);
  TEST_ASSERT_TRUE(silentBlocks(4));
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_sample_bend_changes_speed() {
  sampleInstr(400, kSynthRate);
  send(0, 0, 0xE0, 0x7F, 0x7F);  // ~+2 semitones
  noteOn(0, 0, 60);
  TEST_ASSERT_INT_WITHIN(2, 357, renderSamples(out, 1024));  // 400 / 2^(2/12)
}

void test_sample_reverse_loop_is_mirrored() {
  sampleInstr(100, kSynthRate);
  p->instruments[0].reverse = true;
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::Forward);
  p->instruments[0].loopStart = 0x8000;  // the loop is the second half of the reversed sound: 49..0
  noteOn(0, 0, 60);
  TEST_ASSERT_EQUAL(512, renderSamples(out, 512));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[0]), out[99]);
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[49]), out[100]);
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[49]), out[150]);
}

// ---- FM params cache ----

// Renders n blocks into a, one note of instrument 0 on track 0; cache on or off.
static void renderFm(int16_t* a, int n, bool cache, uint8_t note = 69) {
  s->reset();
  s->setFmCache(cache);
  noteOn(0, 0, note);
  for (int k = 0; k < n; ++k) render1(a + k * Synth::kBlock);
}

static int16_t fa[Synth::kBlock * 250], fb[Synth::kBlock * 250];

void test_fm_cache_static_note_computes_once() {
  useFm(0, FmMachine::Tone);
  p->instruments[0].sustain = 127;
  noteOn(0, 0, 69);
  const uint32_t c0 = s->fmMachineCalls();
  for (int k = 0; k < 50; ++k) render1(buf);
  TEST_ASSERT_TRUE(s->fmMachineCalls() - c0 <= 2);
}

void test_fm_cache_static_note_matches_recompute() {
  const FmMachine ms[] = {FmMachine::Tone, FmMachine::Kick, FmMachine::Snare, FmMachine::Clap, FmMachine::Chord};
  for (FmMachine mc : ms) {
    useFm(0, mc);
    p->instruments[0].sustain = 127;
    renderFm(fa, 100, false);
    renderFm(fb, 100, true);
    TEST_ASSERT_EQUAL_INT16_ARRAY(fa, fb, Synth::kBlock * 100);
  }
}

void test_fm_cache_lfo_macro_close_to_recompute() {
  useFm(0, FmMachine::Tone);
  Instrument& m = p->instruments[0];
  m.sustain = 127;
  m.lfoWave = static_cast<uint8_t>(LfoWave::Sine);
  m.lfoRate = 40;
  m.lfoDepth = 40;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Col);
  renderFm(fa, 250, false);
  const uint32_t c0 = s->fmMachineCalls();
  renderFm(fb, 250, true);
  const uint32_t calls = s->fmMachineCalls() - c0;
  TEST_ASSERT_TRUE(calls > 20);                 // the sweep still recomputes
  TEST_ASSERT_TRUE(calls < 250 * 4);            // but not every tick
  int peak = 0, diff = 0;
  for (int i = 0; i < Synth::kBlock * 250; ++i) {
    peak = abs(fa[i]) > peak ? abs(fa[i]) : peak;
    diff = abs(fa[i] - fb[i]) > diff ? abs(fa[i] - fb[i]) : diff;
  }
  TEST_ASSERT_TRUE(peak > 1000);
  // COLOR also sets the TONE detune: the cache's 0.5-unit macro tolerance shows as a slow phase drift.
  TEST_ASSERT_TRUE(diff < peak / 40);
}

void test_fm_cache_lfo_pitch_close_to_recompute() {
  useFm(0, FmMachine::Tone);
  Instrument& m = p->instruments[0];
  m.sustain = 127;
  m.macro[kMacCol] = 0;  // pure sine
  m.lfoWave = static_cast<uint8_t>(LfoWave::Sine);
  m.lfoRate = 40;
  m.lfoDepth = 10;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Pitch);
  s->setFmCache(false);
  noteOn(0, 0, 69);
  const int plain = crossings(250);
  s->reset();
  s->setFmCache(true);
  noteOn(0, 0, 69);
  const int cached = crossings(250);
  TEST_ASSERT_INT_WITHIN(2, plain, cached);
}

// ---- FM SAT ----

static long diffOf(const int16_t* a, const int16_t* b, int n, long* sum) {
  long d = 0;
  *sum = 0;
  for (int i = 0; i < n; ++i) {
    d += abs(a[i] - b[i]);
    *sum += abs(a[i]);
  }
  return d;
}

// Instrument SAT changes the sound; set outside the machine cache (no extra fmMachine calls).
void test_fm_sat_instrument() {
  useFm(0, FmMachine::Tone);
  Instrument& m = p->instruments[0];
  m.sustain = 127;
  renderFm(fa, 40, true);
  m.fmSat = 127;
  const uint32_t c0 = s->fmMachineCalls();
  renderFm(fb, 40, true);
  TEST_ASSERT_TRUE(s->fmMachineCalls() - c0 <= 2);
  long sum;
  TEST_ASSERT_TRUE(diffOf(fa, fb, Synth::kBlock * 40, &sum) > sum / 10);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.f, s->voice(s->trackVoice(0)).eng.fm.fp.sat);
}

void test_fm_sat_lock_on_note_step() {
  useFm(0, FmMachine::Tone);
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::SAT, 100);
  noteOn(0, 0, 60);
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL_HEX32(1u << kLockSat, v.lockMask);
  TEST_ASSERT_EQUAL(100, v.lock[kLockSat]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 100.f / 127.f, v.eng.fm.fp.sat);
  stepStart(0, 0, 6, true);  // next step: no lock, the instrument's SAT (0)
  noteOn(0, 0, 62);
  render1(buf);
  TEST_ASSERT_EQUAL(0, s->voice(s->trackVoice(0)).lockMask);
  TEST_ASSERT_EQUAL_FLOAT(0, s->voice(s->trackVoice(0)).eng.fm.fp.sat);
}

void test_fm_sat_lock_on_empty_step_hits_sounding_voice() {
  useFm(0, FmMachine::Tone);
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  render1(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::SAT, 127);
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL_HEX32(1u << kLockSat, v.lockMask);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.f, v.eng.fm.fp.sat);
}

void test_fm_sat_lfo() {
  useFm(0, FmMachine::Tone);
  Instrument& m = p->instruments[0];
  m.sustain = 127;
  renderFm(fa, 40, true);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 30;
  m.lfoDepth = 63;  // SAT 0 -> ~63 in the first half cycle
  m.lfoDest = static_cast<uint8_t>(LfoDest::Sat);
  const uint32_t c0 = s->fmMachineCalls();
  renderFm(fb, 40, true);
  TEST_ASSERT_TRUE(s->fmMachineCalls() - c0 <= 2);  // SAT moves without recomputing the machine
  long sum;
  TEST_ASSERT_TRUE(diffOf(fa, fb, Synth::kBlock * 40, &sum) > sum / 20);
  // First half cycle of the square: la = +63/64, so SAT = 0 + 63 (a wrong sign gives 0, a wrong scale not 63).
  s->reset();
  noteOn(0, 0, 69);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.5f / 127.f, 63.f / 127.f, s->voice(s->trackVoice(0)).eng.fm.fp.sat);
}

// SAT on a CHIP track: the voice takes the bit (as any non-macro lock), nothing reads it; it plays on.
void test_fm_sat_lock_ignored_on_chip() {
  const bool withSat[] = {false, true};
  int16_t* outs[] = {fa, fb};
  for (int t = 0; t < 2; ++t) {
    s->reset();
    stepStart(0, 0, 6, true);
    if (withSat[t]) fx(0, 0, Fx::SAT, 127);
    noteOn(0, 0, 60);
    for (int k = 0; k < 20; ++k) render1(outs[t] + k * Synth::kBlock);  // CHIP voice: the bit is set, nothing reads it
    TEST_ASSERT_FALSE(blockSilent());
    if (withSat[t]) TEST_ASSERT_EQUAL_HEX32(1u << kLockSat, s->voice(s->trackVoice(0)).lockMask);
  }
  TEST_ASSERT_EQUAL_INT16_ARRAY(fa, fb, Synth::kBlock * 20);
}

// ---- FM voice limit ----

static int fmVoices() {
  int n = 0;
  for (int i = 0; i < kVoices; ++i) n += s->voice(i).on && s->voice(i).fm;
  return n;
}

void test_fm_ninth_note_steals_oldest_fm_keeps_chip() {
  useFm(1, FmMachine::Tone);
  p->instruments[1].mono = false;
  p->instruments[1].sustain = 127;
  for (int t = 1; t < 4; ++t) p->tracks[t].instr = 1;
  noteOn(0, 0, 40);  // CHIP
  noteOn(0, 0, 43);
  render1(buf);
  for (int t = 1; t <= 2; ++t)
    for (int k = 0; k < 4; ++k) {
      noteOn(0, static_cast<uint8_t>(t), static_cast<uint8_t>(60 + t * 10 + k));
      render1(buf);
    }
  TEST_ASSERT_EQUAL(8, fmVoices());
  noteOn(0, 3, 90);  // 9th: steals track 1's note 70
  renderMs(kStealMs);  // the fade may span two blocks
  TEST_ASSERT_EQUAL(8, fmVoices());
  TEST_ASSERT_EQUAL(10, s->activeVoices());  // 2 CHIP + 8 FM
  bool has70 = false, has90 = false;
  int chip = 0;
  for (int i = 0; i < kVoices; ++i) {
    const Voice& v = s->voice(i);
    if (!v.on) continue;
    chip += !v.fm;
    has70 |= v.fm && v.note == 70;
    has90 |= v.fm && v.note == 90 && v.track == 3;
  }
  TEST_ASSERT_EQUAL(2, chip);
  TEST_ASSERT_FALSE(has70);
  TEST_ASSERT_TRUE(has90);
}

// 16 tracks, 12 of them FM: the heavy-voice cap holds under random note traffic.
void test_fm_voices_never_exceed_cap() {
  useFm(1, FmMachine::Tone);
  useFm(2, FmMachine::Kick);
  useFm(3, FmMachine::Chord);
  p->instruments[1].mono = false;
  p->instruments[1].sustain = 127;
  p->instruments[1].release = 60;
  p->instruments[2].macro[kMacDec] = 120;
  p->instruments[3].sustain = 127;
  for (int t = 0; t < kTracks; ++t) p->tracks[t].instr = static_cast<uint8_t>(t % 4);
  uint32_t r = 12345;
  for (int k = 0; k < 400; ++k) {
    r = r * 1103515245u + 12345u;
    const uint8_t t = (r >> 8) % kTracks, n = 40 + (r >> 16) % 40;
    if ((r >> 4) % 5 == 0) noteOff(0, t, n);
    else noteOn((r >> 20) % Synth::kBlock, t, n);
    render1(buf);
    int load = 0, fading = 0;
    for (int i = 0; i < kVoices; ++i) {
      const Voice& x = s->voice(i);
      load += heavyLoad(x);
      fading += x.on && x.fm && x.stolen;
    }
    TEST_ASSERT_TRUE(load <= kFmVoiceMax);
    // One steal per block at most; a kStealMs fade spans a known number of blocks, so the
    // victims still fading into later blocks are at most one fewer.
    constexpr int kStealBlocks = (kStealMs * kSynthRate / 1000 + Synth::kBlock - 1) / Synth::kBlock + 1;
    TEST_ASSERT_TRUE(fmVoices() <= kFmVoiceMax + kStealBlocks - 1);
    TEST_ASSERT_TRUE(fading <= kStealBlocks);
  }
  // Stolen voices finish their fade: no steal outlives kStealMs.
  for (int k = 0; k < 2 * (kStealMs * kSynthRate / 1000) / Synth::kBlock + 2; ++k) render1(buf);
  TEST_ASSERT_TRUE(fmVoices() <= kFmVoiceMax);
  for (int i = 0; i < kVoices; ++i) TEST_ASSERT_FALSE(s->voice(i).stolen);
}

// ---- DRUM ----

static void drumInstr(int i, DrumMachine mc) {
  Instrument& m = p->instruments[i];
  m.type = InstrType::Drum;
  drumSetMachine(m, static_cast<uint8_t>(mc));
}

void test_drum_sounds_and_ends() {
  drumInstr(0, DrumMachine::Bd8);
  p->instruments[0].macro[kMacDec] = 40;  // short
  noteOn(0, 0, 60);
  TEST_ASSERT_FALSE(silentBlocks(1));
  for (int k = 0; k < 400 && s->activeVoices(); ++k) render1(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_drum_ignores_note_off() {
  drumInstr(0, DrumMachine::Cy8);
  noteOn(0, 0, 60);
  render1(buf);
  noteOff(0, 0, 60);
  for (int k = 0; k < 20; ++k) render1(buf);  // 80 ms
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_drum_mono_choke() {
  drumInstr(0, DrumMachine::Hh8);
  p->instruments[0].macro[kMacDec] = 120;  // open hat
  p->instruments[0].mono = false;           // ignored: DRUM is always mono
  noteOn(0, 0, 60);
  render1(buf);
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_drum_choke_mid_segment_has_no_jump() {
  drumInstr(0, DrumMachine::Bd8);
  p->instruments[0].macro[kMacDec] = 100;
  noteOn(0, 0, 60);
  for (int k = 0; k < 30; ++k) render1(buf);
  const int16_t prev = buf[Synth::kBlock - 1];
  noteOn(50, 0, 60);  // choke inside a control segment
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_TRUE(abs(buf[0] - prev) < 3000);
  TEST_ASSERT_TRUE(abs(buf[50] - buf[49]) < 3000);  // the old hit fades under the new one
}

void test_drum_dec_lock() {
  drumInstr(0, DrumMachine::Hh8);
  p->instruments[0].macro[kMacDec] = 120;
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::DCY, 5);  // closed
  noteOn(0, 0, 60);
  for (int k = 0; k < 40 && s->activeVoices(); ++k) render1(buf);  // 160 ms
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_drum_lock_on_empty_step_hits_sounding_voice() {
  drumInstr(0, DrumMachine::Hh8);
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  render1(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::DCY, 7);
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(1 << kMacDec, v.lockMask);
  TEST_ASSERT_EQUAL(7, v.lock[kMacDec]);
}

void test_drum_filter_applies() {
  drumInstr(0, DrumMachine::Hh8);
  p->instruments[0].macro[kMacDec] = 120;
  noteOn(0, 0, 60);
  const float open = rmsBlocks(4);
  s->reset();
  p->instruments[0].fltMode = static_cast<uint8_t>(FltMode::Lp);
  p->instruments[0].cutoff = 0;
  noteOn(0, 0, 60);
  TEST_ASSERT_TRUE(rmsBlocks(4) < open * 0.1f);
}

// ---- Filter tail, DRUM params cache ----

// Renders until the voice is freed (at most max blocks) and one block more; returns the largest
// step between neighbouring samples relative to the peak, in %. A resonant filter cut off
// mid-ring drops straight to 0: a step close to the peak.
static int endJump(int max) {
  int16_t prev = 0;
  int peak = 1, jump = 0;
  for (int k = 0; k < max && (!k || s->activeVoices()); ++k) {  // a queued note starts on render
    render1(buf);
    for (int i = 0; i < Synth::kBlock; ++i) {
      peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
      if (k || i) jump = abs(buf[i] - prev) > jump ? abs(buf[i] - prev) : jump;
      prev = buf[i];
    }
  }
  TEST_ASSERT_EQUAL(0, s->activeVoices());
  render1(buf);
  jump = abs(buf[0] - prev) > jump ? abs(buf[0] - prev) : jump;
  return jump * 100 / peak;
}

void test_filter_rings_out_after_release() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 40;
  m.reso = 110;
  m.release = 10;  // ~3 ms: shorter than the filter's ring
  noteOn(0, 0, 36);
  for (int k = 0; k < 10; ++k) render1(buf);
  noteOff(0, 0, 36);
  TEST_ASSERT_TRUE(endJump(250) < 20);
}

void test_drum_filter_rings_out() {
  drumInstr(0, DrumMachine::Rs8);
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Bp);
  m.cutoff = 50;
  m.reso = 120;
  noteOn(0, 0, 60);
  TEST_ASSERT_TRUE(endJump(500) < 20);
}

void test_filter_tail_voice_is_reused_first() {
  // A voice ringing out its filter holds no note: a new note takes it before a held one.
  for (auto& m : p->instruments) {
    m.fltMode = static_cast<uint8_t>(FltMode::Lp);
    m.cutoff = 20;
    m.reso = 127;
  }
  for (int k = 0; k < kVoices; ++k) noteOn(0, k % kTracks, 40 + k);
  render1(buf);
  noteOff(0, 3, 43);
  for (int k = 0; k < 4; ++k) render1(buf);  // env done, filter still ringing
  const int tail = [] {
    for (int i = 0; i < kVoices; ++i)
      if (s->voice(i).on && s->voice(i).env.idle()) return i;
    return -1;
  }();
  TEST_ASSERT_TRUE(tail >= 0);
  noteOn(0, 7, 90);  // track 7 is below its poly limit: a new voice
  render1(buf);
  TEST_ASSERT_EQUAL(90, s->voice(tail).note);
}

void test_drum_cache_static_hit_computes_once() {
  drumInstr(0, DrumMachine::Hh8);
  noteOn(0, 0, 60);
  const uint32_t c0 = s->drumMachineCalls();
  for (int k = 0; k < 50; ++k) render1(buf);
  TEST_ASSERT_TRUE(s->drumMachineCalls() - c0 <= 2);
}

// The cached params are the ones drumMachine gives for the voice's inputs (noise is seeded per
// hit, so two renders can't be compared sample by sample).
void test_drum_cache_matches_recompute() {
  const DrumMachine ms[] = {DrumMachine::Bd8, DrumMachine::Sd9, DrumMachine::Cp8, DrumMachine::Cy9};
  for (DrumMachine mc : ms) {
    s->reset();
    drumInstr(0, mc);
    noteOn(0, 0, 62);
    for (int k = 0; k < 10; ++k) render1(buf);
    const Instrument& m = p->instruments[0];
    float mac[kFmMacros];
    for (int k = 0; k < kFmMacros; ++k) mac[k] = m.macro[k];
    DrumParams want;
    drumMachine(static_cast<uint8_t>(mc), mac, 62, want);
    const DrumParams& got = s->voice(s->trackVoice(0)).dp();
    TEST_ASSERT_EQUAL_FLOAT(want.toneHz[0], got.toneHz[0]);
    TEST_ASSERT_EQUAL_FLOAT(want.toneMs, got.toneMs);
    TEST_ASSERT_EQUAL_FLOAT(want.metalMs, got.metalMs);
    TEST_ASSERT_EQUAL_FLOAT(want.metalHp, got.metalHp);
    TEST_ASSERT_EQUAL_FLOAT(want.noiseMs, got.noiseMs);
    TEST_ASSERT_EQUAL_FLOAT(want.noiseHz, got.noiseHz);
    TEST_ASSERT_EQUAL_FLOAT(want.pitchMs, got.pitchMs);
  }
}

void test_drum_cache_follows_lock() {
  drumInstr(0, DrumMachine::Hh8);
  p->instruments[0].macro[kMacDec] = 120;
  noteOn(0, 0, 60);
  render1(buf);
  const float before = s->voice(s->trackVoice(0)).dp().metalMs;
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::DCY, 20);  // lock on the sounding voice
  render1(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).dp().metalMs < before * 0.5f);
}

void test_heavy_voice_limit_counts_drum() {
  for (int t = 0; t < kTracks; ++t) {
    drumInstr(t, DrumMachine::Cy9);
    p->tracks[t].instr = t;
  }
  for (int t = 0; t < kTracks; ++t) noteOn(0, t, 60);
  renderMs(kStealMs);  // steals fade over up to two blocks
  // 8 tracks fill kFmVoiceMax; a 9th heavy note (preview track) steals one of them.
  p->instruments[8].type = InstrType::Fm;
  send(0, kPreviewTrack, 0xC0, 8, 0, 2);
  noteOn(0, kPreviewTrack, 60);
  renderMs(kStealMs);
  TEST_ASSERT_TRUE(s->activeVoices() <= kFmVoiceMax);
}

static void fourSlices() {  // slices at frames 0, 100, 200, 300 of 400
  Instrument& m = p->instruments[0];
  for (int i = 0; i < 4; ++i) sliceInsert(m, frameToFrac(i * 100, 400));
}

void test_slice_note_mode_plays_slice_of_note() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  noteOn(0, 0, 62);  // root 60 -> slice 2, at the root's speed
  TEST_ASSERT_INT_WITHIN(1, 100, renderSamples(out, 1024));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[200]), out[0]);
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[250]), out[50]);
}

void test_slice_note_mode_out_of_range_is_silent() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  noteOn(0, 0, 64);
  noteOn(0, 0, 59);
  TEST_ASSERT_TRUE(silentBlocks(4));
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_slice_note_mode_ignores_loop() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  p->instruments[0].loop = static_cast<uint8_t>(LoopMode::Forward);
  noteOn(0, 0, 61);
  TEST_ASSERT_INT_WITHIN(1, 100, renderSamples(out, 1024));
}

void test_slice_note_mode_reverse_inside_slice() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  p->instruments[0].reverse = true;
  noteOn(0, 0, 61);
  TEST_ASSERT_INT_WITHIN(1, 100, renderSamples(out, 1024));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[199]), out[0]);
}

void test_slice_fx_mode_slc_picks_slice_with_pitch() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Fx);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::SLC, 1);
  noteOn(0, 0, 72);  // octave up: 100 frames in 50 samples
  TEST_ASSERT_INT_WITHIN(1, 50, renderSamples(out, 1024));
  TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[100]), out[0]);
}

void test_slice_fx_mode_without_slc_plays_whole_region() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Fx);
  noteOn(0, 0, 60);
  TEST_ASSERT_INT_WITHIN(1, 400, renderSamples(out, 1024));
}

void test_slice_fx_mode_slc_out_of_range_plays_whole_region() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Fx);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::SLC, 9);
  noteOn(0, 0, 60);
  TEST_ASSERT_INT_WITHIN(1, 400, renderSamples(out, 1024));
}

void test_slice_mode_without_slices_plays_whole_region() {
  sampleInstr(400, kSynthRate);
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  noteOn(0, 0, 65);  // count 0: like OFF (pitched)
  TEST_ASSERT_TRUE(renderSamples(out, 1024) > 0);
}

void test_slc_ignored_when_slice_mode_off() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::SLC, 2);
  noteOn(0, 0, 60);
  TEST_ASSERT_INT_WITHIN(1, 400, renderSamples(out, 1024));
}

void test_preview_slc_plays_slice_in_any_mode() {
  sampleInstr(400, kSynthRate);
  fourSlices();
  const uint8_t modes[] = {static_cast<uint8_t>(SliceMode::Off), static_cast<uint8_t>(SliceMode::Note),
                           static_cast<uint8_t>(SliceMode::Fx)};
  for (uint8_t mode : modes) {
    p->instruments[0].sliceMode = mode;
    p->instruments[0].root = 60;
    delete s;
    s = new Synth(*p);
    s->setBank(ab);
    send(0, kPreviewTrack, 0xC0, 0, 0, 2);
    fx(0, kPreviewTrack, Fx::SLC, 2);
    noteOn(0, kPreviewTrack, 60);  // the root: slice 2 at its own pitch
    TEST_ASSERT_INT_WITHIN(1, 100, renderSamples(out, 1024));
    TEST_ASSERT_FLOAT_WITHIN(2.f, outOf(ab->data[200]) * kPreviewVol / 127.f, out[0]);  // preview track volume
  }
}

// ---- SAMPLE: plays until OFF, choke ----

static FakeBank* sampleSetup(uint8_t loop = 0) {
  static FakeBank bank;
  for (auto& x : bank.data) x = 8192;
  s->setBank(&bank);
  Instrument& m = p->instruments[0];
  m.type = InstrType::Sample;
  strcpy(m.sample, "kick");
  m.loop = loop;
  return &bank;
}

static bool releasing(const Voice& v) { return v.env.stage() == Env::Stage::Release || v.env.idle(); }

void test_sample_ignores_note_off() {
  sampleSetup();
  noteOn(0, 0, 60);
  render1(buf);
  noteOff(0, 0, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_FALSE(releasing(s->voice(s->trackVoice(0))));
  renderMs(100);  // 1000 frames at 16 kHz = 62.5 ms: the sample ends by itself
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_looped_sample_ignores_note_off() {
  sampleSetup(static_cast<uint8_t>(LoopMode::Forward));
  noteOn(0, 0, 60);
  noteOff(0, 0, 60);
  renderMs(200);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  send(0, 0, 0xFF, 0, 0, 1);  // release track (OFF)
  renderMs(10);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_preview_sample_stops_on_note_off() {
  sampleSetup(static_cast<uint8_t>(LoopMode::Forward));
  send(0, kPreviewTrack, 0xC0, 0, 0, 2);
  noteOn(0, kPreviewTrack, 60);
  render1(buf);
  noteOff(0, kPreviewTrack, 60);
  renderMs(10);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_sample_choked_by_next_step() {
  sampleSetup(static_cast<uint8_t>(LoopMode::Forward));
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  render1(buf);
  const int first = s->trackVoice(0);
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 62);
  render1(buf);
  TEST_ASSERT_TRUE(releasing(s->voice(first)));
  renderMs(10);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_sample_chord_in_one_step_not_choked() {
  sampleSetup(static_cast<uint8_t>(LoopMode::Forward));
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  noteOn(0, 0, 64);
  noteOn(0, 0, 67);
  renderMs(10);
  TEST_ASSERT_EQUAL(3, s->activeVoices());
}

void test_sample_same_note_retrigger_chokes() {
  sampleSetup(static_cast<uint8_t>(LoopMode::Forward));
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  render1(buf);
  noteOn(0, 0, 60);  // a ratchet hit
  renderMs(10);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_sample_choke_leaves_other_tracks() {
  sampleSetup(static_cast<uint8_t>(LoopMode::Forward));
  p->tracks[1].instr = 0;
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  stepStart(0, 1, 24, true);
  noteOn(0, 1, 60);
  render1(buf);
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 62);
  renderMs(10);
  TEST_ASSERT_EQUAL(2, s->activeVoices());  // track 1 and the new track 0 note
}

void test_mono_sample_restarts() {
  sampleSetup(static_cast<uint8_t>(LoopMode::Forward));
  p->instruments[0].mono = true;
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  renderMs(20);
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_TRUE((v.pos >> 32) < 200);  // from the start again, not 20 ms in
}

// Delay: 1/16 at 120 BPM = kSynthRate / 8 samples.
static int16_t dline[kSynthRate / 4];  // 250 ms

// Short note at sample 0 on track 0 (one block, release 0), then nBlocks rendered; first non-zero
// sample after a silent gap, or -1.
static int echoStart(int nBlocks) {
  noteOff(Synth::kBlock - 1, 0, 60);
  int gap = 0, at = -1;
  for (int k = 0; k < nBlocks && at < 0; ++k) {
    render1(buf);
    for (int i = 0; i < Synth::kBlock && at < 0; ++i) {
      if (buf[i] == 0) ++gap;
      else if (gap > 1000) at = k * Synth::kBlock + i;
      else gap = 0;
    }
  }
  return at;
}

static void delaySetup(uint8_t send) {
  p->bpm = 120;
  p->dlyTime = 1;
  p->dlyFb = 0;
  p->dlyTone = 127;
  p->dlyLevel = 127;
  p->instruments[0].send = send;
  s->setDelayBuffer(dline, kSynthRate / 4);
}

void test_delay_echo_after_one_sixteenth() {
  delaySetup(127);
  noteOn(0, 0, 60);
  const int at = echoStart(blocksMs(200));
  TEST_ASSERT_TRUE(at >= kSynthRate / 8 - 5 && at <= kSynthRate / 8 + 5);  // 125 ms
}

void test_delay_send_zero_no_echo() {
  delaySetup(0);
  noteOn(0, 0, 60);
  TEST_ASSERT_EQUAL(-1, echoStart(blocksMs(200)));
}

void test_delay_without_buffer_no_echo() {
  delaySetup(127);
  s->setDelayBuffer(nullptr, 0);
  noteOn(0, 0, 60);
  TEST_ASSERT_EQUAL(-1, echoStart(blocksMs(200)));
}

void test_delay_send_zero_mix_unchanged() {
  static int16_t ref[Synth::kBlock * 8];
  noteOn(0, 0, 60);
  for (int k = 0; k < 8; ++k) render1(ref + k * Synth::kBlock);
  s->reset();
  delaySetup(0);
  noteOn(0, 0, 60);
  for (int k = 0; k < 8; ++k) {
    render1(buf);
    TEST_ASSERT_EQUAL_INT16_ARRAY(ref + k * Synth::kBlock, buf, Synth::kBlock);
  }
}

void test_dly_lock_on_note_step() {
  delaySetup(0);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::DLY, 127);
  noteOn(0, 0, 60);
  const int at = echoStart(blocksMs(200));
  TEST_ASSERT_TRUE(at >= kSynthRate / 8 - 5 && at <= kSynthRate / 8 + 5);  // 125 ms
  s->reset();
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);  // a new step drops the lock
  TEST_ASSERT_EQUAL(-1, echoStart(blocksMs(200)));
}

void test_dly_lock_without_note_hits_sounding_voice() {
  delaySetup(0);
  noteOn(0, 0, 60);
  render1(buf);
  stepStart(0, 0, 24, false);
  fx(0, 0, Fx::DLY, 127);
  render1(buf);
  const int at = echoStart(blocksMs(200));  // the note sounded on blocks 1..2 with send
  TEST_ASSERT_TRUE(at >= 0);
}

void test_delay_time_follows_tempo() {
  delaySetup(127);
  p->bpm = 240;  // 1/16 = 62.5 ms
  noteOn(0, 0, 60);
  const int at = echoStart(blocksMs(200));
  TEST_ASSERT_TRUE(at >= kSynthRate / 16 - 5 && at <= kSynthRate / 16 + 5);
}

// ---- SYNTH ----

struct ArrayWt : WtSource {
  int16_t* t;
  const int16_t* findWt(const char* n) const override { return strcmp(n, "*SAWSQR") == 0 ? t : nullptr; }
};

void test_synth_saw_sounds() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  m.sustain = 127;
  noteOn(0, 0, 57);  // 220 Hz
  render1(buf);
  int nz = 0;
  for (int i = 0; i < Synth::kBlock; ++i) nz += buf[i] != 0;
  TEST_ASSERT_TRUE(nz > 100);
  // 100 ms: ~22 falling crossings at 220 Hz.
  TEST_ASSERT_INT_WITHIN(2, 22, crossings(blocksMs(100)));
}

void test_synth_missing_wt_silent() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  m.synOsc[0] = m.synOsc[1] = static_cast<uint8_t>(SynOsc::Wt);
  strcpy(m.synWt[0], "NOPE");
  noteOn(0, 0, 60);
  TEST_ASSERT_TRUE(silentBlocks(4));  // no source set at all
}

void test_synth_wt_plays() {
  static int16_t t[kWtTableSamples];
  WtBuiltinSrc src(0);
  wtBuild(src, t);
  ArrayWt w;
  w.t = t;
  s->setWavetables(&w);
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  m.synOsc[0] = static_cast<uint8_t>(SynOsc::Wt);
  strcpy(m.synWt[0], "*SAWSQR");
  noteOn(0, 0, 60);
  TEST_ASSERT_FALSE(silentBlocks(2));
}

static int synthPolyChords(SynOsc o1, SynOsc o2) {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  m.synOsc[0] = static_cast<uint8_t>(o1);
  m.synOsc[1] = static_cast<uint8_t>(o2);
  m.mono = false;
  m.sustain = 127;
  for (int t = 0; t < kTracks; ++t) {
    p->tracks[t].instr = 0;
    for (int k = 0; k < kPolyPerTrack; ++k) noteOn(0, t, 48 + t * 2 + k * 4);
  }
  render1(buf);
  return s->activeVoices();
}

void test_synth_wt_voices_are_heavy() {
  // A wavetable oscillator puts the voice under the shared heavy limit (CPU, bench 2026-10-06).
  TEST_ASSERT_EQUAL(kFmVoiceMax, synthPolyChords(SynOsc::Saw, SynOsc::Wt));
}

void test_synth_bl_voices_use_whole_pool() {
  TEST_ASSERT_EQUAL(kVoices, synthPolyChords(SynOsc::Saw, SynOsc::Square));
}

// ---- LOFI ----

static void lofiInstr(int i, LofiMachine mc) {
  Instrument& m = p->instruments[i];
  instrSetType(m, InstrType::Lofi);
  lofiSetMachine(m, static_cast<uint8_t>(mc));
  m.sustain = 127;
  m.release = 0;
}

void test_lofi_byte_sounds_and_env_ends() {
  lofiInstr(0, LofiMachine::Byte);
  noteOn(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).lofi);
  TEST_ASSERT_FALSE(silentBlocks(blocksMs(320)));  // formula 0 (42MELODY) rests 256 ms at C-4
  noteOff(0, 0, 60);
  for (int k = 0; k < 400 && s->activeVoices(); ++k) s->render(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_lofi_apc_sounds() {
  lofiInstr(0, LofiMachine::Apc);
  noteOn(0, 0, 57);  // 220 Hz, WIDTH default: one pulse per period
  s->render(buf);
  // 100 ms: ~22 falling crossings at 220 Hz.
  TEST_ASSERT_INT_WITHIN(2, 22, crossings(blocksMs(100)));
}

static int lofiPolyChords(LofiMachine mc) {
  lofiInstr(0, mc);
  p->instruments[0].mono = false;
  for (int t = 0; t < kTracks; ++t) {
    p->tracks[t].instr = 0;
    for (int k = 0; k < kPolyPerTrack; ++k) noteOn(0, t, 48 + t * 2 + k * 4);
  }
  s->render(buf);
  return s->activeVoices();
}

void test_lofi_byte_voices_are_heavy() { TEST_ASSERT_EQUAL(kFmVoiceMax, lofiPolyChords(LofiMachine::Byte)); }

void test_lofi_apc_voices_use_whole_pool() { TEST_ASSERT_EQUAL(kVoices, lofiPolyChords(LofiMachine::Apc)); }

void test_lofi_macro_lock_applies() {
  lofiInstr(0, LofiMachine::Byte);
  send(0, 0, 0xF5, static_cast<uint8_t>(Fx::DCY), 100);  // A
  noteOn(0, 0, 60);
  s->render(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_TRUE(v.lockMask & 1);
  TEST_ASSERT_EQUAL(100, v.lock[0]);
}

// LOFI legato from a CHIP voice starts the LOFI voice afresh (as FM): t from zero, not the
// stale counter the CHIP note left in the engine.
void test_lofi_legato_from_chip_starts_lofi_voice() {
  lofiInstr(1, LofiMachine::Byte);
  p->instruments[1].mono = true;
  p->instruments[0].mono = true;
  pgm(0, 1);
  noteOn(0, 0, 60);
  for (int k = 0; k < 100; ++k) s->render(buf);
  Voice& v = const_cast<Voice&>(s->voice(s->trackVoice(0)));
  TEST_ASSERT_TRUE(v.lv().tick() > 2000);
  pgm(0, 0);
  noteOn(0, 0, 62);  // CHIP legato on the same voice
  s->render(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).lofi);
  pgm(0, 1);
  noteOn(0, 0, 60);  // LOFI legato from a CHIP voice: a new LOFI note
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  Voice& w = const_cast<Voice&>(s->voice(s->trackVoice(0)));
  TEST_ASSERT_TRUE(w.lofi);
  TEST_ASSERT_TRUE(w.lv().tick() < 200);  // one block at C-4: ~32
}

// SYNC: t runs 1024 per 1/16 step of the project tempo (step = 24 ticks), x 2^octave of the note
// from C-4; semitones are ignored. kLofiSyncBlocks: ~0.5 s = 4 steps at 120 BPM.
constexpr int kLofiSyncBlocks = kBlocksPerSec / 2;
static uint32_t lofiSyncTicks(uint16_t bpm, uint8_t note, uint8_t sync, int blocks = kLofiSyncBlocks) {
  lofiInstr(0, LofiMachine::Byte);
  p->instruments[0].lofiSync = sync;
  p->bpm = bpm;
  noteOn(0, 0, note);
  for (int k = 0; k < blocks; ++k) s->render(buf);
  return const_cast<Voice&>(s->voice(s->trackVoice(0))).lv().tick();
}

void test_lofi_sync_step_is_1024() { TEST_ASSERT_UINT32_WITHIN(20, 4096, lofiSyncTicks(120, 60, 1)); }

void test_lofi_sync_octave_up() { TEST_ASSERT_UINT32_WITHIN(40, 8192, lofiSyncTicks(120, 72, 1)); }

void test_lofi_sync_octave_down() { TEST_ASSERT_UINT32_WITHIN(20, 2048, lofiSyncTicks(120, 59, 1)); }

void test_lofi_sync_ignores_semitones() { TEST_ASSERT_UINT32_WITHIN(20, 4096, lofiSyncTicks(120, 61, 1)); }

void test_lofi_sync_half_tempo() { TEST_ASSERT_UINT32_WITHIN(20, 2048, lofiSyncTicks(60, 60, 1)); }

void test_lofi_sync_off_follows_pitch() {
  // C#4 free: 8 kHz x 2^(1/12) for 0.5 s.
  TEST_ASSERT_UINT32_WITHIN(20, 4238, lofiSyncTicks(120, 61, 0));
}

void test_lofi_sync_follows_tempo_change() {
  const uint32_t t0 = lofiSyncTicks(120, 60, 1);
  p->bpm = 60;
  for (int k = 0; k < kLofiSyncBlocks; ++k) s->render(buf);
  TEST_ASSERT_UINT32_WITHIN(30, t0 + 2048, const_cast<Voice&>(s->voice(s->trackVoice(0))).lv().tick());
}

void test_lofi_profile_stage_name() { TEST_ASSERT_EQUAL_STRING("lofi", Synth::profName(Synth::kProfLofi)); }

void test_synth_mac_lock_applies() {
  // fx DCY on a SYNTH step locks SHP1 (macro 0): the lock reaches the voice.
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  send(0, 0, 0xF5, static_cast<uint8_t>(Fx::DCY), 100);
  noteOn(0, 0, 60);
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_TRUE(v.lockMask & (1 << kMacShp1));
  TEST_ASSERT_EQUAL(100, v.lock[kMacShp1]);
}

void test_synth_mac_lock_on_empty_step_hits_sounding_voice() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  render1(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::SHP, 90);  // MIX
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(1 << kMacMix, v.lockMask);
  TEST_ASSERT_EQUAL(90, v.lock[kMacMix]);
}

// ---- LFO fx ----

static void lfo1(Instrument& m, uint8_t sync, uint8_t rate, int8_t depth) {
  m.lfoWave = static_cast<uint8_t>(LfoWave::Sine);
  m.lfoDest = static_cast<uint8_t>(LfoDest::Pitch);
  m.lfoSync = sync;
  m.lfoRate = rate;
  m.lfoDepth = depth;
}

void test_lfo_note_mode_unchanged() {
  lfo1(p->instruments[0], 0, 40, 20);
  noteOn(0, 0, 60);
  for (int k = 0; k < 50; ++k) render1(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).lfoPhase[0] > 0.01f);
  noteOff(0, 0, 60);
  noteOn(0, 0, 62);
  render1(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).lfoPhase[0] < 0.01f);
}

void test_lfo_depth_lock_turns_lfo_on() {
  lfo1(p->instruments[0], 0, 40, 0);  // off in the instrument
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::LFD, 30);
  noteOn(0, 0, 60);
  for (int k = 0; k < 10; ++k) render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL_HEX16(1u << kLfoLkDepth, v.lfoLockMask);
  TEST_ASSERT_EQUAL(30, v.lfoLock[0].depth);
  TEST_ASSERT_TRUE(v.lfoPhase[0] > 0);  // running
}

void test_lfo_select_targets_lfo_2() {
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::LFO, 2);
  fx(0, 0, Fx::LFS, 99);
  fx(0, 0, Fx::LFT, static_cast<uint8_t>(LfoDest::Cutoff));
  noteOn(0, 0, 60);
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL_HEX16((1u << (kLfoLocks + kLfoLkRate)) | (1u << (kLfoLocks + kLfoLkDest)), v.lfoLockMask);
  TEST_ASSERT_EQUAL(99, v.lfoLock[1].rate);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Cutoff), v.lfoLock[1].dest);
  // The next step starts without a selection: LFO 1 again.
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::LFD, 10);
  noteOn(0, 0, 62);
  render1(buf);
  TEST_ASSERT_EQUAL_HEX16(1u << kLfoLkDepth, s->voice(s->trackVoice(0)).lfoLockMask);
}

void test_lfo_rate_lock_changes_speed() {
  lfo1(p->instruments[0], 0, 40, 20);
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::LFS, 80);
  noteOn(0, 0, 60);
  for (int k = 0; k < 10; ++k) render1(buf);
  const float n = 10.f * Synth::kBlock - Synth::kControl;  // the first update precedes the note
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, lfoHz(80) * n / kSynthRate, s->voice(s->trackVoice(0)).lfoPhase[0]);
}

void test_lfo_reset_fx_sets_phase() {
  lfo1(p->instruments[0], 0, 40, 20);
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::LFR, 0x80);
  noteOn(0, 0, 60);
  render1(buf);
  const float adv = lfoHz(40) * (Synth::kBlock - Synth::kControl) / kSynthRate;
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f + adv, s->voice(s->trackVoice(0)).lfoPhase[0]);
  // Retrig OFF: the instrument's shared phase, on a step without a note too.
  lfo1(p->instruments[0], kLfoFree, 40, 20);
  for (int k = 0; k < 20; ++k) render1(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::LFR, 0x40);
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.25f + adv, s->lfoPhase(s->trackVoice(0), 0));
}

void test_lfo_lock_on_empty_step_hits_sounding_voice() {
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  render1(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::LFO, 3);
  fx(0, 0, Fx::LFW, static_cast<uint8_t>(LfoWave::Square));
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL_HEX16(1u << (2 * kLfoLocks + kLfoLkWave), v.lfoLockMask);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoWave::Square), v.lfoLock[2].wave);
}

void test_lfo_wave_lock_sounds() {
  // A square LFO on the pitch locked over a sine of the instrument: +depth semitones in the first half.
  Instrument& m = p->instruments[0];
  lfo1(m, 0, 0, 12);  // 0.05 Hz: the first half covers the test; depth 12 = 2.25 semitones
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::LFW, static_cast<uint8_t>(LfoWave::Square));
  noteOn(0, 0, 69);
  for (int k = 0; k < 4; ++k) render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 69.f + 12.f * 12.f / 64.f, voiceSemis(0));
}

// ---- KIT: lanes ----

static FakeBank* kitSetup(int track, int kitIdx) {
  static FakeBank bank;
  for (auto& x : bank.data) x = 8192;
  s->setBank(&bank);
  Instrument& k = p->instruments[kitIdx];
  instrSetType(k, InstrType::Kit);
  k.send = 50;
  for (int l = 0; l < kKitLanes; ++l) strcpy(k.kit[l].sample, "kick");
  k.kit[0].vol = 80;
  k.kit[0].pitch = 5;
  k.kit[0].decay = 30;
  p->tracks[track].out = TrackOut::Int;
  p->tracks[track].instr = static_cast<uint8_t>(kitIdx);
  return &bank;
}
static int onlyVoice() {
  int f = -1;
  for (int v = 0; v < kVoices; ++v)
    if (s->voice(v).on) f = v;
  return f;
}
static int trackVoices(int track) {
  int n = 0;
  for (int v = 0; v < kVoices; ++v) n += s->voice(v).on && s->voice(v).track == track;
  return n;
}

void test_kit_sampler_lane_builds_scratch_instrument() {
  kitSetup(0, 0);
  noteOn(0, 0, 60, 100);
  render1(buf);
  const int v = onlyVoice();
  TEST_ASSERT_TRUE(v >= 0);
  const Voice& x = s->voice(v);
  const Instrument& li = s->voiceInstrument(v);
  TEST_ASSERT_TRUE(x.lane);
  TEST_ASSERT_TRUE(x.sample);
  TEST_ASSERT_EQUAL(0, x.instr);
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Sample), static_cast<int>(li.type));
  TEST_ASSERT_EQUAL_STRING("kick", li.sample);
  TEST_ASSERT_EQUAL(60, li.root);
  TEST_ASSERT_EQUAL(5, li.transpose);
  TEST_ASSERT_EQUAL(80, li.vol);
  TEST_ASSERT_EQUAL(30, li.decay);
  TEST_ASSERT_EQUAL(0, li.sustain);
  TEST_ASSERT_EQUAL(50, li.send);
  TEST_ASSERT_EQUAL(0, li.fltMode);
  TEST_ASSERT_EQUAL(0, li.sliceCount);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.f, x.pitch);  // lane note + pitch
}

void test_kit_decay_zero_is_one_shot() {
  kitSetup(0, 0);
  noteOn(0, 0, 61, 100);  // lane 2: decay 0
  render1(buf);
  const Voice& x = s->voice(onlyVoice());
  const Instrument& li = s->voiceInstrument(onlyVoice());
  TEST_ASSERT_EQUAL(0, li.decay);
  TEST_ASSERT_EQUAL(127, li.sustain);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 61.f, x.pitch);  // the sample at its own pitch
}

void test_kit_missing_sample_silent() {
  kitSetup(0, 0);
  strcpy(p->instruments[0].kit[0].sample, "nope");
  noteOn(0, 0, 60, 100);
  render1(buf);
  TEST_ASSERT_EQUAL(-1, onlyVoice());
  p->instruments[0].kit[1].sample[0] = 0;  // empty lane
  noteOn(0, 0, 61, 100);
  render1(buf);
  TEST_ASSERT_EQUAL(-1, onlyVoice());
}

void test_kit_inst_lane_plays_instrument() {
  kitSetup(0, 0);
  p->instruments[0].kit[2].instr = 5;
  noteOn(0, 0, 62, 100);
  render1(buf);
  const Voice& x = s->voice(onlyVoice());
  TEST_ASSERT_FALSE(x.lane);
  TEST_ASSERT_FALSE(x.sample);
  TEST_ASSERT_EQUAL(5, x.instr);
  TEST_ASSERT_EQUAL(62, x.note);
}

// A KIT lane note is priced by its lane: a sampler lane as a sample, an instrument lane by that
// instrument (not by the KIT).
void test_kit_lane_voice_cost() {
  kitSetup(0, 0);
  Instrument& w = p->instruments[5];
  instrSetType(w, InstrType::Synth);
  w.sustain = 127;
  w.synOsc[0] = static_cast<uint8_t>(SynOsc::Wt);
  w.fltMode = static_cast<uint8_t>(FltMode::Ladder);
  p->instruments[0].kit[1].instr = 5;
  noteOn(0, 0, 60, 100);  // lane 0: sampler
  noteOn(0, 0, 61, 100);  // lane 1: instrument 5
  render1(buf);
  int seen = 0;
  for (int v = 0; v < kVoices; ++v) {
    const Voice& x = s->voice(v);
    if (!x.on) continue;
    ++seen;
    TEST_ASSERT_EQUAL(x.laneIdx == 0 ? kCostSample : kCostSynWt + kCostLadder, x.cost);
  }
  TEST_ASSERT_EQUAL(2, seen);
}

void test_kit_in_kit_lane_silent() {
  kitSetup(0, 0);
  instrSetType(p->instruments[3], InstrType::Kit);
  p->instruments[0].kit[2].instr = 3;
  noteOn(0, 0, 62, 100);
  render1(buf);
  TEST_ASSERT_EQUAL(-1, onlyVoice());
}

void test_kit_eight_lanes_sound_and_rehit_chokes() {
  kitSetup(0, 0);
  for (int l = 0; l < kKitLanes; ++l) noteOn(0, 0, static_cast<uint8_t>(60 + l), 100);
  render1(buf);
  TEST_ASSERT_EQUAL(8, trackVoices(0));
  int lane0 = -1;
  for (int v = 0; v < kVoices; ++v)
    if (s->voice(v).on && s->voice(v).note == 60) lane0 = v;
  noteOn(0, 0, 60, 100);
  render1(buf);
  TEST_ASSERT_EQUAL(8, trackVoices(0));
  int again = -1;
  for (int v = 0; v < kVoices; ++v)
    if (s->voice(v).on && s->voice(v).note == 60) again = v;
  TEST_ASSERT_EQUAL(lane0, again);  // the lane's own voice, restarted
}

void test_kit_unknown_note_silent() {
  kitSetup(0, 0);
  noteOn(0, 0, 40, 100);
  render1(buf);
  TEST_ASSERT_EQUAL(-1, onlyVoice());
}

// ---- sound fx: drive, reverb, compressor, ARM, velocity ----

static float noteRms(uint8_t note, uint8_t vel, int blocks) {
  s->reset();
  noteOn(0, 0, note, vel);
  return rmsBlocks(blocks);
}

void test_drive_zero_is_bit_identical() {
  noteOn(0, 0, 60);
  int16_t ref[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) render1(ref + k * Synth::kBlock);
  s->reset();
  p->tracks[0].drive = 0;
  p->instruments[0].rsend = 0;
  p->instruments[0].velCut = 63;  // no filter: nothing to move
  noteOn(0, 0, 60);
  int16_t got[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) render1(got + k * Synth::kBlock);
  TEST_ASSERT_EQUAL_MEMORY(ref, got, sizeof(ref));
}

void test_drive_changes_waveform() {
  p->instruments[0].vol = 60;
  const float clean = noteRms(60, 100, 20);
  p->tracks[0].drive = 127;
  const float driven = noteRms(60, 100, 20);
  TEST_ASSERT_TRUE(s->strip(0).drive.on());
  TEST_ASSERT_TRUE(driven > clean * 1.3f);  // the saw squares up
}

void test_drv_lock_on_note_step() {
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::DRV, 127);
  noteOn(0, 0, 60);
  noteOn(0, 1, 60);
  render1(buf);
  TEST_ASSERT_TRUE(s->strip(0).drive.on());
  TEST_ASSERT_FALSE(s->strip(1).drive.on());
  stepStart(0, 0, 24, false);  // without a note: the sounding voice
  fx(0, 0, Fx::DRV, 0);
  render1(buf);
  TEST_ASSERT_FALSE(s->strip(0).drive.on());
}

void test_lfo_drive_dest() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Drive);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoDepth = 63;
  m.lfoRate = 100;
  noteOn(0, 0, 60);
  bool on = false, off = false;
  for (int k = 0; k < kBlocksPerSec; ++k) {
    render1(buf);
    const bool d = s->strip(0).drive.on();
    on |= d;
    off |= !d;
  }
  TEST_ASSERT_TRUE(on && off);
}

// ---- track strip: insert, fader, sends ----

// Two notes on one driven track distort as a sum (intermodulation): not the same as each on its own
// driven track.
void test_strip_drive_acts_on_track_sum() {
  p->tracks[0].drive = p->tracks[1].drive = 127;
  p->tracks[0].vol = p->tracks[1].vol = 40;
  noteOn(0, 0, 60);
  noteOn(0, 0, 67);
  int16_t one[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) s->render(one + k * Synth::kBlock);
  s->reset();
  noteOn(0, 0, 60);
  noteOn(0, 1, 67);
  int16_t two[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) s->render(two + k * Synth::kBlock);
  long diff = 0;
  for (int i = 0; i < Synth::kBlock * 4; ++i) diff += abs(one[i] - two[i]);
  TEST_ASSERT_TRUE(diff > 1000);
}

// The fader comes after the drive: turning the track down scales the driven signal linearly
// (before, the voice amp fed the drive and a quieter track distorted less).
void test_strip_fader_after_drive() {
  p->tracks[0].drive = 127;
  p->instruments[0].vol = 60;
  p->tracks[0].vol = 40;
  const float loud = noteRms(60, 100, 20);
  p->tracks[0].vol = 10;
  const float quiet = noteRms(60, 100, 20);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.f / 40.f, quiet / loud);
}

void test_strip_fader_in_pan_gains() {
  p->tracks[0].vol = 64;
  noteOn(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL_FLOAT(64.f / 127.f, s->strip(0).gl);
  TEST_ASSERT_EQUAL_FLOAT(64.f / 127.f, s->strip(0).gr);
}

void test_strip_crush_from_track() {
  p->tracks[2].crushBits = 100;
  noteOn(0, 2, 60);
  noteOn(0, 3, 60);
  s->render(buf);
  TEST_ASSERT_TRUE(s->strip(2).crush.on());
  TEST_ASSERT_FALSE(s->strip(3).crush.on());
  stepStart(0, 2, 24, false);  // a step without a note locks the sounding voice: off
  fx(0, 2, Fx::BIT, 0);
  s->render(buf);
  TEST_ASSERT_FALSE(s->strip(2).crush.on());
}

// The instrument's own drive / crush no longer sound: only the track's.
void test_strip_ignores_instrument_drive() {
  p->instruments[0].drive = 127;
  p->instruments[0].crushBits = 127;
  noteOn(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_FALSE(s->strip(0).drive.on());
  TEST_ASSERT_FALSE(s->strip(0).crush.on());
}

// A KIT lane that plays another instrument sends with the KIT's levels: one send for the track.
void test_strip_kit_shares_kit_send() {
  kitSetup(0, 0);
  Instrument& k = p->instruments[0];
  k.send = 0;
  k.rsend = 64;
  k.kit[1].instr = 5;
  k.kit[1].note = 61;
  p->instruments[5].send = 127;
  p->instruments[5].rsend = 0;
  noteOn(0, 0, 61);
  s->render(buf);
  TEST_ASSERT_EQUAL_FLOAT(0.f, s->strip(0).send);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 64.f / 127.f, s->strip(0).rsend);
}

// The preview track plays through the insert 0xF7 names; kTracks: none.
void test_strip_preview_insert() {
  p->tracks[3].drive = 100;
  send(0, kPreviewTrack, 0xF7, 3, 0, 2);
  noteOn(0, kPreviewTrack, 60);
  s->render(buf);
  TEST_ASSERT_TRUE(s->strip(kPreviewTrack).drive.on());
  send(0, kPreviewTrack, 0xF7, kTracks, 0, 2);
  s->render(buf);
  s->render(buf);
  TEST_ASSERT_FALSE(s->strip(kPreviewTrack).drive.on());
}

// ---- drive models (TrackCfg::driveModel, fx DTY) ----

void test_strip_drive_model_from_track() {
  p->instruments[0].vol = 60;
  p->tracks[0].drive = 100;
  const float tanhRms = noteRms(60, 100, 20);
  TEST_ASSERT_EQUAL(ModelDrive::kTanh, s->strip(0).drive.model());
  p->tracks[0].driveModel = static_cast<uint8_t>(DriveModel::Fold);
  const float foldRms = noteRms(60, 100, 20);
  TEST_ASSERT_EQUAL(ModelDrive::kFold, s->strip(0).drive.model());
  TEST_ASSERT_TRUE(fabsf(foldRms - tanhRms) > tanhRms * 0.02f);
  p->tracks[0].driveModel = 77;  // unknown: TANH
  noteRms(60, 100, 2);
  TEST_ASSERT_EQUAL(ModelDrive::kTanh, s->strip(0).drive.model());
}

// DTY locks this step's note-ons (or the sounding voices on a step without a note); the next note
// without the lock goes back to the track's model.
void test_dty_lock_then_revert() {
  p->tracks[0].drive = p->tracks[1].drive = 90;
  p->tracks[0].driveModel = p->tracks[1].driveModel = static_cast<uint8_t>(DriveModel::Asym);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::DTY, static_cast<uint8_t>(DriveModel::Clip));
  noteOn(0, 0, 60);
  noteOn(0, 1, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL(ModelDrive::kClip, s->strip(0).drive.model());
  TEST_ASSERT_EQUAL(ModelDrive::kAsym, s->strip(1).drive.model());
  stepStart(0, 0, 24, false);  // without a note: the sounding voice
  fx(0, 0, Fx::DTY, static_cast<uint8_t>(DriveModel::Rect));
  s->render(buf);
  TEST_ASSERT_EQUAL(ModelDrive::kRect, s->strip(0).drive.model());
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 64);  // the newest voice, no lock
  s->render(buf);
  TEST_ASSERT_EQUAL(ModelDrive::kAsym, s->strip(0).drive.model());
}

// The preview track takes the model of the track 0xF7 names.
void test_strip_preview_drive_model() {
  p->tracks[3].drive = 100;
  p->tracks[3].driveModel = static_cast<uint8_t>(DriveModel::Rect);
  send(0, kPreviewTrack, 0xF7, 3, 0, 2);
  noteOn(0, kPreviewTrack, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL(ModelDrive::kRect, s->strip(kPreviewTrack).drive.model());
}

// The newest voice's DLY lock is the track's send; an older voice's lock is not.
void test_strip_send_from_newest_voice() {
  p->instruments[0].send = 20;
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::DLY, 127);
  noteOn(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, s->strip(0).send);
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 64);  // newer, no lock
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.f / 127.f, s->strip(0).send);
}

// LFO 1 on dest: square, full depth, fast; returns {min, max} of f(voice) over one second.
template <class F>
static void lfoSweep(LfoDest dest, F f, float& lo, float& hi) {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(dest);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoDepth = 63;
  m.lfoRate = 100;
  noteOn(0, 0, 60);
  lo = 1e9f;
  hi = -1e9f;
  for (int k = 0; k < kBlocksPerSec; ++k) {
    s->render(buf);
    const float x = f(s->voice(s->trackVoice(0)));
    lo = x < lo ? x : lo;
    hi = x > hi ? x : hi;
  }
}

void test_lfo_send_dests() {
  p->instruments[0].send = 64;
  float lo, hi;
  lfoSweep(LfoDest::Dly, [](const Voice& v) { return v.send; }, lo, hi);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.f, hi);  // 64 + 63 -> 127
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.f, lo);  // 64 - 63 -> ~0
}

void test_lfo_rsend_dest() {
  float lo, hi;
  lfoSweep(LfoDest::Rvb, [](const Voice& v) { return v.rsend; }, lo, hi);
  TEST_ASSERT_EQUAL_FLOAT(0.f, lo);  // clamped at 0
  TEST_ASSERT_TRUE(hi > 0.4f);
}

void test_lfo_crush_dests() {
  float lo, hi;
  lfoSweep(LfoDest::Bit, [](const Voice& v) { return v.bits ? 1.f : 0.f; }, lo, hi);
  TEST_ASSERT_TRUE(lo == 0 && hi == 1);
  tearDown();
  setUp();
  lfoSweep(LfoDest::Srr, [](const Voice& v) { return v.srr ? 1.f : 0.f; }, lo, hi);
  TEST_ASSERT_TRUE(lo == 0 && hi == 1);
}

void test_lfo_reso_dest() {
  p->instruments[0].fltMode = static_cast<uint8_t>(FltMode::Lp);
  p->instruments[0].reso = 64;
  float lo, hi;
  lfoSweep(LfoDest::Reso, [](const Voice& v) { return v.fltRes; }, lo, hi);
  TEST_ASSERT_FLOAT_WITHIN(1.f, 1.f, lo);
  TEST_ASSERT_FLOAT_WITHIN(1.f, 127.f, hi);
}

void test_lfo_fenv_dest() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 40;
  m.fDec = 0;  // hold: the envelope stays at 1
  float lo, hi;
  lfoSweep(LfoDest::Fenv, [](const Voice& v) { return v.octHz; }, lo, hi);
  TEST_ASSERT_TRUE(hi > lo * 16);  // fenv +-63 -> about +-6 octaves
}

void test_lfo_duty_dest() {
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Pulse);
  float lo, hi;
  lfoSweep(LfoDest::Duty, [](const Voice& v) { return v.duty; }, lo, hi);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.01f, lo);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.99f, hi);
}

void test_lfo_fine_dest() {
  float lo, hi;
  lfoSweep(LfoDest::Fine, [](const Voice& v) { return v.hzKey[0]; }, lo, hi);  // CHIP note incl. LFO
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 59.f, lo);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 61.f, hi);
}

constexpr int kLfoSynBlocks = 50;
static int16_t la_[Synth::kBlock * kLfoSynBlocks], lb_[Synth::kBlock * kLfoSynBlocks];

static void renderInto(int16_t* a, uint8_t note) {
  noteOn(0, 0, note);
  for (int k = 0; k < kLfoSynBlocks; ++k) s->render(a + k * Synth::kBlock);
}

// SYNTH (or the current type) with LFO 1 on dest at depth 0 into la_, then at full depth into lb_.
static void renderLfoAB(LfoDest dest) {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(dest);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 100;
  m.lfoDepth = 0;
  renderInto(la_, 57);
  s->reset();
  m.lfoDepth = 63;
  renderInto(lb_, 57);
}

static bool synDiffers() {
  long d = 0, a = 0;
  for (int i = 0; i < Synth::kBlock * kLfoSynBlocks; ++i) {
    d += abs(la_[i] - lb_[i]);
    a += abs(la_[i]);
  }
  return a > 0 && d > a / 10;
}

void test_lfo_syn_sub_dest() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  m.synSub = 0;
  renderLfoAB(LfoDest::Sub);
  TEST_ASSERT_TRUE(synDiffers());
}

void test_lfo_syn_noise_dest() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  m.synNoise = 0;
  renderLfoAB(LfoDest::Noise);
  TEST_ASSERT_TRUE(synDiffers());
}

void test_lfo_syn_semi2_dest() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  m.macro[kMacMix] = 127;  // osc 2 only
  m.lfoDest = static_cast<uint8_t>(LfoDest::Semi2);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 100;
  noteOn(0, 0, 57);
  const int plain = crossings(kBlocksPerSec);
  s->reset();
  m.lfoDepth = 63;
  noteOn(0, 0, 57);
  const int c = crossings(kBlocksPerSec);
  TEST_ASSERT_TRUE(abs(c - plain) > plain / 10);
}

void test_lfo_syn_dests_ignored_on_chip() {
  renderLfoAB(LfoDest::Sub);  // CHIP: SUB is not a target there
  TEST_ASSERT_EQUAL_INT16_ARRAY(la_, lb_, Synth::kBlock * kLfoSynBlocks);
}

static float rvbBuf[Reverb::kBufLen];

void test_rsend_zero_same_with_and_without_buffer() {
  noteOn(0, 0, 60);
  int16_t ref[Synth::kBlock * 8];
  for (int k = 0; k < 8; ++k) render1(ref + k * Synth::kBlock);
  s->reset();
  s->setReverbBuffer(rvbBuf, Reverb::kBufLen);
  noteOn(0, 0, 60);
  int16_t got[Synth::kBlock * 8];
  for (int k = 0; k < 8; ++k) render1(got + k * Synth::kBlock);
  TEST_ASSERT_EQUAL_MEMORY(ref, got, sizeof(ref));
}

static int tailSamples(bool withBuffer) {
  s->reset();
  s->setReverbBuffer(withBuffer ? rvbBuf : nullptr, Reverb::kBufLen);
  p->instruments[0].rsend = 127;
  noteOn(0, 0, 60);
  renderMs(40);
  noteOff(0, 0, 60);  // release 0: the dry note stops at once
  renderMs(20);
  int nz = 0;
  for (int k = 0; k < 75; ++k) {  // 300 ms
    render1(buf);
    for (int i = 0; i < Synth::kBlock; ++i) nz += buf[i] != 0;
  }
  return nz;
}

void test_rsend_makes_tail() {
  TEST_ASSERT_TRUE(tailSamples(true) > 1000);
  TEST_ASSERT_EQUAL(0, tailSamples(false));
}

void test_rvb_lock_on_note_step() {
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::RVB, 127);
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.f, s->voice(s->trackVoice(0)).rsend);
  stepStart(0, 0, 24, true);  // a new step drops the lock
  noteOn(0, 0, 62);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.f, s->voice(s->trackVoice(0)).rsend);
}

// Track 1 holds a quiet note, track 2 hits hard from block 40: 10 blocks of output during the hits,
// with either track muted (instrument volume 0).
static void sidechainRun(bool pad, bool kick, int16_t (&out)[Synth::kBlock * 10]) {
  s->reset();
  p->compAmt = 100;
  p->scTrack = 2;
  p->scDepth = 127;
  p->instruments[0].vol = pad ? 30 : 0;
  p->instruments[1].vol = kick ? 127 : 0;
  noteOn(0, 0, 48);
  for (int k = 0; k < 40; ++k) render1(buf);
  noteOn(0, 1, 36);
  for (int k = 0; k < 10; ++k) render1(out + k * Synth::kBlock);
}

// The pad under the kick (output minus the kick alone) is ducked against the pad alone.
void test_comp_sidechain_track() {
  static int16_t both[Synth::kBlock * 10], kick[Synth::kBlock * 10], pad[Synth::kBlock * 10];
  sidechainRun(true, true, both);
  sidechainRun(false, true, kick);
  sidechainRun(true, false, pad);
  double ducked = 0, plain = 0;
  for (int i = 0; i < Synth::kBlock * 10; ++i) {
    const double d = both[i] - kick[i];
    ducked += d * d;
    plain += double(pad[i]) * pad[i];
  }
  TEST_ASSERT_TRUE(plain > 0);
  TEST_ASSERT_TRUE(sqrt(ducked) < sqrt(plain) * 0.7);
}

// The key track itself goes around the compressor: a lone loud note on the SC track sounds the
// same with the compressor on as off (only the other tracks duck).
static float keyTrackRms(uint8_t compAmt) {
  s->reset();
  p->compAmt = compAmt;
  p->scTrack = 2;
  p->scDepth = 127;
  p->instruments[1].vol = 127;
  noteOn(0, 1, 36);
  return rmsBlocks(10);
}

void test_comp_sidechain_key_track_not_squashed() {
  const float dry = keyTrackRms(0);
  const float comp = keyTrackRms(100);
  TEST_ASSERT_FLOAT_WITHIN(dry * 0.05f, dry, comp);
}

void test_comp_off_is_bit_identical() {
  p->scTrack = 1;
  p->scDepth = 127;  // ignored with compAmt 0
  noteOn(0, 0, 60);
  int16_t got[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) render1(got + k * Synth::kBlock);
  s->reset();
  p->scTrack = 0;
  noteOn(0, 0, 60);
  int16_t ref[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) render1(ref + k * Synth::kBlock);
  TEST_ASSERT_EQUAL_MEMORY(ref, got, sizeof(ref));
}

// ARP 0x47 (0, +4, +7), ARM `arm`: offsets at the middle of the 4 slots of one 120 ms step.
static void arpOffsets(uint8_t arm, int (&got)[4], int chord = -1) {
  s->reset();
  p->bpm = 125;  // 24 ticks = 120 ms: 4 slots of 30 ms
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::ARP, 0x47);
  fx(0, 0, Fx::ARM, arm);
  if (chord >= 0) send(0, 0, 0xF5, kSynthArpChord, static_cast<uint8_t>(chord));
  noteOn(0, 0, 60);
  for (int k = 0, done = 0; k < 4; ++k) {
    for (; done < blocksMs(15 + 30 * k); ++done) render1(buf);  // 15, 45, 75, 105 ms (whole blocks)
    got[k] = s->voice(s->trackVoice(0)).arpOff;
  }
}

void test_arm_up_down_updown() {
  int g[4];
  arpOffsets(0x04, g);  // UP x4
  TEST_ASSERT_EQUAL(0, g[0]);
  TEST_ASSERT_EQUAL(4, g[1]);
  TEST_ASSERT_EQUAL(7, g[2]);
  TEST_ASSERT_EQUAL(0, g[3]);
  arpOffsets(0x14, g);  // DOWN: from the top
  TEST_ASSERT_EQUAL(7, g[0]);
  TEST_ASSERT_EQUAL(4, g[1]);
  TEST_ASSERT_EQUAL(0, g[2]);
  TEST_ASSERT_EQUAL(7, g[3]);
  arpOffsets(0x24, g);  // UPDOWN: the ends once
  TEST_ASSERT_EQUAL(0, g[0]);
  TEST_ASSERT_EQUAL(4, g[1]);
  TEST_ASSERT_EQUAL(7, g[2]);
  TEST_ASSERT_EQUAL(4, g[3]);
}

void test_arm_random_stays_in_set() {
  int g[4];
  for (int r = 0; r < 5; ++r) {
    arpOffsets(0x34, g);
    for (int x : g) TEST_ASSERT_TRUE(x == 0 || x == 4 || x == 7);
  }
}

void test_arp_chord_cycles_chord_notes() {
  int g[4];
  p->scaleType = static_cast<uint8_t>(ScaleType::Minor);  // C minor: the triad of C is 0, 3, 7
  arpOffsets(0x04, g, kChordTriad);
  TEST_ASSERT_EQUAL(0, g[0]);
  TEST_ASSERT_EQUAL(3, g[1]);
  TEST_ASSERT_EQUAL(7, g[2]);
  TEST_ASSERT_EQUAL(0, g[3]);
  arpOffsets(0x04, g);  // a new step without the chord: back to 0, x, y
  TEST_ASSERT_EQUAL(4, g[1]);
}

// RMS ratio of a velocity 127 note over a velocity 64 note (the same filtered saw).
static float velRatio(int8_t velCut) {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 50;
  m.velCut = velCut;
  m.vol = 60;
  const float hi = noteRms(48, 127, 20);
  const float lo = noteRms(48, 64, 20);
  return hi / lo;
}

void test_velcut_opens_filter_with_velocity() {
  const float plain = velRatio(0);  // the gain alone: ~127 / 64
  TEST_ASSERT_FLOAT_WITHIN(0.15f, 127.f / 64.f, plain);
  TEST_ASSERT_TRUE(velRatio(40) > plain * 1.3f);  // brighter at velocity 127 too
}

void test_velmac_shifts_decay() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Drum);
  m.macro[kMacDec] = 40;
  m.velMac = 0;
  noteOn(0, 0, 60, 127);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.6f, 40.f, s->voice(s->trackVoice(0)).fpMac[kMacDec]);
  s->reset();
  m.velMac = 63;
  noteOn(0, 0, 60, 127);
  render1(buf);
  // The DECAY the voice used went up by 63 x 63 / 64 = 62 macro units: its params cache key shows it.
  TEST_ASSERT_FLOAT_WITHIN(0.6f, 40.f + 62.f, s->voice(s->trackVoice(0)).fpMac[kMacDec]);
  s->reset();
  noteOn(0, 0, 60, 1);  // velocity 1: down by 63 x 63 / 64, clamped at 0
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.6f, 0.f, s->voice(s->trackVoice(0)).fpMac[kMacDec]);
}

// ---- pan / stereo ----

static int16_t lr[2 * Synth::kBlock];

// Sum of squares of channel ch (0 = L, 1 = R) in lr.
static double chanEnergy(int ch) {
  double e = 0;
  for (int i = 0; i < Synth::kBlock; ++i) e += double(lr[2 * i + ch]) * lr[2 * i + ch];
  return e;
}

// Centre pan: both channels carry the old mono mix, bit-exact (CHIP, FM and SYNTH voices; DRUM
// output differs between two Synth instances even in mono, so it is checked for L == R below).
void test_stereo_centre_equals_mono() {
  useFm(1, FmMachine::Tone);
  instrSetType(p->instruments[2], InstrType::Synth);
  Synth mono(*p);
  auto hit = [](Synth& x) {
    const uint8_t a[3] = {0x90, 60, 127}, b[3] = {0x90, 64, 100}, c[3] = {0x90, 67, 90};
    x.event(0, 0, a, 3);
    x.event(10, 1, b, 3);
    x.event(50, 2, c, 3);
  };
  hit(mono);
  hit(*s);
  long energy = 0;
  for (int k = 0; k < 40; ++k) {
    s->renderStereo(lr);
    mono.render(buf);
    for (int i = 0; i < Synth::kBlock; ++i) {
      TEST_ASSERT_EQUAL_INT16(lr[2 * i], lr[2 * i + 1]);
      TEST_ASSERT_EQUAL_INT16(buf[i], lr[2 * i]);
      energy += abs(buf[i]);
    }
  }
  TEST_ASSERT_TRUE(energy > 0);
}

void test_stereo_centre_drum_sides_equal() {
  drumInstr(0, DrumMachine::Bd8);
  drumInstr(1, DrumMachine::Hh8);
  noteOn(0, 0, 36);
  noteOn(20, 1, 42);
  long energy = 0;
  for (int k = 0; k < 40; ++k) {
    s->renderStereo(lr);
    for (int i = 0; i < Synth::kBlock; ++i) {
      TEST_ASSERT_EQUAL_INT16(lr[2 * i], lr[2 * i + 1]);
      energy += abs(lr[2 * i]);
    }
  }
  TEST_ASSERT_TRUE(energy > 0);
}

void test_pan_hard_left_silences_right() {
  p->tracks[0].pan = 0;
  noteOn(0, 0, 60);
  for (int k = 0; k < 10; ++k) {
    s->renderStereo(lr);
    TEST_ASSERT_TRUE(chanEnergy(1) == 0);
    TEST_ASSERT_TRUE(chanEnergy(0) > 0);
  }
}

void test_pan_hard_right_silences_left() {
  p->tracks[0].pan = 127;
  noteOn(0, 0, 60);
  for (int k = 0; k < 10; ++k) {
    s->renderStereo(lr);
    TEST_ASSERT_TRUE(chanEnergy(0) == 0);
    TEST_ASSERT_TRUE(chanEnergy(1) > 0);
  }
}

// Constant power x sqrt 2: hard left is sqrt 2 x the centre level on L (quiet: the soft clip stays
// linear enough).
void test_pan_hard_left_constant_power() {
  p->tracks[0].vol = 20;
  noteOn(0, 0, 60);
  s->renderStereo(lr);
  s->renderStereo(lr);
  const double c = chanEnergy(0);
  s->reset();
  p->tracks[0].pan = 0;
  noteOn(0, 0, 60);
  s->renderStereo(lr);
  s->renderStereo(lr);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 2.f, static_cast<float>(chanEnergy(0) / c));
}

void test_fx_pan_lock_on_note_step() {
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::PAN, 0);
  noteOn(0, 0, 60);
  for (int k = 0; k < 4; ++k) {
    s->renderStereo(lr);
    TEST_ASSERT_TRUE(chanEnergy(1) == 0);
    TEST_ASSERT_TRUE(chanEnergy(0) > 0);
  }
  noteOff(0, 0, 60);  // release 0: gone at once
  stepStart(0, 0, 24, true);  // a new step drops the lock: centre again
  noteOn(0, 0, 62);
  s->renderStereo(lr);
  s->renderStereo(lr);
  TEST_ASSERT_TRUE(chanEnergy(0) > 0 && chanEnergy(0) == chanEnergy(1));
}

// Autopan: a fast square LFO at full depth swings the note between the sides.
void test_lfo_pan_dest_alternates() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Pan);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoDepth = 63;
  m.lfoRate = 100;
  noteOn(0, 0, 60);
  int left = 0, right = 0;
  for (int k = 0; k < kBlocksPerSec; ++k) {
    s->renderStereo(lr);
    const double l = chanEnergy(0), r = chanEnergy(1);
    if (l > 10 * r) ++left;
    if (r > 10 * l) ++right;
  }
  TEST_ASSERT_TRUE(left > 10);
  TEST_ASSERT_TRUE(right > 10);
}

// The gains ramp across a control segment: moving a sustained note from hard left to hard right
// between blocks fades R in linearly over the next 32 samples (no step), then R carries what L did.
void test_pan_change_ramps() {
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Pulse);
  p->tracks[0].pan = 0;
  p->tracks[0].vol = 40;  // the soft clip stays near linear: R = x x gr
  static int16_t ref[Synth::kBlock * 6];  // L of the note staying hard left
  {
    Synth left(*p);
    const uint8_t on[3] = {0x90, 45, 127};
    left.event(0, 0, on, 3);
    for (int k = 0; k < 6; ++k) {
      left.renderStereo(lr);
      for (int i = 0; i < Synth::kBlock; ++i) ref[k * Synth::kBlock + i] = lr[2 * i];
    }
  }
  noteOn(0, 0, 45);
  for (int k = 0; k < 4; ++k) s->renderStereo(lr);
  TEST_ASSERT_EQUAL_INT16(0, lr[2 * Synth::kBlock - 1]);
  p->tracks[0].pan = 127;
  int amp = 0;
  for (int k = 4; k < 6; ++k) {
    s->renderStereo(lr);
    for (int i = 0; i < Synth::kBlock; ++i) {
      const int j = (k - 4) * Synth::kBlock + i;
      const float w = j < Synth::kControl ? (j + 1) / static_cast<float>(Synth::kControl) : 1.f;
      const int want = ref[k * Synth::kBlock + i];
      TEST_ASSERT_INT_WITHIN(2 + abs(want) / 100, static_cast<int>(want * w), lr[2 * i + 1]);
      amp = abs(want) > amp ? abs(want) : amp;
    }
  }
  TEST_ASSERT_TRUE(amp > 1000);
}

// A legato note with a new PAN lock keeps sounding on its voice: the gains ramp over the control
// segment instead of jumping (L fades out over 32 samples, then silent).
void test_pan_legato_lock_ramps() {
  p->instruments[0].mono = true;
  p->tracks[0].vol = 40;
  noteOn(0, 0, 45);
  s->renderStereo(lr);
  s->renderStereo(lr);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::PAN, 127);
  noteOn(0, 0, 47);  // legato: the same sounding voice
  s->renderStereo(lr);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  long head = 0;
  for (int i = 0; i < 8; ++i) head += abs(lr[2 * i]);
  TEST_ASSERT_TRUE(head > 1000);  // a jump would silence L at once
  for (int i = Synth::kControl; i < Synth::kBlock; ++i) TEST_ASSERT_EQUAL_INT16(0, lr[2 * i]);
}

// A PAN fx on a step without a note, mid control period (offset 10): the sounding note ramps over
// the 22 samples left and lands on hard left at the boundary.
void test_pan_fx_mid_segment_lands_at_boundary() {
  p->tracks[0].vol = 40;
  noteOn(0, 0, 45);
  s->renderStereo(lr);
  stepStart(10, 0, 24, false);
  fx(10, 0, Fx::PAN, 0);
  s->renderStereo(lr);
  for (int i = 0; i < 10; ++i) TEST_ASSERT_EQUAL_INT16(lr[2 * i], lr[2 * i + 1]);
  for (int i = 10; i < Synth::kControl; ++i) {  // R / L = gr / gl along the ramp
    const float w = (i - 9) / 22.f;
    const float gl = 1.f + (1.41421356f - 1.f) * w, gr = 1.f - w;
    const float want = lr[2 * i] * gr / gl;
    TEST_ASSERT_INT_WITHIN(2 + abs(lr[2 * i]) / 50, static_cast<int>(want), lr[2 * i + 1]);
  }
  long l = 0;
  for (int i = Synth::kControl; i < Synth::kBlock; ++i) {
    TEST_ASSERT_EQUAL_INT16(0, lr[2 * i + 1]);
    l += abs(lr[2 * i]);
  }
  TEST_ASSERT_TRUE(l > 0);
  const Synth::TrackStrip& st = s->strip(0);
  TEST_ASSERT_TRUE(st.gr == 0.f && st.dgl == 0.f && st.dgr == 0.f);
}

// The reverb is pseudo-stereo: a centred note gets different L and R tails.
void test_reverb_makes_sides_differ() {
  s->setReverbBuffer(rvbBuf, Reverb::kBufLen);
  p->instruments[0].rsend = 127;
  noteOn(0, 0, 60);
  long diff = 0;
  for (int k = 0; k < 40; ++k) {
    s->renderStereo(lr);
    for (int i = 0; i < Synth::kBlock; ++i) diff += abs(lr[2 * i] - lr[2 * i + 1]);
  }
  TEST_ASSERT_TRUE(diff > 1000);
}

// PERF: nothing held, a perf buffer only records: the output is bit-exact to a synth without one.
static int16_t perfBuf[2 * 32000], perfBuf2[2 * 32000];
static float rvbBuf2[Reverb::kBufLen];

static void perfSend(Synth& y, uint8_t op, uint8_t val) {
  const uint8_t m[3] = {0xF6, op, val};
  TEST_ASSERT_TRUE(y.event(0, kPreviewTrack, m, 3));
}

static void noteBoth(Synth& a, Synth& b, uint8_t type, uint8_t note) {
  const uint8_t m[3] = {type, note, static_cast<uint8_t>(type == 0x90 ? 127 : 0)};
  TEST_ASSERT_TRUE(a.event(0, 0, m, 3));
  TEST_ASSERT_TRUE(b.event(0, 0, m, 3));
}

void test_perf_idle_bit_exact() {
  Synth ref(*p);
  s->setPerfBuffer(perfBuf, 32000);
  noteBoth(*s, ref, 0x90, 48);
  int16_t a[Synth::kBlock], b[Synth::kBlock];
  for (int k = 0; k < 50; ++k) {
    if (k == 25) noteBoth(*s, ref, 0x80, 48);
    s->render(a);
    ref.render(b);
    TEST_ASSERT_EQUAL_INT16_ARRAY(b, a, Synth::kBlock);
  }
}

// 0xF6 0 CRUSH presses it on the master; 0xF6 3 releases everything: past the 64-sample fade-out
// the two synths match again.
void test_perf_message_crush() {
  Synth ref(*p);
  s->setPerfBuffer(perfBuf, 32000);
  noteBoth(*s, ref, 0x90, 48);
  perfSend(*s, 0, static_cast<uint8_t>(PerfFx::Crush));
  int16_t a[Synth::kBlock], b[Synth::kBlock];
  bool differ = false;
  for (int k = 0; k < 10; ++k) {
    s->render(a);
    ref.render(b);
    differ |= memcmp(a, b, sizeof(a)) != 0;
  }
  TEST_ASSERT_TRUE(differ);
  perfSend(*s, 3, 0);
  for (int k = 0; k < 2; ++k) {
    s->render(a);
    ref.render(b);
  }
  for (int k = 0; k < 10; ++k) {
    s->render(a);
    ref.render(b);
    TEST_ASSERT_EQUAL_INT16_ARRAY(b, a, Synth::kBlock);
  }
}

// REVERB THROW with the reverb return at 0: the throw lifts the return, so the thrown note leaves a
// tail after it ends; without the throw there is none.
static double throwTail(Synth& y, bool doThrow, int16_t* pbuf) {
  y.setPerfBuffer(pbuf, 32000);
  const uint8_t on[3] = {0x90, 60, 127}, off[3] = {0x80, 60, 0};
  TEST_ASSERT_TRUE(y.event(0, 0, on, 3));
  if (doThrow) perfSend(y, 0, static_cast<uint8_t>(PerfFx::ReverbThrow));
  int16_t o[Synth::kBlock];
  for (int k = 0; k < 10; ++k) y.render(o);  // 40 ms
  if (doThrow) perfSend(y, 1, static_cast<uint8_t>(PerfFx::ReverbThrow));
  TEST_ASSERT_TRUE(y.event(0, 0, off, 3));  // release 0: the dry note stops at once
  for (int k = 0; k < 5; ++k) y.render(o);
  double sum = 0;
  for (int k = 0; k < 50; ++k) {  // 200 ms
    y.render(o);
    for (int i = 0; i < Synth::kBlock; ++i) sum += static_cast<double>(o[i]) * o[i];
  }
  return sqrt(sum / (50.0 * Synth::kBlock));
}

void test_reverb_throw_with_level_zero() {
  p->rvbLevel = 0;
  p->instruments[0].rsend = 0;
  Synth ref(*p);
  s->setReverbBuffer(rvbBuf, Reverb::kBufLen);
  ref.setReverbBuffer(rvbBuf2, Reverb::kBufLen);
  TEST_ASSERT_TRUE(throwTail(*s, true, perfBuf) > 10.0);
  TEST_ASSERT_TRUE(throwTail(ref, false, perfBuf2) < 0.5);
}

static void renderBoth(Synth& a, Synth& b, int blocks, bool same) {
  int16_t x[Synth::kBlock], y[Synth::kBlock];
  bool differ = false;
  for (int k = 0; k < blocks; ++k) {
    a.render(x);
    b.render(y);
    if (same) TEST_ASSERT_EQUAL_INT16_ARRAY(y, x, Synth::kBlock);
    differ |= memcmp(x, y, sizeof(x)) != 0;
  }
  if (!same) TEST_ASSERT_TRUE(differ);
}

// REPEAT 1/16 at 126 BPM: a 1/16 step is kSynthRate * 15 / 126 = 5250 samples, so the output
// repeats every 5250 samples (and not every 1000).
void test_perf_repeat_loops_a_sixteenth() {
  constexpr int kSix = kSynthRate * 15 / 126;
  static_assert(kSix * 126 == kSynthRate * 15, "whole samples per 1/16");
  p->bpm = 126;
  s->setPerfBuffer(perfBuf, 32000);
  noteOn(0, 0, 48);
  noteOn(0, 1, 55);  // two notes: the dry mix is far from kSix-periodic
  renderMs(40);
  send(0, kPreviewTrack, 0xF6, 0, static_cast<uint8_t>(PerfFx::Repeat));
  constexpr int kBlocks = (11000 + kSix) / Synth::kBlock + 1;
  static int16_t o[Synth::kBlock * kBlocks];
  for (int k = 0; k < kBlocks; ++k) s->render(o + k * Synth::kBlock);
  int sameSix = 0, same1000 = 0, n = 0;
  for (int i = 1000; i < 11000; ++i, ++n) {  // o[i + kSix]: inside the rendered blocks
    sameSix += o[i] == o[i + kSix];
    same1000 += o[i] == o[i + 1000];
  }
  TEST_ASSERT_TRUE(sameSix > n * 99 / 100);
  TEST_ASSERT_TRUE(same1000 < n / 2);
}

// 0xF6 with an unknown op, effect None / out of range or a short message: nothing happens.
void test_perf_bad_messages_ignored() {
  Synth ref(*p);
  s->setPerfBuffer(perfBuf, 32000);
  noteBoth(*s, ref, 0x90, 48);
  perfSend(*s, 0, 0);    // None
  perfSend(*s, 0, 200);  // out of range
  perfSend(*s, 0, static_cast<uint8_t>(PerfFx::Count));
  perfSend(*s, 9, static_cast<uint8_t>(PerfFx::Crush));  // unknown op
  const uint8_t shortMsg[2] = {0xF6, 0};
  TEST_ASSERT_TRUE(s->event(0, kPreviewTrack, shortMsg, 2));
  renderBoth(*s, ref, 10, true);
  TEST_ASSERT_EQUAL(0, s->perf().stack().count());
}

// 0xFA restarts the PERF grid; it advances one 1/16 per kSynthRate * 15 / 120 samples at 120 BPM.
void test_perf_transport_start_resets_grid() {
  p->bpm = 120;
  for (int k = 0; k < 50; ++k) s->render(buf);
  TEST_ASSERT_TRUE(fabs(s->perf().gridPos() - 50 * 128 / (kSynthRate * 15 / 120.0)) < 1e-3);
  send(0, 0, 0xFA, 0, 0, 1);
  s->render(buf);
  TEST_ASSERT_TRUE(fabs(s->perf().gridPos() - 128 / (kSynthRate * 15 / 120.0)) < 1e-4);
  send(0, 0, 0xFC, 0, 0, 1);  // paused: it stands still
  s->render(buf);
  TEST_ASSERT_TRUE(fabs(s->perf().gridPos() - 128 / (kSynthRate * 15 / 120.0)) < 1e-4);
}

// A full queue: a PERF release (0xF6 1) or release-all (0xF6 3) evicts a control like a note-off
// (a dropped one would leave the effect stuck); a press (0xF6 0) is dropped.
void test_queue_overflow_keeps_perf_releases() {
  perfSend(*s, 0, static_cast<uint8_t>(PerfFx::Crush));
  perfSend(*s, 0, static_cast<uint8_t>(PerfFx::Gate));
  s->render(buf);
  TEST_ASSERT_EQUAL(2, s->perf().stack().count());
  for (int i = 0; i < Synth::kMaxEvents; ++i) send(10, 1, 0xE0, 0, 0x40);
  const uint8_t press[3] = {0xF6, 0, static_cast<uint8_t>(PerfFx::Filter)};
  TEST_ASSERT_FALSE(s->event(5, kPreviewTrack, press, 3));
  const uint8_t rel[3] = {0xF6, 1, static_cast<uint8_t>(PerfFx::Crush)};
  TEST_ASSERT_TRUE(s->event(5, kPreviewTrack, rel, 3));
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->perf().stack().count());
  for (int i = 0; i < Synth::kMaxEvents; ++i) send(10, 1, 0xE0, 0, 0x40);
  const uint8_t all[3] = {0xF6, 3, 0};
  TEST_ASSERT_TRUE(s->event(5, kPreviewTrack, all, 3));
  s->render(buf);
  TEST_ASSERT_EQUAL(0, s->perf().stack().count());
}

// 0xF6 4 step: the step clock re-anchors the PERF grid at the message's sample offset.
void test_perf_step_clock_reanchors_grid() {
  p->bpm = 120;  // kSynthRate * 15 / 120 samples per 1/16
  for (int k = 0; k < 50; ++k) s->render(buf);
  TEST_ASSERT_TRUE(fabs(s->perf().gridPos() - 50 * 128 / (kSynthRate * 15 / 120.0)) < 1e-3);
  send(32, 0, 0xF6, 4, 7);
  s->render(buf);
  TEST_ASSERT_TRUE(fabs(s->perf().gridPos() - (7 + (128 - 32) / (kSynthRate * 15 / 120.0))) < 1e-4);
  send(0, 0, 0xF6, 4, 70);  // mod 64
  s->render(buf);
  TEST_ASSERT_TRUE(fabs(s->perf().gridPos() - (6 + 128 / (kSynthRate * 15 / 120.0))) < 1e-4);
  TEST_ASSERT_EQUAL(0, s->perf().stack().count());
}

// The step clock with nothing held only moves the grid: off-grid clock events (swing-like offsets,
// away from the notes) leave the output bit-exact to a synth without them.
void test_perf_step_clock_idle_bit_exact() {
  p->bpm = 120;
  Synth ref(*p);
  s->setPerfBuffer(perfBuf, 32000);
  ref.setPerfBuffer(perfBuf2, 32000);
  noteBoth(*s, ref, 0x90, 48);
  int16_t a[Synth::kBlock], b[Synth::kBlock];
  for (int k = 0; k < 120; ++k) {
    if (k % 7 == 3) send(53 + k % 50, 0, 0xF6, 4, static_cast<uint8_t>(k / 7));
    if (k == 60) noteBoth(*s, ref, 0x80, 48);
    s->render(a);
    ref.render(b);
    TEST_ASSERT_EQUAL_INT16_ARRAY(b, a, Synth::kBlock);
  }
  TEST_ASSERT_TRUE(fabs(s->perf().gridPos() - ref.perf().gridPos()) > 1e-3);  // the clock moved it
}

// FREEZE ducks the live mix to freezeDry() (0.5 at the default depth), ramped; released, the mix
// is back to the reference.
void test_perf_freeze_ducks_mix() {
  Synth ref(*p);
  noteBoth(*s, ref, 0x90, 48);
  renderBoth(*s, ref, 5, true);
  perfSend(*s, 0, static_cast<uint8_t>(PerfFx::Freeze));
  int16_t x[Synth::kBlock], y[Synth::kBlock];
  s->render(x);
  ref.render(y);
  TEST_ASSERT_TRUE(abs(x[0] - y[0]) <= 1 + abs(y[0]) / 50);  // ramped in, no step
  double sx = 0, sy = 0;
  for (int k = 0; k < 20; ++k) {
    s->render(x);
    ref.render(y);
    for (int i = 0; i < Synth::kBlock; ++i) {
      sx += static_cast<double>(x[i]) * x[i];
      sy += static_cast<double>(y[i]) * y[i];
    }
  }
  const double ratio = sqrt(sx / sy);
  TEST_ASSERT_TRUE(ratio > 0.45 && ratio < 0.55);
  perfSend(*s, 1, static_cast<uint8_t>(PerfFx::Freeze));
  renderBoth(*s, ref, 1, false);  // ramping out
  renderBoth(*s, ref, 10, true);
}

static int16_t dlyA[16000], dlyB[16000];

// Both returns at 0, a throw of each: while its tail runs the thrown synth sounds different; once
// the tails (4 s) and the floors' ramps are over, the project's levels (0) apply again and a new
// note matches a synth that never threw.
void test_perf_return_floor_decays() {
  p->rvbLevel = 0;
  p->dlyLevel = 0;
  p->instruments[0].rsend = 127;
  p->instruments[0].send = 127;
  Synth ref(*p);
  s->setPerfBuffer(perfBuf, 32000);
  ref.setPerfBuffer(perfBuf2, 32000);
  s->setReverbBuffer(rvbBuf, Reverb::kBufLen);
  ref.setReverbBuffer(rvbBuf2, Reverb::kBufLen);
  s->setDelayBuffer(dlyA, 16000);
  ref.setDelayBuffer(dlyB, 16000);
  noteBoth(*s, ref, 0x90, 48);
  perfSend(*s, 0, static_cast<uint8_t>(PerfFx::DelayThrow));
  perfSend(*s, 0, static_cast<uint8_t>(PerfFx::ReverbThrow));
  renderBoth(*s, ref, 25, false);
  perfSend(*s, 3, 0);
  noteBoth(*s, ref, 0x80, 48);
  renderBoth(*s, ref, kBlocksPerSec * 2, false);  // the tails sound
  renderBoth(*s, ref, kBlocksPerSec * 3, false);  // 4 s tail + 100 blocks down
  noteBoth(*s, ref, 0x90, 50);
  renderBoth(*s, ref, 20, true);
}

// reset() mid-throw: nothing raised afterwards, the project's levels (0) apply at once.
void test_perf_reset_drops_throw() {
  p->rvbLevel = 0;
  p->instruments[0].rsend = 127;
  Synth ref(*p);
  s->setPerfBuffer(perfBuf, 32000);
  ref.setPerfBuffer(perfBuf2, 32000);
  s->setReverbBuffer(rvbBuf, Reverb::kBufLen);
  ref.setReverbBuffer(rvbBuf2, Reverb::kBufLen);
  noteBoth(*s, ref, 0x90, 48);
  perfSend(*s, 0, static_cast<uint8_t>(PerfFx::ReverbThrow));
  perfSend(*s, 0, static_cast<uint8_t>(PerfFx::Crush));
  renderBoth(*s, ref, 10, false);
  s->reset();
  ref.reset();
  TEST_ASSERT_FALSE(s->perf().sendTail());
  noteBoth(*s, ref, 0x90, 48);
  renderBoth(*s, ref, 30, true);
}

// ---- trance gate (TrackCfg::gate*, fx GTE, song clock 0xF6 5) ----
// 150 BPM: a 1/16 step = 3200 samples = 25 blocks.

static void gateSetup(uint16_t mask, uint8_t depth = 127) {
  p->bpm = 150;
  TrackCfg& c = p->tracks[0];
  c.gateMask = mask;
  c.gateDepth = depth;
  c.gateAtk = 0;
  c.gateRel = 0;
}
static long rendered = 0;  // blocks since the last 0xFA (transport())
static void transport(uint8_t m) {
  send(0, 0, m, 0, 0, 1);
  if (m == 0xFA) rendered = 0;
}
static const Synth::TrackStrip::Gate& gate0() { return s->strip(0).gate; }
static void blocks(int n) {
  for (int k = 0; k < n; ++k) s->render(buf);
  rendered += n;
}
// The gate tests run at 150 BPM: one 1/16 = kSp16 samples, one block = kBlk16 of a 1/16.
static constexpr float kSp16 = kSynthRate * 60.f / (150 * 4);
static constexpr float kBlk16 = Synth::kBlock / kSp16;
// Renders until steps 1/16 steps have passed since the last 0xFA (at most one block more).
static void runTo(float steps) {
  while (rendered * Synth::kBlock < steps * kSp16) blocks(1);
}
static bool bufSilent() {
  for (int i = 0; i < Synth::kBlock; ++i)
    if (buf[i] != 0) return false;
  return true;
}

// Depth 0 (running), and any depth while stopped: bit-exact to no gate.
void test_gate_off_bit_exact() {
  int16_t ref[Synth::kBlock * 8];
  noteOn(0, 0, 60);
  for (int k = 0; k < 8; ++k) s->render(ref + k * Synth::kBlock);
  for (int pass = 0; pass < 2; ++pass) {
    Synth t(*p);
    if (pass == 0) {
      gateSetup(0x0000, 0);
      const uint8_t m = 0xFA;
      t.event(0, 0, &m, 1);
    } else {
      gateSetup(0x0000, 127);  // never started
    }
    const uint8_t on[3] = {0x90, 60, 127};
    t.event(0, 0, on, 3);
    int16_t out[Synth::kBlock];
    for (int k = 0; k < 8; ++k) {
      t.render(out);
      TEST_ASSERT_EQUAL_INT16_ARRAY(ref + k * Synth::kBlock, out, Synth::kBlock);
    }
    TEST_ASSERT_EQUAL_FLOAT(1.f, t.strip(0).gate.g);
    p->bpm = 120;
  }
}

// Mask bit i opens 1/16 step i + 1; closed steps at 1 - depth / 127.
void test_gate_mask_positions() {
  gateSetup(0x0001);
  transport(0xFA);
  noteOn(0, 0, 60);
  runTo(0.48f);
  TEST_ASSERT_EQUAL_FLOAT(1.f, gate0().g);
  TEST_ASSERT_FALSE(bufSilent());
  runTo(1.48f);  // step 2
  TEST_ASSERT_EQUAL_FLOAT(0.f, gate0().g);
  TEST_ASSERT_TRUE(bufSilent());
  runTo(16.48f);  // step 1 of the next bar
  TEST_ASSERT_EQUAL_FLOAT(1.f, gate0().g);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, rendered * kBlk16, s->gatePos());
  p->tracks[0].gateDepth = 64;
  runTo(17.48f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.f - 64.f / 127.f, gate0().g);
}

// Rates: 1/32 halves the gate step, 1/8 doubles it.
void test_gate_rates() {
  gateSetup(0x0001);
  p->tracks[0].gateRate = static_cast<uint8_t>(GateRate::R8);
  transport(0xFA);
  noteOn(0, 0, 60);
  runTo(1.48f);  // 1/16 step 2: still the first 1/8
  TEST_ASSERT_EQUAL_FLOAT(1.f, gate0().g);
  runTo(2.48f);
  TEST_ASSERT_EQUAL_FLOAT(0.f, gate0().g);
  p->tracks[0].gateRate = static_cast<uint8_t>(GateRate::R32);
  transport(0xFA);
  runTo(0.8f);  // the second 1/32
  TEST_ASSERT_EQUAL_FLOAT(0.f, gate0().g);
}

// 0xFA restarts the gate at step 1.
void test_gate_transport_start_restarts() {
  gateSetup(0x0001);
  transport(0xFA);
  noteOn(0, 0, 60);
  runTo(1.48f);
  TEST_ASSERT_EQUAL_FLOAT(0.f, gate0().g);
  transport(0xFA);
  blocks(1);
  TEST_ASSERT_EQUAL_FLOAT(1.f, gate0().g);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, kBlk16, s->gatePos());
}

// 0xF6 5 idx re-anchors the song clock at its sample.
void test_gate_song_clock_reanchors() {
  gateSetup(0x0001);
  transport(0xFA);
  noteOn(0, 0, 60);
  blocks(2);
  send(64, 0, 0xF6, 5, 1);
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.f + 64.f / kSp16, s->gatePos());
  TEST_ASSERT_EQUAL_FLOAT(0.f, gate0().g);
  for (int i = 0; i < 32; ++i) TEST_ASSERT_NOT_EQUAL(0, buf[i]);  // open before the clock
  send(0, 0, 0xF6, 5, 64 + 16);  // mod 64
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 16.f + kBlk16, s->gatePos());
  TEST_ASSERT_EQUAL_FLOAT(1.f, gate0().g);
}

// Paused: the position (and so the gain) holds; resume runs on.
void test_gate_pause_holds() {
  gateSetup(0x0001);
  transport(0xFA);
  noteOn(0, 0, 60);
  blocks(10);
  transport(0xFC);
  blocks(1);
  const float at = s->gatePos();
  blocks(40);
  TEST_ASSERT_EQUAL_FLOAT(at, s->gatePos());
  TEST_ASSERT_EQUAL_FLOAT(1.f, gate0().g);
  transport(0xFB);
  blocks(1);
  TEST_ASSERT_TRUE(s->gatePos() > at);
}

// Stop (0xFD): the gate bypasses, its gain ramps back to 1; the position reads -1.
void test_gate_stop_bypasses() {
  gateSetup(0x0000);
  transport(0xFA);
  noteOn(0, 0, 60);
  blocks(2);
  TEST_ASSERT_EQUAL_FLOAT(0.f, gate0().g);
  TEST_ASSERT_TRUE(bufSilent());
  transport(0xFD);
  blocks(1);
  TEST_ASSERT_EQUAL_FLOAT(1.f, gate0().g);
  TEST_ASSERT_FALSE(bufSilent());
  TEST_ASSERT_EQUAL_FLOAT(-1.f, s->gatePos());
}

// Length 100 % merges adjacent open steps; below it the gate closes inside each step.
void test_gate_length() {
  gateSetup(0x0003);
  p->tracks[0].gateAtk = 127;  // 50 ms: a dip would still show a block later
  transport(0xFA);
  noteOn(0, 0, 60);
  runTo(1.04f);
  TEST_ASSERT_EQUAL_FLOAT(1.f, gate0().g);
  p->tracks[0].gateLen = 99;
  transport(0xFA);
  runTo(1.04f);  // closed for the last 1 % of step 1, reopening slowly
  TEST_ASSERT_TRUE(gate0().g < 0.2f);
  p->tracks[0].gateLen = 50;
  p->tracks[0].gateAtk = 0;
  transport(0xFA);
  runTo(0.8f);  // 80 % into step 1
  TEST_ASSERT_EQUAL_FLOAT(0.f, gate0().g);
}

// The gain ramps at most 1 / attack samples per sample: no click on an edge.
void test_gate_ramp_slope() {
  gateSetup(0x0002);
  p->tracks[0].gateAtk = 127;  // 50 ms
  const float atk = 0.05f * kSynthRate;
  transport(0xFA);
  noteOn(0, 0, 60);
  blocks(1);
  TEST_ASSERT_EQUAL_FLOAT(0.f, gate0().g);  // snapped closed at the note-on
  runTo(1.f + kBlk16 * 0.5f);  // into step 2
  const float into = rendered * Synth::kBlock - kSp16;
  TEST_ASSERT_FLOAT_WITHIN(2e-3f, into / atk, gate0().g);
  blocks(static_cast<int>((atk - into) / Synth::kBlock) + 1);
  TEST_ASSERT_EQUAL_FLOAT(1.f, gate0().g);
}

// GTE locks the depth through the newest voice; the next note without it takes the track's.
void test_gte_lock_then_revert() {
  gateSetup(0x0000, 0);
  transport(0xFA);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::GTE, 127);
  noteOn(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_TRUE(gate0().on);
  TEST_ASSERT_EQUAL_FLOAT(0.f, gate0().floor);
  TEST_ASSERT_TRUE(bufSilent());
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 64);
  blocks(2);
  TEST_ASSERT_FALSE(gate0().on);
  TEST_ASSERT_EQUAL_FLOAT(1.f, gate0().g);
}

// The preview track takes the gate of the track 0xF7 names.
void test_gate_preview_track() {
  p->tracks[3].gateDepth = 100;
  send(0, kPreviewTrack, 0xF7, 3, 0, 2);
  noteOn(0, kPreviewTrack, 60);
  s->render(buf);
  TEST_ASSERT_TRUE(s->strip(kPreviewTrack).gate.on);
}

// ---- envelope follower (TrackCfg::fol*) ----

// Track dst follows track src (1-based in TrackCfg): target, amount, instant attack, release rel.
static void folSetup(int dst, int src, LfoDest dest, int8_t amt, uint8_t rel = 0) {
  TrackCfg& c = p->tracks[dst];
  c.folSrc = static_cast<uint8_t>(src + 1);
  c.folDest = static_cast<uint8_t>(dest);
  c.folAmt = amt;
  c.folAtk = 0;
  c.folRel = rel;
}
// Instrument i low-passed, cutoff 127; track t plays it.
static void folFilter(int t, int i) {
  p->tracks[t].instr = static_cast<uint8_t>(i);
  p->instruments[i].fltMode = static_cast<uint8_t>(FltMode::Lp);
  p->instruments[i].cutoff = 127;
}
static float folOct(uint8_t t) {
  const int v = s->trackVoice(t);
  TEST_ASSERT_TRUE(v >= 0);
  return s->voice(v).octKey;
}
constexpr float kOctOpen = 9.451211f;  // cutoff 127

// A silent source: the output is bit-exact to no follower.
void test_fol_silent_source_bit_exact() {
  folFilter(1, 1);
  int16_t ref[Synth::kBlock * 8];
  noteOn(0, 1, 60);
  for (int k = 0; k < 8; ++k) s->render(ref + k * Synth::kBlock);
  folSetup(1, 5, LfoDest::Cutoff, -64);
  Synth t(*p);
  const uint8_t on[3] = {0x90, 60, 127};
  t.event(0, 1, on, 3);
  int16_t out[Synth::kBlock];
  for (int k = 0; k < 8; ++k) {
    t.render(out);
    TEST_ASSERT_EQUAL_INT16_ARRAY(ref + k * Synth::kBlock, out, Synth::kBlock);
  }
  TEST_ASSERT_EQUAL_FLOAT(0.f, t.followerLevel(1));
}

// The kick (track 0) closes the bass's (track 1) cutoff, which recovers after the release.
void test_fol_cutoff_ducks_and_recovers() {
  folFilter(1, 1);
  folSetup(1, 0, LfoDest::Cutoff, -64, 20);
  noteOn(0, 1, 48);
  blocks(2);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, kOctOpen, folOct(1));
  noteOn(0, 0, 60);
  blocks(2);
  TEST_ASSERT_TRUE(s->followerLevel(1) > 0.5f);
  TEST_ASSERT_TRUE(folOct(1) < kOctOpen - 2.f);
  noteOff(0, 0, 60);
  blocks(kBlocksPerSec / 2);
  TEST_ASSERT_TRUE(s->followerLevel(1) < 0.01f);
  TEST_ASSERT_TRUE(folOct(1) > kOctOpen - 0.1f);
}

// Amount + opens: a closed cutoff rises with the source.
void test_fol_positive_amount_opens() {
  folFilter(1, 1);
  p->instruments[1].cutoff = 20;
  folSetup(1, 0, LfoDest::Cutoff, 63);
  noteOn(0, 1, 48);
  blocks(2);
  const float closed = folOct(1);
  noteOn(0, 0, 60);
  blocks(2);
  TEST_ASSERT_TRUE(folOct(1) > closed + 2.f);
}

// Vol: the strip's fader ducks (gains and sends), the voice stays as it was.
void test_fol_vol_ducks_strip() {
  p->instruments[1].send = 127;
  p->tracks[1].instr = 1;
  folSetup(1, 0, LfoDest::Vol, -64);
  noteOn(0, 1, 48);
  blocks(2);
  TEST_ASSERT_EQUAL_FLOAT(1.f, s->strip(1).fader);
  const float send = s->strip(1).send, tgl = s->strip(1).tgl;
  const float amp = s->voice(s->trackVoice(1)).amp;
  noteOn(0, 0, 60);
  blocks(2);
  const float l = s->followerLevel(1);
  TEST_ASSERT_TRUE(l > 0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.f - l, s->strip(1).fader);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, send * (1.f - l), s->strip(1).send);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, (1.f - l) * tgl, s->strip(1).tgl);
  TEST_ASSERT_EQUAL_FLOAT(amp, s->voice(s->trackVoice(1)).amp);
}

// The duck ramps over the segment: no step inside the strip's gains.
void test_fol_vol_ramped() {
  folSetup(1, 0, LfoDest::Vol, -64);
  noteOn(0, 1, 48);
  blocks(2);
  noteOn(0, 0, 60);
  s->render(buf);  // the follower sees the kick from the next boundary on
  const Synth::TrackStrip& st = s->strip(1);
  TEST_ASSERT_TRUE(st.tgl < st.gl || st.dgl == 0.f);  // landed at the boundary, or ramping down
  // A ramp per segment, landing exactly on each boundary (a block is whole segments). Not monotonic:
  // with an instant release the envelope follows each 32-sample segment's peak, which moves with
  // the source's phase (at 44.1 kHz a segment is shorter than at 32 kHz).
  for (int k = 0; k < 4; ++k) {
    s->render(buf);
    TEST_ASSERT_EQUAL_FLOAT(st.tgl, st.gl);
    TEST_ASSERT_TRUE(st.fader < 0.9f);  // ducked
  }
}

// Attack / release: the envelope reaches ~63 % in its time (steps of folStepMs).
void test_fol_attack_release_times() {
  folSetup(1, 0, LfoDest::Cutoff, -64, 70);
  p->tracks[1].folAtk = 50;
  noteOn(0, 1, 48);
  noteOn(0, 0, 60);
  blocks(kBlocksPerSec);  // settled
  const float top = s->followerLevel(1);
  TEST_ASSERT_TRUE(top > 0.5f);
  noteOff(0, 0, 60);
  const int relMs = envTimeMs(70);
  blocks(relMs * kBlocksPerSec / 1000);
  TEST_ASSERT_FLOAT_WITHIN(0.06f * top, 0.37f * top, s->followerLevel(1));
  blocks(kBlocksPerSec * 2);
  noteOn(0, 0, 60);
  const int atkMs = envTimeMs(50);
  blocks(atkMs * kBlocksPerSec / 1000);
  TEST_ASSERT_FLOAT_WITHIN(0.08f * top, 0.63f * top, s->followerLevel(1));
}

// Source / destination order does not matter: track 5 <- 1 and track 1 <- 5 give the same envelope.
void test_fol_order_independent() {
  folSetup(5, 1, LfoDest::Cutoff, -64, 40);
  noteOn(0, 1, 60);
  noteOn(0, 5, 48);
  float a[20];
  for (float& x : a) {
    s->render(buf);
    x = s->followerLevel(5);
  }
  s->reset();
  p->tracks[5].folSrc = 0;
  folSetup(1, 5, LfoDest::Cutoff, -64, 40);
  noteOn(0, 5, 60);
  noteOn(0, 1, 48);
  for (float x : a) {
    s->render(buf);
    TEST_ASSERT_EQUAL_FLOAT(x, s->followerLevel(1));
  }
}

// The detector is pre-fader: a key at fader 0 still drives; self-follow on Vol does not feed back.
void test_fol_ghost_key_and_self_vol() {
  folSetup(1, 0, LfoDest::Cutoff, -64);
  p->tracks[0].vol = 0;
  noteOn(0, 0, 60);
  noteOn(0, 1, 48);
  blocks(2);
  const float l = s->followerLevel(1);
  TEST_ASSERT_TRUE(l > 0.5f);
  s->reset();
  p->tracks[0].vol = 127;
  folSetup(0, 0, LfoDest::Vol, -64);
  noteOn(0, 0, 60);
  blocks(2);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, l, s->followerLevel(0));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.f - l, s->strip(0).fader);
}

// Strip targets through the newest voice (Pan), LFO targets through the LFO mods (L1 DEPTH).
void test_fol_pan_and_lfo_depth() {
  folSetup(1, 0, LfoDest::Pan, 63);
  noteOn(0, 1, 48);
  noteOn(0, 0, 60);
  blocks(2);
  TEST_ASSERT_TRUE(s->strip(1).tgr > s->strip(1).tgl * 2.f);
  s->reset();
  folFilter(1, 1);
  Instrument& m = p->instruments[1];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 0;
  m.lfoDepth = -64;
  p->tracks[1].folSrc = 0;
  noteOn(0, 1, 48);
  blocks(2);
  const float lfoOnly = folOct(1);
  s->reset();
  folSetup(1, 0, LfoDest::Depth1, -64);
  noteOn(0, 1, 48);
  noteOn(0, 0, 60);
  blocks(2);
  TEST_ASSERT_TRUE(folOct(1) > lfoOnly + 1.f);  // the LFO's depth shrinks with the source
}

// Off: amount 0, a MIDI source, a source out of range; the preview track neither follows nor drives.
void test_fol_off_cases_and_preview() {
  folSetup(1, 0, LfoDest::Vol, 0);
  noteOn(0, 0, 60);
  noteOn(0, 1, 48);
  blocks(2);
  TEST_ASSERT_EQUAL_FLOAT(1.f, s->strip(1).fader);
  p->tracks[1].folAmt = -64;
  p->tracks[0].out = TrackOut::Midi;
  blocks(2);
  TEST_ASSERT_EQUAL_FLOAT(1.f, s->strip(1).fader);
  p->tracks[0].out = TrackOut::Int;
  p->tracks[1].folSrc = kTracks + 1;
  blocks(2);
  TEST_ASSERT_EQUAL_FLOAT(1.f, s->strip(1).fader);
  s->reset();
  folSetup(1, 0, LfoDest::Vol, -64);
  send(0, kPreviewTrack, 0xF7, 1, 0, 2);  // preview through track 1's insert
  noteOn(0, kPreviewTrack, 48);
  noteOn(0, 0, 60);
  blocks(2);
  TEST_ASSERT_EQUAL_FLOAT(kPreviewVol / 127.f, s->strip(kPreviewTrack).fader);
}

// A KIT destination: the follower acts on its lanes' voices (sampler lanes: cutoff of the lane).
void test_fol_kit_track_strip() {
  kitSetup(1, 2);
  folSetup(1, 0, LfoDest::Vol, -64);
  noteOn(0, 0, 60);
  noteOn(0, 1, 60);
  blocks(2);
  TEST_ASSERT_TRUE(s->strip(1).fader < 0.5f);
}


// ---- strip ramps when a new note's locks take over a sounding track ----

// A new note with a DRV lock while an older note sounds: the track sum's drive crossfades over a
// control period instead of stepping (0 -> 127 would jump the older note's waveform at once).
void test_strip_drive_lock_crossfades() {
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Triangle);
  p->instruments[0].rsend = 0;
  p->instruments[0].send = 0;
  p->tracks[0].vol = 30;
  noteOn(0, 0, 36);
  for (int k = 0; k < 20; ++k) s->render(buf);
  int base = 0;
  int16_t prev = buf[Synth::kBlock - 1];
  for (int k = 0; k < 4; ++k) {
    s->render(buf);
    base = maxStep(buf, prev) > base ? maxStep(buf, prev) : base;
    prev = buf[Synth::kBlock - 1];
  }
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::DRV, 127);
  noteOn(0, 0, 36, 1);
  s->render(buf);
  const int jump = maxStep(buf, prev);
  char msg[64];
  snprintf(msg, sizeof(msg), "jump %d base %d peak %d", jump, base, peakOf(buf));
  // The crossfade is kControl samples (main: 1 ms at 32 kHz); here 0.73 ms, while the triangle's own
  // step shrinks with the rate: the bound scales base to 32 kHz.
  TEST_ASSERT_TRUE_MESSAGE(jump < base * 4 * kSynthRate / 32000 + 50, msg);
  TEST_ASSERT_TRUE(s->strip(0).drive.on());
}

// A new note with a DLY lock while an older note sounds: the send ramps in over a control period
// (R carries only the echo: the dry track is hard left).
void test_strip_send_lock_ramps() {
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Triangle);
  p->instruments[0].rsend = 0;
  p->instruments[0].send = 0;
  p->tracks[0].vol = 30;
  p->tracks[0].pan = 0;
  p->rvbLevel = 0;
  delaySetup(0);
  noteOn(0, 0, 36);
  for (int k = 0; k < 20; ++k) s->renderStereo(lr);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::DLY, 127);
  noteOn(0, 0, 36, 1);
  static int16_t r[Synth::kBlock * 100];
  for (int k = 0; k < 100; ++k) {
    s->renderStereo(lr);
    for (int i = 0; i < Synth::kBlock; ++i) r[k * Synth::kBlock + i] = lr[2 * i + 1];
  }
  int st = 0;
  while (st < Synth::kBlock * 100 && r[st] == 0) ++st;
  TEST_ASSERT_TRUE(st < Synth::kBlock * 90);
  int peak = 0;
  for (int i = st; i < st + 4000; ++i) peak = abs(r[i]) > peak ? abs(r[i]) : peak;
  int jump = 0, base = 0;  // base: the echo's own largest step once the send has landed
  for (int i = st; i < st + 64; ++i) jump = abs(r[i] - r[i - 1]) > jump ? abs(r[i] - r[i - 1]) : jump;
  for (int i = st + 200; i < st + 4000; ++i) base = abs(r[i] - r[i - 1]) > base ? abs(r[i] - r[i - 1]) : base;
  char msg[64];
  snprintf(msg, sizeof(msg), "jump %d base %d peak %d", jump, base, peak);
  TEST_ASSERT_TRUE_MESSAGE(peak > 500 && jump < base * 3 + 10, msg);
}

// A note whose envelope has ended but whose filter still rings is still sounding: a new note on
// the track ramps the pan (no snap that cuts the tail out of L at once).
void test_ringing_tail_keeps_strip() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 40;
  m.reso = 120;
  m.release = 10;
  p->tracks[0].vol = 40;
  p->tracks[0].pan = 0;
  noteOn(0, 0, 36);
  for (int k = 0; k < 10; ++k) s->renderStereo(lr);
  noteOff(0, 0, 36);
  for (int k = 0; k < 3; ++k) s->renderStereo(lr);
  bool tail = false;
  for (int i = 0; i < kVoices; ++i) tail |= s->voice(i).on && s->voice(i).env.idle();
  TEST_ASSERT_TRUE(tail);  // envelope over, filter ringing
  const int16_t last = lr[2 * (Synth::kBlock - 1)];
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::PAN, 127);
  noteOn(0, 0, 60);
  s->renderStereo(lr);
  char msg[64];
  snprintf(msg, sizeof(msg), "last %d first %d", last, lr[0]);
  TEST_ASSERT_TRUE_MESSAGE(abs(lr[0] - last) < abs(last) / 4 + 20, msg);
}

// ---- SYNTH fold / ring (plan A) ----

// Track 0: a CHIP note (instrument 0); track 1: a SYNTH note (instrument 1, set up by the caller).
// FNV-1a of 16 stereo blocks from a fresh synth.
static uint32_t synFoldHash() {
  p->tracks[0].instr = 0;
  p->tracks[1].instr = 1;
  Synth* t = new Synth(*p);
  const uint8_t on0[3] = {0x90, 60, 127}, on1[3] = {0x90, 57, 127};
  t->event(0, 0, on0, 3);
  t->event(0, 1, on1, 3);
  uint32_t h = 2166136261u;
  int16_t l[Synth::kBlock], r[Synth::kBlock];
  for (int k = 0; k < 16; ++k) {
    t->render(l, r);
    for (int i = 0; i < Synth::kBlock; ++i) {
      h = (h ^ static_cast<uint16_t>(l[i])) * 16777619u;
      h = (h ^ static_cast<uint16_t>(r[i])) * 16777619u;
    }
  }
  delete t;
  return h;
}
static Instrument& synFoldInst() {
  Instrument& m = p->instruments[1];
  instrSetType(m, InstrType::Synth);
  return m;
}
constexpr uint32_t kSynFoldRef = 0x250BF7C3;  // today's build (fold 0, ring 0), captured before the change

void test_synth_fold_ring_off_bit_exact() {
  Instrument& m = synFoldInst();
  TEST_ASSERT_EQUAL(0, m.synFold);
  TEST_ASSERT_EQUAL(0, m.synRing);
  TEST_ASSERT_EQUAL_HEX32(kSynFoldRef, synFoldHash());
}

void test_synth_fold_changes_output() {
  synFoldInst();
  const uint32_t ref = synFoldHash();
  p->instruments[1].synFold = 100;
  TEST_ASSERT_NOT_EQUAL(ref, synFoldHash());
  p->instruments[1].synFold = 0;
  p->instruments[1].synRing = 100;
  TEST_ASSERT_NOT_EQUAL(ref, synFoldHash());
}

void test_synth_lfo_fold_changes_output() {
  Instrument& m = synFoldInst();
  const uint32_t ref = synFoldHash();
  m.lfoDest = static_cast<uint8_t>(LfoDest::Fold);
  m.lfoDepth = 63;
  m.lfoRate = 100;
  TEST_ASSERT_NOT_EQUAL(ref, synFoldHash());
}

void test_synth_follower_ring_changes_output() {
  synFoldInst();
  const uint32_t ref = synFoldHash();
  folSetup(1, 0, LfoDest::Ring, 63);
  TEST_ASSERT_NOT_EQUAL(ref, synFoldHash());
}

// ---- filter types (VoiceFilter) ----

// FNV-1a over 4 s of L / R: LP on CHIP, BP on FM, HP on SYNTH (chords, releases, filter env, key
// track): pins the LP / BP / HP path bit-exact across the filter rework.
static uint32_t lpBpHpRenderHash() {
  Instrument& c = p->instruments[0];  // CHIP saw, LP
  c.fltMode = static_cast<uint8_t>(FltMode::Lp);
  c.cutoff = 60;
  c.reso = 100;
  c.fenv = 30;
  c.fDec = 60;
  c.keytrack = 64;
  c.release = 50;
  Instrument& f = p->instruments[1];  // FM, BP
  useFm(1, FmMachine::Kick);
  instrSetType(f, InstrType::Fm);
  f.fltMode = static_cast<uint8_t>(FltMode::Bp);
  f.cutoff = 90;
  f.reso = 70;
  f.release = 40;
  Instrument& y = p->instruments[2];  // SYNTH, HP
  instrSetType(y, InstrType::Synth);
  y.fltMode = static_cast<uint8_t>(FltMode::Hp);
  y.cutoff = 50;
  y.reso = 40;
  y.release = 60;
  for (int t = 0; t < 3; ++t) p->tracks[t].instr = static_cast<uint8_t>(t);
  delete s;
  s = new Synth(*p);
  uint32_t h = 2166136261u;
  int peak = 0;
  auto mix = [&h, &peak](const int16_t* x) {
    for (int i = 0; i < Synth::kBlock; ++i) {
      h = (h ^ static_cast<uint16_t>(x[i])) * 16777619u;
      peak = abs(x[i]) > peak ? abs(x[i]) : peak;
    }
  };
  const int blocks = 4 * kSynthRate / Synth::kBlock;
  for (int k = 0; k < blocks; ++k) {
    if (k == 0) {
      noteOn(0, 0, 48);
      noteOn(3, 0, 55);
      noteOn(5, 1, 60);
      noteOn(9, 2, 64);
    }
    if (k == blocks / 4) {
      noteOff(0, 0, 48);
      noteOn(7, 1, 67);
      noteOn(11, 2, 71, 90);
    }
    if (k == blocks / 2) {
      noteOff(1, 0, 55);
      noteOff(2, 1, 60);
      noteOff(2, 1, 67);
      noteOff(4, 2, 64);
      noteOff(4, 2, 71);
    }
    render1(buf);
    mix(buf);
    mix(bufR);
  }
  TEST_ASSERT_TRUE(peak > 1000);  // not a hash of silence
  return h;
}

void test_lp_bp_hp_render_unchanged() {
  const uint32_t h = lpBpHpRenderHash();
  // Captured before VoiceFilter replaced the Svf; re-captured after the FM KICK body (filter path unchanged).
  TEST_ASSERT_EQUAL_HEX32(0x09EFE345u, h);
}

// A CHIP saw through each new type: it sounds, and the voice is freed within 3 s of the release
// (a self-oscillating LADDER / MS20 tail fades out).
void test_new_filter_types_sound_and_free() {
  const struct { FltMode m; uint8_t reso; } cases[] = {
      {FltMode::Ladder, 100}, {FltMode::Ms20, 100}, {FltMode::Comb, 100}, {FltMode::Formant, 100},
      {FltMode::Ladder, 127}, {FltMode::Ms20, 127}, {FltMode::Comb, 127}, {FltMode::Formant, 127}};
  for (const auto& c : cases) {
    s->reset();
    Instrument& m = p->instruments[0];
    m.fltMode = static_cast<uint8_t>(c.m);
    m.cutoff = 70;
    m.reso = c.reso;
    noteOn(0, 0, 57);
    TEST_ASSERT_TRUE(rmsBlocks(40) > 100.f);
    noteOff(0, 0, 57);
    for (int k = 0; k < 3 * kSynthRate / Synth::kBlock && s->activeVoices() > 0; ++k) render1(buf);
    TEST_ASSERT_EQUAL(0, s->activeVoices());
  }
}

// FORMANT: the cutoff is the vowel, key track does not move it.
static void renderNote(int16_t* out, int blocks, uint8_t note) {
  delete s;
  s = new Synth(*p);
  noteOn(0, 0, note);
  for (int k = 0; k < blocks; ++k) {
    render1(buf);
    memcpy(out + k * Synth::kBlock, buf, sizeof(buf));
  }
}

void test_formant_ignores_key_track() {
  static int16_t a[64 * Synth::kBlock], b[64 * Synth::kBlock];
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Formant);
  m.cutoff = 40;
  m.reso = 90;
  m.keytrack = 0;
  renderNote(a, 64, 84);
  m.keytrack = 127;
  renderNote(b, 64, 84);
  TEST_ASSERT_EQUAL_INT16_ARRAY(a, b, 64 * Synth::kBlock);
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);  // LP does track
  renderNote(b, 64, 84);
  m.keytrack = 0;
  renderNote(a, 64, 84);
  TEST_ASSERT_TRUE(memcmp(a, b, sizeof(a)) != 0);
}

// COMB on two notes of one track: each voice its own line (the chord is the sum of the two notes
// alone), tuned to each note (key track 100 %).
void test_comb_voices_have_own_lines() {
  static int16_t a[96 * Synth::kBlock], b[96 * Synth::kBlock], ab[96 * Synth::kBlock];
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Comb);
  m.cutoff = 50;  // ~261 Hz at C4
  m.reso = 115;
  m.keytrack = 127;
  m.vol = 8;  // far below the master soft clip (the tuned comb is ~3x the saw)
  renderNote(a, 96, 60);
  renderNote(b, 96, 67);
  delete s;
  s = new Synth(*p);
  noteOn(0, 0, 60);
  noteOn(0, 0, 67);
  for (int k = 0; k < 96; ++k) {
    render1(buf);
    memcpy(ab + k * Synth::kBlock, buf, sizeof(buf));
  }
  int worst = 0, peak = 0;
  for (int i = 0; i < 96 * Synth::kBlock; ++i) {
    worst = abs(ab[i] - a[i] - b[i]) > worst ? abs(ab[i] - a[i] - b[i]) : worst;
    peak = abs(ab[i]) > peak ? abs(ab[i]) : peak;
  }
  TEST_ASSERT_TRUE(peak > 500);
  TEST_ASSERT_TRUE(worst <= 4 + peak / 100);
}

// RMS of the second half (the comb settled).
static float rmsTail(const int16_t* x, int blocks) {
  double sum = 0;
  for (int i = blocks / 2 * Synth::kBlock; i < blocks * Synth::kBlock; ++i) sum += double(x[i]) * x[i];
  return static_cast<float>(sqrt(sum / (blocks / 2 * Synth::kBlock)));
}

// COMB follows the key: at key track 100 % its peaks (multiples of the comb pitch) sit on every
// harmonic of the saw, so G4 rings far louder than at key track 0. There the comb stays at C4's
// 261 Hz: its peaks (261, 523, 785 Hz ...) meet only G4's even harmonics (784 Hz ~ the 3rd peak, ...),
// the fundamental 392 Hz and the other odd ones fall between peaks. On C4 key track changes nothing
// (the reference note).
void test_comb_key_track_tunes_to_note() {
  static int16_t x[96 * Synth::kBlock];
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Comb);
  m.cutoff = 50;  // ~261 Hz at C4
  m.reso = 115;
  m.vol = 8;
  m.keytrack = 127;
  renderNote(x, 96, 67);
  const float tracked = rmsTail(x, 96);
  renderNote(x, 96, 60);
  const float c4Tracked = rmsTail(x, 96);
  m.keytrack = 0;
  renderNote(x, 96, 67);
  const float fixed = rmsTail(x, 96);
  renderNote(x, 96, 60);
  const float c4Fixed = rmsTail(x, 96);
  TEST_ASSERT_TRUE(tracked > 2.f * fixed);
  TEST_ASSERT_FLOAT_WITHIN(0.01f * c4Fixed, c4Fixed, c4Tracked);
}

// ---- macro locks (DRUM) ----

constexpr int kMacBlocks = 40;
static int16_t ma_[Synth::kBlock * kMacBlocks], mb_[Synth::kBlock * kMacBlocks];

// Sum of |a - b| and of |a| over samples [from, to).
static void macDiff(int from, int to, long& d, long& a) {
  d = a = 0;
  for (int i = from; i < to; ++i) {
    d += abs(ma_[i] - mb_[i]);
    a += abs(ma_[i]);
  }
}

static void renderMac(int16_t* out, uint8_t note = 57, uint8_t vel = 127) {
  noteOn(0, 0, note, vel);
  for (int k = 0; k < kMacBlocks; ++k) s->render(out + k * Synth::kBlock);
}

void test_drum_col_lock_changes_output() {
  Instrument& m = p->instruments[0];
  m.machine = static_cast<uint8_t>(DrumMachine::Sd8);
  instrSetType(m, InstrType::Drum);
  renderMac(ma_);
  s->reset();
  fx(0, 0, Fx::COL, 127);  // COLOR
  noteOn(0, 0, 57, 127);
  s->render(mb_);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).lockMask & (1 << kMacCol));  // read while the hit sounds
  for (int k = 1; k < kMacBlocks; ++k) s->render(mb_ + k * Synth::kBlock);
  long d, a;
  macDiff(0, Synth::kBlock * kMacBlocks, d, a);
  TEST_ASSERT_TRUE(a > 0 && d > a / 10);
}

static uint32_t macClk = 0;
static uint32_t macClock() { return macClk += 3; }

// ---- STRING ----

static Instrument& stringInst(StringModel mdl) {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::String);
  m.machine = static_cast<uint8_t>(mdl);
  m.sustain = 127;
  return m;
}

constexpr int kStrBlocks = 200;  // ~0.58 s
static int16_t sa_[Synth::kBlock * kStrBlocks], sb_[Synth::kBlock * kStrBlocks];

static void renderStr(int16_t* out, uint8_t note = 57, uint8_t vel = 127) {
  noteOn(0, 0, note, vel);
  for (int k = 0; k < kStrBlocks; ++k) s->render(out + k * Synth::kBlock);
}

// Sum of |x| over samples [from, to).
static long absSum(const int16_t* x, int from, int to) {
  long a = 0;
  for (int i = from; i < to; ++i) a += abs(x[i]);
  return a;
}

// The lag in [lo, hi] with the largest autocorrelation of x[from, from + len).
static int acLag(const int16_t* x, int from, int len, int lo, int hi) {
  int best = lo;
  double bestV = -1e300;
  for (int l = lo; l <= hi; ++l) {
    double c = 0;
    for (int i = from; i < from + len; ++i) c += double(x[i]) * x[i + l];
    if (c > bestV) {
      bestV = c;
      best = l;
    }
  }
  return best;
}

void test_string_sounds_at_note_pitch() {
  for (int k = 0; k < 2; ++k) {
    s->reset();
    Instrument& m = stringInst(k ? StringModel::Modal : StringModel::String);
    m.wave = static_cast<uint8_t>(Wave::Noise);  // a CHIP fallback would hiss
    renderStr(sa_, 57);  // 220 Hz: 200.45 samples
    TEST_ASSERT_TRUE(absSum(sa_, 0, Synth::kBlock * 20) > 1000);
    TEST_ASSERT_INT_WITHIN(2, 200, acLag(sa_, 4410, 4096, 150, 260));
    TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).str);
  }
}

void test_string_voices_are_heavy() {
  stringInst(StringModel::String).mono = false;
  for (int t = 0; t < 3; ++t) p->tracks[t].instr = 0;
  for (int k = 0; k < kFmVoiceMax + 1; ++k) {  // the last one steals one
    noteOn(0, static_cast<uint8_t>(k / 4), static_cast<uint8_t>(48 + k));
    render1(buf);
  }
  renderMs(kStealMs);
  int heavy = 0;
  for (int i = 0; i < kVoices; ++i) heavy += heavyLoad(s->voice(i));
  TEST_ASSERT_EQUAL(kFmVoiceMax, heavy);
  TEST_ASSERT_EQUAL(kFmVoiceMax, s->activeVoices());
}

// A held note whose resonance died (DAMP 0: T60 50 ms) frees its voice before the note-off.
void test_string_done_frees_held_voice() {
  for (int k = 0; k < 2; ++k) {
    s->reset();
    Instrument& m = stringInst(k ? StringModel::Modal : StringModel::String);
    m.macro[kMacDamp] = 0;
    m.macro[kMacExc] = 0;  // click
    noteOn(0, 0, 57);
    render1(buf);
    const int vi = s->trackVoice(0);
    TEST_ASSERT_TRUE(vi >= 0);
    renderMs(20);
    TEST_ASSERT_TRUE(s->voice(vi).on);
    renderMs(300);
    TEST_ASSERT_FALSE(s->voice(vi).on);
    TEST_ASSERT_EQUAL(0, s->activeVoices());
  }
}

// Tail energy (the last third of the buffer) relative to the whole.
static long strTail(const int16_t* x) { return absSum(x, Synth::kBlock * kStrBlocks * 2 / 3, Synth::kBlock * kStrBlocks); }

void test_string_dcy_lock_changes_decay() {
  Instrument& m = stringInst(StringModel::String);
  m.macro[kMacDamp] = 40;
  renderStr(sa_);
  s->reset();
  fx(0, 0, Fx::DCY, 120);  // DAMP: a longer ring
  renderStr(sb_);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).lockMask & (1 << kMacDamp));
  TEST_ASSERT_TRUE(strTail(sb_) > 4 * strTail(sa_) + 100);
}

void test_string_lfo_damp_changes_decay() {
  Instrument& m = stringInst(StringModel::Modal);
  m.macro[kMacDamp] = 40;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Dec);  // DAMP
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 10;
  m.lfoDepth = 0;
  renderStr(sa_);
  s->reset();
  m.lfoDepth = 63;  // the square's first half: DAMP up
  renderStr(sb_);
  long d = 0, a = 0;
  for (int i = 0; i < Synth::kBlock * kStrBlocks; ++i) {
    d += abs(sa_[i] - sb_[i]);
    a += abs(sa_[i]);
  }
  TEST_ASSERT_TRUE(a > 0 && d > a / 10);
}

// COMB needs the voice's line, which STRING holds: on a STRING instrument it renders as filter Off.
void test_string_comb_filter_is_off() {
  Instrument& m = stringInst(StringModel::String);
  m.fltMode = static_cast<uint8_t>(FltMode::Off);
  renderStr(sa_);
  delete s;
  s = new Synth(*p);
  m.fltMode = static_cast<uint8_t>(FltMode::Comb);
  m.cutoff = 50;
  m.reso = 115;
  renderStr(sb_);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).fltOn);
  TEST_ASSERT_EQUAL_INT16_ARRAY(sa_, sb_, Synth::kBlock * kStrBlocks);
}

// A STRING note after a SYNTH note on the same mono track: the engine union is rebuilt, the string starts
// from a cleared line; finite and sounding.
void test_string_after_synth_same_track_sounds() {
  Instrument& y = p->instruments[1];
  instrSetType(y, InstrType::Synth);
  y.mono = true;
  y.sustain = 127;
  stringInst(StringModel::String).mono = true;
  pgm(0, 1);
  noteOn(0, 0, 60);
  for (int k = 0; k < 20; ++k) render1(buf);
  pgm(0, 0);
  noteOn(0, 0, 57);
  long a = 0;
  int rails = 0;
  for (int k = 0; k < 20; ++k) {
    render1(buf);
    for (int i = 0; i < Synth::kBlock; ++i) {
      a += abs(buf[i]);
      rails += buf[i] == 32767 || buf[i] == -32768;
    }
  }
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_TRUE(v.str);
  TEST_ASSERT_FALSE(v.syn);
  TEST_ASSERT_TRUE(rails < Synth::kBlock);
  TEST_ASSERT_TRUE(a > 20 * Synth::kBlock * 50);
}

void test_string_profile_stage() {
  stringInst(StringModel::Modal);
  TEST_ASSERT_EQUAL_STRING("string", Synth::profName(Synth::kProfString));
  noteOn(0, 0, 57);
  s->render(buf);
  s->setProfiler(macClock);
  s->render(buf);
  s->render(buf);
  uint32_t c[Synth::kProfStages], n[Synth::kProfStages], blocks = 0;
  s->takeProfile(c, n, blocks);
  TEST_ASSERT_TRUE(n[Synth::kProfString] > 0);
  TEST_ASSERT_TRUE(c[Synth::kProfString] > 0);
  TEST_ASSERT_EQUAL_UINT32(0, n[Synth::kProfChip]);
  TEST_ASSERT_EQUAL_UINT32(0, n[Synth::kProfSpare]);
  s->setProfiler(nullptr);
}

// VSL scales a STRING note's level (the strike's level is the velocity; VSL moves Voice::gain after it).
void test_string_vsl_changes_level() {
  Instrument& m = stringInst(StringModel::String);
  m.macro[kMacDamp] = 110;  // rings through
  p->bpm = 150;             // 24 ticks = 100 ms
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 57, 127);
  for (int k = 0; k < 60; ++k) render1(sa_ + k * Synth::kBlock);
  s->reset();
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::VSL, static_cast<uint8_t>(-64));
  noteOn(0, 0, 57, 127);
  for (int k = 0; k < 60; ++k) render1(sb_ + k * Synth::kBlock);
  const int from = 40 * Synth::kBlock, to = 60 * Synth::kBlock;  // ~116 .. 174 ms: VSL at 0
  TEST_ASSERT_TRUE(absSum(sa_, from, to) > 1000);
  TEST_ASSERT_TRUE(absSum(sb_, from, to) < absSum(sa_, from, to) / 50);
}

// Legato on one mono track: STRING -> other (SYNTH, FM) -> STRING, every note finite and sounding.
static void strLegatoStep(uint8_t instr, uint8_t note, bool str) {
  pgm(0, instr);
  noteOn(0, 0, note);
  long a = 0;
  int rails = 0;
  for (int k = 0; k < 20; ++k) {
    render1(buf);
    for (int i = 0; i < Synth::kBlock; ++i) {
      a += abs(buf[i]);
      rails += buf[i] == 32767 || buf[i] == -32768;
    }
  }
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(str, v.str);
  TEST_ASSERT_TRUE(rails < Synth::kBlock);
  TEST_ASSERT_TRUE(a > 20 * Synth::kBlock * 50);
}

void test_string_legato_with_synth_and_fm() {
  for (int k = 0; k < 2; ++k) {
    s->reset();
    Instrument& y = p->instruments[1];
    instrSetType(y, k ? InstrType::Fm : InstrType::Synth);
    y.mono = true;
    y.sustain = 127;
    stringInst(StringModel::String).mono = true;
    strLegatoStep(0, 57, true);
    const int vi = s->trackVoice(0);
    strLegatoStep(1, 60, false);
    strLegatoStep(0, 57, true);
    TEST_ASSERT_EQUAL(vi, s->trackVoice(0));
  }
}

// A legato re-strike on a ringing STRING voice adds energy into the resonance, without a click.
void test_string_legato_restrike_adds_energy() {
  Instrument& m = stringInst(StringModel::String);
  m.mono = true;
  m.macro[kMacDamp] = 100;
  noteOn(0, 0, 57, 127);
  for (int k = 0; k < 60; ++k) render1(sa_ + k * Synth::kBlock);
  const int vi = s->trackVoice(0);
  noteOn(0, 0, 57, 127);  // legato: same voice, same note
  for (int k = 60; k < 80; ++k) render1(sa_ + k * Synth::kBlock);
  TEST_ASSERT_EQUAL(vi, s->trackVoice(0));
  const int at = 60 * Synth::kBlock, w = 4 * Synth::kBlock;
  TEST_ASSERT_TRUE(absSum(sa_, at, at + w) > absSum(sa_, at - w, at) * 5 / 4);
  int jump = 0, fresh = 0;
  for (int i = at - w; i < at + w; ++i) jump = abs(sa_[i] - sa_[i - 1]) > jump ? abs(sa_[i] - sa_[i - 1]) : jump;
  for (int i = 1; i < w; ++i) fresh = abs(sa_[i] - sa_[i - 1]) > fresh ? abs(sa_[i] - sa_[i - 1]) : fresh;
  TEST_ASSERT_TRUE(jump < 16384);
  TEST_ASSERT_TRUE(jump <= 2 * fresh);  // no larger step than a fresh strike's own
}

// ---- microtonal tuning (Project::tuning, tuning.h) ----

static void setEdo(TuningTable& t, int n) {
  tuningClear(t);
  t.count = static_cast<uint8_t>(n);
  for (int k = 1; k <= n; ++k) t.mc[k - 1] = static_cast<int32_t>(1200000LL * k / n);
  strcpy(t.name, "EDO");
}

void test_tuning_12tet_voices_untuned() {
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).tuned);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.f, voiceSemis(0));
}

// A tuning of 12 equal steps plays the 12-TET pitches through the tuned path: fine, transpose, bend,
// ARP and VIB land where they do untuned.
void test_tuning_12_edo_matches_12tet() {
  Project* p2 = new Project(*p);
  setEdo(p2->tuning, 12);
  Synth* s2 = new Synth(*p2);
  Project* projs[2] = {p, p2};
  for (Project* q : projs) {
    q->instruments[0].fine = 30;
    q->instruments[0].transpose = 2;
  }
  Synth* both[2] = {s, s2};
  for (Synth* x : both) {
    Synth* keep = s;
    s = x;
    send(0, 0, 0xE0, 0x00, 0x50);  // bend up a bit
    stepStart(0, 0, 24, true);
    fx(0, 0, Fx::ARP, 0x47);
    fx(0, 0, Fx::VIB, 0x48);
    noteOn(0, 0, 57);
    s = keep;
  }
  for (int k = 0; k < 60; ++k) {
    s->render(buf, bufR);
    s2->render(buf, bufR);
    const float a = s->voice(s->trackVoice(0)).inc, b = s2->voice(s2->trackVoice(0)).inc;
    TEST_ASSERT_FLOAT_WITHIN(a * 2e-5f, a, b);
  }
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).tuned);
  TEST_ASSERT_TRUE(s2->voice(s2->trackVoice(0)).tuned);
  delete s2;
  delete p2;
}

void test_tuning_24_edo_chip_and_fine() {
  setEdo(p->tuning, 24);
  noteOn(0, 0, 61);
  render1(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).tuned);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.5f, voiceSemis(0));
  noteOn(0, 0, 36);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 48.f, voiceSemis(0));
  // Fine: cents over the tuning, live.
  p->instruments[0].fine = 50;
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 48.5f, voiceSemis(0));
}

void test_tuning_just_auto_root() {
  TEST_ASSERT_TRUE(factoryTuning(factoryTuningFind("JUST 5-LIMIT"), p->tuning));
  noteOn(0, 0, 64);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 63.863f, voiceSemis(0));  // 5/4 over C-4
  noteOn(0, 1, 60);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.f, voiceSemis(1));  // the root keeps its pitch
  p->scaleRoot = 4;  // AUTO follows: E-4 is the root now
  noteOn(0, 2, 64);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 64.f, voiceSemis(2));
}

void test_tuning_glide_ends_on_degree() {
  setEdo(p->tuning, 24);
  Instrument& m = p->instruments[0];
  m.mono = true;
  m.glide = 25;  // 100 ms
  noteOn(0, 0, 60);
  render1(buf);
  noteOn(0, 0, 62);
  renderMs(48);
  const float mid = voiceSemis(0);
  TEST_ASSERT_TRUE(mid > 60.1f && mid < 60.9f);
  renderMs(80);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 61.f, voiceSemis(0));
}

void test_tuning_table_change_applies_live() {
  setEdo(p->tuning, 24);
  noteOn(0, 0, 62);
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 61.f, voiceSemis(0));
  setEdo(p->tuning, 12 * 4);  // 48-EDO
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.5f, voiceSemis(0));
  tuningClear(p->tuning);  // 12-TET: the sounding tuned voice plays plain keys
  render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 62.f, voiceSemis(0));
}

void test_tuning_sample_by_note() {
  sampleInstr(400, kSynthRate);
  setEdo(p->tuning, 24);
  noteOn(0, 0, 62);
  render1(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).tuned);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.f, 12.f * log2f(s->voice(s->trackVoice(0)).inc));  // root 60 + 1 st
}

void test_tuning_synth_sounds_tuned() {
  instrSetType(p->instruments[0], InstrType::Synth);
  p->instruments[0].attack = 0;
  p->instruments[0].sustain = 127;
  setEdo(p->tuning, 24);
  noteOn(0, 0, 81);  // 70.5 st
  render1(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).tuned);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 70.5f, s->voice(s->trackVoice(0)).hzKey[0]);  // osc 1's note
}

void test_tuning_eligible_types() {
  setEdo(p->tuning, 24);
  const InstrType yes[] = {InstrType::Synth, InstrType::String, InstrType::Fm};
  for (InstrType t : yes) {
    instrSetType(p->instruments[1], t);
    if (t == InstrType::Fm) fmSetMachine(p->instruments[1], static_cast<uint8_t>(FmMachine::Tone));
    p->tracks[1].instr = 1;
    s->startTrack(1);
    noteOn(0, 1, 61);
    render1(buf);
    TEST_ASSERT_TRUE(s->voice(s->trackVoice(1)).tuned);
    if (t != InstrType::Fm) TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.5f, s->voice(s->trackVoice(1)).hzKey[0]);
    noteOff(0, 1, 61);
  }
  // Drums, FM drum machines: not tuned.
  instrSetType(p->instruments[2], InstrType::Drum);
  p->tracks[2].instr = 2;
  s->startTrack(2);
  noteOn(0, 2, 60);
  instrSetType(p->instruments[3], InstrType::Fm);
  fmSetMachine(p->instruments[3], static_cast<uint8_t>(FmMachine::Kick));
  p->tracks[3].instr = 3;
  s->startTrack(3);
  noteOn(0, 3, 60);
  render1(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(2)).tuned);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(3)).tuned);
}

void test_tuning_kit_lane_and_note_slices_untuned() {
  setEdo(p->tuning, 24);
  // A KIT lane playing a CHIP instrument.
  Instrument& k = p->instruments[4];
  instrSetType(k, InstrType::Kit);
  k.kit[0].note = 61;
  k.kit[0].instr = 5;
  p->tracks[4].instr = 4;
  s->startTrack(4);
  noteOn(0, 4, 61);
  render1(buf);
  bool found = false;
  for (int i = 0; i < kVoices; ++i)
    if (s->voice(i).on && s->voice(i).track == 4) {
      found = true;
      TEST_ASSERT_FALSE(s->voice(i).tuned);
    }
  TEST_ASSERT_TRUE(found);
  // NOTE slices: the note picks a slice, no pitch.
  sampleInstr(400, kSynthRate);
  fourSlices();
  p->instruments[0].sliceMode = static_cast<uint8_t>(SliceMode::Note);
  noteOn(0, 0, 61);
  render1(buf);  // the 100-frame slice ends inside the block: find its (freed) voice
  found = false;
  for (int i = 0; i < kVoices; ++i)
    if (s->voice(i).track == 0 && s->voice(i).note == 61 && s->voice(i).sample) {
      found = true;
      TEST_ASSERT_FALSE(s->voice(i).tuned);
    }
  TEST_ASSERT_TRUE(found);
}


// ---- scenes / crossfader (Project::scenes, SynthModel::sceneLive / xfade, scene.h) ----

// Renders n blocks of L and R into out (2 * n * kBlock).
static void renderLR(Synth* x, int16_t* out, int n) {
  for (int k = 0; k < n; ++k) x->render(out + 2 * k * Synth::kBlock, out + (2 * k + 1) * Synth::kBlock);
}

void test_scene_empty_is_bit_identical() {
  for (auto& m : p->instruments) m.send = 60, m.rsend = 70;
  for (int xf = 0; xf <= 127; xf += 127) {
    Project* p1 = new Project(*p);
    Project* p2 = new Project(*p);
    p2->xfade = static_cast<uint8_t>(xf);
    sceneSetTrk(p2->scenes[4], 0, kScnVol, 0);  // a scene that is neither A nor B
    sceneSetMst(p2->scenes[4], kScnMain, 10);
    Synth* s1 = new Synth(*p1);
    Synth* s2 = new Synth(*p2);
    Synth* both[2] = {s1, s2};
    for (Synth* x : both) {
      Synth* keep = s;
      s = x;
      noteOn(0, 0, 60);
      noteOn(10, 1, 64);
      s = keep;
    }
    static int16_t a[2 * 30 * Synth::kBlock], b[2 * 30 * Synth::kBlock];
    renderLR(s1, a, 30);
    renderLR(s2, b, 30);
    TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof a);
    delete s1;
    delete s2;
    delete p1;
    delete p2;
  }
}

void test_scene_morphs_voice_fields() {
  p->tracks[0].instr = p->tracks[1].instr = 0;
  p->instruments[0].send = 10;
  Scene& b = p->sceneLive[1];
  sceneSetTrk(b, 0, kScnPan, 0);
  sceneSetTrk(b, 0, kScnDrv, 100);
  sceneSetTrk(b, 0, kScnDly, 127);
  sceneSetTrk(b, 0, kScnMod, 50);
  sceneSetTrk(b, 0, kScnGte, 90);
  sceneSetTrk(b, 0, kScnBit, 30);
  sceneSetTrk(b, 0, kScnSrr, 20);
  sceneSetTrk(b, 0, kScnDty, 2);
  p->xfade = 127;
  noteOn(0, 0, 60);
  noteOn(0, 1, 60);
  render1(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(0, v.pan);
  TEST_ASSERT_EQUAL(100, v.drive);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.f, v.send);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 50.f / 127.f, v.msend);
  TEST_ASSERT_EQUAL(90, v.gdepth);
  TEST_ASSERT_EQUAL(30, v.bits);
  TEST_ASSERT_EQUAL(20, v.srr);
  TEST_ASSERT_EQUAL(2, v.dmodel);
  // Track 1 plays the same instrument: its own (base) values.
  const Voice& w = s->voice(s->trackVoice(1));
  TEST_ASSERT_EQUAL(64, w.pan);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.f / 127.f, w.send);
  TEST_ASSERT_EQUAL(0, w.drive);
  // Halfway: continuous values in between, the drive model (discrete) on B from 64.
  p->xfade = 64;
  for (int k = 0; k < 30; ++k) render1(buf);
  TEST_ASSERT_EQUAL(32, s->voice(s->trackVoice(0)).pan);
  TEST_ASSERT_EQUAL(50, s->voice(s->trackVoice(0)).drive);
  TEST_ASSERT_EQUAL(2, s->voice(s->trackVoice(0)).dmodel);
  p->xfade = 0;
  for (int k = 0; k < 30; ++k) render1(buf);
  TEST_ASSERT_EQUAL(64, s->voice(s->trackVoice(0)).pan);
  TEST_ASSERT_EQUAL(0, s->voice(s->trackVoice(0)).dmodel);
}

// Two tracks on one instrument: only the track the scene holds feeds the delay.
static int echoStartOf(uint8_t track) {
  noteOn(0, track, 60);
  noteOff(Synth::kBlock - 1, track, 60);
  int gap = 0, at = -1;
  for (int k = 0; k < 60 && at < 0; ++k) {
    s->render(buf);
    for (int i = 0; i < Synth::kBlock && at < 0; ++i) {
      if (buf[i] == 0) ++gap;
      else if (gap > 1000) at = k * Synth::kBlock + i;
      else gap = 0;
    }
  }
  return at;
}

void test_scene_dly_send_per_track() {
  delaySetup(0);
  p->tracks[0].instr = p->tracks[1].instr = 0;
  sceneSetTrk(p->sceneLive[1], 1, kScnDly, 127);
  p->xfade = 127;
  TEST_ASSERT_EQUAL(-1, echoStartOf(0));  // track 0: the instrument's send 0, no echo
  const int at = echoStartOf(1);          // track 1: the scene's send, an echo 1/16 later
  TEST_ASSERT_INT_WITHIN(5, kSynthRate / 8, at);
}

void test_scene_lfo_pan_adds_to_morph() {
  Instrument& m = p->instruments[0];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Pan);
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoDepth = 63;
  m.lfoRate = 100;
  sceneSetTrk(p->sceneLive[1], 0, kScnPan, 32);
  p->xfade = 127;
  noteOn(0, 0, 60);
  int lo = 127, hi = 0;
  for (int k = 0; k < 200; ++k) {
    render1(buf);
    const int pn = s->voice(s->trackVoice(0)).pan;
    lo = pn < lo ? pn : lo;
    hi = pn > hi ? pn : hi;
  }
  TEST_ASSERT_EQUAL(0, lo);
  TEST_ASSERT_TRUE(hi > 80 && hi < 110);  // 32 + ~63, not the base's 64 + 63
}

void test_scene_vol_and_first_block_snap() {
  sceneSetTrk(p->sceneLive[1], 0, kScnVol, 0);
  p->xfade = 127;  // set before the first block: no glide from A
  noteOn(0, 0, 60);
  TEST_ASSERT_TRUE(silentBlocks(5));
  p->xfade = 0;
  for (int k = 0; k < 20; ++k) render1(buf);
  TEST_ASSERT_TRUE(rmsBlocks(4) > 1000.f);
}

void test_scene_reset_snaps() {
  sceneSetTrk(p->sceneLive[1], 0, kScnVol, 0);
  for (int k = 0; k < 2; ++k) render1(buf);
  p->xfade = 127;
  s->reset();
  noteOn(0, 0, 60);
  TEST_ASSERT_TRUE(silentBlocks(3));
}

void test_scene_smoothing_takes_about_30ms() {
  sceneSetTrk(p->sceneLive[1], 0, kScnVol, 0);
  noteOn(0, 0, 60);
  for (int k = 0; k < 4; ++k) render1(buf);
  float lo = 1e9f, hi = 0;  // the per-block RMS of the steady note
  for (int k = 0; k < 8; ++k) {
    const float r = rmsBlocks(1);
    lo = r < lo ? r : lo;
    hi = r > hi ? r : hi;
  }
  p->xfade = 127;
  int silentAt = -1;
  for (int k = 0; k < 20 && silentAt < 0; ++k) {
    const float r = rmsBlocks(1);
    if (k == 0) TEST_ASSERT_TRUE(r > 0.7f * lo);   // no step down
    if (k == 5) TEST_ASSERT_TRUE(r < 0.75f * hi);  // half way down
    TEST_ASSERT_TRUE(r <= hi * 1.05f);  // never above the steady note (its per-block RMS swings ~30% at 44.1 kHz)
    if (r == 0.f) silentAt = k;
  }
  // 30 ms = 10.3 blocks of 128 at 44.1 kHz.
  TEST_ASSERT_TRUE(silentAt >= 9 && silentAt <= 13);
}

void test_scene_cutoff_morph_and_flt_lock_wins() {
  for (auto& m : p->instruments) m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  noteOn(0, 0, 48);
  for (int k = 0; k < 4; ++k) render1(buf);
  const float open = rmsBlocks(8);
  sceneSetTrk(p->sceneLive[1], 0, kScnCut, 0);
  p->xfade = 127;
  for (int k = 0; k < 20; ++k) render1(buf);
  const float closed = rmsBlocks(8);
  TEST_ASSERT_TRUE(closed < 0.3f * open);
  stepStart(0, 0, 24, false);
  fx(0, 0, Fx::FLT, 127);  // the step's lock beats the scene
  for (int k = 0; k < 4; ++k) render1(buf);
  const float locked = rmsBlocks(8);
  TEST_ASSERT_FLOAT_WITHIN(0.1f * open, open, locked);
}

void test_scene_master_vol_morph() {
  p->masterVol = 100;
  noteOn(0, 0, 60, 40);
  for (int k = 0; k < 4; ++k) render1(buf);
  const float base = rmsBlocks(8);
  sceneSetMst(p->sceneLive[0], kScnMain, 50);
  for (int k = 0; k < 20; ++k) render1(buf);
  const float half = rmsBlocks(8);
  TEST_ASSERT_TRUE(half > 0.4f * base && half < 0.6f * base);
  p->xfade = 127;  // B does not hold MAIN: the base
  for (int k = 0; k < 20; ++k) render1(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.05f * base, base, rmsBlocks(8));
}


// ---- DRUM BDA..HHR (Drum2Voice) ----

static int peakBlocks(int n) {
  int pk = 0;
  for (int k = 0; k < n; ++k) {
    render1(buf);
    const int q = peakOf(buf);
    if (q > pk) pk = q;
  }
  return pk;
}

void test_drum2_each_machine_sounds() {
  for (int mc = kDrum2First; mc < static_cast<int>(DrumMachine::Count); ++mc) {
    if (mc == static_cast<int>(DrumMachine::Unused22)) continue;  // retired (BDF)
    s->reset();
    drumInstr(0, static_cast<DrumMachine>(mc));
    p->instruments[0].macro[kMacDec] = 40;
    noteOn(0, 0, 60);
    const int pk = peakBlocks(blocksMs(300));
    TEST_ASSERT_TRUE_MESSAGE(pk > 328, drumMachineName(static_cast<uint8_t>(mc)));  // louder than -40 dB
    for (int k = 0; k < blocksMs(6000) && s->activeVoices(); ++k) render1(buf);
    TEST_ASSERT_EQUAL_MESSAGE(0, s->activeVoices(), drumMachineName(static_cast<uint8_t>(mc)));
    TEST_ASSERT_TRUE(silentBlocks(2));
  }
}

void test_drum2_choke_same_track() {
  drumInstr(0, DrumMachine::Bda);
  p->instruments[0].macro[kMacDec] = 110;
  noteOn(0, 0, 60);
  renderMs(40);
  const int16_t prev = buf[Synth::kBlock - 1];
  noteOn(0, 0, 60);
  render1(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_TRUE(maxStep(buf, prev) < 0.2f * 32767);
}

void test_drum2_after_old_drum() {
  drumInstr(0, DrumMachine::Bd8);
  drumInstr(1, DrumMachine::Bda);
  const int order[] = {0, 1, 0};
  for (int i : order) {
    pgm(0, static_cast<uint8_t>(i));
    noteOn(0, 0, 60);
    const int pk = peakBlocks(blocksMs(60));
    TEST_ASSERT_TRUE(pk > 328);
  }
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_drum2_lock_macro() {
  drumInstr(0, DrumMachine::Bda);
  int16_t a[Synth::kBlock * 8];
  noteOn(0, 0, 60);
  for (int k = 0; k < 8; ++k) render1(a + k * Synth::kBlock);
  s->reset();
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::COL, 127);  // TONE
  noteOn(0, 0, 60);
  long diff = 0;
  for (int k = 0; k < 8; ++k) {
    render1(buf);
    for (int i = 0; i < Synth::kBlock; ++i) diff += abs(buf[i] - a[k * Synth::kBlock + i]);
  }
  TEST_ASSERT_EQUAL(127, s->voice(s->trackVoice(0)).lock[kMacCol]);
  TEST_ASSERT_TRUE(diff > 1000);
}

static float drum2Hz(uint8_t note) {
  s->reset();
  noteOn(0, 0, note);
  renderMs(30);  // past the click
  const int n = blocksMs(500);
  return crossings(n) / (n * float(Synth::kBlock) / kSynthRate);
}

void test_drum2_pitch_note() {
  drumInstr(0, DrumMachine::Bds);
  Instrument& m = p->instruments[0];
  m.macro[kMacDec] = 127;
  m.macro[kMacShp] = 0;  // DIRT
  m.macro[kMacSwp] = 0;  // SWEEP
  const float lo = drum2Hz(60), hi = drum2Hz(72);
  TEST_ASSERT_TRUE(lo > 20);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.f, hi / lo);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sample_ignores_note_off);
  RUN_TEST(test_looped_sample_ignores_note_off);
  RUN_TEST(test_preview_sample_stops_on_note_off);
  RUN_TEST(test_sample_choked_by_next_step);
  RUN_TEST(test_sample_chord_in_one_step_not_choked);
  RUN_TEST(test_sample_same_note_retrigger_chokes);
  RUN_TEST(test_sample_choke_leaves_other_tracks);
  RUN_TEST(test_mono_sample_restarts);
  RUN_TEST(test_event_offset);
  RUN_TEST(test_silent_without_events);
  RUN_TEST(test_note_on_lands_on_its_sample);
  RUN_TEST(test_centred_track_l_equals_r);
  RUN_TEST(test_track_peaks);
  RUN_TEST(test_events_sorted_by_offset);
  RUN_TEST(test_note_off_frees_voice_after_release);
  RUN_TEST(test_velocity_zero_is_note_off);
  RUN_TEST(test_track_volume_zero_is_silent);
  RUN_TEST(test_master_volume_zero_is_silent);
  RUN_TEST(test_instrument_volume_scales_output);
  RUN_TEST(test_guard_sheds_one_voice_per_block_down_to_min);
  RUN_TEST(test_guard_sheds_releasing_first_then_oldest);
  RUN_TEST(test_guard_cap_limits_new_notes_and_recovers);
  RUN_TEST(test_guard_ignores_single_spikes);
  RUN_TEST(test_guard_hard_spike_sheds_at_once);
  RUN_TEST(test_pool_budget_off_by_default);
  RUN_TEST(test_pool_budget_steals_oldest_chip);
  RUN_TEST(test_pool_budget_holds_through_ladder_steps);
  RUN_TEST(test_pool_budget_wt_synth_ladder);
  RUN_TEST(test_guard_panic_keeps_the_cap);
  RUN_TEST(test_guard_smoothed_load_sheds_without_a_spike);
  RUN_TEST(test_guard_reset_restores_pool);
  RUN_TEST(test_sixteen_voices_soft_clip);
  RUN_TEST(test_program_change_selects_instrument);
  RUN_TEST(test_default_instrument_from_track_cfg);
  RUN_TEST(test_start_track_resets_program);
  RUN_TEST(test_pitch_includes_transpose_and_fine);
  RUN_TEST(test_saw_frequency);
  RUN_TEST(test_pitch_bend_up_two_semitones);
  RUN_TEST(test_pitch_bend_center_is_neutral);
  RUN_TEST(test_all_off_releases_track);
  RUN_TEST(test_poly_limit_per_track);
  RUN_TEST(test_mono_glide);
  RUN_TEST(test_mono_without_glide_jumps);
  RUN_TEST(test_mono_glide_not_after_release);
  RUN_TEST(test_preview_track);
  RUN_TEST(test_queue_overflow_keeps_note_offs);
  RUN_TEST(test_sample_without_bank_is_silent);
  RUN_TEST(test_sample_resolved_by_name);
  RUN_TEST(test_fx_sld_slides_to_next_note);
  RUN_TEST(test_pool_sld_held_voice_takes_new_cost);
  RUN_TEST(test_pool_sld_into_expensive_note_steals);
  RUN_TEST(test_fx_vib_depth_and_period);
  RUN_TEST(test_fx_arp_three_notes_per_step);
  RUN_TEST(test_fx_vsl_fades_over_step);
  RUN_TEST(test_fx_vsl_on_empty_step_fades_sounding_note);
  RUN_TEST(test_fx_cut_kills_after_ticks);
  RUN_TEST(test_fx_ofs_stored_for_sample_note);
  RUN_TEST(test_start_track_clears_fx);
  RUN_TEST(test_sample_root_note_repeats_data);
  RUN_TEST(test_sample_octave_up_is_twice_as_fast);
  RUN_TEST(test_sample_half_rate_lasts_twice_as_long);
  RUN_TEST(test_sample_start_end_region);
  RUN_TEST(test_sample_forward_loop_sounds_longer);
  RUN_TEST(test_sample_ping_pong_turns_back);
  RUN_TEST(test_sample_reverse_starts_at_the_end);
  RUN_TEST(test_sample_reverse_loop_is_mirrored);
  RUN_TEST(test_sample_ofs_starts_from_the_middle);
  RUN_TEST(test_sample_mono_retrigger_after_release_restarts);
  RUN_TEST(test_sample_missing_is_silent);
  RUN_TEST(test_sample_bend_changes_speed);
  RUN_TEST(test_sample_gain_matches_chip);
  RUN_TEST(test_noise_clock_is_93x_note);
  RUN_TEST(test_master_volume_200_is_louder);
  RUN_TEST(test_fm_kick_sounds_and_ends_by_itself);
  RUN_TEST(test_fm_drum_ignores_note_off);
  RUN_TEST(test_fm_drum_chokes_on_same_track);
  RUN_TEST(test_fm_tone_poly_and_gated);
  RUN_TEST(test_fm_chord_is_one_voice);
  RUN_TEST(test_fm_lock_on_note_step);
  RUN_TEST(test_fm_lock_on_empty_step_hits_sounding_voice);
  RUN_TEST(test_fm_lock_ignored_on_chip);
  RUN_TEST(test_fm_lfo_on_pitch);
  RUN_TEST(test_fm_choke_mid_segment_has_no_jump);
  RUN_TEST(test_fm_drum_all_off_fades);
  RUN_TEST(test_fm_legato_from_chip_starts_fm_voice);
  RUN_TEST(test_fm_tone_after_drum_takes_its_own_env);
  RUN_TEST(test_fm_machine_switch_keeps_sounding_drum_one_shot);
  RUN_TEST(test_fm_lfo_random_starts_off_zero);
  RUN_TEST(test_fm_lfo_on_vol);
  RUN_TEST(test_fm_lfo_on_macro);
  RUN_TEST(test_fm_cache_static_note_computes_once);
  RUN_TEST(test_fm_cache_static_note_matches_recompute);
  RUN_TEST(test_fm_cache_lfo_macro_close_to_recompute);
  RUN_TEST(test_fm_cache_lfo_pitch_close_to_recompute);
  RUN_TEST(test_fm_sat_instrument);
  RUN_TEST(test_fm_sat_lock_on_note_step);
  RUN_TEST(test_fm_sat_lock_on_empty_step_hits_sounding_voice);
  RUN_TEST(test_fm_sat_lfo);
  RUN_TEST(test_fm_sat_lock_ignored_on_chip);
  RUN_TEST(test_fm_ninth_note_steals_oldest_fm_keeps_chip);
  RUN_TEST(test_fm_voices_never_exceed_cap);
  RUN_TEST(test_filter_off_is_bit_identical);
  RUN_TEST(test_lp_darkens_saw);
  RUN_TEST(test_filter_env_opens);
  RUN_TEST(test_max_reso_stays_finite);
  RUN_TEST(test_lp_bp_hp_render_unchanged);
  RUN_TEST(test_new_filter_types_sound_and_free);
  RUN_TEST(test_formant_ignores_key_track);
  RUN_TEST(test_comb_voices_have_own_lines);
  RUN_TEST(test_comb_key_track_tunes_to_note);
  RUN_TEST(test_flt_lock_on_note_step);
  RUN_TEST(test_flt_lock_without_note_hits_sounding_voice);
  RUN_TEST(test_res_lock_on_chip_sets_lock_bit);
  RUN_TEST(test_lfo_pitch_on_chip);
  RUN_TEST(test_lfo_cutoff_on_chip);
  RUN_TEST(test_lfo_macro_dest_ignored_on_chip);
  RUN_TEST(test_filter_turned_on_mid_note_starts_clean);
  RUN_TEST(test_lfo_restarts_on_mono_retrigger_after_release);
  RUN_TEST(test_lfo_retrig_off_shares_phase);
  RUN_TEST(test_lfo_retrig_on_restarts_per_note);
  RUN_TEST(test_lfo_transport_start_resets_tempo_retrig_off);
  RUN_TEST(test_lfo_transport_pause_holds_tempo_retrig_off);
  RUN_TEST(test_lfo_reset_keeps_retrig_off_phase);
  RUN_TEST(test_lfo_rate_mod);
  RUN_TEST(test_lfo_depth_mod);
  RUN_TEST(test_lfo_rtrg_mod);
  RUN_TEST(test_lfo_rtrg_mod_shared_source);
  RUN_TEST(test_lfo_rtrg_mod_shared_target);
  RUN_TEST(test_lfo_self_rtrg_ignored);
  RUN_TEST(test_lfo_byte_follows_formula);
  RUN_TEST(test_lfo_byte_lfo2_own_formula_shared_phase);
  RUN_TEST(test_lfo_byte_changes_with_a);
  RUN_TEST(test_lfo_byte_tempo_period);
  RUN_TEST(test_lfo_rtrg_survives_mid_tick_update);
  RUN_TEST(test_lfo_shared_target_ignores_voice_rtrg);
  RUN_TEST(test_lfo_shared_target_rate_sources);
  RUN_TEST(test_lfo_shared_target_depth_from_voice);
  RUN_TEST(test_filter_env_long_attack_not_dropped);
  RUN_TEST(test_filter_env_tail_dropped);
  RUN_TEST(test_drum_sounds_and_ends);
  RUN_TEST(test_drum_ignores_note_off);
  RUN_TEST(test_drum_mono_choke);
  RUN_TEST(test_drum_choke_mid_segment_has_no_jump);
  RUN_TEST(test_drum_dec_lock);
  RUN_TEST(test_drum_lock_on_empty_step_hits_sounding_voice);
  RUN_TEST(test_drum_filter_applies);
  RUN_TEST(test_heavy_voice_limit_counts_drum);
  RUN_TEST(test_filter_rings_out_after_release);
  RUN_TEST(test_drum_filter_rings_out);
  RUN_TEST(test_filter_tail_voice_is_reused_first);
  RUN_TEST(test_drum_cache_static_hit_computes_once);
  RUN_TEST(test_drum_cache_matches_recompute);
  RUN_TEST(test_drum_cache_follows_lock);
  RUN_TEST(test_slice_note_mode_plays_slice_of_note);
  RUN_TEST(test_slice_note_mode_out_of_range_is_silent);
  RUN_TEST(test_slice_note_mode_ignores_loop);
  RUN_TEST(test_slice_note_mode_reverse_inside_slice);
  RUN_TEST(test_slice_fx_mode_slc_picks_slice_with_pitch);
  RUN_TEST(test_slice_fx_mode_without_slc_plays_whole_region);
  RUN_TEST(test_slice_fx_mode_slc_out_of_range_plays_whole_region);
  RUN_TEST(test_slice_mode_without_slices_plays_whole_region);
  RUN_TEST(test_slc_ignored_when_slice_mode_off);
  RUN_TEST(test_preview_slc_plays_slice_in_any_mode);
  RUN_TEST(test_delay_echo_after_one_sixteenth);
  RUN_TEST(test_delay_send_zero_no_echo);
  RUN_TEST(test_delay_without_buffer_no_echo);
  RUN_TEST(test_delay_send_zero_mix_unchanged);
  RUN_TEST(test_dly_lock_on_note_step);
  RUN_TEST(test_dly_lock_without_note_hits_sounding_voice);
  RUN_TEST(test_delay_time_follows_tempo);
  RUN_TEST(test_synth_saw_sounds);
  RUN_TEST(test_synth_missing_wt_silent);
  RUN_TEST(test_synth_wt_plays);
  RUN_TEST(test_synth_wt_voices_are_heavy);
  RUN_TEST(test_synth_bl_voices_use_whole_pool);
  RUN_TEST(test_lofi_byte_sounds_and_env_ends);
  RUN_TEST(test_lofi_apc_sounds);
  RUN_TEST(test_lofi_byte_voices_are_heavy);
  RUN_TEST(test_lofi_apc_voices_use_whole_pool);
  RUN_TEST(test_lofi_macro_lock_applies);
  RUN_TEST(test_lofi_legato_from_chip_starts_lofi_voice);
  RUN_TEST(test_lofi_sync_step_is_1024);
  RUN_TEST(test_lofi_sync_octave_up);
  RUN_TEST(test_lofi_sync_octave_down);
  RUN_TEST(test_lofi_sync_ignores_semitones);
  RUN_TEST(test_lofi_sync_half_tempo);
  RUN_TEST(test_lofi_sync_off_follows_pitch);
  RUN_TEST(test_lofi_sync_follows_tempo_change);
  RUN_TEST(test_lofi_profile_stage_name);
  RUN_TEST(test_synth_mac_lock_applies);
  RUN_TEST(test_synth_mac_lock_on_empty_step_hits_sounding_voice);
  RUN_TEST(test_kit_sampler_lane_builds_scratch_instrument);
  RUN_TEST(test_kit_decay_zero_is_one_shot);
  RUN_TEST(test_kit_missing_sample_silent);
  RUN_TEST(test_kit_inst_lane_plays_instrument);
  RUN_TEST(test_kit_lane_voice_cost);
  RUN_TEST(test_kit_in_kit_lane_silent);
  RUN_TEST(test_kit_eight_lanes_sound_and_rehit_chokes);
  RUN_TEST(test_kit_unknown_note_silent);
  RUN_TEST(test_drive_zero_is_bit_identical);
  RUN_TEST(test_drive_changes_waveform);
  RUN_TEST(test_drv_lock_on_note_step);
  RUN_TEST(test_lfo_drive_dest);
  RUN_TEST(test_strip_drive_acts_on_track_sum);
  RUN_TEST(test_strip_fader_after_drive);
  RUN_TEST(test_strip_fader_in_pan_gains);
  RUN_TEST(test_strip_crush_from_track);
  RUN_TEST(test_strip_ignores_instrument_drive);
  RUN_TEST(test_strip_kit_shares_kit_send);
  RUN_TEST(test_strip_preview_insert);
  RUN_TEST(test_strip_drive_model_from_track);
  RUN_TEST(test_dty_lock_then_revert);
  RUN_TEST(test_strip_preview_drive_model);
  RUN_TEST(test_gate_off_bit_exact);
  RUN_TEST(test_gate_mask_positions);
  RUN_TEST(test_gate_rates);
  RUN_TEST(test_gate_transport_start_restarts);
  RUN_TEST(test_gate_song_clock_reanchors);
  RUN_TEST(test_gate_pause_holds);
  RUN_TEST(test_gate_stop_bypasses);
  RUN_TEST(test_gate_length);
  RUN_TEST(test_gate_ramp_slope);
  RUN_TEST(test_gte_lock_then_revert);
  RUN_TEST(test_gate_preview_track);
  RUN_TEST(test_strip_send_from_newest_voice);
  RUN_TEST(test_strip_drive_lock_crossfades);
  RUN_TEST(test_strip_send_lock_ramps);
  RUN_TEST(test_ringing_tail_keeps_strip);
  RUN_TEST(test_lfo_send_dests);
  RUN_TEST(test_lfo_rsend_dest);
  RUN_TEST(test_lfo_crush_dests);
  RUN_TEST(test_lfo_reso_dest);
  RUN_TEST(test_lfo_fenv_dest);
  RUN_TEST(test_lfo_duty_dest);
  RUN_TEST(test_lfo_fine_dest);
  RUN_TEST(test_lfo_syn_sub_dest);
  RUN_TEST(test_lfo_syn_noise_dest);
  RUN_TEST(test_lfo_syn_semi2_dest);
  RUN_TEST(test_lfo_syn_dests_ignored_on_chip);
  RUN_TEST(test_rsend_zero_same_with_and_without_buffer);
  RUN_TEST(test_rsend_makes_tail);
  RUN_TEST(test_rvb_lock_on_note_step);
  RUN_TEST(test_comp_sidechain_track);
  RUN_TEST(test_comp_sidechain_key_track_not_squashed);
  RUN_TEST(test_comp_off_is_bit_identical);
  RUN_TEST(test_arm_up_down_updown);
  RUN_TEST(test_arm_random_stays_in_set);
  RUN_TEST(test_arp_chord_cycles_chord_notes);
  RUN_TEST(test_velcut_opens_filter_with_velocity);
  RUN_TEST(test_velmac_shifts_decay);
  RUN_TEST(test_lfo_note_mode_unchanged);
  RUN_TEST(test_lfo_depth_lock_turns_lfo_on);
  RUN_TEST(test_lfo_select_targets_lfo_2);
  RUN_TEST(test_lfo_rate_lock_changes_speed);
  RUN_TEST(test_lfo_reset_fx_sets_phase);
  RUN_TEST(test_lfo_lock_on_empty_step_hits_sounding_voice);
  RUN_TEST(test_lfo_wave_lock_sounds);
  RUN_TEST(test_stereo_centre_equals_mono);
  RUN_TEST(test_stereo_centre_drum_sides_equal);
  RUN_TEST(test_pan_hard_left_silences_right);
  RUN_TEST(test_pan_hard_right_silences_left);
  RUN_TEST(test_pan_hard_left_constant_power);
  RUN_TEST(test_fx_pan_lock_on_note_step);
  RUN_TEST(test_lfo_pan_dest_alternates);
  RUN_TEST(test_pan_change_ramps);
  RUN_TEST(test_pan_legato_lock_ramps);
  RUN_TEST(test_pan_fx_mid_segment_lands_at_boundary);
  RUN_TEST(test_reverb_makes_sides_differ);
  RUN_TEST(test_perf_idle_bit_exact);
  RUN_TEST(test_perf_message_crush);
  RUN_TEST(test_reverb_throw_with_level_zero);
  RUN_TEST(test_perf_repeat_loops_a_sixteenth);
  RUN_TEST(test_perf_bad_messages_ignored);
  RUN_TEST(test_perf_transport_start_resets_grid);
  RUN_TEST(test_queue_overflow_keeps_perf_releases);
  RUN_TEST(test_perf_step_clock_reanchors_grid);
  RUN_TEST(test_perf_step_clock_idle_bit_exact);
  RUN_TEST(test_perf_freeze_ducks_mix);
  RUN_TEST(test_perf_return_floor_decays);
  RUN_TEST(test_perf_reset_drops_throw);
  RUN_TEST(test_fol_silent_source_bit_exact);
  RUN_TEST(test_synth_fold_ring_off_bit_exact);
  RUN_TEST(test_synth_fold_changes_output);
  RUN_TEST(test_synth_lfo_fold_changes_output);
  RUN_TEST(test_synth_follower_ring_changes_output);
  RUN_TEST(test_fol_cutoff_ducks_and_recovers);
  RUN_TEST(test_fol_positive_amount_opens);
  RUN_TEST(test_fol_vol_ducks_strip);
  RUN_TEST(test_fol_vol_ramped);
  RUN_TEST(test_fol_attack_release_times);
  RUN_TEST(test_fol_order_independent);
  RUN_TEST(test_fol_ghost_key_and_self_vol);
  RUN_TEST(test_fol_pan_and_lfo_depth);
  RUN_TEST(test_fol_off_cases_and_preview);
  RUN_TEST(test_fol_kit_track_strip);
  RUN_TEST(test_profiler_arms_at_block_start);
  RUN_TEST(test_drum_col_lock_changes_output);
  RUN_TEST(test_string_sounds_at_note_pitch);
  RUN_TEST(test_string_voices_are_heavy);
  RUN_TEST(test_string_done_frees_held_voice);
  RUN_TEST(test_string_dcy_lock_changes_decay);
  RUN_TEST(test_string_lfo_damp_changes_decay);
  RUN_TEST(test_string_comb_filter_is_off);
  RUN_TEST(test_string_after_synth_same_track_sounds);
  RUN_TEST(test_string_profile_stage);
  RUN_TEST(test_string_vsl_changes_level);
  RUN_TEST(test_string_legato_with_synth_and_fm);
  RUN_TEST(test_string_legato_restrike_adds_energy);
  RUN_TEST(test_tuning_12tet_voices_untuned);
  RUN_TEST(test_tuning_12_edo_matches_12tet);
  RUN_TEST(test_tuning_24_edo_chip_and_fine);
  RUN_TEST(test_tuning_just_auto_root);
  RUN_TEST(test_tuning_glide_ends_on_degree);
  RUN_TEST(test_tuning_table_change_applies_live);
  RUN_TEST(test_tuning_sample_by_note);
  RUN_TEST(test_tuning_synth_sounds_tuned);
  RUN_TEST(test_tuning_eligible_types);
  RUN_TEST(test_tuning_kit_lane_and_note_slices_untuned);
  RUN_TEST(test_scene_empty_is_bit_identical);
  RUN_TEST(test_scene_morphs_voice_fields);
  RUN_TEST(test_scene_dly_send_per_track);
  RUN_TEST(test_scene_lfo_pan_adds_to_morph);
  RUN_TEST(test_scene_vol_and_first_block_snap);
  RUN_TEST(test_scene_reset_snaps);
  RUN_TEST(test_scene_smoothing_takes_about_30ms);
  RUN_TEST(test_scene_cutoff_morph_and_flt_lock_wins);
  RUN_TEST(test_scene_master_vol_morph);
  RUN_TEST(test_drum2_each_machine_sounds);
  RUN_TEST(test_drum2_choke_same_track);
  RUN_TEST(test_drum2_after_old_drum);
  RUN_TEST(test_drum2_lock_macro);
  RUN_TEST(test_drum2_pitch_note);
  return UNITY_END();
}
