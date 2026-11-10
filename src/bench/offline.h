#pragma once
// Diagnostic algorithm timings only: source generation and SD/filesystem I/O are excluded.
#include <string.h>
#include "sample_extract.h"
#include "time_stretch.h"
#include "tempo_detect.h"
#include "wt_make.h"
#include "wt_audition.h"
#include "wav.h"
namespace {
volatile int32_t offlineChecksum;
struct BenchReader : mt::TimeStretchReader {
  const int16_t* pcm = nullptr;
  uint32_t frames = 0;
  bool read(uint32_t first, uint32_t n, int16_t* out) override {
    if (first > frames || n > frames - first) return false;
    memcpy(out, pcm + first, n * sizeof(int16_t)); return true;
  }
};
struct OfflineWork {
  int16_t samples[1024], wave[256];
  float rawFloat[1024], scratch[256];
  mt::Onset onsets[128], dense[128];
};
void runOffline() {
  constexpr uint32_t rate = 32000, maxFrames = 8 * rate;
  auto* pcm = static_cast<int16_t*>(heap_caps_malloc(maxFrames * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* mem = static_cast<OfflineWork*>(heap_caps_malloc(sizeof(OfflineWork), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  auto* work = mem ? mem->samples : nullptr;
  if (!pcm || !work) { Serial.println("OFFLINE FATAL allocation"); free(pcm); free(mem); return; }
  for (int seconds : {2, 8}) {
    const uint32_t frames = seconds * rate;
    // Pitched carrier plus decaying 120 BPM transients: useful for WT and BPM.
    for (uint32_t i = 0; i < frames; ++i) {
      const uint32_t beat = i % (rate / 2);
      const float amp = beat < 3000 ? 0.2f + 0.8f * expf(-static_cast<float>(beat) / 700.f) : 0.2f;
      pcm[i] = 14000.f * amp * sinf(6.2831853f * i / 128.f);
    }
    uint64_t copiedStart = esp_timer_get_time();
    for (uint32_t at=0; at<frames; at+=1024) {
      uint32_t n=frames-at<1024 ? frames-at : 1024;
      memcpy(work,pcm+at,n*2); offlineChecksum ^= work[n-1];
    }
    Serial.printf("OFFLINE EXTRACT_COPY seconds=%d total_us=%llu workspace_internal=2048 checksum=%ld\n",seconds,esp_timer_get_time()-copiedStart,long(offlineChecksum));
    float* rawFloat = mem->rawFloat; for(int i=0;i<1024;++i) rawFloat[i]=(i-512)/512.f;
    mt::WavInfo floatInfo; floatInfo.channels=1; floatInfo.bits=32; floatInfo.isFloat=true;
    copiedStart=esp_timer_get_time();
    for(uint32_t at=0;at<frames;at+=1024) {
      uint32_t n=frames-at<1024 ? frames-at : 1024;
      mt::wavToMono(reinterpret_cast<const uint8_t*>(rawFloat),n,floatInfo,work); offlineChecksum ^= work[n-1];
    }
    Serial.printf("OFFLINE FLOAT32 seconds=%d total_us=%llu workspace_internal=6144 checksum=%ld\n",seconds,esp_timer_get_time()-copiedStart,long(offlineChecksum));
    int32_t peak = 0; uint32_t maxStep = 0;
    uint64_t start = esp_timer_get_time();
    for (uint32_t at = 0; at < frames; at += 1024) {
      uint32_t t = esp_timer_get_time(); const uint32_t n = frames - at < 1024 ? frames - at : 1024;
      memcpy(work, pcm + at, n * 2);
      peak = mt::sampleExtractPeak(work, n, peak);
      uint32_t dt = esp_timer_get_time() - t; if (dt > maxStep) maxStep = dt;
    }
    // The actual job performs a complete peak pass before its normalization pass.
    for (uint32_t at = 0; at < frames; at += 1024) {
      uint32_t t = esp_timer_get_time(); const uint32_t n = frames - at < 1024 ? frames - at : 1024;
      memcpy(work, pcm + at, n * 2);
      mt::sampleExtractNormalize(work, n, peak);
      offlineChecksum ^= work[n-1];
      uint32_t dt = esp_timer_get_time() - t; if (dt > maxStep) maxStep = dt;
    }
    Serial.printf("OFFLINE NORMALIZE seconds=%d total_us=%llu max_step_us=%lu peak=%ld workspace_internal=2048\n", seconds, esp_timer_get_time()-start, maxStep, long(peak));
    auto* scanMem = heap_caps_malloc(sizeof(mt::OnsetScan), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scanMem) { Serial.println("OFFLINE FATAL onset allocation"); break; }
    auto* onsets = mem->onsets;
    start = esp_timer_get_time(); auto* scan = new (scanMem) mt::OnsetScan(frames, rate, 128); maxStep = 0;
    for (uint32_t at = 0; at < frames; at += 1024) {
      uint32_t t = esp_timer_get_time(); const uint32_t n = frames - at < 1024 ? frames - at : 1024;
      memcpy(work, pcm + at, n * 2); scan->feed(work, n);
      uint32_t dt = esp_timer_get_time()-t; if (dt > maxStep) maxStep = dt;
      if ((at & 16383) == 0) delay(1);
    }
    int count = scan->finish(onsets); auto tempo = mt::estimateTempo(onsets, count, rate, frames);
    Serial.printf("OFFLINE TEMPO seconds=%d total_us=%llu max_step_us=%lu onsets=%d bpm10=%u confidence=%u workspace_psram=%u workspace_internal=%u\n", seconds, esp_timer_get_time()-start, maxStep, count, tempo.bpm10, tempo.confidence, sizeof(mt::OnsetScan), unsigned(sizeof(mem->onsets)+2048));
    scan->~OnsetScan(); free(scanMem);
    BenchReader reader; reader.pcm = pcm; reader.frames = frames;
    for (int ratio : {50, 100, 200}) {
      mt::TimeStretch stretch; const auto before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      const auto beforePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      start = esp_timer_get_time(); auto state = stretch.start(frames, rate, frames * ratio / 100);
      const auto allocated = before - heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      const auto allocatedPsram = beforePsram - heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      uint32_t total = 0; maxStep = 0; int calls = 0;
      while (state == mt::TimeStretchResult::Running) {
        uint32_t produced = 0; uint32_t t = esp_timer_get_time();
        state = stretch.step(reader, work, 1024, produced); total += produced;
        uint32_t dt = esp_timer_get_time()-t; if (dt > maxStep) maxStep = dt;
        if ((++calls & 15) == 0) delay(1);
      }
      Serial.printf("OFFLINE STRETCH seconds=%d ratio=%d total_us=%llu max_step_us=%lu frames=%lu state=%u workspace_internal_delta=%lu workspace_psram_delta=%lu workspace_declared_max=8192\n", seconds, ratio, esp_timer_get_time()-start, maxStep, total, unsigned(state), allocated, allocatedPsram);
    }
    for (auto mode : {mt::WtMode::Cycle, mt::WtMode::Window}) {
      mt::WtMaker maker; mt::WtMakeParams params; params.mode = mode;
      const auto before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      start = esp_timer_get_time(); maker.begin(params, 0, frames, rate);
      const auto allocated = before - heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      maxStep = 0; int calls = 0;
      while (!maker.done()) {
        uint32_t at, n, t = esp_timer_get_time();
        if (maker.nextRead(at,n)) { memcpy(work, pcm+at, n*2); maker.feed(work,n); } else maker.step();
        uint32_t dt = esp_timer_get_time()-t; if (dt > maxStep) maxStep = dt;
        if ((++calls & 31) == 0) delay(1);
      }
      Serial.printf("OFFLINE WT seconds=%d mode=%u total_us=%llu max_step_us=%lu pitched=%d result=%u workspace_psram=%lu output_bytes=%u\n", seconds, unsigned(mode), esp_timer_get_time()-start, maxStep, maker.pitched(), unsigned(maker.error()), allocated, unsigned(mt::kWtSrcSamples*2));
    }
  }
  // Worst bounded tempo candidate list, independently of transient detection density.
  auto* dense = mem->dense; for (int i=0;i<128;++i) dense[i] = {uint32_t(i*2000), uint16_t(1000-i)};
  uint64_t t = esp_timer_get_time(); auto tempo = mt::estimateTempo(dense,128,rate,maxFrames);
  Serial.printf("OFFLINE TEMPO128 total_us=%llu bpm10=%u workspace_internal=%u\n",esp_timer_get_time()-t,tempo.bpm10,unsigned(sizeof(mem->dense)));
  float* scratch = mem->scratch; int16_t* wave = mem->wave;
  for (int i=0;i<256;++i) wave[i] = (i-128)*200;
  t = esp_timer_get_time(); mt::wtAuditionPrepare(wave,wave,scratch);
  Serial.printf("OFFLINE AUDITION_PREP total_us=%llu workspace_internal=%u\n",esp_timer_get_time()-t,unsigned(sizeof(mem->scratch)+sizeof(mem->wave)));
  free(mem); free(pcm);
}
}
