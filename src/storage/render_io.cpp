#include "render_io.h"
#include <Arduino.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include "audio/audio.h"
#include "audio/bank.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "hw/sdcard.h"
#include "sample_set.h"
#include "wav.h"

namespace storage {
namespace {

constexpr int kBlock = mt::Synth::kBlock;

bool stopped() {
  const engine::Status s = engine::status();
  return !s.playing && !s.paused;
}

// The render object (a Sequencer inside, ~40 KB) lives in PSRAM for the render only.
struct RenderMem {
  void* mem = heap_caps_malloc(sizeof(mt::OfflineRender), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  mt::OfflineRender* r = nullptr;
  ~RenderMem() {
    end();
    heap_caps_free(mem);
  }
  mt::OfflineRender* start(mt::Project& p, mt::Synth& synth, const mt::RenderSpec& spec) {
    end();
    synth.reset();
    r = new (mem) mt::OfflineRender(p, synth, spec);
    return r;
  }
  void end() {
    if (r) r->~OfflineRender();
    r = nullptr;
  }
};

// Audio parked, project locked, songMode / mutes set for the render; undone in reverse order.
struct Session {
  audio::Paused paused;
  mt::Synth* synth = nullptr;
  bool locked = false;
  explicit Session() {
    if (!paused.ok) return;
    synth = audio::liveSynth();
    engine::lockProject();
    locked = true;
  }
  ~Session() {
    if (synth) synth->reset();
    if (locked) engine::unlockProject();
  }
};

}  // namespace

void renderPath(const mt::Project& p, const mt::RenderSpec& spec, char* out, int n, int stem) {
  char tr[8] = "";
  if (stem >= 0) snprintf(tr, sizeof(tr), "_T%02d", stem + 1);
  if (spec.mode == mt::RenderSpec::Mode::Song) snprintf(out, n, "%s/%s_SONG%s.wav", kRenderDir, p.name, tr);
  else snprintf(out, n, "%s/%s_P%02u%s.wav", kRenderDir, p.name, spec.pattern + 1u, tr);
}

Result renderWav(mt::Project& p, const mt::RenderSpec& spec, const char* path, RenderStats& st, RenderProgress cb,
                 void* ctx) {
  st = {0, 0, 0};
  if (!hw::sdReady()) return Result::NoSd;
  if (!stopped()) return Result::EngineBusy;
  fs::FS& fs = hw::sdFs();
  if (!fs.exists(kRenderDir) && !fs.mkdir(kRenderDir)) return Result::WriteFail;
  // Blocks are gathered in internal RAM and written 16 at a time.
  constexpr int kGroup = 16;
  int16_t* buf = static_cast<int16_t*>(heap_caps_malloc(kGroup * kBlock * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  RenderMem rm;
  if (!buf || !rm.mem) {
    heap_caps_free(buf);
    return Result::NoMemory;
  }
  char tmp[96];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  if (fs.exists(tmp)) fs.remove(tmp);
  Result r = Result::Ok;
  uint32_t crc = 0, frames = 0;
  {
    Session ses;
    if (!ses.synth) {
      heap_caps_free(buf);
      return Result::AudioBusy;
    }
    mt::OfflineRender::Guard guard(p, spec);
    fs::File f = fs.open(tmp, FILE_WRITE);
    if (!f) r = Result::WriteFail;
    uint8_t hdr[mt::kWavHeaderBytes] = {0};
    if (r == Result::Ok && f.write(hdr, sizeof(hdr)) != sizeof(hdr)) r = Result::DiskFull;
    mt::OfflineRender* ren = r == Result::Ok ? rm.start(p, *ses.synth, spec) : nullptr;
    while (r == Result::Ok) {
      int n = 0;
      while (n < kGroup && ren->renderBlock(buf + n * kBlock)) ++n;
      if (n == 0) break;
      const size_t bytes = static_cast<size_t>(n) * kBlock * 2;
      crc = mt::sampleCrc(buf, static_cast<uint32_t>(n) * kBlock, crc);
      frames += static_cast<uint32_t>(n) * kBlock;
      if (f.write(reinterpret_cast<const uint8_t*>(buf), bytes) != bytes) r = Result::DiskFull;
      else if (cb && !cb(ren->blocksDone(), ren->blocksTotal(), ctx)) r = Result::Cancelled;
    }
    if (r == Result::Ok) {
      mt::wavHeader(hdr, frames, mt::kSynthRate, 60, crc);
      if (!f.seek(0) || f.write(hdr, sizeof(hdr)) != sizeof(hdr)) r = Result::DiskFull;
    }
    if (ren) st = {frames, ren->peak(), ren->clips()};
    if (f) f.close();
    rm.end();
  }
  heap_caps_free(buf);
  if (r == Result::Ok) {
    if (fs.exists(path)) fs.remove(path);
    if (!fs.rename(tmp, path)) r = Result::WriteFail;
  }
  if (r != Result::Ok && fs.exists(tmp)) fs.remove(tmp);
  return r;
}

namespace {

// Pass 2 of a resample: the bank asks for consecutive pieces, served from rendered blocks.
struct ResampleFill {
  mt::OfflineRender* ren;
  int16_t peak;
  int16_t block[kBlock];
  int at = kBlock;  // next sample of block to hand out
  uint32_t base = 0;  // progress: blocks of pass 1
  RenderProgress cb;
  void* ctx;
  bool cancelled = false;
};

bool fillResample(int16_t* out, uint32_t, uint32_t n, void* c) {
  ResampleFill& f = *static_cast<ResampleFill*>(c);
  for (uint32_t i = 0; i < n; ++i) {
    if (f.at == kBlock) {
      if (!f.ren->renderBlock(f.block)) memset(f.block, 0, sizeof(f.block));  // not reached: frames <= total
      mt::normalizePeak(f.block, kBlock, f.peak);
      f.at = 0;
      if (f.cb && !f.cb(f.base + f.ren->blocksDone(), 2 * f.base, f.ctx)) {
        f.cancelled = true;
        return false;
      }
    }
    out[i] = f.block[f.at++];
  }
  return true;
}

Result fromBank(audio::BankResult b) {
  switch (b) {
    case audio::BankResult::Ok: return Result::Ok;
    case audio::BankResult::Full: return Result::BankFull;
    case audio::BankResult::NoBank: return Result::NoBank;
    case audio::BankResult::Busy: return Result::EngineBusy;
    case audio::BankResult::NoMemory: return Result::NoMemory;
    default: return Result::WriteFail;
  }
}

}  // namespace

Result resample(mt::Project& p, const mt::RenderSpec& spec, char name[mt::kSampleNameMax + 1], RenderStats& st,
                RenderProgress cb, void* ctx) {
  st = {0, 0, 0};
  name[0] = 0;
  if (!audio::bankMounted()) return Result::NoBank;
  if (!stopped()) return Result::EngineBusy;
  if (p.sampleCount >= mt::kProjSamples) return Result::BankFull;
  RenderMem rm;
  if (!rm.mem) return Result::NoMemory;
  Session ses;
  if (!ses.synth) return Result::AudioBusy;
  mt::OfflineRender::Guard guard(p, spec);
  // Pass 1: peak and the last block above -60 dBFS.
  mt::OfflineRender* ren = rm.start(p, *ses.synth, spec);
  const uint32_t total = ren->blocksTotal();
  const uint32_t maxBlocks = kResampleMaxFrames / kBlock;
  int16_t block[kBlock];
  uint32_t n = 0, lastLoud = 0;
  bool capped = false;
  while (ren->renderBlock(block)) {
    for (int16_t x : block)
      if (x > mt::kSilence || x < -mt::kSilence) lastLoud = n + 1;
    ++n;
    if (cb && !cb(n, 2 * total, ctx)) return Result::Cancelled;
    if (n >= maxBlocks && ren->blocksDone() < total) {
      capped = true;
      break;
    }
  }
  const int16_t peak = ren->peak();
  const uint32_t frames = (lastLoud ? lastLoud : 1) * kBlock;
  // Pass 2: the same render, normalized, into the bank.
  ResampleFill fill{rm.start(p, *ses.synth, spec), peak, {}, kBlock, total, cb, ctx};
  audio::ImportOut out{};
  const audio::BankResult br = audio::cacheWrite(p, frames, fillResample, &fill, out);
  rm.end();
  if (fill.cancelled) return Result::Cancelled;
  if (br != audio::BankResult::Ok) return fromBank(br);
  mt::nextResampleName(p, name);
  if (mt::projSampleSet(p, name, out.crc, out.frames) < 0) {
    name[0] = 0;
    return Result::BankFull;  // the list is full (checked above) or the name is bad (never)
  }
  st = {out.frames, peak, 0};
  return capped ? Result::Capped : Result::Ok;
}

}  // namespace storage
