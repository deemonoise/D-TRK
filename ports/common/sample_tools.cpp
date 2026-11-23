#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio.h"
#include "brick_runtime.h"
#include "engine/engine.h"
#include "engine_native.h"
#include "link/bank_client.h"
#include "render.h"
#include "sample_extract.h"
#include "sample_set.h"
#include "time_stretch.h"

namespace audio {
namespace {
using Guard = std::lock_guard<brick::ProjectMutex>;
using Progress = bool (*)(uint32_t, uint32_t, void*);
struct Region final : mt::TimeStretchReader {
  const int16_t* data = nullptr;
  uint32_t len = 0, rate = 0;
  bool set(int index, uint32_t crc, uint32_t from, uint32_t to) {
    const auto& m = *mirrorProject();
    auto& b = brick::bank();
    int i = mt::projSampleBank(m, b, index);
    const auto* entry = b.entry(i);
    if (!entry || m.samples[index].crc != crc || from >= to || to > entry->frames) return false;
    data = b.data(i) + from;
    len = to - from;
    rate = entry->rate;
    return true;
  }
  bool read(uint32_t at, uint32_t n, int16_t* out) override {
    if (at > len || n > len - at) return false;
    std::copy_n(data + at, n, out);
    return true;
  }
};
bool copyPcm(int16_t* dst, uint32_t at, uint32_t n, void* data) {
  std::copy_n(static_cast<int16_t*>(data) + at, n, dst);
  return true;
}
BankResult edit(int index, uint32_t crc, uint32_t from, uint32_t to, bool norm, uint32_t ratio, ImportOut& out,
                Progress cb, void* ctx) {
  Guard guard(brick::projectMutex());
  auto status = engine::status();
  if (status.playing || status.paused) return BankResult::Busy;
  Region src;
  if (!src.set(index, crc, from, to)) return BankResult::ReadFail;
  const uint32_t len = mt::sampleEditFrames(src.len, ratio);
  if (!len || uint64_t(len) * 2 > brick::bank().capacity()) return BankResult::Full;
  std::unique_ptr<int16_t[]> pcm(new (std::nothrow) int16_t[len]);
  if (!pcm) return BankResult::NoMemory;
  mt::TimeStretch stretch;
  auto s = stretch.start(src.len, src.rate, len);
  int32_t peak = 0;
  uint32_t done = 0;
  while (s == mt::TimeStretchResult::Running) {
    if (cb && !cb(done, len, ctx)) return BankResult::Cancelled;
    uint32_t n = 0;
    s = stretch.step(src, pcm.get() + done, len - done, n);
    if (norm)
      for (uint32_t i = 0; i < n; ++i) peak = std::max(peak, std::abs(int(pcm[done + i])));
    done += n;
  }
  if (s != mt::TimeStretchResult::Done || done != len) return BankResult::ReadFail;
  if (norm && peak) {
    constexpr int target = static_cast<int>(mt::kNormPeak + 0.5f);
    for (uint32_t i = 0; i < len; ++i) {
      int v = pcm[i];
      int mag = (std::abs(v) * target + peak / 2) / peak;
      pcm[i] = v < 0 ? -mag : mag;
    }
  }
  if (cb && !cb(len, len, ctx)) return BankResult::Cancelled;
  brick::resetAudio();
  return mt::bankWriteFrames(brick::bank(), *mirrorProject(), len, src.rate, copyPcm, pcm.get(), out);
}
}  // namespace

BankResult extractSample(int i, uint32_t crc, uint32_t from, uint32_t to, bool norm, ImportOut& o, Progress cb,
                         void* c) {
  return edit(i, crc, from, to, norm, 65536, o, cb, c);
}
BankResult stretchSample(int i, uint32_t crc, uint32_t from, uint32_t to, uint32_t ratio, ImportOut& o,
                         Progress cb, void* c) {
  return edit(i, crc, from, to, false, ratio, o, cb, c);
}
BankResult detectSampleTempo(int i, uint32_t crc, uint32_t from, uint32_t to, mt::TempoEstimate& out, Progress cb,
                             void* ctx) {
  Guard guard(brick::projectMutex());
  Region src;
  if (!src.set(i, crc, from, to)) return BankResult::ReadFail;
  mt::OnsetScan scan(src.len, src.rate, mt::kMaxOnsets);
  constexpr uint32_t chunk = 16384;
  for (uint32_t at = 0; at < src.len; at += chunk) {
    if (cb && !cb(at, src.len, ctx)) return BankResult::Cancelled;
    scan.feed(src.data + at, std::min(src.len - at, chunk));
  }
  mt::Onset found[mt::kMaxOnsets];
  const int count = scan.finish(found);
  out = mt::estimateTempo(found, count, src.rate, src.len);
  return BankResult::Ok;
}
BankResult wtRender(int i, uint32_t from, uint32_t to, const mt::WtMakeParams& params, const char* path,
                    mt::WtMakeErr& error, int& pitched, BankProgressFn cb, void* ctx) {
  Guard guard(brick::projectMutex());
  const auto& m = *mirrorProject();
  Region src;
  if (i < 0 || i >= m.sampleCount || !src.set(i, m.samples[i].crc, from, to)) return BankResult::ReadFail;
  mt::WtMaker maker;
  error = maker.begin(params, 0, src.len, src.rate);
  while (!maker.done()) {
    uint32_t at, n;
    if (maker.nextRead(at, n))
      maker.feed(src.data + at, n);
    else
      maker.step();
    if (cb) cb(maker.progress(), mt::kWtFrames, ctx);
  }
  pitched = maker.pitched();
  error = maker.error();
  if (error != mt::WtMakeErr::Ok) return BankResult::Ok;
  auto& d = brick::disk();
  std::string tmp = std::string(path) + ".tmp";
  auto* f = d.openWrite(tmp.c_str());
  if (!f) return BankResult::OpenFail;
  bool ok = mt::wtWavWrite(maker.result(), *f);
  if (!d.closeWrite() || !ok || !d.rename(tmp.c_str(), path)) {
    d.remove(tmp.c_str());
    return BankResult::WriteFail;
  }
  return BankResult::Ok;
}
}  // namespace audio
