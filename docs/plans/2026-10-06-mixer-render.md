# Mixer View and Offline Render Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** A MIXER view on the TRACK screen (8 strips of the cursor's half: fader, sends, M / S), and an offline render of the internal synth: FILE → `Render WAV...` (pattern or song to the project folder) and GRID → `Resample track / pattern` (pattern into a new project sample `RSn`).

**Architecture:** `mt::OfflineRender` (core, hardware-free) drives its own `Sequencer` over the live project with a virtual block clock and feeds the live `Synth` with `eventOffset` time-stamping exactly as `audio.cpp::drain` does; the length is analytic (pass length x passes), the sink lets only note-offs through after the end. The firmware parks the audio task (`pauseForFlash`, which resets the synth), renders on the UI core with the project locked, and writes blocks to a WAV on SD or (two passes: measure, then normalize and write) to the flash sample bank. The mixer is a view flag on `TrackScreen` reusing GRID's half rule.

**Tech Stack:** C++17, PlatformIO (`pio test -e native`, `pio run -e wt32`), Unity. Design: `docs/plans/2026-10-06-mixer-render-design.md`. Blocks 1–4 are in: `kTracks = 16`, `Instrument::rsend`, chain repeats (`chainRep[]` or whatever block 3 named it — read `model.h` first and use that name), compressor / reverb in `Synth::render`.

**Rules:** no commits (the user commits); run every task to its end without stopping for hardware checks; summarize at the end. Do not edit `lib/core/src/synth_syn*`, `wt_*` beyond what a task names.

---

### Task 1: `Sequencer::setTrackMask`

**Files:**
- Modify: `lib/core/src/sequencer.h` (public API + member), `lib/core/src/sequencer.cpp:378,385,498`
- Test: `test/test_sequencer/test_main.cpp`

**Step 1: Failing test** (use the file's existing fixture that collects a `MidiSink`'s NoteOns per channel; names below follow its style):

```cpp
void test_track_mask_silences_others() {
  Project p;
  p.tracks[0].out = TrackOut::Midi;
  p.tracks[3].out = TrackOut::Midi;
  p.patterns[0].steps[0][0].note = 60;
  p.patterns[0].steps[3][0].note = 64;
  Sequencer s(p);
  s.setTrackMask(1u << 3);
  CollectSink out;
  s.start(0, out);
  s.process(2000000, out);  // 2 s: the step has long gone out
  TEST_ASSERT_EQUAL(1, out.noteOns);
  TEST_ASSERT_EQUAL(64, out.lastNote);
}
```

**Step 2:** `pio test -e native -f test_sequencer` → FAIL (no `setTrackMask`).

**Step 3: Implement**

`sequencer.h`, public: `void setTrackMask(uint16_t m) { mask_ = m; }` with the comment
`// Tracks that may sound (bit = track), on top of mute / solo. Render-only; default all.`; private:
`uint16_t mask_ = 0xFFFF;` and `bool audible(int tr) const { return p_.trackAudible(tr) && ((mask_ >> tr) & 1); }`.
`sequencer.cpp`: the three `p_.trackAudible(tr)` → `audible(tr)`.

**Step 4:** `pio test -e native -f test_sequencer` → PASS.

---

### Task 2: WAV header round trip

**Files:**
- Test: `test/test_wav/test_main.cpp`

`wavHeader(out, frames, rate, root, crc)` already exists (`wav.h:34`, used by `exportWav`). Only a test is missing.

**Step 1:**

```cpp
void test_header_parses_back() {
  std::vector<uint8_t> f(kWavHeaderBytes);
  wavHeader(f.data(), 1000, 32000, 60, 0xDEADBEEF);
  for (int i = 0; i < 1000; ++i) { f.push_back(static_cast<uint8_t>(i)); f.push_back(static_cast<uint8_t>(i >> 8)); }
  VecSource src(f);  // the file's ByteSource over a vector
  WavInfo w;
  TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(src, w)));
  TEST_ASSERT_EQUAL(1000, w.frames());
  TEST_ASSERT_EQUAL(32000, w.rate);
  TEST_ASSERT_EQUAL(60, w.root);
  TEST_ASSERT_TRUE(w.hasCrc);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, w.crc);
}
```

**Step 2:** `pio test -e native -f test_wav` → PASS (if it fails, the header writer and parser disagree: fix `wavHeader`, not the test).

---

### Task 3: Render helpers (pure functions)

**Files:**
- Create: `lib/core/src/render.h`, `lib/core/src/render.cpp`
- Create: `test/test_render/test_main.cpp`

**Step 1: Failing tests**

```cpp
#include <unity.h>
#include "render.h"
#include "sample_set.h"
using namespace mt;
void setUp() {}
void tearDown() {}

void test_pass_us() {
  Project p;
  p.bpm = 120;
  p.patterns[2].length = 16;  // 16 x 24 ticks x 625000 / 120 = 2 000 000 us
  TEST_ASSERT_EQUAL_UINT64(2000000ull, passUs(p, 2));
  p.patterns[2].res = Resolution::Eighth;
  TEST_ASSERT_EQUAL_UINT64(4000000ull, passUs(p, 2));
}

void test_song_us_sums_chain_with_repeats() {
  Project p;
  p.bpm = 120;
  p.patterns[0].length = 16;
  p.patterns[1].length = 32;
  p.chain[0] = 0; p.chain[1] = 1; p.chain[2] = 0;
  p.chainLen = 3;
  p.chainRep[1] = 2;  // block 3's repeat field: 1 / 2 / 1
  TEST_ASSERT_EQUAL_UINT64(2000000ull + 2 * 4000000ull + 2000000ull, songUs(p));
}

void test_normalize_peak() {
  int16_t b[4] = {100, -3000, 1500, 0};
  normalizePeak(b, 4, 3000);
  TEST_ASSERT_EQUAL(-29204, b[1]);  // -1 dBFS
  TEST_ASSERT_EQUAL(0, b[3]);
  int16_t z[2] = {0, 0};
  normalizePeak(z, 2, 0);  // silence: unchanged
  TEST_ASSERT_EQUAL(0, z[0]);
}

void test_trim_tail() {
  int16_t b[400] = {0};
  b[10] = 1000;
  b[300] = 40;  // above -60 dBFS (33)
  b[350] = 20;  // below
  TEST_ASSERT_EQUAL(384, trimTail(b, 400, 128));  // 301 rounded up to a block
  int16_t s[256] = {0};
  TEST_ASSERT_EQUAL(128, trimTail(s, 256, 128));   // never less than one block
}

void test_next_resample_name() {
  Project p;
  char nm[kSampleNameMax + 1];
  nextResampleName(p, nm);
  TEST_ASSERT_EQUAL_STRING("RS1", nm);
  projSampleSet(p, "rs1", 1, 100);
  nextResampleName(p, nm);
  TEST_ASSERT_EQUAL_STRING("RS2", nm);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pass_us);
  RUN_TEST(test_song_us_sums_chain_with_repeats);
  RUN_TEST(test_normalize_peak);
  RUN_TEST(test_trim_tail);
  RUN_TEST(test_next_resample_name);
  return UNITY_END();
}
```

**Step 2:** `pio test -e native -f test_render` → FAIL (no `render.h`).

**Step 3: Implement** `render.h`:

```cpp
#pragma once
#include <stdint.h>
#include "model.h"
#include "sequencer.h"
#include "synth.h"

namespace mt {

constexpr uint32_t kRenderBlockUs = 1000000u * Synth::kBlock / kSynthRate;  // 4000
constexpr int16_t kSilence = 33;             // -60 dBFS
constexpr float kNormPeak = 0.891f * 32767;  // -1 dBFS

// One pass of pattern idx / the whole chain (with repeats) at the project's BPM, us.
uint64_t passUs(const Project& p, int idx);
uint64_t songUs(const Project& p);
// Scales so that `peak` lands at -1 dBFS; peak 0 leaves the buffer as is.
void normalizePeak(int16_t* b, uint32_t n, int16_t peak);
// Frames to keep: past the last sample above kSilence, rounded up to `block`, at least one block.
uint32_t trimTail(const int16_t* b, uint32_t n, uint32_t block);
// "RS1", "RS2", ...: the first not in p's sample list (ignoring case).
void nextResampleName(const Project& p, char out[kSampleNameMax + 1]);

struct RenderSpec {
  enum class Mode : uint8_t { Pattern, Song };
  Mode mode = Mode::Pattern;
  uint8_t pattern = 0;
  uint16_t tracksMask = 0xFFFF;
  uint32_t tailBlocks = 0;
};

// Renders the project through `synth` block by block on a virtual clock (see the design).
// The caller sets songMode / the selected pattern (Guard) and owns the synth's state (reset
// before and after).
class OfflineRender : public MidiSink {
 public:
  OfflineRender(Project& p, Synth& synth, const RenderSpec& spec);
  bool renderBlock(int16_t out[Synth::kBlock]);
  uint32_t blocksTotal() const { return bodyBlocks_ + spec_.tailBlocks; }
  uint32_t blocksDone() const { return done_; }
  int16_t peak() const { return peak_; }
  uint32_t clips() const { return clips_; }
  // MidiSink: MIDI is dropped, synth events are time-stamped into the current block.
  void send(const uint8_t*, uint8_t) override {}
  void synth(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) override;

  // Sets songMode (and the pattern) for the render, restores them in the destructor.
  struct Guard {
    Guard(Project& p, const RenderSpec& s);
    ~Guard();
    Project& p;
    bool songMode;
  };

 private:
  struct Ev { uint64_t t; uint8_t track, len, b[3]; };
  static constexpr int kFifo = 64;
  void drain();
  Project& p_;
  Synth& synth_;
  RenderSpec spec_;
  Sequencer seq_;
  uint64_t blockT_ = 0, bodyUs_ = 0;
  uint32_t bodyBlocks_ = 0, done_ = 0, clips_ = 0;
  int16_t peak_ = 0;
  Ev fifo_[kFifo];
  int fifoN_ = 0;
};

}  // namespace mt
```

`render.cpp` helpers:

```cpp
uint64_t passUs(const Project& p, int idx) {
  const Pattern& pt = p.patterns[idx < kPatterns ? idx : 0];
  const uint64_t len = pt.length < kMinSteps ? kMinSteps : (pt.length > kMaxSteps ? kMaxSteps : pt.length);
  const uint16_t bpm = p.bpm ? p.bpm : 120;
  return len * ticksPerStep(pt.res) * 625000ull / bpm;
}

uint64_t songUs(const Project& p) {
  uint64_t us = 0;
  const int n = p.chainLen > kChainMax ? kChainMax : p.chainLen;
  for (int i = 0; i < n; ++i) {
    const int rep = p.chainRep[i] ? p.chainRep[i] : 1;  // block 3's field name
    us += passUs(p, p.chain[i] < kPatterns ? p.chain[i] : kPatterns - 1) * rep;
  }
  return us;
}

void normalizePeak(int16_t* b, uint32_t n, int16_t peak) {
  if (peak <= 0) return;
  const float g = kNormPeak / peak;
  for (uint32_t i = 0; i < n; ++i) {
    const float v = b[i] * g;
    b[i] = static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
  }
}

uint32_t trimTail(const int16_t* b, uint32_t n, uint32_t block) {
  uint32_t last = 0;
  for (uint32_t i = 0; i < n; ++i)
    if (b[i] > kSilence || b[i] < -kSilence) last = i + 1;
  const uint32_t keep = (last + block - 1) / block * block;
  return keep < block ? block : (keep > n ? n : keep);
}

void nextResampleName(const Project& p, char out[kSampleNameMax + 1]) {
  for (int n = 1;; ++n) {
    snprintf(out, kSampleNameMax + 1, "RS%d", n);
    if (projSampleFind(p, out) < 0) return;
  }
}
```

**Step 4:** `pio test -e native -f test_render` → PASS.

---

### Task 4: `OfflineRender` loop

**Files:**
- Modify: `lib/core/src/render.cpp`
- Test: `test/test_render/test_main.cpp`

**Step 1: Failing tests** (CHIP saw instrument on an INT track; `Synth` needs no bank for CHIP):

```cpp
static Project* mkProj() {  // static storage: Project is 460 KB
  static Project p;
  p.reset();
  p.bpm = 120;
  p.instruments[0] = Instrument();
  p.instruments[0].wave = static_cast<uint8_t>(Wave::Saw);
  p.instruments[0].sustain = 127;
  p.tracks[0].out = TrackOut::Int;
  p.tracks[0].instr = 0;
  p.tracks[1].out = TrackOut::Int;
  p.tracks[1].instr = 0;
  p.patterns[0].length = 16;
  return &p;
}

static int firstLoudBlock(OfflineRender& r, int16_t& peak) {
  int16_t out[Synth::kBlock];
  int first = -1, n = 0;
  while (r.renderBlock(out)) {
    for (int i = 0; i < Synth::kBlock; ++i)
      if (first < 0 && (out[i] > 50 || out[i] < -50)) first = n;
    ++n;
  }
  peak = r.peak();
  return first;
}

void test_note_lands_in_its_block() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][4].note = 60;  // step 4 = 500 000 us = block 125
  RenderSpec s;
  Synth synth(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_EQUAL(500, r.blocksTotal());  // 2 s / 4 ms, no tail
  int16_t peak;
  const int first = firstLoudBlock(r, peak);
  TEST_ASSERT_INT_WITHIN(1, 125, first);
  TEST_ASSERT_TRUE(peak > 1000);
  TEST_ASSERT_EQUAL(500, r.blocksDone());
}

void test_mask_silences_other_track() {
  Project& p = *mkProj();
  p.patterns[0].steps[1][0].note = 60;
  RenderSpec s;
  s.tracksMask = 1;  // track 0 only
  Synth synth(p);
  OfflineRender::Guard g(p, s);
  OfflineRender r(p, synth, s);
  int16_t peak;
  TEST_ASSERT_EQUAL(-1, firstLoudBlock(r, peak));
  TEST_ASSERT_EQUAL(0, peak);
}

void test_song_total_and_tail() {
  Project& p = *mkProj();
  p.patterns[1].length = 32;
  p.chain[0] = 0; p.chain[1] = 1; p.chainLen = 2;
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  s.tailBlocks = 10;
  Synth synth(p);
  OfflineRender::Guard g(p, s);
  TEST_ASSERT_TRUE(p.songMode);
  OfflineRender r(p, synth, s);
  TEST_ASSERT_EQUAL(500 + 1000 + 10, r.blocksTotal());
}

void test_guard_restores_flags() {
  Project& p = *mkProj();
  p.songMode = false;
  RenderSpec s;
  s.mode = RenderSpec::Mode::Song;
  { OfflineRender::Guard g(p, s); TEST_ASSERT_TRUE(p.songMode); }
  TEST_ASSERT_FALSE(p.songMode);
}

void test_two_renders_identical() {
  Project& p = *mkProj();
  p.patterns[0].steps[0][0].note = 60;
  p.patterns[0].steps[0][8].fx[0] = {Fx::PRB, 50};
  p.patterns[0].steps[0][8].note = 67;
  RenderSpec s;
  Synth synth(p);
  OfflineRender::Guard g(p, s);
  std::vector<int16_t> a, b;
  for (std::vector<int16_t>* v : {&a, &b}) {
    synth.reset();
    OfflineRender r(p, synth, s);
    int16_t out[Synth::kBlock];
    while (r.renderBlock(out)) v->insert(v->end(), out, out + Synth::kBlock);
  }
  TEST_ASSERT_EQUAL(a.size(), b.size());
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.size());
}
```

**Step 2:** `pio test -e native -f test_render` → FAIL (link errors).

**Step 3: Implement**

```cpp
OfflineRender::Guard::Guard(Project& pr, const RenderSpec& s) : p(pr), songMode(pr.songMode) {
  p.songMode = s.mode == RenderSpec::Mode::Song;
}
OfflineRender::Guard::~Guard() { p.songMode = songMode; }

OfflineRender::OfflineRender(Project& p, Synth& synth, const RenderSpec& spec)
    : p_(p), synth_(synth), spec_(spec), seq_(p) {
  seq_.setTrackMask(spec.tracksMask);
  seq_.seed(0x5EED);
  if (spec.mode == RenderSpec::Mode::Pattern) seq_.selectPattern(spec.pattern);
  bodyUs_ = spec.mode == RenderSpec::Mode::Song ? songUs(p) : passUs(p, spec.pattern);
  bodyBlocks_ = static_cast<uint32_t>((bodyUs_ + kRenderBlockUs - 1) / kRenderBlockUs);
  seq_.start(0, *this);
}

// Same rule as audio.cpp::drain: late events at offset 0, later blocks keep theirs.
void OfflineRender::synth(uint64_t t, uint8_t track, const uint8_t* b, uint8_t len) {
  if (len == 0 || len > 3) return;
  const bool off = (b[0] & 0xF0) == 0x80 || (b[0] & 0xF0) == 0xB0 || b[0] == 0xF5;  // offs, controls, synth msgs
  if (t >= bodyUs_ && !off) return;  // the next pass: note-ons never sound
  if (fifoN_ == kFifo) drain();     // full: flush what is due (never in practice)
  if (fifoN_ == kFifo) return;
  Ev& e = fifo_[fifoN_++];
  e.t = t; e.track = track; e.len = len;
  memcpy(e.b, b, len);
}

void OfflineRender::drain() {
  int kept = 0;
  for (int i = 0; i < fifoN_; ++i) {
    const int off = eventOffset(fifo_[i].t, blockT_);
    if (off < 0) { fifo_[kept++] = fifo_[i]; continue; }  // a later block
    synth_.event(off, fifo_[i].track, fifo_[i].b, fifo_[i].len);
  }
  fifoN_ = kept;
}

bool OfflineRender::renderBlock(int16_t out[Synth::kBlock]) {
  if (done_ >= blocksTotal()) return false;
  if (done_ < bodyBlocks_) seq_.process(blockT_ + kRenderBlockUs, *this);  // scheduled a block ahead
  else if (done_ == bodyBlocks_) seq_.stop(blockT_, *this);                // body over: release everything
  drain();
  synth_.render(out);
  for (int i = 0; i < Synth::kBlock; ++i) {
    const int16_t a = out[i] < 0 ? static_cast<int16_t>(-out[i]) : out[i];
    if (a > peak_) peak_ = a;
    if (out[i] == 32767 || out[i] == -32768) ++clips_;
  }
  blockT_ += kRenderBlockUs;
  ++done_;
  return true;
}
```

Check against `audio.cpp`: the engine calls `process` up to `kLagUs` = 6 ms ahead of the block it renders; one block (4 ms) plus the sequencer's own lookahead (`kLookTicks` = 48 ticks, ≥ 125 ms) is enough — every event of block n is scheduled when `process(blockT + 4000)` returns. If `Synth` keeps per-block queued events itself (check `Synth::event` with `offset`), the FIFO still orders them by block.

Which message types count as `off` in `synth()`: read `Synth::event` (`synth.cpp`) and `kSynthStep` (`model.h:177`) — the 0xF5 step messages and 0xB0 controls must pass after the body so running locks / fx settle; note-ons (0x90 with velocity > 0) must not. Write the predicate from that reading and name it `passesAfterEnd(b)`.

**Step 4:** `pio test -e native -f test_render` → PASS. `pio test -e native` → all PASS.

---

### Task 5: Audio task: `audio::Paused` and `liveSynth()`

**Files:**
- Modify: `src/audio/audio.h`, `src/audio/audio.cpp:462` (keep the pointer), `src/audio/bank.cpp:172-180` (`FlashWork` → `audio::Paused`)

**Step 1:** `audio.h`:

```cpp
// Audio task parked (silent, synth reset, queues flushed) for the object's lifetime; ok = parked.
// While parked the synth may be driven from the UI core (offline render) and flash written.
struct Paused {
  bool ok;
  Paused() : ok(pauseForFlash()) {}
  ~Paused() { if (ok) resumeAfterFlash(); }
  Paused(const Paused&) = delete;
  Paused& operator=(const Paused&) = delete;
};
// The live synth, for an offline render while Paused. Reset it when done.
mt::Synth* liveSynth();
```

`audio.cpp`: `mt::Synth* liveSynth() { return synth; }`. `bank.cpp` `FlashWork`: keep its `flash.remap()` in the destructor, build it on `audio::Paused` (`Paused paused; bool ok = paused.ok;`) — or leave `FlashWork` and only add `Paused`; smallest diff wins.

**Step 2:** `pio run -e wt32` → compiles.

---

### Task 6: Render driver in `src/storage/render_io.h/.cpp`

**Files:**
- Create: `src/storage/render_io.h`, `src/storage/render_io.cpp`
- Modify: `src/audio/bank.h/.cpp` (export `finishImport`, `bankBeginWrite`)

**Step 1: API**

```cpp
namespace storage {
using RenderProgress = void (*)(uint32_t done, uint32_t total, void* ctx);
struct RenderStats { uint32_t frames; int16_t peak; uint32_t clips; };
// Renders spec into /projects/<p.name>/<file> (mono 16-bit 32 kHz WAV, written via .tmp).
// Playback must be stopped (caller checks). Progress by blocks; cb returns false to cancel.
Result renderWav(mt::Project& p, const mt::RenderSpec& spec, const char* file, RenderStats& st,
                 RenderProgress cb, void* ctx);
// Two-pass resample of spec into the bank + p's sample list as RSn; name out. Frames capped at
// kResampleMaxFrames (Result::Capped: added, but cut).
Result resample(mt::Project& p, const mt::RenderSpec& spec, char name[mt::kSampleNameMax + 1],
                RenderStats& st, RenderProgress cb, void* ctx);
constexpr uint32_t kResampleMaxFrames = 60 * 32000;
}
```

`renderWav`:
1. `audio::Paused paused; if (!paused.ok) return Result::AudioBusy;` (new `Result` value, text `AUDIO BUSY`).
2. `engine::lockProject()`; `mt::OfflineRender::Guard g(p, spec)`; `mt::Synth& synth = *audio::liveSynth(); synth.reset();`
3. Make `/projects/<name>` (`storage` has the folder helper used by Save — reuse it), open `<path>.tmp` for write, write `kWavHeaderBytes` zeros.
4. `OfflineRender r(p, synth, spec)`; loop `renderBlock` into a 128-sample internal-RAM buffer, `sampleCrc` running, `f.write`; short write → close, remove `.tmp`, `Result::DiskFull`; `cb(done, total)` false → remove, `Result::Cancelled`.
5. `f.seek(0)`, `wavHeader(hdr, frames, 32000, 60, crc)`, write, close, remove the old file, rename `.tmp`.
6. `synth.reset()`, unlock. `st = {frames, r.peak(), r.clips()}`.

`resample`: same setup; pass 1 renders into nothing but tracks `peak` and `lastLoud` (apply `trimTail`'s rule per block: remember the last block index with a sample above `kSilence`); stop at `kResampleMaxFrames` (flag capped). `frames = max(1, lastLoudBlock + 1) * 128`. `bankMakeRoom(bank, p, frames)` false → `Result::BankFull`. `bank.begin(kImportName, frames, 32000, 60)`; pass 2: `synth.reset()`, new `OfflineRender`, per block `normalizePeak(buf, 128, peak)` then `bank.write` + crc until `frames` written; then `bank.cpp`'s rename-to-key tail factored into `BankResult finishImport(uint32_t crc, uint32_t frames, uint32_t& outCrc)` (the code at `bank.cpp:448-470`, both `importToCache` and `resample` call it). `nextResampleName(p, name)`, `projSampleSet(p, name, crc, frames)`, `markDirty` by the caller. Progress total = `2 * blocksTotal`.

**Step 2:** `pio run -e wt32` → compiles.

---

### Task 7: FILE → `Render WAV...` dialog

**Files:**
- Create: `src/ui/render_dialog.h/.cpp` (model: `euclid_dialog.*` — `open()`, `isOpen()`, `onInput`, `onTouch`, `draw`, a `ParamList` of 2 rows + OK / CANCEL buttons)
- Modify: `src/ui/file_screen.h:27` (`kRender` after `kImport`), `src/ui/file_screen.cpp:15` (label `"Render WAV..."`), the action switch (`:85`), `onEnter` (`render_.close()`), `onInput` / `onTouch` / `draw` forwarding while open (as `import_`)

**Step 1:** Dialog rows: `Source` → `PATTERN %02d` / `SONG` (SONG only if `chainLen > 0`; turn past 16 reaches SONG), default = `app_.pattern()`; `Tracks` → `ALL` / `SOLOED` (`SOLOED` dimmed when `!p.anySolo()`). OK: if `playbackBusy()` → toast `STOP FIRST`; if `!hw::sdReady()` → `NO SD CARD`; build `RenderSpec{mode, pattern, mask, tailBlocks = 500}` (mask = solo tracks or `0xFFFF`), file = `render.wav` / `pNN.wav`; if `storage::exists`-style check finds the file → `app_.menu().open("OVERWRITE file?", {Cancel, Overwrite})`; then `storage::renderWav(...)` with `App::showProgress("RENDER", done, total)` as the callback (`ctx = App*`; cancel = `app_.cancelRequested()` — add a flag set by a long press while `progLabel_` is shown, if `App` has none). Toast: `snprintf("%u.%us  PEAK %.1fdB%s", frames/32000, (frames%32000)/3200, 20*log10f(peak/32767.f), clips ? "  CLIP" : "")`; errors via `storage::resultText`.

**Step 2:** `pio run -e wt32` → compiles.

---

### Task 8: GRID → `Resample track` / `Resample pattern`

**Files:**
- Modify: `src/ui/grid_screen.h:29-32` (`kResampleTrack, kResamplePattern`), `src/ui/grid_screen.cpp:434` (`add("Resample track", kResampleTrack, true); add("Resample pattern", kResamplePattern, true);` in the no-selection menu after `Euclid...`), `onMenu` switch (`:565`)

**Step 1:** Handler:

```cpp
void GridScreen::resample(bool wholePattern) {
  if (app_.playbackBusy()) { app_.toast("STOP FIRST"); return; }  // same test FILE uses
  const mt::Project& p = app_.project();
  mt::RenderSpec s;
  s.pattern = static_cast<uint8_t>(app_.pattern());
  s.tracksMask = 0;
  for (int t = 0; t < mt::kTracks; ++t)
    if (wholePattern ? p.trackAudible(t) : t == track()) s.tracksMask |= static_cast<uint16_t>(1u << t);
  char name[mt::kSampleNameMax + 1];
  storage::RenderStats st;
  const storage::Result r = storage::resample(app_.project(), s, name, st, App::renderProgress, &app_);
  if (r == storage::Result::Ok || r == storage::Result::Capped) {
    app_.markDirty();
    char msg[32];
    snprintf(msg, sizeof(msg), "%s %u.%us%s", name, st.frames / 32000, (st.frames % 32000) / 3200,
             r == storage::Result::Capped ? " CAP" : "");
    app_.toast(msg);
  } else app_.toast(storage::resultText(r));
}
```

(`Result::Capped` text `SAMPLE CAP`, `BankFull` → `BANK FULL` already exists in `bankResultText` — map bank results to `storage::Result` as `file_screen`'s import path does.)

**Step 2:** `pio run -e wt32` → compiles.

---

### Task 9: MIXER view

**Files:**
- Modify: `src/ui/track_screen.h` (`mixer_`, `mixSel_`, `toggleMixer()`, strip geometry, `drawMixer`, `hitStrip`), `src/ui/track_screen.cpp`
- Modify: `src/ui/app.cpp:138-146` (Shift + tap on the active TRACK tab → `track_.toggleMixer()`), `src/ui/app.h` (expose `track_` or add `App::toggleTrackMixer()`)
- Modify: `src/ui/proj_screen.cpp` (remove the `Volume` row; the mixer owns `masterVol`)

**Step 1: Geometry and helpers** (`track_screen.h`):

```cpp
  // 8 track strips of the cursor's half + the master strip: 8 + 8 x 52 + 8 + 48 = 480.
  static constexpr int kStrips = 8, kStripW = 52, kStripX0 = 8;
  static constexpr int kMasterW = 48, kMasterX = kStripX0 + kStrips * kStripW + 8;  // 432
  static constexpr int kMaster = kStrips;  // mixSel_ value for the master strip
  static constexpr int kNameY = 4, kFaderY = 24, kFaderH = 120, kFaderW = 12, kValY = 148;
  static constexpr int kSendY = 164, kBtnY = 184, kBtnW = 22, kBtnH = 16;
  int firstTrack() const { return app_.curTrack() / kStrips * kStrips; }
  void toggleMixer();
  void drawMixer(LGFX_Sprite& s, int y0);
  void drawFader(LGFX_Sprite& s, int x, int y, int value, int max, uint16_t fill);  // shared by strips and master
  // Mixer hit test: strip (0-7, kMaster) and part; false outside.
  enum class Part : uint8_t { Name, Fader, Mute, Solo };
  bool hitStrip(int x, int y, int& strip, Part& part) const;
  void setMasterVol(int v);  // clamps 0..kMasterVolMax, writes under the lock, markDirty-free (device setting)
  bool mixer_ = false;
  uint8_t mixSel_ = 0;  // 0..7 = strip of the half (follows curTrack), kMaster = master strip
```

`mixSel_` follows the cursor: `App::setCurTrack` changes → `mixSel_ = curTrack % 8` (compare in `wantsRedraw` / `onEnter`); Shift + turn on the encoder moves `mixSel_` 0..8 and, for 0..7, calls `app_.setCurTrack(firstTrack() + mixSel_)`.

**Step 2: Draw**

```cpp
void TrackScreen::drawFader(LGFX_Sprite& s, int x, int y, int value, int max, uint16_t fill) {
  s.drawRect(x, y, kFaderW, kFaderH, kDim);
  const int h = value * (kFaderH - 2) / max;
  if (h > 0) s.fillRect(x + 1, y + kFaderH - 1 - h, kFaderW - 2, h, fill);
}

void TrackScreen::drawMixer(LGFX_Sprite& s, int y0) {
  const mt::Project& p = app_.project();
  char buf[8];
  for (int k = 0; k < kStrips; ++k) {
    const int tr = firstTrack() + k;
    const mt::TrackCfg& t = p.tracks[tr];
    const bool internal = t.out == mt::TrackOut::Int;
    const int x = kStripX0 + k * kStripW;
    if (mixSel_ == k && tr == app_.curTrack()) s.drawRect(x, y0 + 1, kStripW, kAreaH - 2, kCursor);
    s.setTextColor(t.solo ? kCursor : (p.trackAudible(tr) ? kText : kDim));
    s.drawString(t.name, x + 2, y0 + kNameY);  // 8 chars x 6 px = 48 < 52
    const int fx = x + (kStripW - kFaderW) / 2, fy = y0 + kFaderY;
    if (internal) drawFader(s, fx, fy, t.vol, 127, p.trackAudible(tr) ? kText : kDim);
    else s.drawRect(fx, fy, kFaderW, kFaderH, kDim);
    if (internal) snprintf(buf, sizeof(buf), "%3u", t.vol); else snprintf(buf, sizeof(buf), "MIDI");
    s.setTextColor(internal ? kText : kDim);
    s.drawString(buf, x + 14, y0 + kValY);
    const mt::Instrument& in = p.instruments[t.instr % mt::kInstruments];
    if (internal) snprintf(buf, sizeof(buf), "S%2u R%2u", in.send * 100 / 127, in.rsend * 100 / 127);
    else snprintf(buf, sizeof(buf), "S-- R--");
    s.setTextColor(kDim);
    s.drawString(buf, x + 2, y0 + kSendY);
    const int by = y0 + kBtnY;
    s.fillRect(x + 3, by, kBtnW, kBtnH, t.mute ? kCursor : kBeatBg);
    s.fillRect(x + 3 + kBtnW + 2, by, kBtnW, kBtnH, t.solo ? kCursor : kBeatBg);
    s.setTextColor(kText);
    s.drawString("M", x + 3 + (kBtnW - 6) / 2, by + 2);
    s.drawString("S", x + 3 + kBtnW + 2 + (kBtnW - 6) / 2, by + 2);
  }
  // Master strip: masterVol 0..200 %, above 100 the fill turns kCursor.
  s.drawFastVLine(kMasterX - 4, y0, kAreaH, kDim);
  if (mixSel_ == kMaster) s.drawRect(kMasterX, y0 + 1, kMasterW, kAreaH - 2, kCursor);
  s.setTextColor(kText);
  s.drawString("MAIN", kMasterX + 12, y0 + kNameY);
  const int mv = p.masterVol;
  drawFader(s, kMasterX + (kMasterW - kFaderW) / 2, y0 + kFaderY, mv, mt::kMasterVolMax, mv > 100 ? kCursor : kText);
  snprintf(buf, sizeof(buf), "%3u%%", mv);
  s.drawString(buf, kMasterX + 12, y0 + kValY);
}
```

`draw()`: `if (mixer_) { drawMixer(s, y0); return; }` before the list / header. (Check `kAreaH` ≥ 204 in `screen.h`; shrink `kFaderH` if not.)

**Step 3: Hit test and touch**

```cpp
bool TrackScreen::hitStrip(int x, int y, int& strip, Part& part) const {
  const int ly = y - y0_;
  if (x >= kMasterX && x < kMasterX + kMasterW) {
    strip = kMaster;
    if (ly < kFaderY) part = Part::Name;
    else if (ly < kFaderY + kFaderH + 8) part = Part::Fader;
    else return false;
    return true;
  }
  if (x < kStripX0 || x >= kStripX0 + kStrips * kStripW) return false;
  strip = (x - kStripX0) / kStripW;
  const int lx = (x - kStripX0) % kStripW;
  if (ly < kFaderY) part = Part::Name;
  else if (ly < kFaderY + kFaderH + 8) part = Part::Fader;
  else if (ly >= kBtnY && ly < kBtnY + kBtnH) part = lx < 3 + kBtnW + 1 ? Part::Mute : Part::Solo;
  else return false;
  return true;
}
```

`onTouch` while `mixer_`: `hitStrip`; strip 0..7: `Name` + Tap → `app_.setCurTrack(firstTrack() + strip)`, `mixSel_ = strip`; `Mute` / `Solo` + Tap → toggle under `engine::lockProject()`, `markDirty()`; `Fader` + (Tap or Drag) on an INT track → `vol = clamp(127 - (ev.y - (y0_ + kFaderY)) * 127 / kFaderH, 0, 127)` under lock, publish the way the list's `kVol` edit does (`track_screen.cpp:40`), `markDirty()`. Strip `kMaster`: `Name` + Tap → `mixSel_ = kMaster`; `Fader` → `setMasterVol(clamp(kMasterVolMax - (ev.y - (y0_ + kFaderY)) * kMasterVolMax / kFaderH, 0, kMasterVolMax))`, `mixSel_ = kMaster`. Long press → the context menu (`Settings` / `Mixer`, plus nothing else).

`setMasterVol(v)`: `p.masterVol = v` under the lock; it is a device setting (`App::saveVolumeIdle` writes it to NVS a second after it stops changing — verify that it polls `p_->masterVol` and does not depend on the PROJ screen; the dirty flag is not set, as PROJ did not set it).

`onInput` while `mixer_`: turn → `mixSel_ < kMaster`: vol ± (shift ? 10 : 1) on the current track if INT; `mixSel_ == kMaster`: `setMasterVol(masterVol ± (shift ? 10 : 1))`; Shift + turn → `mixSel_ = clamp(mixSel_ + d, 0, kMaster)`, and for 0..7 `app_.setCurTrack(firstTrack() + mixSel_)` (crossing the half: when `mixSel_` would pass 0 or 7 on Shift + turn, keep the list behaviour of moving the track so the half follows: `app_.setCurTrack(curTrack ± 1)`, then `mixSel_ = curTrack % 8`); click → mute toggle (nothing on the master); Shift + click → `toggleMixer()`; long press → menu.

`wantsRedraw(st)`: in mixer mode compare a signature `hash(curTrack, mixSel_, masterVol, mute/solo bits, vol[firstTrack..+8])`; redraw when it changes.

`App::onTouch` (`app.cpp:138-146`): `if (shift_ && static_cast<Tab>(i) == Tab::Track && tab_ == Tab::Track) { track_.toggleMixer(); return; }` before `setTab`.

**Step 4: PROJ loses Volume** — `proj_screen.cpp`: remove the `Volume` row and its handler; check `App::saveVolumeIdle` / `storage::saveVolume` still run from the main loop (they poll `p_->masterVol`, independent of the screen). Update the manual pointer in Task 10.

**Step 5:** `pio run -e wt32` → compiles.

---

### Task 9b: Hold a track button + turn = track volume

**Files:**
- Modify: `src/ui/app.h` (`int8_t heldTrackBtn_ = -1; bool heldTurned_ = false;`), `src/ui/app.cpp` (input dispatch where `trackKey` is called, `app.cpp:94`, and the encoder-turn dispatch above it)
- Requires: block 3's `hw::InputEvent` kinds `TrackPress` / `TrackRelease` (`trackio.cpp`, song-live plan Task 8). If only the press event exists, add the release event first exactly as that task describes.

**Step 1: State**

In the input dispatch (`App::onInput`):

```cpp
  if (ev.type == hw::InputType::TrackPress) {
    heldTrackBtn_ = static_cast<int8_t>(ev.delta);
    heldTurned_ = false;
    if (!menu_.isOpen()) trackKey(ev.delta, ev.shift);  // select / mute / note entry as today
    return;
  }
  if (ev.type == hw::InputType::TrackRelease) {
    if (ev.delta == heldTrackBtn_) heldTrackBtn_ = -1;
    return;
  }
  if (ev.type == hw::InputType::Turn && heldTrackBtn_ >= 0 && holdVolumeAllowed()) {
    heldTurned_ = true;
    nudgeTrackVol((curTrack_ / 8) * 8 + heldTrackBtn_, ev.delta * (ev.shift ? 10 : 1));
    return;  // consumed: the screen does not see the turn
  }
```

`holdVolumeAllowed()`: `!(tab_ == Tab::Grid && grid_.editing())` and not PERF (`!grid_.perf()` from block 3; if absent, omit). Add `bool editing() const { return edit_; }` to `GridScreen` if missing.

**Step 2: The edit**

```cpp
void App::nudgeTrackVol(int track, int d) {
  mt::TrackCfg& t = p_->tracks[track];
  char msg[20];
  if (t.out != mt::TrackOut::Int) {
    snprintf(msg, sizeof(msg), "TRK%d MIDI", track + 1);
    toast(msg);
    return;
  }
  const int v = clampi(t.vol + d, 0, 127);
  if (v == t.vol) return;
  engine::lockProject();
  t.vol = static_cast<uint8_t>(v);
  engine::unlockProject();
  // publish like track_screen.cpp:40 does for kVol (engine::post(...) if the list posts a command)
  markDirty();
  snprintf(msg, sizeof(msg), "TRK%d VOL %d", track + 1, v);
  toast(msg);
}
```

If the mixer view is open it redraws through its signature (vol changed). The TRACK list redraws its `Vol` row the way it does for other external changes (check `TrackScreen::wantsRedraw`; add `vol` of the current track to its signature if it only watches the status).

**Step 3:** `pio run -e wt32` → compiles. Manual check later: press-and-release still selects / mutes; hold + turn changes the volume, cursor does not move.

---

### Task 10: Docs and final check

**Files:** `README.md` (Звук: рендер и ресэмплинг; TRACK/MIXER), `docs/manual.html` (TRACK → MIXER view; FILE → Render WAV; GRID menu Resample; errors)

**Step 1:** README, раздел «Звук»: FILE → Render WAV… (паттерн / песня, ALL / SOLOED, файл в папке проекта, 2 с хвоста, мастер-эффекты включены); GRID → Resample track / pattern (RSn в сэмплах проекта, −1 dBFS, обрезка тишины, кап 60 с); MIXER на TRACK (Shift+клик энкодера или Shift+тап по вкладке TRACK; 8 полос половины курсора + полоса MAIN справа — мастер-громкость 0–200 %, переехала из PROJ; тач по фейдеру, M / S); громкость дорожки с любого экрана — удерживать кнопку дорожки и крутить энкодер (Shift ×10), не в правке GRID. README «Звук»: «Громкость по умолчанию 40 % (PROJ → Volume …)» → «(TRACK → MIXER → MAIN …)».

**Step 2:** manual: the same three sections, with the toasts (`STOP FIRST`, `BANK FULL`, `SAMPLE CAP`, `DISK FULL`, `AUDIO BUSY`) and the note that a render equals playback minus the 14 ms output latency and that notes nudged past the pattern end are cut.

**Step 3:** `pio test -e native` → all PASS; `pio run -e wt32` → compiles. Summarize: tests added, the `Sequencer` size printed in the boot log (if flashed), any `Result` values added.
