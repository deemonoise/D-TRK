#include "audio/audio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iterator>
#include <memory>

#include "brick_runtime.h"
#include "engine/engine.h"
#include "engine_native.h"
#include "link/bank_client.h"
#include "link_msg.h"
#include "practice.h"
#include "rec.h"
#include "synth.h"

namespace {
using Guard = std::lock_guard<brick::ProjectMutex>;
using mt::Synth;
constexpr int N = Synth::kBlock;
constexpr int perfSec = 6;
constexpr int voiceBudget = 1920;  // A53 prices in voice_cost.cpp
std::unique_ptr<Synth> dsp;
mt::Project* project;
mt::RecCapture* capture;
std::mutex captureMutex;  // Never held during file I/O, independent of the project and DSP lock.
std::atomic<bool> offlineOutput{false};
std::atomic<uint32_t> missedOutput{0};
mt::MetronomeVoice click;
int16_t delayLine[2 * mt::kSynthRate], perfLine[2 * perfSec * mt::kSynthRate];
float reverbLine[mt::Reverb::kBufLen], shimmerWet[N];
int16_t shimmerLine[mt::PitchShift::kLen], modLine[mt::ModFx::kLineLen];
int16_t cached[N * 2], scope[audio::kScopeLen];
uint64_t frame, blockUs, previewEnd;
int unread, previewNote, scopeAt, outputPeak;
int phones = 100;
bool meters, monitor;
constexpr unsigned monitorFrames = 4096;
int16_t monitorRing[2 * monitorFrames]{};
unsigned monitorRead, monitorWrite;
void mixMonitor(int16_t* left, int16_t* right) {
  for (int i = 0; i < N && monitorRead != monitorWrite; ++i, ++monitorRead) {
    const auto at = 2 * (monitorRead % monitorFrames);
    left[i] = std::clamp(int(left[i]) + monitorRing[at], -32768, 32767);
    right[i] = std::clamp(int(right[i]) + monitorRing[at + 1], -32768, 32767);
  }
}
uint32_t lost, parks, profPeak;
audio::Load load{};
uint32_t microClock() {
  using namespace std::chrono;
  return static_cast<uint32_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}
uint64_t frameTime(uint64_t f) { return f * 1000000 / mt::kSynthRate; }

void previewEvent(uint8_t n, uint8_t kind, uint8_t a, uint8_t b, int off = 0) {
  uint8_t msg[] = {kind, a, b};
  if (!dsp->event(off, mt::kPreviewTrack, msg, n)) ++lost;
}
void startPreview(uint8_t instr, uint8_t tr, uint8_t note, int slice, unsigned ms) {
  Guard guard(brick::projectMutex());
  if (!dsp) return;
  audio::previewStop();
  if (previewEnd) previewEvent(3, 0x80, previewNote, 0);
  previewEvent(2, 0xF7, tr, 0);
  previewEvent(2, 0xC0, instr, 0);
  if (slice >= 0) previewEvent(3, 0xF5, static_cast<uint8_t>(mt::Fx::SLC), slice);
  previewEvent(3, 0x90, note, 100);
  previewNote = note;
  previewEnd = frame + static_cast<uint64_t>(ms ? ms : 300) * mt::kSynthRate / 1000;
}
void renderBlock() {
  const uint32_t start = microClock();
  blockUs = frameTime(frame);
  brick::advance(frameTime(frame + N - 1));
  if (previewEnd && previewEnd < frame + N) {
    previewEvent(3, 0x80, previewNote, 0, previewEnd > frame ? previewEnd - frame : 0);
    previewEnd = 0;
  }
  int16_t l[N], r[N];
  dsp->render(l, r);
  {
    std::lock_guard<std::mutex> guard(captureMutex);
    if (capture && capture->source() == mt::RecSource::Resample) capture->feed(l, r, N);
    if (monitor) mixMonitor(l, r);
  }
  click.mix(l, r, N);
  brick::mixPreview(l, r, N);
  for (int i = 0; i < N; ++i) {
    if (meters) {
      scope[scopeAt] = (int(l[i]) + r[i]) / 2;
      scopeAt = (scopeAt + 1) % audio::kScopeLen;
      outputPeak = std::max({outputPeak, std::abs(int(l[i])), std::abs(int(r[i]))});
    }
    cached[2 * i] = int(l[i]) * phones * phones / 10000;
    cached[2 * i + 1] = int(r[i]) * phones * phones / 10000;
  }
  unread = N;
  frame += N;
  blockUs = frameTime(frame);  // UI notes arriving between callbacks start at offset zero.
  const auto elapsed = microClock() - start;
  load.sumUs += elapsed;
  ++load.blocks;
  load.peakUs = std::max(load.peakUs, elapsed);
  if (elapsed > frameTime(N)) ++load.stalls;
  if (dsp->profiling()) profPeak = std::max(profPeak, elapsed);
  dsp->setLoad(static_cast<float>(elapsed) / frameTime(N));
}
}  // namespace

namespace brick {
mt::Synth& synth() { return *dsp; }
void setCapture(mt::RecCapture* c) {
  std::lock_guard<std::mutex> guard(captureMutex);
  capture = c;
}
void monitorInput(bool on) {
  std::lock_guard<std::mutex> guard(captureMutex);
  if (on != monitor) monitorRead = monitorWrite = 0;
  monitor = on;
}
void captureInput(const int16_t* in, int frames) {
  std::lock_guard<std::mutex> guard(captureMutex);
  if (!capture || capture->source() == mt::RecSource::Resample) return;
  while (frames > 0) {
    int16_t l[N], r[N];
    int n = std::min(frames, N);
    for (int i = 0; i < n; ++i) {
      l[i] = in[2 * i];
      r[i] = capture->source() == mt::RecSource::Mic ? l[i] : in[2 * i + 1];
    }
    capture->feed(l, r, n);
    if (monitor)
      for (int i = 0; i < n; ++i) {
        if (monitorWrite - monitorRead == monitorFrames) ++monitorRead;
        const auto at = 2 * (monitorWrite++ % monitorFrames);
        monitorRing[at] = l[i];
        monitorRing[at + 1] = r[i];
      }
    frames -= n;
    in += 2 * n;
  }
}
void render(int16_t* out, int count) {
  // Fast edits may briefly own the project. A save/import/export can own it much longer
  // than an audio period: hand SDL a complete buffer even while the DSP is unavailable.
  std::unique_lock<ProjectMutex> guard(projectMutex(), std::defer_lock);
  if (!offlineOutput.load() && !guard.try_lock_for(std::chrono::microseconds(250)) && !offlineOutput.load())
    ++missedOutput;
  if (!guard.owns_lock() || offlineOutput.load() || !dsp) {
    std::fill_n(out, 2 * count, 0);
    return;
  }
  while (count > 0) {
    if (!unread) renderBlock();
    int n = std::min(unread, count);
    std::memcpy(out, cached + 2 * (N - unread), 2 * n * sizeof(int16_t));
    out += 2 * n;
    unread -= n;
    count -= n;
  }
}
void resetAudio() {
  Guard guard(projectMutex());
  if (dsp) dsp->reset();
  unread = 0;
  previewEnd = 0;
  click.reset();
  ++parks;
}
void parkForRender(bool parked) {
  Guard guard(projectMutex());
  offlineOutput.store(parked);
  resetAudio();  // clear cached frames, pending previews and click as well as the synth
}
}  // namespace brick

namespace audio {
void begin(mt::Project* p) {
  Guard guard(brick::projectMutex());
  dsp = std::make_unique<Synth>(*p);
  dsp->setVoiceBudget(voiceBudget);
  project = p;
  frame = blockUs = unread = previewEnd = 0;
  dsp->setDelayBuffer(delayLine, std::size(delayLine));
  dsp->setReverbBuffer(reverbLine, std::size(reverbLine));
  dsp->setShimmerBuffer(shimmerLine, std::size(shimmerLine), shimmerWet);
  dsp->setModBuffer(modLine, std::size(modLine));
  dsp->setPerfBuffer(perfLine, perfSec * mt::kSynthRate);
  brick::beginBank(*p);
}
bool synthUp() { return dsp != nullptr; }
bool synced() { return synthUp(); }
bool versionOk() { return synthUp(); }
const char* synthFw() { return "Brick native"; }
uint16_t synthProtocol() { return mt::link::kProtocol; }
const mt::Project* mirrorProject() { return project; }
void pump() {}
void pollLog() {}
LinkCounters linkCounters() {
  Guard guard(brick::projectMutex());
  return {lost, 0, 0, 0, 0};
}
void post(uint64_t t, uint8_t tr, const uint8_t* b, uint8_t n) {
  Guard guard(brick::projectMutex());
  if (!dsp || !n) return;
  const int off =
      t <= blockUs ? 0 : std::min<uint64_t>(N - 1, ((t - blockUs) * mt::kSynthRate + 500000) / 1000000);
  if (b[0] == mt::kMetronomeMsg && n == 3)
    click.trigger(off, b[1], b[2]);
  else if (!dsp->event(off, tr, b, n))
    ++lost;
}
void preview(uint8_t instr, uint8_t note, uint8_t tr) { startPreview(instr, tr, note, -1, 300); }
void previewSlice(uint8_t instr, uint8_t slice, uint8_t root, uint32_t hold) {
  startPreview(instr, mt::kTracks, root, slice, std::min<uint32_t>(hold, 25000));
}
bool perf(uint8_t op, uint8_t val) {
  Guard guard(brick::projectMutex());
  uint8_t msg[] = {0xF6, op, val};
  return dsp && dsp->event(0, mt::kPreviewTrack, msg, 3);
}
uint32_t parkCount() {
  Guard guard(brick::projectMutex());
  return parks;
}
uint32_t perfBuffer(bool on) { return on ? perfSec : 0; }
void setPhones(uint8_t pct) {
  Guard guard(brick::projectMutex());
  phones = std::min<int>(100, pct);
}
void setMeters(bool on) {
  Guard guard(brick::projectMutex());
  meters = on;
  if (dsp) dsp->setMeters(on);
}
void scopeRead(int16_t* out, int n) {
  Guard guard(brick::projectMutex());
  n = std::clamp(n, 0, kScopeLen);
  for (int i = 0; i < n; ++i) out[i] = scope[(scopeAt + kScopeLen - n + i) % kScopeLen];
}
int16_t scopePeak() {
  Guard guard(brick::projectMutex());
  int n = outputPeak;
  outputPeak = 0;
  return std::min(n, 32767);
}
void trackPeaks(float out[16]) {
  Guard guard(brick::projectMutex());
  dsp->takeTrackPeaks(out);
}
float gatePos() {
  Guard guard(brick::projectMutex());
  return dsp->gatePos();
}
float followerLevel(int t) {
  Guard guard(brick::projectMutex());
  return dsp->followerLevel(t);
}
Load takeLoad() {
  Guard guard(brick::projectMutex());
  auto v = load;
  v.underruns += missedOutput.exchange(0);
  load = {};
  return v;
}
StreamInfo streamInfo() { return {}; }  // Samples reside in mapped host memory.
void profileStart() {
  Guard guard(brick::projectMutex());
  profPeak = 0;
  dsp->setProfiler(microClock);
}
bool profileRunning() {
  Guard guard(brick::projectMutex());
  return dsp->profilerArmed();
}
bool profileStop(Profile& out) {
  Guard guard(brick::projectMutex());
  uint32_t times[kProfStages]{}, counts[kProfStages]{};
  static_assert(int(kProfStages) == int(Synth::kProfStages));
  dsp->takeProfile(times, counts, out.blocks);
  dsp->setProfiler(nullptr);
  if (!out.blocks) return false;
  for (int i = 0; i < kProfStages; ++i) {
    out.us[i] = static_cast<float>(times[i]) / out.blocks;
    out.n[i] = static_cast<float>(counts[i]) / out.blocks;
  }
  out.peakUs = profPeak;
  return true;
}
}  // namespace audio
