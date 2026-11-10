// USB compute benchmark for an ESP32-S3 N16R8 without screen, SD or audio hardware.
// Never compiled into the WT32 application; all buffers follow the production placement.
#ifdef DTRK_HEADLESS_BENCH
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
#include <math.h>
#include "model.h"
#include "synth.h"
#include "voice_cost.h"
#include "offline.h"

namespace {
mt::Project* project;
mt::Synth* synth;
int16_t output[2 * mt::Synth::kBlock];
volatile int32_t checksum;
uint32_t noteQueueRejects;
constexpr int kWarm = 64, kMeasure = 256;
struct Source : mt::SampleSource {
  int16_t pcm[4096];
  const int16_t* find(const char*, uint32_t& frames, uint32_t& rate) const override {
    frames = 4096; rate = mt::kSynthRate; return pcm;
  }
} *source;
void* external(size_t n) { return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void note(int track, int key) {
  const uint8_t b[3] = {0x90, static_cast<uint8_t>(key), 100};
  if (!synth->event(0, track, b, 3)) ++noteQueueRejects;
}
void configure(mt::InstrType type, int machine, int voices, bool fx, int matrix, bool paired) {
  noteQueueRejects = 0;
  synth->reset();
  synth->reseed();
  project->reset();
  project->compAmt = fx ? 30 : 0;
  project->rvbLevel = fx ? 80 : 0;
  project->dlyLevel = fx ? 80 : 0;
  for (int t = 0; t < mt::kTracks; ++t) {
    auto& tr = project->tracks[t]; tr.out = mt::TrackOut::Int; tr.instr = t;
    auto& m = project->instruments[t];
    mt::instrSetType(m, type);
    if (type == mt::InstrType::Fm) mt::fmSetMachine(m, machine);
    if (type == mt::InstrType::Drum) mt::drumSetMachine(m, machine);
    if (type == mt::InstrType::Chip) m.wave = static_cast<uint8_t>(mt::Wave::Saw);
    if (type == mt::InstrType::Sample) {
      strcpy(m.sample, "BENCH"); m.loop = static_cast<uint8_t>(mt::LoopMode::Forward);
    }
    if (type == mt::InstrType::Synth) {
      m.synOsc[0] = static_cast<uint8_t>(mt::SynOsc::Saw);
      m.synOsc[1] = static_cast<uint8_t>(mt::SynOsc::Square);
      m.macro[mt::kMacMix] = 64;
    }
    m.mono = false; m.vol = 40; m.sustain = 127;
    m.macro[mt::kMacDec] = paired ? 64 : 127;
    if (paired) m.macro[mt::kMacCol] = 64;
    m.send = fx ? 40 : 0; m.rsend = fx ? 40 : 0;
    if (matrix) {
      // Both routes modes run all eight slots; mode 2 also forces four raw LFOs
      // and continuous parameter recomputation, including the expensive FM mapper.
      for (int r = 0; r < mt::kModRoutes; ++r) {
        auto& route = m.mod[r];
        route.source = static_cast<uint8_t>(matrix == 1 ? mt::ModSource::Velocity : static_cast<mt::ModSource>(1 + r % 4));
        route.dest = static_cast<uint8_t>(mt::LfoDest::Pan);
        route.amount = 4;
      }
      if (matrix == 2) {
        const mt::LfoDest dest[] = {mt::LfoDest::Pitch, mt::LfoDest::Cutoff, mt::LfoDest::Pan, mt::LfoDest::Vol,
                                  (type == mt::InstrType::Chip || type == mt::InstrType::Sample) ? mt::LfoDest::Bit : mt::LfoDest::Dec,
                                  (type == mt::InstrType::Chip || type == mt::InstrType::Sample) ? mt::LfoDest::Srr : mt::LfoDest::Col, type == mt::InstrType::Fm ? mt::LfoDest::Sat : mt::LfoDest::Drive,
                                  mt::LfoDest::Rate1};
        for (int r = 0; r < mt::kModRoutes; ++r) {
          m.mod[r].dest = static_cast<uint8_t>(dest[r]);
          m.mod[r].amount = r == 0 || r == 7 ? 8 : 48;
          if (r >= 4) m.mod[r].via = static_cast<uint8_t>(mt::ModSource::Velocity);
        }
        m.mod[4].via = static_cast<uint8_t>(mt::ModSource::FilterEnv);
        m.mod[5].via = static_cast<uint8_t>(mt::ModSource::Follower);
        m.mod[6].via = static_cast<uint8_t>(mt::ModSource::Random);
        m.mod[7].via = static_cast<uint8_t>(mt::ModSource::Note);
        for (int l = 0; l < mt::kLfos; ++l) { auto q = mt::lfoRef(m, l); q.rate = 95 + l * 5; q.depth = 0; }
      }
    }
    const uint8_t pgm[2] = {0xC0, static_cast<uint8_t>(t)};
    synth->event(0, t, pgm, 2);
  }
  for (int i = 0; i < voices; ++i) note(i % mt::kTracks, 48 + (i / mt::kTracks) * 7);
}
void measure(const char* name, mt::InstrType type, int machine, int voices, bool fx, int matrix = 0, bool perf = false, bool guard = false) {
  // Set the allocation budget before notes are posted; reset preserves it.
  synth->setVoiceBudget(guard ? mt::kVoiceBudgetEsp : mt::kVoiceBudgetOff);
  synth->setLoad(0);
  configure(type, machine, voices, fx, matrix, strncmp(name, "PAIR_", 5) == 0 || strncmp(name, "CAP_", 4) == 0 || strncmp(name, "GUARD_", 6) == 0);
  uint64_t energy = 0;
  if (perf) {
    const mt::PerfFx effects[] = {mt::PerfFx::Repeat, mt::PerfFx::Gate, mt::PerfFx::Filter, mt::PerfFx::Crush,
                                mt::PerfFx::Downsample, mt::PerfFx::Drive, mt::PerfFx::DelayThrow, mt::PerfFx::ReverbThrow};
    for (auto f : effects) { const uint8_t msg[] = {0xF6, 0, static_cast<uint8_t>(f)}; synth->event(0, mt::kPreviewTrack, msg, 3); }
  }
  uint64_t sum = 0; uint32_t peak = 0, warmPeak = 0; uint32_t sounding = 0;
  int minActive = mt::kVoices, maxActive = 0;
  for (int b = 0; b < kWarm + kMeasure; ++b) {
    if (type == mt::InstrType::Drum || (type == mt::InstrType::Fm && !mt::fmGated(machine))) {
      if (b % (strncmp(name, "PAIR_", 5) == 0 || strncmp(name, "CAP_", 4) == 0 || strncmp(name, "GUARD_", 6) == 0 ? 2 : 31) == 0)
        for (int i = 0; i < voices; ++i) note(i % mt::kTracks, 48 + (i / mt::kTracks) * 7);
    }
    const uint64_t start = esp_timer_get_time();
    synth->renderStereo(output);
    const uint32_t us = esp_timer_get_time() - start;
    if (guard) synth->setLoad(us / 4000.f);
    checksum = checksum ^ output[b % (2 * mt::Synth::kBlock)];
    if (b < kWarm && us > warmPeak) warmPeak = us;
    if (b >= kWarm) {
      const int active = synth->activeVoices();
      if (active < minActive) minActive = active;
      if (active > maxActive) maxActive = active;
      sum += us; if (us > peak) peak = us; sounding += synth->activeVoices();
      for (auto v : output) energy += int64_t(v) * v; }
    if ((b & 15) == 0) delay(1);
  }
  Serial.printf("BENCH %s machine=%d voices=%d active=%.2f fx=%d matrix=%d perf=%d guard=%d avg_us=%.2f peak_us=%lu warmup_peak_us=%lu min_active=%d max_active=%d not_retained=%d queue_rejects=%lu load=%.3f rms=%.2f checksum=%ld\n",
                name, machine, voices, sounding / float(kMeasure), fx, matrix, perf, guard, sum / float(kMeasure),
                static_cast<unsigned long>(peak), static_cast<unsigned long>(warmPeak), minActive, maxActive, voices - synth->activeVoices(), static_cast<unsigned long>(noteQueueRejects), sum / float(kMeasure * 4000), sqrt(double(energy) / (kMeasure * 2 * mt::Synth::kBlock)), static_cast<long>(checksum));
}
void run() {
#ifdef DTRK_MATRIX_GUARD_ONLY
  for (int matrix : {0,1,2}) {
    measure("GUARD_CHIP", mt::InstrType::Chip, 0, 24, true, matrix, true, true);
    measure("GUARD_FMHAT", mt::InstrType::Fm, 7, 8, true, matrix, true, true);
    measure("GUARD_SYNTH", mt::InstrType::Synth, 0, 8, true, matrix, true, true);
    measure("GUARD_SAMPLE", mt::InstrType::Sample, 0, 16, true, matrix, true, true);
  }
  Serial.printf("BENCH DONE free_internal=%lu min_internal=%lu\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  return;
#endif
  measure("IDLE", mt::InstrType::Chip, 0, 0, false);
  measure("IDLE", mt::InstrType::Chip, 0, 0, true);
  for (int n : {4, 8, 16, 24}) measure("CHIP", mt::InstrType::Chip, 0, n, true);
  for (int m = 0; m < static_cast<int>(mt::FmMachine::Count); ++m)
    for (int n : {2, 4, 8}) measure("FM", mt::InstrType::Fm, m, n, true);
  for (int m : {0, 1, 4, 9, 15})
    for (int n : {4, 8}) measure("DRUM", mt::InstrType::Drum, m, n, true);
  for (int n : {4, 8}) measure("SYNTH", mt::InstrType::Synth, 0, n, true);
  for (int n : {4, 8, 16}) measure("SAMPLE", mt::InstrType::Sample, 0, n, true);
  for (int matrix : {0, 1, 2}) {
    for (bool perf : {false, true}) {
      measure("PAIR_CHIP", mt::InstrType::Chip, 0, 24, true, matrix, perf);
      measure("PAIR_FMHAT", mt::InstrType::Fm, 7, 4, true, matrix, perf);
      measure("PAIR_FMHAT", mt::InstrType::Fm, 7, 8, true, matrix, perf);
      measure("PAIR_SYNTH", mt::InstrType::Synth, 0, 8, true, matrix, perf);
      measure("PAIR_SAMPLE", mt::InstrType::Sample, 0, 16, true, matrix, perf);
    }
  }
  for (int n : {8,16}) measure("CAP_CHIP", mt::InstrType::Chip, 0, n, true, 2, true);
  measure("CAP_FMHAT", mt::InstrType::Fm, 7, 2, true, 2, true);
  measure("CAP_SYNTH", mt::InstrType::Synth, 0, 4, true, 2, true);
  for (int n : {4,8}) measure("CAP_SAMPLE", mt::InstrType::Sample, 0, n, true, 2, true);
  measure("GUARD_CHIP", mt::InstrType::Chip, 0, 24, true, 2, true, true);
  measure("GUARD_SAMPLE", mt::InstrType::Sample, 0, 16, true, 2, true, true);
  runOffline();
  Serial.printf("BENCH DONE free_internal=%lu min_internal=%lu\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}
}
void setup() {
  rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
  Serial.begin(115200);
  delay(3000);
  Serial.printf("HEADLESS chip=%s revision=%u cpu_mhz=%u flash=%lu psram=%lu synth_rate=%d block=%d revision_code=%s\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz(), ESP.getFlashChipSize(),
                ESP.getPsramSize(), mt::kSynthRate, mt::Synth::kBlock, DTRK_BENCH_REV);
  if (ESP.getFlashChipSize() != 16 * 1024 * 1024 || ESP.getPsramSize() != 8 * 1024 * 1024) {
    Serial.println("FATAL expected N16R8; benchmark refused"); return;
  }
  void* projectMem = external(sizeof(mt::Project));
  void* synthMem = heap_caps_malloc(sizeof(mt::Synth), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  void* cacheMem = external(sizeof(mt::Synth::MachineCache));
  void* sourceMem = external(sizeof(Source));
  if (!projectMem || !synthMem || !cacheMem || !sourceMem) {
    Serial.println("FATAL no memory for benchmark state"); return;
  }
  project = new (projectMem) mt::Project();
  synth = new (synthMem) mt::Synth(*project);
  auto* cache = new (cacheMem) mt::Synth::MachineCache();
  synth->setMachineCache(cache);
  source = new (sourceMem) Source();
  for (int i=0;i<4096;++i) source->pcm[i] = 12000 * sinf(6.2831853f*i/64);
  synth->setBank(source);
  auto* delayLine = static_cast<int16_t*>(external(4 * mt::kSynthRate * sizeof(int16_t)));
  synth->setDelayBuffer(delayLine, 4 * mt::kSynthRate);
  auto* reverb = static_cast<float*>(heap_caps_calloc(mt::Reverb::kBufLen, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  synth->setReverbBuffer(reverb, mt::Reverb::kBufLen);
  auto* shim = static_cast<int16_t*>(heap_caps_calloc(mt::PitchShift::kLen, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  auto* wet = static_cast<float*>(heap_caps_calloc(mt::Synth::kBlock, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  synth->setShimmerBuffer(shim, mt::PitchShift::kLen, wet);
  auto* mod = static_cast<int16_t*>(heap_caps_calloc(mt::ModFx::kLineLen, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  synth->setModBuffer(mod, mt::ModFx::kLineLen);
  if (!delayLine || !reverb || !shim || !wet || !mod) {
    Serial.println("FATAL missing effect buffers; benchmark refused"); return;
  }
  auto* perf = static_cast<int16_t*>(external(192000 * 2 * sizeof(int16_t)));
  if (!perf) { Serial.println("FATAL missing PERF buffer"); return; }
  synth->setPerfBuffer(perf, 192000, false);
  Serial.printf("PLACEMENT synth_internal=%d reverb_internal=%d sizeof_synth=%u sizeof_project=%u free_internal=%lu\n",
                esp_ptr_internal(synth), esp_ptr_internal(reverb), sizeof(mt::Synth), sizeof(mt::Project), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  run();
}
void loop() { delay(1000); }
#endif
