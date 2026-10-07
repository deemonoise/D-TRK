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
#include "esp_memory_utils.h"
#include "esp_cpu.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#ifdef AUDIO_BENCH_POOL
#include "storage/crashlog.h"
#endif
#include "hw/pins.h"
#include "oneshot.h"
#include "synth.h"
#include "synth_fm.h"
#include "synth_fm_machines.h"
#include "synth_drum.h"
#include "synth_drum_machines.h"

// Bench: build with -DAUDIO_BENCH (add to build_flags of [env:wt32]) to replace the synth with
// 16 fake sampler voices reading the sample partition; the render time goes to Serial once a second.
// FM bench: -DAUDIO_BENCH_FM instead keeps the whole 16-voice pool busy with FM through the real
// synth: 12 TONE voices (POLY, tracks 1..3) + 4 HAT voices (tracks 4..7), the heaviest mix that
// can be held (see benchFmBegin). Same Serial line. It overwrites instruments 15, 16 and the
// tracks' out / instrument of the loaded project in RAM: there is no autosave, but do not SAVE
// the project from a bench build.
// DRUM bench: -DAUDIO_BENCH_DRUM puts a DRUM machine on every track (BD8, SD8, open HH8, CY9, CP8,
// CB8, TOM9, HH9), all retriggered every 16th at 120 BPM, plus held CHIP saw voices through a
// resonant LP filter with an envelope on the preview track (see benchDrumBegin). Same Serial line.
// It overwrites instruments 7..15 and every track's out / instrument, like the FM bench.
// SYNTH bench: -DAUDIO_BENCH_SYN puts a SYNTH on every track (two wavetable oscillators *SAWSQR /
// *FORMANT mixed, LFO on SHP1, sub, LP filter) and plays a 2-note chord per track every 2 s: 16 voices.
// With -DAUDIO_BENCH_SYN_N=8 one note per track (8 voices). Same Serial line. It overwrites
// instruments 8..15 and every track's out / instrument, like the FM bench.
// FX bench: -DAUDIO_BENCH_FX is the DRUM bench with drive 100 and reverb send 100 on every bench
// instrument, reverb level 100, compressor 100 keyed by track 1. Compare in PROJ while it runs:
// Rvb level 0 = no reverb, Comp OFF = no compressor (the status-bar CPU figure).
// Pool bench: -DAUDIO_BENCH_POOL with -DMT_VOICES=N -DMT_HEAVY_MAX=H (envs wt32-pool24 /
// wt32-pool32): first 16 CHIP voices alone (their cost per voice), then the whole N-voice pool
// with 8, 10, H heavy voices (DRUM and FM HAT on alternate tracks, retriggered every 16th), the
// rest CHIP saw voices through a resonant LP filter with an envelope; drive and reverb on all,
// reverb 100, compressor on. The CPU guard (Synth::setLoad) sheds what does not fit.
// Each step lasts 10 s; the last 8 s are measured (with the CPU profile), printed to Serial and
// appended to /diag/cpuprof.txt with the profile. Then the bench stops (all notes off).
// It overwrites instruments 16..28 and every track's out / instrument, like the FM bench.
#if defined(AUDIO_BENCH_FX) && !defined(AUDIO_BENCH_DRUM)
#define AUDIO_BENCH_DRUM
#endif
#if defined(AUDIO_BENCH) || defined(AUDIO_BENCH_FM) || defined(AUDIO_BENCH_DRUM) || defined(AUDIO_BENCH_SYN) || \
    defined(AUDIO_BENCH_POOL)
#define AUDIO_BENCH_ANY
#endif

namespace audio {
namespace {

// Scope ring: written by the audio task after every block, read by the UI (~1 us per block).
int16_t scope[kScopeLen];
std::atomic<int> scopeAt{0};
std::atomic<int> scopePk{0};

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
  uint8_t hold = 0;  // preview note-on: length x100 ms, 0 = kPreviewUs
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
void* synthMem = nullptr;  // reserve()
bool reverbInt = false;    // the reverb buffer is in internal RAM
float* reverbBuf = nullptr;  // the reverb buffer (internal or PSRAM)
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
      previewOffT = e->t + (e->hold ? e->hold * 100000ull : kPreviewUs);
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
  const uint32_t q0 = synth->profNow();
  advanceClock();
  drain(engineQ);
  drain(uiQ);
  if (synth->profiling()) synth->addProfile(mt::Synth::kProfQueue, synth->profNow() - q0);
  synth->render(out);
  takePreviewRequest();
  if (oneshot.playing()) {
    // Level of a sample at full instrument volume (synth: x4 x0.25 x masterVol).
    const uint8_t mv = project->masterVol > mt::kMasterVolMax ? mt::kMasterVolMax : project->masterVol;
    oneshot.mix(out, kBlock, mv * 0.01f);
  }
  pvPlaying.store(oneshot.playing(), std::memory_order_relaxed);
}

#ifdef AUDIO_BENCH_ANY
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
// But heavy voices (FM, DRUM, wavetable SYNTH) are capped at kFmVoiceMax = 8 sounding at once,
// whatever the track count, so 16 HATs cannot exist; drums (and CHORD) are mono on a track and
// only POLY TONE reaches 4 voices per track. A load that fills the 16-voice pool with the heavy
// cap saturated: 3 TONE tracks x 4 + 4 HAT tracks = 16 voices. HAT is one-shot (DECAY max = 4 s),
// so benchFmTick() retriggers it every second (a choke on the same voice).
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

#ifdef AUDIO_BENCH_DRUM
// Heaviest DRUM load the synth can hold: heavy voices are capped at kFmVoiceMax = 8, so 8 mono DRUM
// tracks saturate it however many tracks the pattern has. The rest of the pool cannot be reached
// from those tracks (a mono DRUM note takes its track's newest voice), so the CHIP voices sit on the
// preview track: kPolyPerTrack POLY voices with the filter. Long decays keep every DRUM voice
// sounding into the next 16th (choke).
constexpr uint32_t kBenchDrumEvery = 31;  // blocks, ~124 ms: a 16th at 120 BPM
constexpr uint8_t kBenchDrumInstr = 8;    // instruments 8..15: drums, 7: CHIP
constexpr uint8_t kBenchChipInstr = 7;

constexpr int kBenchTracks = 8;  // one DRUM instrument per bench track: instruments 8..15, tracks 1..8
static_assert(kBenchDrumInstr + kBenchTracks <= mt::kInstruments && kBenchTracks <= mt::kTracks, "bench layout");

void benchDrumHits() {
  for (int t = 0; t < kBenchTracks; ++t) {
    const uint8_t on[3] = {0x90, 60, 100};
    synth->event(0, static_cast<uint8_t>(t), on, 3);
  }
}

void benchDrumBegin() {
  using mt::DrumMachine;
  static const DrumMachine kMachines[kBenchTracks] = {DrumMachine::Bd8, DrumMachine::Sd8, DrumMachine::Hh8,
                                                      DrumMachine::Cy9, DrumMachine::Cp8, DrumMachine::Cb8,
                                                      DrumMachine::Tom9, DrumMachine::Hh9};
  for (int t = 0; t < kBenchTracks; ++t) {
    mt::Instrument& d = project->instruments[kBenchDrumInstr + t];
    d = mt::Instrument();
    mt::instrSetType(d, mt::InstrType::Drum);
    mt::drumSetMachine(d, static_cast<uint8_t>(kMachines[t]));
    d.macro[mt::kMacDec] = 110;  // longer than a 16th: HH8 open, the rest still ringing at the choke
    project->tracks[t].out = mt::TrackOut::Int;
    project->tracks[t].instr = static_cast<uint8_t>(kBenchDrumInstr + t);
  }
  mt::Instrument& chip = project->instruments[kBenchChipInstr];
  chip = mt::Instrument();
  chip.wave = static_cast<uint8_t>(mt::Wave::Saw);
  chip.sustain = 127;
  chip.mono = false;
  chip.fltMode = static_cast<uint8_t>(mt::FltMode::Lp);
  chip.cutoff = 60;
  chip.reso = 100;
  chip.fenv = 40;
  chip.fDec = 0;  // the envelope holds: the cutoff stays modulated
  const uint8_t pgm[2] = {0xC0, kBenchChipInstr};
  synth->event(0, mt::kPreviewTrack, pgm, 2);
  for (int k = 0; k < mt::kPolyPerTrack; ++k) {
    const uint8_t on[3] = {0x90, static_cast<uint8_t>(48 + k * 5), 100};
    synth->event(0, mt::kPreviewTrack, on, 3);
  }
#ifdef AUDIO_BENCH_FX
  for (int i = kBenchChipInstr; i < kBenchDrumInstr + kBenchTracks; ++i) {
    project->instruments[i].drive = 100;
    project->instruments[i].rsend = 100;
  }
  project->rvbLevel = 100;
  project->compAmt = 100;
  project->scTrack = 1;
#endif
  benchDrumHits();
}

// Audio task, before the block is rendered.
void benchDrumTick() {
  static uint32_t blocks;
  if (++blocks % kBenchDrumEvery == 0) benchDrumHits();
}
#endif

#ifdef AUDIO_BENCH_SYN
#ifndef AUDIO_BENCH_SYN_N
#define AUDIO_BENCH_SYN_N 16
#endif
static_assert(AUDIO_BENCH_SYN_N == 8 || AUDIO_BENCH_SYN_N == 16, "AUDIO_BENCH_SYN_N: 8 or 16 voices");
constexpr int kBenchSynNotes = AUDIO_BENCH_SYN_N / mt::kTracks;  // per track: 1 or 2 (a chord)
constexpr uint32_t kBenchSynEvery = 500;                          // blocks, 2 s
constexpr uint8_t kBenchSynInstr = 8;                             // instruments 8..15

// Releases the previous chord and plays the next one: the voices stay at AUDIO_BENCH_SYN_N
// (release tails end well before the next chord, see benchSynBegin).
void benchSynChords() {
  static uint8_t shift;
  for (int t = 0; t < mt::kTracks; ++t) {
    for (int k = 0; k < kBenchSynNotes; ++k) {
      const uint8_t note = static_cast<uint8_t>(40 + t * 5 + k * 7);
      const uint8_t off[3] = {0x80, static_cast<uint8_t>(note + shift), 0};
      const uint8_t on[3] = {0x90, static_cast<uint8_t>(note + (shift ^ 2)), 100};
      synth->event(0, static_cast<uint8_t>(t), off, 3);
      synth->event(0, static_cast<uint8_t>(t), on, 3);
    }
  }
  shift ^= 2;  // a whole tone up and back: new notes, not retriggers of the same ones
}

void benchSynBegin() {
  for (int t = 0; t < mt::kTracks; ++t) {
    mt::Instrument& m = project->instruments[kBenchSynInstr + t];
    m = mt::Instrument();
    mt::instrSetType(m, mt::InstrType::Synth);
    m.synOsc[0] = m.synOsc[1] = static_cast<uint8_t>(mt::SynOsc::Wt);
    strlcpy(m.synWt[0], "*SAWSQR", sizeof(m.synWt[0]));
    strlcpy(m.synWt[1], "*FORMANT", sizeof(m.synWt[1]));
    m.macro[mt::kMacMix] = 64;
    m.lfoWave = static_cast<uint8_t>(mt::LfoWave::Sine);
    m.lfoDest = static_cast<uint8_t>(mt::LfoDest::Dec);  // SHP1
    m.lfoDepth = 30;
    m.lfoRate = 40;
    m.synSub = 40;
    m.fltMode = static_cast<uint8_t>(mt::FltMode::Lp);
    m.cutoff = 90;
    m.sustain = 127;
    m.release = 10;  // short tails: the next chord starts on free voices
    m.mono = false;  // POLY
    project->tracks[t].out = mt::TrackOut::Int;
    project->tracks[t].instr = static_cast<uint8_t>(kBenchSynInstr + t);
  }
  benchSynChords();
}

// Audio task, before the block is rendered.
void benchSynTick() {
  static uint32_t blocks;
  if (++blocks % kBenchSynEvery == 0) benchSynChords();
}
#endif

#ifdef AUDIO_BENCH_POOL
static_assert(mt::kFmVoiceMax >= 8 && mt::kFmVoiceMax <= 12, "pool bench: heavy tracks 1..12");
constexpr int kPoolHeavyInstr = 16;  // instruments 16..27: heavy, 28: CHIP
constexpr int kPoolChipInstr = kPoolHeavyInstr + 12;
static_assert(kPoolChipInstr < mt::kInstruments, "pool bench layout");
// The light voices of the biggest heavy step fit on the tracks left and the preview track.
static_assert((mt::kTracks - mt::kFmVoiceMax + 1) * mt::kPolyPerTrack >= mt::kVoices - mt::kFmVoiceMax,
              "pool bench: light voices");
constexpr uint32_t kPoolHitEvery = 31;    // blocks, ~124 ms: a 16th at 120 BPM
constexpr uint32_t kPoolStepBlocks = 2500;  // 10 s
constexpr uint32_t kPoolSkipBlocks = 500;   // settling, not measured
// Steps: CHIP alone (16 voices, the per-voice cost), then the whole pool with 8, 10, 12 heavy.
struct PoolStep {
  int heavy, light;
};
constexpr PoolStep kPoolSteps[] = {{0, 16},
                                   {8, mt::kVoices - 8},
                                   {mt::kFmVoiceMax >= 10 ? 10 : 8, mt::kVoices - (mt::kFmVoiceMax >= 10 ? 10 : 8)},
                                   {mt::kFmVoiceMax, mt::kVoices - mt::kFmVoiceMax}};
constexpr uint32_t kPoolStepCount = sizeof(kPoolSteps) / sizeof(kPoolSteps[0]);

uint32_t poolStepsDone;
int poolHeavy = kPoolSteps[0].heavy, poolLightN = kPoolSteps[0].light;
uint32_t poolBlocks, poolSum, poolN, poolPeak, poolVoiceSum, poolCapMin;
std::atomic<uint32_t> poolSeq{0}, poolResHeavy{0}, poolResLight{0}, poolResAvg{0}, poolResPeak{0},
    poolResVoices{0}, poolResCap{0};
std::atomic<uint32_t> poolMeasureSeq{0};  // a step's measured part began: pollLog starts the profile

void poolNote(int track, uint8_t status, uint8_t note) {
  const uint8_t b[3] = {status, note, 100};
  synth->event(0, static_cast<uint8_t>(track), b, 3);
}

void poolHits() {
  for (int t = 0; t < poolHeavy; ++t) poolNote(t, 0x90, 60);
}

// Light notes of the step: kPolyPerTrack per track from poolHeavy up, then the preview track.
template <typename F>
void poolLight(F f) {
  int left = poolLightN;
  for (int t = poolHeavy; t <= mt::kTracks && left > 0; ++t) {
    const int track = t < mt::kTracks ? t : mt::kPreviewTrack;
    for (int k = 0; k < mt::kPolyPerTrack && left > 0; ++k, --left)
      f(track, static_cast<uint8_t>(48 + (t % 4) * 3 + k * 5));
  }
}

void poolStep() {
  for (int t = 0; t < mt::kTracks; ++t) {
    project->tracks[t].out = mt::TrackOut::Int;
    project->tracks[t].instr = static_cast<uint8_t>(t < poolHeavy ? kPoolHeavyInstr + t : kPoolChipInstr);
  }
  poolLight([](int track, uint8_t note) { poolNote(track, 0x90, note); });
  poolHits();
}

void benchPoolBegin() {
  using mt::DrumMachine;
  static const DrumMachine kMachines[6] = {DrumMachine::Bd8, DrumMachine::Sd8, DrumMachine::Hh8,
                                           DrumMachine::Cy9, DrumMachine::Cp8, DrumMachine::Tom9};
  for (int i = 0; i < 12; ++i) {
    mt::Instrument& m = project->instruments[kPoolHeavyInstr + i];
    m = mt::Instrument();
    if (i % 2 == 0) {
      mt::instrSetType(m, mt::InstrType::Drum);
      mt::drumSetMachine(m, static_cast<uint8_t>(kMachines[i / 2]));
      m.macro[mt::kMacDec] = 110;  // still ringing at the choke
    } else {
      mt::instrSetType(m, mt::InstrType::Fm);
      mt::fmSetMachine(m, static_cast<uint8_t>(mt::FmMachine::Hat));
      m.macro[mt::kMacDec] = 127;
      m.macro[mt::kMacCon] = 127;
      m.macro[mt::kMacShp] = 64;
    }
  }
  mt::Instrument& chip = project->instruments[kPoolChipInstr];
  chip = mt::Instrument();
  chip.wave = static_cast<uint8_t>(mt::Wave::Saw);
  chip.sustain = 127;
  chip.mono = false;
  chip.fltMode = static_cast<uint8_t>(mt::FltMode::Lp);
  chip.cutoff = 60;
  chip.reso = 100;
  chip.fenv = 40;
  chip.fDec = 0;
  for (int i = kPoolHeavyInstr; i <= kPoolChipInstr; ++i) {
    project->instruments[i].drive = 100;
    project->instruments[i].rsend = 100;
  }
  project->rvbLevel = 100;
  project->compAmt = 100;
  project->scTrack = 1;
  const uint8_t pgm[2] = {0xC0, kPoolChipInstr};
  synth->event(0, mt::kPreviewTrack, pgm, 2);
  poolStep();
}

// Audio task, before the block is rendered: renderUs holds the previous block.
void benchPoolTick() {
  if (poolStepsDone >= kPoolStepCount) return;
  ++poolBlocks;
  if (poolBlocks == kPoolSkipBlocks) {
    poolMeasureSeq.fetch_add(1, std::memory_order_release);
    poolCapMin = mt::kVoices;
  }
  if (poolBlocks > kPoolSkipBlocks) {
    const uint32_t us = renderUs.load(std::memory_order_relaxed);
    poolSum += us;
    ++poolN;
    if (us > poolPeak) poolPeak = us;
    poolVoiceSum += synth->activeVoices();
    if (static_cast<uint32_t>(synth->voiceCap()) < poolCapMin) poolCapMin = synth->voiceCap();
  }
  if (poolBlocks % kPoolHitEvery == 0) poolHits();
  if (poolBlocks < kPoolStepBlocks) return;
  poolResHeavy.store(poolHeavy, std::memory_order_relaxed);
  poolResLight.store(poolLightN, std::memory_order_relaxed);
  poolResAvg.store(poolN ? poolSum / poolN : 0, std::memory_order_relaxed);
  poolResPeak.store(poolPeak, std::memory_order_relaxed);
  poolResVoices.store(poolN ? (poolVoiceSum + poolN / 2) / poolN : 0, std::memory_order_relaxed);
  poolResCap.store(poolCapMin, std::memory_order_relaxed);
  poolSeq.fetch_add(1, std::memory_order_release);
  poolBlocks = poolSum = poolN = poolPeak = poolVoiceSum = 0;
  poolLight([](int track, uint8_t note) { poolNote(track, 0x80, note); });
  if (++poolStepsDone >= kPoolStepCount) return;  // quiet: the drums decay on their own
  poolHeavy = kPoolSteps[poolStepsDone].heavy;
  poolLightN = kPoolSteps[poolStepsDone].light;
  poolStep();
}
#endif

void render(int16_t* out) {
#ifdef AUDIO_BENCH
  renderBench(out);
#else
#ifdef AUDIO_BENCH_FM
  benchFmTick();
#endif
#ifdef AUDIO_BENCH_DRUM
  benchDrumTick();
#endif
#ifdef AUDIO_BENCH_SYN
  benchSynTick();
#endif
#ifdef AUDIO_BENCH_POOL
  benchPoolTick();
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

// Consecutive non-blocking I2S writes before the overload yield (~0.5 s of blocks).
constexpr int kOverYieldBlocks = 125;
int overBlocks = 0;

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
    if (synth) synth->setLoad(us * (1.f / kBlockUs));  // CPU guard
    // Single writer: a reset by the UI between load and store at worst keeps this block's time.
    loadSum.fetch_add(us, std::memory_order_relaxed);
    loadBlocks.fetch_add(1, std::memory_order_relaxed);
    if (us > loadPeak.load(std::memory_order_relaxed)) loadPeak.store(us, std::memory_order_relaxed);
#ifdef AUDIO_BENCH_ANY
    renderUs.store(us, std::memory_order_relaxed);
    if (us > benchPeak.load(std::memory_order_relaxed)) benchPeak.store(us, std::memory_order_relaxed);
#endif
    for (int i = 0; i < kBlock; ++i) lr[2 * i] = lr[2 * i + 1] = mono[i];
    {
      int at = scopeAt.load(std::memory_order_relaxed);
      int pk = scopePk.load(std::memory_order_relaxed);
      for (int i = 0; i < kBlock; ++i) {
        scope[at] = mono[i];
        at = (at + 1) % kScopeLen;
        const int a = mono[i] < 0 ? -mono[i] : mono[i];
        if (a > pk) pk = a;
      }
      scopeAt.store(at, std::memory_order_relaxed);
      scopePk.store(pk, std::memory_order_relaxed);
    }
    size_t written = 0;
    const int64_t w0 = esp_timer_get_time();
    i2s_channel_write(tx, lr, sizeof(lr), &written, portMAX_DELAY);
    // Overload: with the DMA queue drained the write never blocks and this task (core 0, high
    // priority) starves IDLE0 until the task watchdog resets the board. Yield a tick now and then.
    if (esp_timer_get_time() - w0 < 50) {
      if (++overBlocks >= kOverYieldBlocks) {
        overBlocks = 0;
        vTaskDelay(1);
      }
    } else {
      overBlocks = 0;
    }
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

void reserve() {
  if (!synthMem) synthMem = heap_caps_malloc(sizeof(mt::Synth), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

bool synthInternal() { return synth && esp_ptr_internal(synth); }

bool reverbInternal() { return reverbInt; }

namespace {
constexpr size_t kRvBytes = mt::Reverb::kBufLen * sizeof(float);
// Internal RAM left free with the reverb in it: the SD card and short work buffers (a few KB each).
// Wi-Fi needs far more, but the Wi-Fi dialog moves the reverb to PSRAM first.
constexpr size_t kRvKeepFree = 24 * 1024;

// Swaps the reverb onto a new zeroed buffer with the audio task parked (the tail is lost).
bool moveReverb(uint32_t caps) {
  auto* nb = static_cast<float*>(heap_caps_malloc(kRvBytes, caps));
  if (!nb) return false;
  memset(nb, 0, kRvBytes);
  {
    Paused parked;
    synth->setReverbBuffer(nb, mt::Reverb::kBufLen);
  }
  heap_caps_free(reverbBuf);
  reverbBuf = nb;
  reverbInt = esp_ptr_internal(nb);
  return true;
}
}  // namespace

void reverbToPsram() {
  if (synth && reverbInt) moveReverb(MALLOC_CAP_SPIRAM);
}

void reverbToInternal() {
  if (!synth || reverbInt || !reverbBuf) return;
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < kRvBytes + kRvKeepFree) return;
  moveReverb(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static_assert(kProfStages == mt::Synth::kProfStages, "profile stages");

static uint32_t cycles() { return esp_cpu_get_cycle_count(); }

void profileStart() {
  if (!synth) return;
  uint32_t sink[kProfStages], blocks;
  synth->takeProfile(sink, blocks);  // clear (a block in flight may land either side)
  synth->setProfiler(cycles);
}

bool profileRunning() { return synth && synth->profiling(); }

bool profileStop(Profile& out) {
  if (!synth) return false;
  synth->setProfiler(nullptr);
  vTaskDelay(pdMS_TO_TICKS(10));  // let a block that read the clock finish
  uint32_t c[kProfStages];
  synth->takeProfile(c, out.blocks);
  if (!out.blocks) return false;
  const float perUs = 1.f / (static_cast<float>(getCpuFrequencyMhz()) * out.blocks);
  for (int i = 0; i < kProfStages; ++i) out.us[i] = c[i] * perUs;
  return true;
}

void begin(mt::Project* p) {
  project = p;
  // Internal RAM: the synth state is touched on every sample. reserve() took it at boot.
  reserve();
  if (!synthMem) Serial.println("audio: no internal RAM for the synth, PSRAM (slower)");
  synth = synthMem ? new (synthMem) mt::Synth(*p) : new mt::Synth(*p);
  bankSetProject(p);
  bankBegin();
  synth->setBank(sampleSource());
  synth->setWavetables(wavetableSource());
  // Delay line in PSRAM: 4 s holds 16/16 down to 60 BPM. Read and written once per sample.
  constexpr uint32_t kDelayLen = 4 * kRate;
  if (auto* line = static_cast<int16_t*>(heap_caps_malloc(kDelayLen * sizeof(int16_t), MALLOC_CAP_SPIRAM)))
    synth->setDelayBuffer(line, kDelayLen);
  else
    Serial.println("audio: no PSRAM for the delay line");
  // Reverb: 5934 floats (23 KB), read and written every sample: internal RAM when there is room,
  // else PSRAM (slower through the cache); none = no reverb.
  // Keeps kRvKeepFree of internal RAM free.
  float* rvMem = nullptr;
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= kRvBytes + kRvKeepFree)
    rvMem = static_cast<float*>(heap_caps_malloc(kRvBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!rvMem) rvMem = static_cast<float*>(heap_caps_malloc(kRvBytes, MALLOC_CAP_SPIRAM));
  reverbInt = rvMem && esp_ptr_internal(rvMem);
  reverbBuf = rvMem;
  if (auto* rv = rvMem)
    synth->setReverbBuffer(rv, mt::Reverb::kBufLen);
  else
    Serial.println("audio: no PSRAM for the reverb");
#ifdef AUDIO_BENCH
  benchBegin();
#endif
#ifdef AUDIO_BENCH_FM
  benchFmBegin();
#endif
#ifdef AUDIO_BENCH_DRUM
  benchDrumBegin();
#endif
#ifdef AUDIO_BENCH_SYN
  benchSynBegin();
#endif
#ifdef AUDIO_BENCH_POOL
  benchPoolBegin();
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

void previewSlice(uint8_t instr, uint8_t slice, uint8_t root, uint32_t holdMs) {
  const uint64_t t = engine::nowUs();
  const uint32_t h = holdMs / 100 + 1;
  const Ev pg{t, mt::kPreviewTrack, 2, {0xC0, static_cast<uint8_t>(instr & 15), 0}};
  const Ev slc{t, mt::kPreviewTrack, 3, {0xF5, static_cast<uint8_t>(mt::Fx::SLC), slice}};
  const Ev on{t, mt::kPreviewTrack, 3, {0x90, static_cast<uint8_t>(root & 127), 100},
              static_cast<uint8_t>(h > 255 ? 255 : h)};
  if (!uiQ.push(pg) || !uiQ.push(slc) || !uiQ.push(on)) lostEvents.fetch_add(1, std::memory_order_relaxed);
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

void scopeRead(int16_t* out, int n) {
  if (n > kScopeLen) n = kScopeLen;
  const int at = scopeAt.load(std::memory_order_relaxed);
  for (int i = 0; i < n; ++i) out[i] = scope[(at - n + i + kScopeLen) % kScopeLen];
}

int16_t scopePeak() {
  const int pk = scopePk.exchange(0, std::memory_order_relaxed);
  return static_cast<int16_t>(pk > 32767 ? 32767 : pk);
}

void trackPeaks(float out[16]) {
  static_assert(mt::kTracks == 16, "trackPeaks");
  if (synth) synth->takeTrackPeaks(out);
  else for (int t = 0; t < mt::kTracks; ++t) out[t] = 0;
}

void setMeters(bool on) {
  if (synth) synth->setMeters(on);
}

Load takeLoad() {
  Load l;
  l.blocks = loadBlocks.exchange(0, std::memory_order_relaxed);
  l.sumUs = loadSum.exchange(0, std::memory_order_relaxed);
  l.peakUs = loadPeak.exchange(0, std::memory_order_relaxed);
  static uint32_t seenResyncs;
  const uint32_t r = resyncs.load(std::memory_order_relaxed);
  l.stalls = r - seenResyncs;
  seenResyncs = r;
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
  // DRUM: one voice per machine, retriggered every 31 blocks (16ths at 120 BPM), long DECAY.
  static mt::DrumVoice dv;
  mt::DrumParams dp;
  float dmac[mt::kFmMacros] = {110, 64, 64, 64, 64};
  for (int m = 0; m < static_cast<int>(mt::DrumMachine::Count); ++m) {
    int64_t tm = 0, tc = 0, tr = 0;
    for (int b = 0; b < 100; ++b) {
      if (b % 31 == 0) dv.trigger(b != 0);
      for (int c = 0; c < kBlock / 32; ++c) {
        int64_t t0 = esp_timer_get_time();
        mt::drumMachine(m, dmac, 60, dp);
        int64_t t1 = esp_timer_get_time();
        dv.control(dp, 32);
        int64_t t2 = esp_timer_get_time();
        dv.render(buf + c * 32, 32, 1.f);
        int64_t t3 = esp_timer_get_time();
        tm += t1 - t0; tc += t2 - t1; tr += t3 - t2;
      }
    }
    Serial.printf("micro drum %s: per block machine %u us, control %u us, render %u us\n", mt::drumMachineName(m),
                  static_cast<unsigned>(tm / 100), static_cast<unsigned>(tc / 100), static_cast<unsigned>(tr / 100));
  }
  {
    // Voice filter: set (tanf) once per 32 samples + process per sample.
    mt::Svf f;
    int64_t t0 = esp_timer_get_time();
    for (int b = 0; b < 100; ++b)
      for (int c = 0; c < kBlock / 32; ++c) {
        f.set(mt::Svf::Mode::Lp, 500.f + c * 100.f + b, 4.f);
        for (int i = 0; i < 32; ++i) buf[c * 32 + i] = f.process(buf[c * 32 + i]);
      }
    int64_t t1 = esp_timer_get_time();
    Serial.printf("micro svf: per block %u us\n", static_cast<unsigned>((t1 - t0) / 100));
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
#ifdef AUDIO_BENCH_ANY
  static int64_t lastBench;
  if (now - lastBench >= 1000000) {
    lastBench = now;
    Serial.printf("audio bench: render %u us (peak %u) / 4000 us\n",
                  static_cast<unsigned>(renderUs.load(std::memory_order_relaxed)),
                  static_cast<unsigned>(benchPeak.exchange(0, std::memory_order_relaxed)));
  }
#endif
#ifdef AUDIO_BENCH_POOL
  // Each step's measured part is profiled and goes to /diag/cpuprof.txt.
  static uint32_t poolPrinted, poolStarted;
  const uint32_t mseq = poolMeasureSeq.load(std::memory_order_acquire);
  if (mseq != poolStarted) {
    poolStarted = mseq;
    if (!profileRunning()) profileStart();
  }
  const uint32_t seq = poolSeq.load(std::memory_order_acquire);
  if (seq != poolPrinted) {
    poolPrinted = seq;
    Profile pr;
    const bool prof = profileRunning() && profileStop(pr);
    const unsigned avg = poolResAvg.load(std::memory_order_relaxed);
    char line[200];
    snprintf(line, sizeof(line),
             "pool bench %s, pool %d: heavy %u + light %u, sounding %u (guard cap min %u): avg %u us (%u%%), "
             "peak %u us / 4000 us\n",
             storage::firmwareRev(), mt::kVoices, static_cast<unsigned>(poolResHeavy.load(std::memory_order_relaxed)),
             static_cast<unsigned>(poolResLight.load(std::memory_order_relaxed)),
             static_cast<unsigned>(poolResVoices.load(std::memory_order_relaxed)),
             static_cast<unsigned>(poolResCap.load(std::memory_order_relaxed)), avg, avg / 40,
             static_cast<unsigned>(poolResPeak.load(std::memory_order_relaxed)));
    Serial.print(line);
    if (prof) storage::appendCpuProfile(line, pr);
    if (seq >= kPoolStepCount) Serial.println("pool bench: done, quiet");
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

mt::Synth* liveSynth() { return synth; }

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
