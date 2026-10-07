#include <math.h>
#include <string.h>
#include <unity.h>
#include "scale.h"
#include "slices.h"
#include "synth.h"
#include "synth_drum_machines.h"
#include "wt_builtin.h"

using namespace mt;

static Project* p;
static Synth* s;
struct ArrayBank;
static ArrayBank* ab = nullptr;
static void freeBank();
static int16_t buf[Synth::kBlock];

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
    s->render(buf);
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
    s->render(buf);
    for (int i = 0; i < Synth::kBlock; ++i) {
      if (!first && prev >= 0 && buf[i] < 0) ++c;
      prev = buf[i];
      first = false;
    }
  }
  return c;
}

constexpr int kBlocksPerSec = kSynthRate / Synth::kBlock;  // 250

void test_event_offset() {
  const uint64_t bt = 1000000;
  TEST_ASSERT_EQUAL(0, eventOffset(bt, bt));
  TEST_ASSERT_EQUAL(10, eventOffset(bt + 313, bt));  // 31.25 us per sample
  TEST_ASSERT_EQUAL(0, eventOffset(bt - 100, bt));   // late: as soon as possible
  TEST_ASSERT_EQUAL(125, eventOffset(bt + 3900, bt));
  TEST_ASSERT_EQUAL(-1, eventOffset(bt + 4000, bt));  // next block
  TEST_ASSERT_EQUAL(-1, eventOffset(bt + 100000, bt));
}

void test_silent_without_events() {
  TEST_ASSERT_TRUE(silentBlocks(3));
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_track_peaks() {
  float pk[kTracks];
  s->setMeters(true);
  s->takeTrackPeaks(pk);
  noteOn(0, 2, 60);
  s->render(buf);
  s->takeTrackPeaks(pk);
  for (int t = 0; t < kTracks; ++t) {
    if (t == 2) TEST_ASSERT_TRUE(pk[t] > 0.01f && pk[t] <= 1.f);
    else TEST_ASSERT_EQUAL_FLOAT(0.f, pk[t]);
  }
  s->takeTrackPeaks(pk);  // cleared by the read
  TEST_ASSERT_EQUAL_FLOAT(0.f, pk[2]);
  s->setMeters(false);  // off: no peaks for a voice without sends
  s->render(buf);
  s->takeTrackPeaks(pk);
  TEST_ASSERT_EQUAL_FLOAT(0.f, pk[2]);
}

void test_note_on_lands_on_its_sample() {
  noteOn(64, 0, 60);
  s->render(buf);
  for (int i = 0; i < 64; ++i) TEST_ASSERT_EQUAL(0, buf[i]);
  TEST_ASSERT_NOT_EQUAL(0, buf[64]);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_events_sorted_by_offset() {
  noteOn(100, 0, 60);
  noteOff(110, 0, 60);
  noteOn(20, 1, 72);  // queued later, plays earlier
  s->render(buf);
  for (int i = 0; i < 20; ++i) TEST_ASSERT_EQUAL(0, buf[i]);
  TEST_ASSERT_NOT_EQUAL(0, buf[20]);
}

void test_note_off_frees_voice_after_release() {
  p->instruments[0].release = 40;  // envTimeMs(40)
  const int relMs = envTimeMs(40);
  noteOn(0, 0, 60);
  s->render(buf);
  noteOff(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  const int blocks = relMs * kSynthRate / 1000 / Synth::kBlock + 2;
  for (int k = 0; k < blocks; ++k) s->render(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
  TEST_ASSERT_TRUE(silentBlocks(1));
}

void test_velocity_zero_is_note_off() {
  noteOn(0, 0, 60);
  s->render(buf);
  noteOn(0, 0, 60, 0);
  s->render(buf);
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
  s->render(buf);
  int loud = 0;
  for (int i = 0; i < Synth::kBlock; ++i) loud = abs(buf[i]) > loud ? abs(buf[i]) : loud;
  delete s;
  s = new Synth(*p);
  p->instruments[0].vol = 32;
  noteOn(0, 0, 60);
  s->render(buf);
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
  s->render(buf);
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
  s->setLoad(1.2f);  // one block over budget
  TEST_ASSERT_EQUAL(full - 1, liveVoices());
  TEST_ASSERT_EQUAL(full - 1, s->voiceCap());
  for (int k = 0; k < 100; ++k) {
    s->setLoad(1.2f);
    s->render(buf);
  }
  TEST_ASSERT_EQUAL(Synth::kMinVoices, liveVoices());
  TEST_ASSERT_TRUE(s->activeVoices() <= Synth::kMinVoices + 1);  // the faded ones are freed
}

void test_guard_sheds_releasing_first_then_oldest() {
  fillVoices();
  noteOff(0, 5, 75);  // a releasing voice
  s->render(buf);
  s->setLoad(1.2f);
  for (int i = 0; i < kVoices; ++i) {
    const Voice& v = s->voice(i);
    if (v.stolen) TEST_ASSERT_TRUE(v.track == 5 && v.note == 75);
  }
  int oldest = -1;  // next: the oldest sounding one
  for (int i = 0; i < kVoices; ++i) {
    const Voice& v = s->voice(i);
    if (v.on && !v.stolen && (oldest < 0 || v.age < s->voice(oldest).age)) oldest = i;
  }
  s->setLoad(1.2f);
  TEST_ASSERT_TRUE(s->voice(oldest).stolen);
  int stolen = 0;
  for (int i = 0; i < kVoices; ++i) stolen += s->voice(i).stolen;
  TEST_ASSERT_EQUAL(2, stolen);
}

void test_guard_cap_limits_new_notes_and_recovers() {
  fillVoices();
  for (int k = 0; k < 6; ++k) s->setLoad(1.2f);
  const int cap = s->voiceCap();
  for (int k = 0; k < 4; ++k) s->render(buf);  // the fades end
  for (int t = 0; t < kTracks; ++t) noteOn(0, static_cast<uint8_t>(t), static_cast<uint8_t>(90 + t));
  s->render(buf);
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
    s->render(buf);
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
  s->render(buf);
  const int v = s->trackVoice(3);
  TEST_ASSERT_TRUE(v >= 0);
  TEST_ASSERT_EQUAL(5, s->voiceInstr(v));
  send(0, 3, 0xC0, 100, 0, 2);  // out of range: last instrument
  noteOn(0, 3, 62);
  s->render(buf);
  TEST_ASSERT_EQUAL(kInstruments - 1, s->voiceInstr(s->trackVoice(3)));
}

void test_default_instrument_from_track_cfg() {
  p->tracks[4].instr = 11;
  noteOn(0, 4, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL(11, s->voiceInstr(s->trackVoice(4)));
}

void test_start_track_resets_program() {
  send(0, 3, 0xC0, 5, 0, 2);
  send(0, 3, 0xFE, 0, 0, 1);
  noteOn(0, 3, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL(3, s->voiceInstr(s->trackVoice(3)));
}

void test_pitch_includes_transpose_and_fine() {
  p->instruments[0].transpose = -12;
  p->instruments[0].fine = 50;
  noteOn(0, 0, 60);
  s->render(buf);
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
  s->render(buf);
  send(0, 0, 0xFF, 0, 0, 1);
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());  // release 0: track 0 voices are gone
  TEST_ASSERT_EQUAL(1, s->voice(s->trackVoice(1)).track);
}

void test_poly_limit_per_track() {
  for (int n = 0; n < 6; ++n) noteOn(0, 0, static_cast<uint8_t>(60 + n));
  s->render(buf);
  TEST_ASSERT_EQUAL(kPolyPerTrack, s->activeVoices());
}

void test_mono_glide() {
  Instrument& m = p->instruments[0];
  m.mono = true;
  m.glide = 25;  // 100 ms
  noteOn(0, 0, 60);
  s->render(buf);
  noteOn(0, 0, 72);
  for (int k = 0; k < 12; ++k) s->render(buf);  // ~48 ms
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  const float mid = s->voice(s->trackVoice(0)).pitch;
  TEST_ASSERT_TRUE(mid > 62.f && mid < 70.f);
  for (int k = 0; k < 20; ++k) s->render(buf);  // past 100 ms
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 72.f, s->voice(s->trackVoice(0)).pitch);
}

void test_mono_without_glide_jumps() {
  p->instruments[0].mono = true;
  noteOn(0, 0, 60);
  s->render(buf);
  noteOn(0, 0, 72);
  noteOff(1, 0, 60);  // the old note's off must not cut the legato note
  s->render(buf);
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
  s->render(buf);
  noteOff(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  noteOn(0, 0, 72);  // not overlapping: no glide
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 72.f, s->voice(s->trackVoice(0)).pitch);
}

void test_preview_track() {
  p->instruments[2].vol = 127;
  send(0, kPreviewTrack, 0xC0, 2, 0, 2);
  noteOn(0, kPreviewTrack, 60);
  s->render(buf);
  TEST_ASSERT_NOT_EQUAL(0, buf[5]);
  TEST_ASSERT_EQUAL(2, s->voiceInstr(s->trackVoice(kPreviewTrack)));
  TEST_ASSERT_FALSE(s->event(0, kSynthTracks, reinterpret_cast<const uint8_t*>("\x90\x3C\x7F"), 3));
}

void test_queue_overflow_keeps_note_offs() {
  noteOn(0, 0, 60);
  s->render(buf);
  for (int i = 0; i < Synth::kMaxEvents; ++i) send(10, 1, 0xE0, 0, 0x40);
  const uint8_t off[3] = {0x80, 60, 0};
  TEST_ASSERT_TRUE(s->event(5, 0, off, 3));  // evicts a control
  const uint8_t on[3] = {0x90, 61, 100};
  TEST_ASSERT_FALSE(s->event(5, 1, on, 3));  // full: dropped
  s->render(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

struct FakeBank : SampleSource {
  mutable char asked[kSampleNameMax + 1] = {0};
  int16_t data[1000] = {0};
  const int16_t* find(const char* name, uint32_t& frames, uint32_t& rate) const override {
    strncpy(asked, name, kSampleNameMax);
    if (strcmp(name, "kick") != 0) return nullptr;
    frames = 1000;
    rate = 16000;
    return data;
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
  s->render(buf);
  TEST_ASSERT_EQUAL_STRING("kick", bank.asked);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_TRUE(v.sample);
  TEST_ASSERT_EQUAL_PTR(bank.data, v.smp);
  TEST_ASSERT_EQUAL(1000u, v.smpLen);
  TEST_ASSERT_EQUAL(16000u, v.smpRate);
  strcpy(p->instruments[0].sample, "snare");  // not in the bank
  noteOn(0, 0, 62);
  s->render(buf);
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
  s->render(buf);
  s->render(buf);
  const int chip = blockPeak();
  delete s;
  s = new Synth(*p);
  FakeBank bank;
  for (auto& x : bank.data) x = 8192;
  s->setBank(&bank);
  p->instruments[0].type = InstrType::Sample;
  strcpy(p->instruments[0].sample, "kick");
  noteOn(0, 0, 60);
  s->render(buf);
  s->render(buf);
  TEST_ASSERT_INT_WITHIN(chip / 50, chip, blockPeak());
}

// The LFSR is clocked 93x the note frequency: METAL (period 93) sounds at the played pitch.
void test_noise_clock_is_93x_note() {
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Metal);
  noteOn(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, noteHz(60) * 93.f / kSynthRate, s->voice(s->trackVoice(0)).inc);
  noteOff(0, 0, 60);
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Noise);
  noteOn(0, 0, 127);
  s->render(buf);
  TEST_ASSERT_EQUAL_FLOAT(8.f, s->voice(s->trackVoice(0)).inc);  // capped: 8 LFSR steps per sample
}

// Volume above 100 % adds up to +6 dB (x2 before the soft clip).
void test_master_volume_200_is_louder() {
  p->instruments[0].wave = static_cast<uint8_t>(Wave::Pulse);
  noteOn(0, 0, 60);
  s->render(buf);
  s->render(buf);
  const int at100 = blockPeak();
  p->masterVol = 200;
  s->render(buf);
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
  for (int k = 0; k < ms * kSynthRate / 1000 / Synth::kBlock; ++k) s->render(buf);
}

void test_fx_sld_slides_to_next_note() {
  noteOn(0, 0, 60);
  s->render(buf);
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
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(noteHz(60) * 0.01f, noteHz(60), voiceHz(0));
}

void test_fx_vib_depth_and_period() {
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::VIB, 0x8F);  // 4 Hz, +-2 semitones
  noteOn(0, 0, 60);
  float lo = 100, hi = 0, prev = 60;
  int firstUp = -1, lastUp = -1, ups = 0;
  for (int k = 0; k < kBlocksPerSec; ++k) {
    s->render(buf);
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
  const float periodMs = (lastUp - firstUp) * 4.f / (ups - 1);
  TEST_ASSERT_FLOAT_WITHIN(250.f * 0.05f, 250.f, periodMs);
  // A new note without VIB ends it.
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  for (int k = 0; k < 40; ++k) {
    s->render(buf);
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
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.f, s->voice(s->trackVoice(0)).gain);
}

void test_fx_vsl_on_empty_step_fades_sounding_note() {
  p->bpm = 150;
  noteOn(0, 0, 60, 127);
  s->render(buf);
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
  for (int k = 0; k < 8; ++k) s->render(all + k * Synth::kBlock);
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
  s->render(buf);
  TEST_ASSERT_EQUAL(128, s->voice(s->trackVoice(0)).ofs);
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 62);
  s->render(buf);
  TEST_ASSERT_EQUAL(0, s->voice(s->trackVoice(0)).ofs);
}

void test_start_track_clears_fx() {
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::VIB, 0x8F);
  noteOn(0, 0, 60);
  s->render(buf);
  s->startTrack(0);
  for (int k = 0; k < 40; ++k) {
    s->render(buf);
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
  s->render(buf);
  for (int i = 0; i < Synth::kBlock; ++i)
    if (buf[i] != 0) return false;
  return true;
}

void test_fm_kick_sounds_and_ends_by_itself() {
  useFm(0, FmMachine::Kick);
  p->instruments[0].macro[kMacDec] = 20;  // ~14 ms
  noteOn(0, 0, 60);
  TEST_ASSERT_FALSE(blockSilent());
  for (int k = 0; k < 30; ++k) s->render(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_fm_drum_ignores_note_off() {
  useFm(0, FmMachine::Kick);
  p->instruments[0].macro[kMacDec] = 110;
  noteOn(0, 0, 60);
  noteOff(10, 0, 60);
  for (int k = 0; k < 4; ++k) s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_FALSE(blockSilent());
}

void test_fm_drum_chokes_on_same_track() {
  useFm(0, FmMachine::Hat);
  p->instruments[0].mono = false;  // ignored: drums are mono
  noteOn(0, 0, 60);
  noteOn(50, 0, 64);
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_fm_tone_poly_and_gated() {
  useFm(0, FmMachine::Tone);
  p->instruments[0].mono = false;
  p->instruments[0].sustain = 127;
  noteOn(0, 0, 60);
  noteOn(0, 0, 64);
  noteOn(0, 0, 67);
  for (int k = 0; k < 50; ++k) s->render(buf);
  TEST_ASSERT_EQUAL(3, s->activeVoices());  // held
  noteOff(0, 0, 60);
  noteOff(0, 0, 64);
  noteOff(0, 0, 67);
  s->render(buf);
  s->render(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());  // release 0
}

void test_fm_chord_is_one_voice() {
  useFm(0, FmMachine::Chord);
  p->instruments[0].mono = false;
  noteOn(0, 0, 60);
  noteOn(0, 0, 65);
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_fm_lock_on_note_step() {
  useFm(0, FmMachine::Tone);
  stepStart(0, 0, 6, true);
  fx(0, 0, Fx::COL, 99);
  noteOn(0, 0, 60);
  s->render(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(1 << kMacCol, v.lockMask);
  TEST_ASSERT_EQUAL(99, v.lock[kMacCol]);
  stepStart(0, 0, 6, true);  // next step: no lock
  noteOn(0, 0, 62);
  s->render(buf);
  TEST_ASSERT_EQUAL(0, s->voice(s->trackVoice(0)).lockMask);
}

void test_fm_lock_on_empty_step_hits_sounding_voice() {
  useFm(0, FmMachine::Tone);
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  s->render(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::SHP, 127);
  s->render(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(1 << kMacShp, v.lockMask);
  TEST_ASSERT_EQUAL(127, v.lock[kMacShp]);
}

void test_fm_lock_ignored_on_chip() {
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  s->render(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::DCY, 1);
  s->render(buf);
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

static int peakOf(const int16_t* a) {
  int m = 0;
  for (int i = 0; i < Synth::kBlock; ++i) m = abs(a[i]) > m ? abs(a[i]) : m;
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
    s->render(buf);
    peak = peakOf(buf) > peak ? peakOf(buf) : peak;
  }
  for (int k = 0; k < 60; ++k) s->render(buf);  // decays to ~0.2
  const int16_t prev = buf[Synth::kBlock - 1];
  noteOn(50, 0, 72);  // choke inside a control segment
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_TRUE(peakOf(buf) > peak / 2);  // retriggered
  TEST_ASSERT_TRUE(maxStep(buf, prev) < peak / 8);
}

void test_fm_drum_all_off_fades() {
  useFm(0, FmMachine::Kick);
  p->instruments[0].macro[kMacDec] = 110;
  noteOn(0, 0, 60);
  for (int k = 0; k < 4; ++k) s->render(buf);
  const int16_t prev = buf[Synth::kBlock - 1];
  send(0, 0, 0xFF, 0, 0, 1);  // all off (stop)
  s->render(buf);
  TEST_ASSERT_TRUE(buf[0] != 0 || buf[1] != 0);  // a short fade, not a cut
  TEST_ASSERT_TRUE(maxStep(buf, prev) < 2000);
  s->render(buf);
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
  for (int k = 0; k < 100; ++k) s->render(buf);
  const float hz = lfoHz(f.lfoRate);
  const float fresh = hz * (Synth::kBlock - Synth::kControl) / kSynthRate;  // first update precedes the note
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).lfoPhase[0] > fresh * 4);
  pgm(0, 0);
  noteOn(0, 0, 62);  // CHIP legato on the same voice
  s->render(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).fm);
  pgm(0, 1);
  noteOn(0, 0, 64);  // FM legato from a CHIP voice: a new FM note
  s->render(buf);
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
  s->render(buf);
  pgm(0, 1);
  noteOn(0, 0, 60);  // legato into the sounding kick's voice
  for (int k = 0; k < 10; ++k) s->render(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());  // TONE's env, not the drum gate
}

void test_fm_machine_switch_keeps_sounding_drum_one_shot() {
  useFm(0, FmMachine::Kick);
  p->instruments[0].macro[kMacDec] = 40;  // ~40 ms
  noteOn(0, 0, 60);
  s->render(buf);
  fmSetMachine(p->instruments[0], static_cast<uint8_t>(FmMachine::Tone));  // UI edit while it sounds
  p->instruments[0].sustain = 127;
  for (int k = 0; k < 100; ++k) s->render(buf);  // 400 ms
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
  s->render(buf);
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
  for (int k = 0; k < 50; ++k) s->render(buf);
  const int loud = blockPeak();
  for (int k = 0; k < 250; ++k) s->render(buf);
  const int quiet = blockPeak();
  TEST_ASSERT_TRUE(loud > quiet * 4);
}

void test_fm_lfo_on_macro() {
  useFm(0, FmMachine::Tone);
  Instrument& m = p->instruments[0];
  m.macro[kMacCol] = 0;  // pure sine
  noteOn(0, 0, 69);
  static int16_t a[Synth::kBlock * 20];
  for (int k = 0; k < 20; ++k) s->render(a + k * Synth::kBlock);
  s->reset();
  m.lfoWave = static_cast<uint8_t>(LfoWave::Square);
  m.lfoRate = 30;
  m.lfoDepth = 63;  // COLOR 0 -> ~63 in the first half cycle
  m.lfoDest = static_cast<uint8_t>(LfoDest::Col);
  noteOn(0, 0, 69);
  long diff = 0, sum = 0;
  for (int k = 0; k < 20; ++k) {
    s->render(buf);
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
    s->render(buf);
    for (int i = 0; i < Synth::kBlock; ++i) acc += double(buf[i]) * buf[i];
  }
  return sqrtf(static_cast<float>(acc / (n * Synth::kBlock)));
}

void test_filter_off_is_bit_identical() {
  noteOn(0, 0, 72);
  int16_t ref[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) s->render(ref + k * Synth::kBlock);
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
    s->render(buf);
    TEST_ASSERT_EQUAL_INT16_ARRAY(ref + k * Synth::kBlock, buf, Synth::kBlock);
  }
}

void test_lp_darkens_saw() {
  noteOn(0, 0, 84);
  s->render(buf);
  const float open = rmsBlocks(8);
  s->reset();
  p->instruments[0].fltMode = static_cast<uint8_t>(FltMode::Lp);
  p->instruments[0].cutoff = 20;  // ~56 Hz, far below C6
  noteOn(0, 0, 84);
  s->render(buf);
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
  for (int k = 0; k < 50; ++k) s->render(buf);  // softClip keeps int16 in range; must not hang / NaN
  TEST_ASSERT_TRUE(s->activeVoices() == 1);
  Svf f = s->voice(s->trackVoice(0)).flt;
  TEST_ASSERT_TRUE(isfinite(f.process(0)));
}

void test_flt_lock_on_note_step() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 127;
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::FLT, 15);
  noteOn(0, 0, 84);
  s->render(buf);
  const float locked = rmsBlocks(8);
  s->reset();
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 84);  // a new step drops the lock
  s->render(buf);
  TEST_ASSERT_TRUE(locked < rmsBlocks(8) * 0.3f);
}

void test_flt_lock_without_note_hits_sounding_voice() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  noteOn(0, 0, 84);
  s->render(buf);
  const float open = rmsBlocks(4);
  stepStart(0, 0, 24, false);
  fx(0, 0, Fx::FLT, 15);
  for (int k = 0; k < 8; ++k) s->render(buf);  // the filter's state rings down from the open cutoff
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
  for (int k = 0; k < 8; ++k) s->render(buf);
  m.fltMode = static_cast<uint8_t>(FltMode::Off);
  for (int k = 0; k < 2; ++k) s->render(buf);
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
  for (int k = 0; k < 50; ++k) s->render(buf);  // ~0.2 cycle
  noteOff(0, 0, 69);
  s->render(buf);
  noteOn(0, 0, 69);
  s->render(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).lfoPhase[0] < 0.02f);
}

void test_filter_env_long_attack_not_dropped() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.fenv = 63;
  m.fAtk = 127;  // 10 s: the envelope starts below -60 dB
  noteOn(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).fenvDone);
}

void test_filter_env_tail_dropped() {
  Instrument& m = p->instruments[0];
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.fenv = 63;
  m.fDec = 70;  // ~160 ms to -60 dB
  noteOn(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).fenvDone);
  for (int k = 0; k < 60; ++k) s->render(buf);  // 240 ms
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).fenvDone);
}

void test_res_lock_on_chip_sets_lock_bit() {
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  s->render(buf);
  stepStart(0, 0, 24, false);
  fx(0, 0, Fx::RES, 99);
  s->render(buf);
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
  s->render(buf);
  const float swept = rmsBlocks(8);
  s->reset();
  m.lfoDepth = 0;
  noteOn(0, 0, 84);
  s->render(buf);
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
  const int16_t* find(const char* name, uint32_t& frames, uint32_t& r) const override {
    if (strcmp(name, "s") != 0) return nullptr;
    frames = n;
    r = rate;
    return data;
  }
};
static void freeBank() {
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
    s->render(a + k * Synth::kBlock);
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
  s->render(buf);
  noteOff(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  noteOn(0, 0, 60);
  s->render(out);
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
  for (int k = 0; k < n; ++k) s->render(a + k * Synth::kBlock);
}

static int16_t fa[Synth::kBlock * 250], fb[Synth::kBlock * 250];

void test_fm_cache_static_note_computes_once() {
  useFm(0, FmMachine::Tone);
  p->instruments[0].sustain = 127;
  noteOn(0, 0, 69);
  const uint32_t c0 = s->fmMachineCalls();
  for (int k = 0; k < 50; ++k) s->render(buf);
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
  TEST_ASSERT_TRUE(diff < peak / 50);
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
  s->render(buf);
  for (int t = 1; t <= 2; ++t)
    for (int k = 0; k < 4; ++k) {
      noteOn(0, static_cast<uint8_t>(t), static_cast<uint8_t>(60 + t * 10 + k));
      s->render(buf);
    }
  TEST_ASSERT_EQUAL(8, fmVoices());
  noteOn(0, 3, 90);  // 9th: steals track 1's note 70
  s->render(buf);
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
    s->render(buf);
    int load = 0, fading = 0;
    for (int i = 0; i < kVoices; ++i) {
      const Voice& x = s->voice(i);
      load += heavyLoad(x);
      fading += x.on && x.fm && x.stolen;
    }
    TEST_ASSERT_TRUE(load <= kFmVoiceMax);
    // The fade is exactly one block (4 ms at 32 kHz): a steal mid-block leaves at most one
    // victim fading into the next one.
    TEST_ASSERT_TRUE(fmVoices() <= kFmVoiceMax + 1);
    // One steal per block at most; a kStealMs fade spans a known number of blocks.
    constexpr int kStealBlocks = (kStealMs * kSynthRate / 1000 + Synth::kBlock - 1) / Synth::kBlock + 1;
    TEST_ASSERT_TRUE(fading <= kStealBlocks);
  }
  // Stolen voices finish their fade: no steal outlives kStealMs.
  for (int k = 0; k < 2 * (kStealMs * kSynthRate / 1000) / Synth::kBlock + 2; ++k) s->render(buf);
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
  for (int k = 0; k < 400 && s->activeVoices(); ++k) s->render(buf);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_drum_ignores_note_off() {
  drumInstr(0, DrumMachine::Cy8);
  noteOn(0, 0, 60);
  s->render(buf);
  noteOff(0, 0, 60);
  for (int k = 0; k < 20; ++k) s->render(buf);  // 80 ms
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_drum_mono_choke() {
  drumInstr(0, DrumMachine::Hh8);
  p->instruments[0].macro[kMacDec] = 120;  // open hat
  p->instruments[0].mono = false;           // ignored: DRUM is always mono
  noteOn(0, 0, 60);
  s->render(buf);
  noteOn(0, 0, 60);
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
}

void test_drum_choke_mid_segment_has_no_jump() {
  drumInstr(0, DrumMachine::Bd8);
  p->instruments[0].macro[kMacDec] = 100;
  noteOn(0, 0, 60);
  for (int k = 0; k < 30; ++k) s->render(buf);
  const int16_t prev = buf[Synth::kBlock - 1];
  noteOn(50, 0, 60);  // choke inside a control segment
  s->render(buf);
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
  for (int k = 0; k < 40 && s->activeVoices(); ++k) s->render(buf);  // 160 ms
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_drum_lock_on_empty_step_hits_sounding_voice() {
  drumInstr(0, DrumMachine::Hh8);
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  s->render(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::DCY, 7);
  s->render(buf);
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
    s->render(buf);
    for (int i = 0; i < Synth::kBlock; ++i) {
      peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
      if (k || i) jump = abs(buf[i] - prev) > jump ? abs(buf[i] - prev) : jump;
      prev = buf[i];
    }
  }
  TEST_ASSERT_EQUAL(0, s->activeVoices());
  s->render(buf);
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
  for (int k = 0; k < 10; ++k) s->render(buf);
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
  s->render(buf);
  noteOff(0, 3, 43);
  for (int k = 0; k < 4; ++k) s->render(buf);  // env done, filter still ringing
  const int tail = [] {
    for (int i = 0; i < kVoices; ++i)
      if (s->voice(i).on && s->voice(i).env.idle()) return i;
    return -1;
  }();
  TEST_ASSERT_TRUE(tail >= 0);
  noteOn(0, 7, 90);  // track 7 is below its poly limit: a new voice
  s->render(buf);
  TEST_ASSERT_EQUAL(90, s->voice(tail).note);
}

void test_drum_cache_static_hit_computes_once() {
  drumInstr(0, DrumMachine::Hh8);
  noteOn(0, 0, 60);
  const uint32_t c0 = s->drumMachineCalls();
  for (int k = 0; k < 50; ++k) s->render(buf);
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
    for (int k = 0; k < 10; ++k) s->render(buf);
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
  s->render(buf);
  const float before = s->voice(s->trackVoice(0)).dp().metalMs;
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::DCY, 20);  // lock on the sounding voice
  s->render(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).dp().metalMs < before * 0.5f);
}

void test_heavy_voice_limit_counts_drum() {
  for (int t = 0; t < kTracks; ++t) {
    drumInstr(t, DrumMachine::Cy9);
    p->tracks[t].instr = t;
  }
  for (int t = 0; t < kTracks; ++t) noteOn(0, t, 60);
  s->render(buf);
  // 8 tracks fill kFmVoiceMax; a 9th heavy note (preview track) steals one of them.
  p->instruments[8].type = InstrType::Fm;
  send(0, kPreviewTrack, 0xC0, 8, 0, 2);
  noteOn(0, kPreviewTrack, 60);
  s->render(buf);
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
  s->render(buf);
  noteOff(0, 0, 60);
  s->render(buf);
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
  s->render(buf);
  noteOff(0, kPreviewTrack, 60);
  renderMs(10);
  TEST_ASSERT_EQUAL(0, s->activeVoices());
}

void test_sample_choked_by_next_step() {
  sampleSetup(static_cast<uint8_t>(LoopMode::Forward));
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);
  s->render(buf);
  const int first = s->trackVoice(0);
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 62);
  s->render(buf);
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
  s->render(buf);
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
  s->render(buf);
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
  s->render(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_TRUE((v.pos >> 32) < 200);  // from the start again, not 20 ms in
}

// Delay: 1/16 at 120 BPM = 4000 samples.
static int16_t dline[8000];

// Short note at sample 0 on track 0 (one block, release 0), then nBlocks rendered; first non-zero
// sample after a silent gap, or -1.
static int echoStart(int nBlocks) {
  noteOff(Synth::kBlock - 1, 0, 60);
  int gap = 0, at = -1;
  for (int k = 0; k < nBlocks && at < 0; ++k) {
    s->render(buf);
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
  s->setDelayBuffer(dline, 8000);
}

void test_delay_echo_after_one_sixteenth() {
  delaySetup(127);
  noteOn(0, 0, 60);
  const int at = echoStart(40);
  TEST_ASSERT_TRUE(at >= 3995 && at <= 4005);
}

void test_delay_send_zero_no_echo() {
  delaySetup(0);
  noteOn(0, 0, 60);
  TEST_ASSERT_EQUAL(-1, echoStart(40));
}

void test_delay_without_buffer_no_echo() {
  delaySetup(127);
  s->setDelayBuffer(nullptr, 0);
  noteOn(0, 0, 60);
  TEST_ASSERT_EQUAL(-1, echoStart(40));
}

void test_delay_send_zero_mix_unchanged() {
  static int16_t ref[Synth::kBlock * 8];
  noteOn(0, 0, 60);
  for (int k = 0; k < 8; ++k) s->render(ref + k * Synth::kBlock);
  s->reset();
  delaySetup(0);
  noteOn(0, 0, 60);
  for (int k = 0; k < 8; ++k) {
    s->render(buf);
    TEST_ASSERT_EQUAL_INT16_ARRAY(ref + k * Synth::kBlock, buf, Synth::kBlock);
  }
}

void test_dly_lock_on_note_step() {
  delaySetup(0);
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::DLY, 127);
  noteOn(0, 0, 60);
  const int at = echoStart(40);
  TEST_ASSERT_TRUE(at >= 3995 && at <= 4005);
  s->reset();
  stepStart(0, 0, 24, true);
  noteOn(0, 0, 60);  // a new step drops the lock
  TEST_ASSERT_EQUAL(-1, echoStart(40));
}

void test_dly_lock_without_note_hits_sounding_voice() {
  delaySetup(0);
  noteOn(0, 0, 60);
  s->render(buf);
  stepStart(0, 0, 24, false);
  fx(0, 0, Fx::DLY, 127);
  s->render(buf);
  const int at = echoStart(40);  // the note sounded on blocks 1..2 with send
  TEST_ASSERT_TRUE(at >= 0);
}

void test_delay_time_follows_tempo() {
  delaySetup(127);
  p->bpm = 240;  // 1/16 = 2000 samples
  noteOn(0, 0, 60);
  const int at = echoStart(40);
  TEST_ASSERT_TRUE(at >= 1995 && at <= 2005);
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
  s->render(buf);
  int nz = 0;
  for (int i = 0; i < Synth::kBlock; ++i) nz += buf[i] != 0;
  TEST_ASSERT_TRUE(nz > 100);
  // 25 blocks = 100 ms: ~22 falling crossings at 220 Hz.
  TEST_ASSERT_INT_WITHIN(2, 22, crossings(25));
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
  s->render(buf);
  return s->activeVoices();
}

void test_synth_wt_voices_are_heavy() {
  // A wavetable oscillator puts the voice under the shared heavy limit (CPU, bench 2026-10-06).
  TEST_ASSERT_EQUAL(kFmVoiceMax, synthPolyChords(SynOsc::Saw, SynOsc::Wt));
}

void test_synth_bl_voices_use_whole_pool() {
  TEST_ASSERT_EQUAL(kVoices, synthPolyChords(SynOsc::Saw, SynOsc::Square));
}

void test_synth_macro_lock_applies() {
  // fx DCY on a SYNTH step locks SHP1 (macro 0): the lock reaches the voice.
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  send(0, 0, 0xF5, static_cast<uint8_t>(Fx::DCY), 100);
  noteOn(0, 0, 60);
  s->render(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_TRUE(v.lockMask & (1 << kMacShp1));
  TEST_ASSERT_EQUAL(100, v.lock[kMacShp1]);
}

void test_synth_macro_lock_on_empty_step_hits_sounding_voice() {
  Instrument& m = p->instruments[0];
  instrSetType(m, InstrType::Synth);
  stepStart(0, 0, 6, true);
  noteOn(0, 0, 60);
  s->render(buf);
  stepStart(0, 0, 6, false);
  fx(0, 0, Fx::SHP, 90);  // MIX
  s->render(buf);
  const Voice& v = s->voice(s->trackVoice(0));
  TEST_ASSERT_EQUAL(1 << kMacMix, v.lockMask);
  TEST_ASSERT_EQUAL(90, v.lock[kMacMix]);
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
  s->render(buf);
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
  s->render(buf);
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
  s->render(buf);
  TEST_ASSERT_EQUAL(-1, onlyVoice());
  p->instruments[0].kit[1].sample[0] = 0;  // empty lane
  noteOn(0, 0, 61, 100);
  s->render(buf);
  TEST_ASSERT_EQUAL(-1, onlyVoice());
}

void test_kit_inst_lane_plays_instrument() {
  kitSetup(0, 0);
  p->instruments[0].kit[2].instr = 5;
  noteOn(0, 0, 62, 100);
  s->render(buf);
  const Voice& x = s->voice(onlyVoice());
  TEST_ASSERT_FALSE(x.lane);
  TEST_ASSERT_FALSE(x.sample);
  TEST_ASSERT_EQUAL(5, x.instr);
  TEST_ASSERT_EQUAL(62, x.note);
}

void test_kit_in_kit_lane_silent() {
  kitSetup(0, 0);
  instrSetType(p->instruments[3], InstrType::Kit);
  p->instruments[0].kit[2].instr = 3;
  noteOn(0, 0, 62, 100);
  s->render(buf);
  TEST_ASSERT_EQUAL(-1, onlyVoice());
}

void test_kit_eight_lanes_sound_and_rehit_chokes() {
  kitSetup(0, 0);
  for (int l = 0; l < kKitLanes; ++l) noteOn(0, 0, static_cast<uint8_t>(60 + l), 100);
  s->render(buf);
  TEST_ASSERT_EQUAL(8, trackVoices(0));
  int lane0 = -1;
  for (int v = 0; v < kVoices; ++v)
    if (s->voice(v).on && s->voice(v).note == 60) lane0 = v;
  noteOn(0, 0, 60, 100);
  s->render(buf);
  TEST_ASSERT_EQUAL(8, trackVoices(0));
  int again = -1;
  for (int v = 0; v < kVoices; ++v)
    if (s->voice(v).on && s->voice(v).note == 60) again = v;
  TEST_ASSERT_EQUAL(lane0, again);  // the lane's own voice, restarted
}

void test_kit_unknown_note_silent() {
  kitSetup(0, 0);
  noteOn(0, 0, 40, 100);
  s->render(buf);
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
  for (int k = 0; k < 4; ++k) s->render(ref + k * Synth::kBlock);
  s->reset();
  p->instruments[0].drive = 0;
  p->instruments[0].rsend = 0;
  p->instruments[0].velCut = 63;  // no filter: nothing to move
  noteOn(0, 0, 60);
  int16_t got[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) s->render(got + k * Synth::kBlock);
  TEST_ASSERT_EQUAL_MEMORY(ref, got, sizeof(ref));
}

void test_drive_changes_waveform() {
  p->instruments[0].vol = 60;
  const float clean = noteRms(60, 100, 20);
  p->instruments[0].drive = 127;
  const float driven = noteRms(60, 100, 20);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).drive.on());
  TEST_ASSERT_TRUE(driven > clean * 1.3f);  // the saw squares up
}

void test_drv_lock_on_note_step() {
  stepStart(0, 0, 24, true);
  fx(0, 0, Fx::DRV, 127);
  noteOn(0, 0, 60);
  noteOn(0, 1, 60);
  s->render(buf);
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).drive.on());
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(1)).drive.on());
  stepStart(0, 0, 24, false);  // without a note: the sounding voice
  fx(0, 0, Fx::DRV, 0);
  s->render(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).drive.on());
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
    s->render(buf);
    const bool d = s->voice(s->trackVoice(0)).drive.on();
    on |= d;
    off |= !d;
  }
  TEST_ASSERT_TRUE(on && off);
}

static float rvbBuf[Reverb::kBufLen];

void test_rsend_zero_same_with_and_without_buffer() {
  noteOn(0, 0, 60);
  int16_t ref[Synth::kBlock * 8];
  for (int k = 0; k < 8; ++k) s->render(ref + k * Synth::kBlock);
  s->reset();
  s->setReverbBuffer(rvbBuf, Reverb::kBufLen);
  noteOn(0, 0, 60);
  int16_t got[Synth::kBlock * 8];
  for (int k = 0; k < 8; ++k) s->render(got + k * Synth::kBlock);
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
    s->render(buf);
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
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.f, s->voice(s->trackVoice(0)).rsend);
  stepStart(0, 0, 24, true);  // a new step drops the lock
  noteOn(0, 0, 62);
  s->render(buf);
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
  for (int k = 0; k < 40; ++k) s->render(buf);
  noteOn(0, 1, 36);
  for (int k = 0; k < 10; ++k) s->render(out + k * Synth::kBlock);
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
  for (int k = 0; k < 4; ++k) s->render(got + k * Synth::kBlock);
  s->reset();
  p->scTrack = 0;
  noteOn(0, 0, 60);
  int16_t ref[Synth::kBlock * 4];
  for (int k = 0; k < 4; ++k) s->render(ref + k * Synth::kBlock);
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
  for (int k = 0; k < 4; ++k) {
    renderMs(k == 0 ? 16 : 32);  // 16, 48, 80, 112 ms (whole blocks)
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
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.6f, 40.f, s->voice(s->trackVoice(0)).fpMac[kMacDec]);
  s->reset();
  m.velMac = 63;
  noteOn(0, 0, 60, 127);
  s->render(buf);
  // The DECAY the voice used went up by 63 x 63 / 64 = 62 macro units: its params cache key shows it.
  TEST_ASSERT_FLOAT_WITHIN(0.6f, 40.f + 62.f, s->voice(s->trackVoice(0)).fpMac[kMacDec]);
  s->reset();
  noteOn(0, 0, 60, 1);  // velocity 1: down by 63 x 63 / 64, clamped at 0
  s->render(buf);
  TEST_ASSERT_FLOAT_WITHIN(0.6f, 0.f, s->voice(s->trackVoice(0)).fpMac[kMacDec]);
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
  RUN_TEST(test_fm_ninth_note_steals_oldest_fm_keeps_chip);
  RUN_TEST(test_fm_voices_never_exceed_cap);
  RUN_TEST(test_filter_off_is_bit_identical);
  RUN_TEST(test_lp_darkens_saw);
  RUN_TEST(test_filter_env_opens);
  RUN_TEST(test_max_reso_stays_finite);
  RUN_TEST(test_flt_lock_on_note_step);
  RUN_TEST(test_flt_lock_without_note_hits_sounding_voice);
  RUN_TEST(test_res_lock_on_chip_sets_lock_bit);
  RUN_TEST(test_lfo_pitch_on_chip);
  RUN_TEST(test_lfo_cutoff_on_chip);
  RUN_TEST(test_lfo_macro_dest_ignored_on_chip);
  RUN_TEST(test_filter_turned_on_mid_note_starts_clean);
  RUN_TEST(test_lfo_restarts_on_mono_retrigger_after_release);
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
  RUN_TEST(test_synth_macro_lock_applies);
  RUN_TEST(test_synth_macro_lock_on_empty_step_hits_sounding_voice);
  RUN_TEST(test_kit_sampler_lane_builds_scratch_instrument);
  RUN_TEST(test_kit_decay_zero_is_one_shot);
  RUN_TEST(test_kit_missing_sample_silent);
  RUN_TEST(test_kit_inst_lane_plays_instrument);
  RUN_TEST(test_kit_in_kit_lane_silent);
  RUN_TEST(test_kit_eight_lanes_sound_and_rehit_chokes);
  RUN_TEST(test_kit_unknown_note_silent);
  RUN_TEST(test_drive_zero_is_bit_identical);
  RUN_TEST(test_drive_changes_waveform);
  RUN_TEST(test_drv_lock_on_note_step);
  RUN_TEST(test_lfo_drive_dest);
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
  return UNITY_END();
}
