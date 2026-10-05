#include <math.h>
#include <string.h>
#include <unity.h>
#include "synth.h"

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
  TEST_ASSERT_EQUAL(16, s->activeVoices());
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
  TEST_ASSERT_TRUE(s->voice(s->trackVoice(0)).lfoPhase > fresh * 4);
  pgm(0, 0);
  noteOn(0, 0, 62);  // CHIP legato on the same voice
  s->render(buf);
  TEST_ASSERT_FALSE(s->voice(s->trackVoice(0)).fm);
  pgm(0, 1);
  noteOn(0, 0, 64);  // FM legato from a CHIP voice: a new FM note
  s->render(buf);
  TEST_ASSERT_EQUAL(1, s->activeVoices());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, fresh, s->voice(s->trackVoice(0)).lfoPhase);
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
  TEST_ASSERT_TRUE(fabsf(s->voice(s->trackVoice(0)).lfoRnd) > 0.05f);
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

void test_fm_voices_never_exceed_eight() {
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
    TEST_ASSERT_TRUE(fmVoices() <= kFmVoiceMax);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_event_offset);
  RUN_TEST(test_silent_without_events);
  RUN_TEST(test_note_on_lands_on_its_sample);
  RUN_TEST(test_events_sorted_by_offset);
  RUN_TEST(test_note_off_frees_voice_after_release);
  RUN_TEST(test_velocity_zero_is_note_off);
  RUN_TEST(test_track_volume_zero_is_silent);
  RUN_TEST(test_master_volume_zero_is_silent);
  RUN_TEST(test_instrument_volume_scales_output);
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
  RUN_TEST(test_fm_voices_never_exceed_eight);
  return UNITY_END();
}
