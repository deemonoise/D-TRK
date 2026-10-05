#include "audio.h"
#include "bank.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <atomic>
#include <new>
#include "driver/i2s_std.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "hw/pins.h"
#include "oneshot.h"
#include "synth.h"
#include "synth_fm.h"
#include "synth_fm_machines.h"

// Bench: build with -DAUDIO_BENCH (add to build_flags of [env:wt32]) to replace the synth with
// 16 fake sampler voices reading the sample partition; the render time goes to Serial once a second.
// FM bench: -DAUDIO_BENCH_FM instead keeps the whole 16-voice pool busy with FM through the real
// synth: 12 TONE voices (POLY, tracks 1..3) + 4 HAT voices (tracks 4..7), the heaviest mix that
// can be held (see benchFmBegin). Same Serial line. It overwrites instruments 15, 16 and the
// tracks' out / instrument of the loaded project in RAM: there is no autosave, but do not SAVE
// the project from a bench build.

namespace audio {
namespace {

static_assert(kBlock == mt::Synth::kBlock, "one synth block per DMA block");
constexpr int kDmaBlocks = 3;  // DMA buffers: a block plays ~(kDmaBlocks - 1) blocks after it is written
constexpr uint32_t kBlockUs = 1000000u * kBlock / kRate;  // 4000
// Events are stamped with their scheduled time and the engine sends them at or shortly after it;
// block n covers [blockT, blockT + kBlockUs) and is rendered no earlier than kLagUs after blockT,
// so every event of it has arrived: one block plus slack for a late engine wakeup (it may wait
// on the project lock). An event sent later than that lands at the block start, counted late.
constexpr uint32_t kLagUs = kBlockUs + 2000;
constexpr uint32_t kPreviewUs = 300000;  // preview note length

// Single-producer single-consumer ring: one writer task, the audio task reads.
struct Ev {
  uint64_t t;
  uint8_t track, len;
  uint8_t b[3];
};
struct Ring {
  static constexpr uint32_t kSize = 256;  // power of two
  Ev buf[kSize];
  std::atomic<uint32_t> head{0};  // written by the producer
  std::atomic<uint32_t> tail{0};  // written by the consumer
  bool push(const Ev& e) {
    const uint32_t h = head.load(std::memory_order_relaxed);
    if (h - tail.load(std::memory_order_acquire) >= kSize) return false;
    buf[h & (kSize - 1)] = e;
    head.store(h + 1, std::memory_order_release);
    return true;
  }
  const Ev* peek() const {
    const uint32_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) return nullptr;
    return &buf[t & (kSize - 1)];
  }
  void pop() { tail.store(tail.load(std::memory_order_relaxed) + 1, std::memory_order_release); }
};

mt::Project* project;
i2s_chan_handle_t tx;
// Since the last takeLoad() (status bar CPU). Audio task is the only writer, the UI resets.
std::atomic<uint32_t> loadSum{0}, loadBlocks{0}, loadPeak{0};
std::atomic<uint32_t> lostEvents{0};
TaskHandle_t task;
// Flash writes (sample bank): the UI asks, the audio task goes silent and parks. Every request
// has a generation: park() echoes the one it parked for and stays until that one is resumed, so a
// semaphore give left over from a timed-out request never satisfies a later one.
std::atomic<bool> pauseReq{false};
std::atomic<uint32_t> pauseGen{0};   // last request (UI)
std::atomic<uint32_t> parkedGen{0};  // request the task parked for (audio task)
std::atomic<uint32_t> resumeGen{0};  // last request resumed or withdrawn (UI)
SemaphoreHandle_t pausedSem, resumeSem;

mt::Synth* synth;
Ring engineQ;  // engine task -> audio task
Ring uiQ;      // UI task (preview) -> audio task
uint64_t blockT;  // start of the block being rendered, engine time
bool clockSet;
// Counters for pollLog() (UI task): the audio task must not print, USB CDC may block.
std::atomic<uint32_t> lateEvents{0}, resyncs{0};
// Pending preview NoteOff.
bool previewOn;
uint8_t previewNote;
uint64_t previewOffT;

// Buffer preview (WAV import): the UI fills pvData/pvFrames/pvRate (nullptr = stop) and bumps
// pvReq (release); the audio task takes the request and echoes it in pvAck. A buffer is free
// once a stop request is acknowledged.
const int16_t* pvData;
uint32_t pvFrames, pvRate;
std::atomic<uint32_t> pvReq{0}, pvAck{0};
std::atomic<bool> pvPlaying{false};
mt::OneShot oneshot;

void takePreviewRequest() {
  const uint32_t r = pvReq.load(std::memory_order_acquire);
  if (r == pvAck.load(std::memory_order_relaxed)) return;
  if (pvData) oneshot.start(pvData, pvFrames, pvRate);
  else oneshot.stop();
  pvAck.store(r, std::memory_order_release);
}

// Internal RAM: touched every block.
int16_t mono[kBlock];
int16_t lr[kBlock * 2];

// The block clock steps by exactly kBlockUs (the DMA consumes samples at that rate), so the
// delay stays constant whatever the wakeup jitter. It is set again at start (the DMA is filled
// ahead of time), after a stall (underrun) and when clock drift has added up.
void advanceClock() {
  const uint64_t want = engine::nowUs() - kLagUs;
  if (clockSet) blockT += kBlockUs;
  const int64_t d = static_cast<int64_t>(want - blockT);  // > 0: rendered later than needed
  if (!clockSet || d < -1000 || d > static_cast<int64_t>(2 * kBlockUs)) {
    if (clockSet) resyncs.fetch_add(1, std::memory_order_relaxed);
    blockT = want;
    clockSet = true;
  }
}

#ifdef AUDIO_CPU_LOG
int blockEvents;  // events fed into the current block (diagnostics)
#endif

void feed(uint8_t track, const uint8_t* b, uint8_t len, int off) {
#ifdef AUDIO_CPU_LOG
  ++blockEvents;
#endif
  if (!synth->event(off, track, b, len)) lostEvents.fetch_add(1, std::memory_order_relaxed);
}

// Moves every queued event that falls into this block into the synth (FIFO = time order).
void drain(Ring& q) {
  while (const Ev* e = q.peek()) {
    const int off = mt::eventOffset(e->t, blockT);
    if (off < 0) break;  // a later block
    if (e->t < blockT) lateEvents.fetch_add(1, std::memory_order_relaxed);
    if (&q == &uiQ && e->track == mt::kPreviewTrack && (e->b[0] & 0xF0) == 0x90) {
      if (previewOn) {
        const uint8_t m[3] = {0x80, previewNote, 0};
        feed(mt::kPreviewTrack, m, 3, off);
      }
      previewOn = true;
      previewNote = e->b[1];
      previewOffT = e->t + kPreviewUs;
    }
    feed(e->track, e->b, e->len, off);
    q.pop();
  }
  if (previewOn && mt::eventOffset(previewOffT, blockT) >= 0) {
    const uint8_t m[3] = {0x80, previewNote, 0};
    feed(mt::kPreviewTrack, m, 3, mt::eventOffset(previewOffT, blockT));
    previewOn = false;
  }
}

[[maybe_unused]] void renderSynth(int16_t* out) {
  advanceClock();
  drain(engineQ);
  drain(uiQ);
  synth->render(out);
  takePreviewRequest();
  if (oneshot.playing()) {
    // Level of a sample at full instrument volume (synth: x4 x0.25 x masterVol).
    const uint8_t mv = project->masterVol > mt::kMasterVolMax ? mt::kMasterVolMax : project->masterVol;
    oneshot.mix(out, kBlock, mv * 0.01f);
  }
  pvPlaying.store(oneshot.playing(), std::memory_order_relaxed);
}

#if defined(AUDIO_BENCH) || defined(AUDIO_BENCH_FM)
std::atomic<uint32_t> renderUs{0};   // last block
std::atomic<uint32_t> benchPeak{0};  // render peak since the last pollLog() print
#endif

#ifdef AUDIO_BENCH
constexpr int kBenchVoices = 16;
constexpr uint32_t kBenchSpan = 65536;  // samples each voice loops over

struct BenchVoice {
  const int16_t* data;
  float phase;
  float inc;
  float env;
};
BenchVoice bench[kBenchVoices];
bool benchOn;

void benchBegin() {
  const uint8_t* bankBase = samplesBase();
  const uint32_t avail = bankBase ? samplesSize() / 2 : 0;  // in samples
  benchOn = avail > kBenchSpan + 1;
  if (!benchOn) {
    Serial.println("audio bench: samples partition not mapped, bench off");
    return;
  }
  for (int v = 0; v < kBenchVoices; ++v) {
    BenchVoice& b = bench[v];
    // Spread voices over the bank so they miss the flash cache like real samples.
    const uint32_t off = (avail - kBenchSpan - 1) / kBenchVoices * v;
    b.data = reinterpret_cast<const int16_t*>(bankBase) + off;
    b.phase = 0.0f;
    b.inc = 0.5f + 0.1f * v;
    b.env = 1.0f;
  }
}

void renderBench(int16_t* out) {
  if (!benchOn) {
    memset(out, 0, sizeof(int16_t) * kBlock);
    return;
  }
  for (int i = 0; i < kBlock; ++i) {
    float acc = 0.0f;
    for (BenchVoice& b : bench) {
      const uint32_t idx = static_cast<uint32_t>(b.phase);
      const float frac = b.phase - static_cast<float>(idx);
      const float a = b.data[idx];
      const float s = a + (static_cast<float>(b.data[idx + 1]) - a) * frac;
      acc += s * b.env;
      b.env *= 0.99999f;
      if (b.env < 0.01f) b.env = 1.0f;
      b.phase += b.inc;
      if (b.phase >= static_cast<float>(kBenchSpan)) b.phase -= static_cast<float>(kBenchSpan);
    }
    // Keep it inaudible-ish: the point is the timing, not the sound.
    out[i] = static_cast<int16_t>(acc * (1.0f / 1024.0f));
  }
}
#endif

#ifdef AUDIO_BENCH_FM
// Worst case per voice is HAT: 4 feedback operators + noise + SVF on everything (each algorithm
// runs all 4 operators every sample, so TONE / CHORD cost 4 operators without noise / filter).
// But drums (and CHORD) are mono on a track, and there are only 8 tracks, so 16 HATs cannot
// exist; only POLY TONE reaches 4 voices per track. With 8 tracks the most HATs that still fill
// the pool: 3 TONE tracks x 4 + 4 HAT tracks = 16 voices. HAT is one-shot (DECAY max = 4 s), so
// benchFmTick() retriggers it every second (a choke on the same voice).
constexpr int kBenchToneTracks = 3, kBenchHatTracks = 4;
constexpr uint32_t kBenchHatEvery = 250;  // blocks, 1 s

void benchFmHats() {
  for (int t = kBenchToneTracks; t < kBenchToneTracks + kBenchHatTracks; ++t) {
    const uint8_t on[3] = {0x90, static_cast<uint8_t>(60 + t), 100};
    synth->event(0, static_cast<uint8_t>(t), on, 3);
  }
}

void benchFmBegin() {
  mt::Instrument& tone = project->instruments[15];
  tone = mt::Instrument();
  tone.type = mt::InstrType::Fm;
  mt::fmSetMachine(tone, static_cast<uint8_t>(mt::FmMachine::Tone));
  tone.macro[mt::kMacShp] = 80;  // stack zone
  tone.macro[mt::kMacCol] = 100;
  tone.sustain = 127;
  tone.mono = false;
  mt::Instrument& hat = project->instruments[14];
  hat = mt::Instrument();
  hat.type = mt::InstrType::Fm;
  mt::fmSetMachine(hat, static_cast<uint8_t>(mt::FmMachine::Hat));
  hat.macro[mt::kMacDec] = 127;  // 4 s: outlives the retrigger period
  hat.macro[mt::kMacCon] = 127;  // operators ring as long as the amp
  hat.macro[mt::kMacShp] = 64;   // operators and noise both audible
  for (int t = 0; t < kBenchToneTracks; ++t) {
    project->tracks[t].out = mt::TrackOut::Int;
    project->tracks[t].instr = 15;
    for (int k = 0; k < mt::kPolyPerTrack; ++k) {
      const uint8_t on[3] = {0x90, static_cast<uint8_t>(48 + t * 7 + k * 3), 100};
      synth->event(0, static_cast<uint8_t>(t), on, 3);
    }
  }
  for (int t = kBenchToneTracks; t < kBenchToneTracks + kBenchHatTracks; ++t) {
    project->tracks[t].out = mt::TrackOut::Int;
    project->tracks[t].instr = 14;
  }
  benchFmHats();
}

// Audio task, before the block is rendered.
void benchFmTick() {
  static uint32_t blocks;
  if (++blocks % kBenchHatEvery == 0) benchFmHats();
}
#endif

void render(int16_t* out) {
#ifdef AUDIO_BENCH
  renderBench(out);
#else
#ifdef AUDIO_BENCH_FM
  benchFmTick();
#endif
  renderSynth(out);
#endif
}

// Parks the task until resumeAfterFlash(). Every DMA buffer is filled with silence first: while
// flash is written the I2S interrupt may not run and the DMA keeps cycling through its buffers.
void park() {
  const uint32_t g = pauseGen.load(std::memory_order_acquire);
  synth->reset();
  while (engineQ.peek()) engineQ.pop();
  while (uiQ.peek()) uiQ.pop();
  previewOn = false;
  oneshot.stop();
  pvPlaying.store(false, std::memory_order_relaxed);
  pvAck.store(pvReq.load(std::memory_order_acquire), std::memory_order_release);
  memset(lr, 0, sizeof(lr));
  for (int i = 0; i <= kDmaBlocks; ++i) {
    size_t written = 0;
    i2s_channel_write(tx, lr, sizeof(lr), &written, portMAX_DELAY);
  }
  parkedGen.store(g, std::memory_order_release);
  xSemaphoreGive(pausedSem);
  // A stale give (an earlier withdrawn request) does not release this park.
  do {
    xSemaphoreTake(resumeSem, portMAX_DELAY);
  } while (static_cast<int32_t>(resumeGen.load(std::memory_order_acquire) - g) < 0);
  clockSet = false;  // the DMA ran dry meanwhile
}

#ifdef AUDIO_CPU_LOG
std::atomic<uint32_t> cpuSum{0}, cpuBlocks{0}, cpuOver{0}, cpuPeakEv{0}, cpuPeakQuiet{0};
#endif

void run(void*) {
  for (;;) {
    if (pauseReq.load(std::memory_order_acquire)) {
      park();
      continue;
    }
#ifdef AUDIO_CPU_LOG
    blockEvents = 0;
#endif
    const int64_t t0 = esp_timer_get_time();
    render(mono);
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
#ifdef AUDIO_CPU_LOG
    cpuSum.fetch_add(us, std::memory_order_relaxed);
    cpuBlocks.fetch_add(1, std::memory_order_relaxed);
    if (us > kBlockUs) cpuOver.fetch_add(1, std::memory_order_relaxed);
    std::atomic<uint32_t>& pk = blockEvents ? cpuPeakEv : cpuPeakQuiet;
    if (us > pk.load(std::memory_order_relaxed)) pk.store(us, std::memory_order_relaxed);
#endif
    // Single writer: a reset by the UI between load and store at worst keeps this block's time.
    loadSum.fetch_add(us, std::memory_order_relaxed);
    loadBlocks.fetch_add(1, std::memory_order_relaxed);
    if (us > loadPeak.load(std::memory_order_relaxed)) loadPeak.store(us, std::memory_order_relaxed);
#if defined(AUDIO_BENCH) || defined(AUDIO_BENCH_FM)
    renderUs.store(us, std::memory_order_relaxed);
    if (us > benchPeak.load(std::memory_order_relaxed)) benchPeak.store(us, std::memory_order_relaxed);
#endif
    for (int i = 0; i < kBlock; ++i) lr[2 * i] = lr[2 * i + 1] = mono[i];
    size_t written = 0;
    i2s_channel_write(tx, lr, sizeof(lr), &written, portMAX_DELAY);
  }
}

bool initI2s() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = kDmaBlocks;
  cc.dma_frame_num = kBlock;
  cc.auto_clear = true;  // silence instead of a repeated block on underrun
  if (i2s_new_channel(&cc, &tx, nullptr) != ESP_OK) return false;

  i2s_std_config_t sc = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = static_cast<gpio_num_t>(pins::kI2sBclk),
              .ws = static_cast<gpio_num_t>(pins::kI2sWs),
              .dout = static_cast<gpio_num_t>(pins::kI2sDout),
              .din = I2S_GPIO_UNUSED,
              .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
          },
  };
  if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK) return false;
  return i2s_channel_enable(tx) == ESP_OK;
}

}  // namespace

void begin(mt::Project* p) {
  project = p;
  // Internal RAM: the synth state is touched on every sample.
  void* mem = heap_caps_malloc(sizeof(mt::Synth), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  synth = mem ? new (mem) mt::Synth(*p) : new mt::Synth(*p);
  bankBegin();
  synth->setBank(sampleSource());
#ifdef AUDIO_BENCH
  benchBegin();
#endif
#ifdef AUDIO_BENCH_FM
  benchFmBegin();
#endif
  if (!initI2s()) {
    Serial.println("audio: I2S init failed");
    return;
  }
  // Below the engine (configMAX_PRIORITIES - 2) on the same core.
  pausedSem = xSemaphoreCreateBinary();
  resumeSem = xSemaphoreCreateBinary();
  if (!pausedSem || !resumeSem) return;
  xTaskCreatePinnedToCore(run, "audio", 4096, nullptr, configMAX_PRIORITIES - 3, &task, 0);
}

void post(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) {
  if (len == 0 || len > 3) return;
  Ev e{t, track, len, {0, 0, 0}};
  memcpy(e.b, b, len);
  if (!engineQ.push(e)) lostEvents.fetch_add(1, std::memory_order_relaxed);
}

void preview(uint8_t instr, uint8_t note) {
  const uint64_t t = engine::nowUs();
  const Ev pg{t, mt::kPreviewTrack, 2, {0xC0, static_cast<uint8_t>(instr & 15), 0}};
  const Ev on{t, mt::kPreviewTrack, 3, {0x90, static_cast<uint8_t>(note & 127), 100}};
  if (!uiQ.push(pg) || !uiQ.push(on)) lostEvents.fetch_add(1, std::memory_order_relaxed);
}

namespace {
bool sendPreview(const int16_t* d, uint32_t frames, uint32_t rate) {
  if (!task) return d == nullptr;  // no audio task: nothing reads the buffer
  pvData = d;
  pvFrames = frames;
  pvRate = rate;
  const uint32_t r = pvReq.load(std::memory_order_relaxed) + 1;
  pvReq.store(r, std::memory_order_release);
  // The task takes requests once per block (4 ms); a parked task acknowledges on park.
  for (int i = 0; i < 25; ++i) {
    if (pvAck.load(std::memory_order_acquire) == r) return true;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return false;
}
}  // namespace

void previewBuffer(const int16_t* d, uint32_t frames, uint32_t rate) {
  if (!sendPreview(d, frames, rate)) return;
  pvPlaying.store(true, std::memory_order_relaxed);  // until the task's next block says otherwise
}

bool previewStop() { return sendPreview(nullptr, 0, 0); }

bool previewPlaying() { return pvPlaying.load(std::memory_order_relaxed); }

Load takeLoad() {
  Load l;
  l.blocks = loadBlocks.exchange(0, std::memory_order_relaxed);
  l.sumUs = loadSum.exchange(0, std::memory_order_relaxed);
  l.peakUs = loadPeak.exchange(0, std::memory_order_relaxed);
  return l;
}

#ifdef AUDIO_CPU_LOG
// One-off timings on the UI core (diagnostics): one FM voice per machine, 100 blocks.
void microBench() {
  static float buf[kBlock];
  static mt::FmVoice v;
  mt::FmParams prm;
  float mac[mt::kFmMacros] = {100, 64, 64, 64, 64};
  for (int m = 0; m < static_cast<int>(mt::FmMachine::Count); ++m) {
    v.trigger(false);
    int64_t tm = 0, tc = 0, tr = 0;
    for (int b = 0; b < 100; ++b) {
      for (int c = 0; c < kBlock / 32; ++c) {
        int64_t t0 = esp_timer_get_time();
        mt::fmMachine(m, mac, 60, prm);
        int64_t t1 = esp_timer_get_time();
        v.control(prm, 32);
        int64_t t2 = esp_timer_get_time();
        v.render(buf + c * 32, 32, 1.f);
        int64_t t3 = esp_timer_get_time();
        tm += t1 - t0; tc += t2 - t1; tr += t3 - t2;
      }
    }
    Serial.printf("micro m%d: per block machine %u us, control %u us, render %u us\n", m,
                  static_cast<unsigned>(tm / 100), static_cast<unsigned>(tc / 100), static_cast<unsigned>(tr / 100));
  }
  volatile float acc = 0;
  int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < 1000; ++i) acc += expf(-0.001f * i);
  int64_t t1 = esp_timer_get_time();
  for (int i = 0; i < 1000; ++i) acc += powf(1.001f, 0.01f * i);
  int64_t t2 = esp_timer_get_time();
  for (int i = 0; i < 1000; ++i) acc += acc * 1.0001f + 0.5f;
  int64_t t3 = esp_timer_get_time();
  Serial.printf("micro: 1000x expf %u us, powf %u us, fmadd %u us\n", static_cast<unsigned>(t1 - t0),
                static_cast<unsigned>(t2 - t1), static_cast<unsigned>(t3 - t2));
}
#endif

void pollLog() {
  const int64_t now = esp_timer_get_time();
#if defined(AUDIO_BENCH) || defined(AUDIO_BENCH_FM)
  static int64_t lastBench;
  if (now - lastBench >= 1000000) {
    lastBench = now;
    Serial.printf("audio bench: render %u us (peak %u) / 4000 us\n",
                  static_cast<unsigned>(renderUs.load(std::memory_order_relaxed)),
                  static_cast<unsigned>(benchPeak.exchange(0, std::memory_order_relaxed)));
  }
#endif
#ifdef AUDIO_CPU_LOG
  static int64_t lastCpu;
  if (now - lastCpu >= 1000000) {
    lastCpu = now;
    const uint32_t n = cpuBlocks.exchange(0, std::memory_order_relaxed);
    const uint32_t sum = cpuSum.exchange(0, std::memory_order_relaxed);
    Serial.printf("cpu: blocks %u avg %u us, over4ms %u, peak ev %u quiet %u, resyncs %u, voices %d\n",
                  static_cast<unsigned>(n), static_cast<unsigned>(n ? sum / n : 0),
                  static_cast<unsigned>(cpuOver.exchange(0, std::memory_order_relaxed)),
                  static_cast<unsigned>(cpuPeakEv.exchange(0, std::memory_order_relaxed)),
                  static_cast<unsigned>(cpuPeakQuiet.exchange(0, std::memory_order_relaxed)),
                  static_cast<unsigned>(resyncs.load(std::memory_order_relaxed)), synth ? synth->activeVoices() : -1);
  }
#endif
#ifdef AUDIO_CPU_LOG
  static bool microDone;
  if (!microDone && now > 5000000) {
    microDone = true;
    microBench();
  }
#endif
  // Lost / late events, at most every 5 s and only when they changed.
  static int64_t last;
  static uint32_t shownLost, shownLate, shownResync;
  if (now - last < 5000000) return;
  last = now;
  const uint32_t lost = lostEvents.load(std::memory_order_relaxed);
  const uint32_t late = lateEvents.load(std::memory_order_relaxed);
  const uint32_t resync = resyncs.load(std::memory_order_relaxed);
  if (lost == shownLost && late == shownLate && resync == shownResync) return;
  shownLost = lost;
  shownLate = late;
  shownResync = resync;
  Serial.printf("audio: events lost %u, late %u, clock resyncs %u\n", static_cast<unsigned>(lost),
                static_cast<unsigned>(late), static_cast<unsigned>(resync));
}

bool pauseForFlash() {
  if (!task) return true;  // no audio task: nothing reads the bank
  const uint32_t g = pauseGen.fetch_add(1, std::memory_order_acq_rel) + 1;
  pauseReq.store(true, std::memory_order_release);
  const TickType_t start = xTaskGetTickCount(), wait = pdMS_TO_TICKS(500);
  for (;;) {
    const TickType_t spent = xTaskGetTickCount() - start;
    if (spent >= wait) break;
    // Gives for older requests are skipped.
    if (xSemaphoreTake(pausedSem, wait - spent) == pdTRUE && parkedGen.load(std::memory_order_acquire) == g)
      return true;
  }
  // Not parked in time: withdraw. A late park() for this request is released by the give below.
  pauseReq.store(false, std::memory_order_release);
  resumeGen.store(g, std::memory_order_release);
  xSemaphoreGive(resumeSem);
  return false;
}

void resumeAfterFlash() {
  if (!task) return;
  pauseReq.store(false, std::memory_order_release);
  resumeGen.store(pauseGen.load(std::memory_order_acquire), std::memory_order_release);
  xSemaphoreGive(resumeSem);
}

}  // namespace audio
