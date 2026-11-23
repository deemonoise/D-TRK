#include <algorithm>
#include <cstring>
#include <iterator>

#include "audio/audio.h"
#include "brick_runtime.h"
#include "engine/engine.h"
#include "engine_native.h"
#include "link/bank_client.h"
#include "sample_set.h"
#include "wave_peaks.h"
#include "wt_builtin.h"

namespace audio {
namespace {
using Guard = std::lock_guard<brick::ProjectMutex>;
const mt::Project& model() { return *mirrorProject(); }
bool playing() {
  auto s = engine::status();
  return s.playing || s.paused;
}
BankResult prepare() {
  if (playing()) return BankResult::Busy;
  if (!brick::bank().capacity()) return BankResult::NoBank;
  brick::resetAudio();
  return BankResult::Ok;
}
BankResult run(mt::BankJob& job, BankResult result, BankItemFn item, BankProgressFn progress, void* ctx) {
  unsigned previous = 0;
  while (result == BankResult::Running) {
    {
      Guard guard(brick::projectMutex());
      result = job.step();
    }
    const auto done = job.done(), total = job.total();
    const auto percent = total ? uint64_t(done) * 100 / total : 0;
    if (percent != previous || result != BankResult::Running) {
      previous = percent;
      if (progress) progress(done, total, ctx);
      if (item) item(job.item(), done, total, ctx);
    }
  }
  return result;
}
BankResult import(const char* path, bool wt, ImportOut& out, const uint32_t* crc, BankProgressFn cb, void* ctx) {
  Guard guard(brick::projectMutex());
  auto r = prepare();
  if (r != BankResult::Ok) return r;
  mt::BankJob job(brick::bank(), brick::disk());
  r = run(job, job.startImport(path, wt, model(), crc), nullptr, cb, ctx);
  if (r == BankResult::Ok) out = job.imported();
  return r;
}
BankResult assets(bool save, const char* name, BankSets* out, BankItemFn cb, void* ctx) {
  std::unique_lock<brick::ProjectMutex> guard(brick::projectMutex());
  if (out) *out = {};
  if (!save) {
    auto r = prepare();
    if (r != BankResult::Ok) return r;
  }
  mt::BankJob job(brick::bank(), brick::disk());
  auto r = save ? job.startSave(name, model()) : job.startSync(name, model());
  if (save) guard.unlock();
  r = run(job, r, cb, nullptr, ctx);
  if (out) {
    out->missing = job.missing();
    out->failed = job.failed();
    for (int i = 0; i < mt::kProjSamples; ++i)
      if (job.sampleMissing(i)) out->samples[i / 8] |= 1u << (i % 8);
    for (int i = 0; i < mt::kProjWavetables; ++i)
      if (job.wtMissing(i)) out->wavetables[i / 8] |= 1u << (i % 8);
  }
  return r;
}
}  // namespace

const char* bankResultText(BankResult r) {
  constexpr const char* labels[] = {
      "OK",          "STOP PLAYBACK FIRST", "NO SD CARD",      "NO SAMPLE BANK", "CANNOT OPEN FILE",
      "READ ERROR",  "NOT A WAV FILE",      "UNSUPPORTED WAV", "WAV TRUNCATED",  "BANK FULL",
      "WRITE ERROR", "NO MEMORY",           "SYNTH OFFLINE",   "BUSY",           "CANCELLED"};
  return static_cast<unsigned>(r) < std::size(labels) ? labels[static_cast<unsigned>(r)] : "?";
}
BankResult importToCache(const char* p, ImportOut& out, const uint32_t* crc, BankProgressFn cb, void* ctx) {
  return import(p, false, out, crc, cb, ctx);
}
BankResult importWtToCache(const char* p, uint32_t& crc, const uint32_t* known, BankProgressFn cb, void* ctx) {
  ImportOut out;
  auto r = import(p, true, out, known, cb, ctx);
  if (r == BankResult::Ok) crc = out.crc;
  return r;
}
BankResult syncProject(const char* n, BankSets* out, BankItemFn cb, void* ctx) {
  return assets(false, n, out, cb, ctx);
}
BankResult saveAssets(const char* n, BankSets* out, BankItemFn cb, void* ctx) {
  return assets(true, n, out, cb, ctx);
}
BankResult clearCache(int* removed) {
  Guard guard(brick::projectMutex());
  auto r = prepare();
  if (r != BankResult::Ok) return r;
  int n = mt::bankClearUnused(brick::bank(), model());
  if (removed) *removed = n;
  return BankResult::Ok;
}
BankResult compactBank(BankProgressFn cb, void* ctx) {
  Guard guard(brick::projectMutex());
  auto r = prepare();
  if (r != BankResult::Ok) return r;
  return brick::bank().compact(cb, ctx) ? BankResult::Ok : BankResult::WriteFail;
}
const BankView& bankView() {
  Guard guard(brick::projectMutex());
  static BankView v;
  v = {};
  const auto& b = brick::bank();
  const auto& p = model();
  v.ok = v.mounted = b.capacity() > 0;
  v.count = b.count();
  v.capacity = b.capacity();
  v.free = b.freeBytes();
  v.unused = mt::bankUnusedBytes(b, p);
  v.gen = b.generation();
  for (int i = 0; i < p.sampleCount; ++i)
    if (auto* e = b.entry(mt::projSampleBank(p, b, i))) {
      v.samples[i / 8] |= 1u << (i % 8);
      v.rate[i] = e->rate;
    }
  for (int i = 0; i < p.wavetableCount; ++i)
    if (mt::projWtBank(p, b, i) >= 0) v.wavetables[i / 8] |= 1u << (i % 8);
  for (int i = 0; i < mt::kWtBuiltins; ++i)
    if (b.find(mt::wtBuiltinName(i)) >= 0) v.builtins |= 1u << i;
  return v;
}
void bankViewStale() {}  // bankView() reads the local index directly.
BankResult sampleInfo(int i, SampleInfo& out) {
  Guard guard(brick::projectMutex());
  auto& b = brick::bank();
  auto* e = b.entry(mt::projSampleBank(model(), b, i));
  out = {};
  if (!e) return BankResult::OpenFail;
  out = {true, e->frames, e->rate, e->root, e->loop};
  return BankResult::Ok;
}
bool wtCached(const char* name) {
  Guard guard(brick::projectMutex());
  return mt::bankWtIndex(brick::bank(), model(), name) >= 0;
}
bool wtFrame(const char* name, int f, int8_t* out) {
  Guard guard(brick::projectMutex());
  return mt::bankWtFrame(brick::bank(), model(), name, f, out);
}
BankResult wtFrame16(const char* name, int f, int16_t* out) {
  Guard guard(brick::projectMutex());
  const auto& b = brick::bank();
  auto* p = b.data(mt::bankWtIndex(b, model(), name));
  if (!p || !out || f < 0 || f >= mt::kWtFrames) return BankResult::OpenFail;
  std::copy_n(p + f * mt::kWtFramePts, mt::kWtFrameLen, out);
  return BankResult::Ok;
}
BankResult wavePeaks(int i, uint32_t col, uint32_t span, int width, int cols, int8_t* lo, int8_t* hi) {
  Guard guard(brick::projectMutex());
  const auto& b = brick::bank();
  int at = mt::projSampleBank(model(), b, i);
  auto* e = b.entry(at);
  if (!e) return BankResult::OpenFail;
  mt::wavePeaks(b.data(at), e->frames, col, span, width, cols, lo, hi);
  return BankResult::Ok;
}
int onsets(int i, mt::Onset* out, int max, BankResult* err) {
  Guard guard(brick::projectMutex());
  const auto& b = brick::bank();
  int at = mt::projSampleBank(model(), b, i);
  auto* e = b.entry(at);
  if (err) *err = e ? BankResult::Ok : BankResult::OpenFail;
  return e ? mt::detectOnsets(b.data(at), e->frames, e->rate, out, max) : -1;
}
}  // namespace audio

namespace brick {
mt::BankResult importRecording(const char* path, mt::ImportOut& out, audio::BankProgressFn cb, void* ctx) {
  using namespace audio;
  if (!playing()) return importToCache(path, out, nullptr, cb, ctx);
  // Moving an existing bank entry during playback would invalidate sounding voices.
  // Only start a concurrent import if it fits without compacting or evicting anything.
  auto& d = disk();
  auto* f = d.openRead(path);
  if (!f) return BankResult::OpenFail;
  mt::WavInfo w;
  const auto err = mt::wavParse(*f, w);
  d.closeRead();
  if (err != mt::WavErr::Ok) return BankResult::NotWav;
  if (!bank().fits(w.frames())) return BankResult::Full;
  mt::BankJob job(bank(), d);
  const auto r = run(job, job.startImport(path, false, model()), nullptr, cb, ctx);
  if (r == BankResult::Ok) out = job.imported();
  return r;
}
}  // namespace brick
