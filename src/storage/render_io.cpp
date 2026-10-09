#include "render_io.h"
#include <Arduino.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include "audio/audio.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "hw/sdcard.h"
#include "link/bank_client.h"
#include "link/link.h"
#include "link_msg.h"
#include "render_link.h"
#include "sample_set.h"

using namespace mt::link;

namespace storage {
namespace {

constexpr int kBlock = mt::Synth::kBlock;
constexpr uint32_t kSyncWaitMs = 2000;
constexpr uint32_t kStartMs = 1500;   // opening the file on the board's card
constexpr uint32_t kBlocksMs = 1500;  // up to 8 blocks rendered and written
constexpr uint32_t kProgressMs = 50;

bool stopped() {
  const engine::Status s = engine::status();
  return !s.playing && !s.paused;
}

// The synth board has the current sound state (it renders with its mirror of it).
bool waitSynced() {
  const uint32_t t0 = millis();
  for (;;) {
    audio::pump();
    if (audio::synced()) return true;
    if ((audio::synthUp() && !audio::versionOk()) || millis() - t0 >= kSyncWaitMs) return false;
    delay(5);
  }
}

// The sequence (a Sequencer inside, ~40 KB) and its packer live in PSRAM for the render only.
struct RenderMem {
  struct Parts {
    mt::OfflineSequence seq;
    mt::RenderPacker pack;
    RenderBlocks frame;
    Parts(mt::Project& p, const mt::RenderSpec& spec, uint32_t maxBlocks) : seq(p, spec), pack(seq, maxBlocks) {}
  };
  void* mem = heap_caps_malloc(sizeof(Parts), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  Parts* r = nullptr;
  ~RenderMem() {
    if (r) r->~Parts();
    heap_caps_free(mem);
  }
  Parts* start(mt::Project& p, const mt::RenderSpec& spec, uint32_t maxBlocks) {
    r = new (mem) Parts(p, spec, maxBlocks);
    return r;
  }
};

// Project locked for the render (the engine is stopped: it only waits for the lock on commands).
struct Session {
  Session() { engine::lockProject(); }
  ~Session() { engine::unlockProject(); }
};

Result fromRender(const RenderRep& rep) {
  switch (static_cast<RenderResult>(rep.result)) {
    case RenderResult::Ok: return Result::Ok;
    case RenderResult::NoSd: return Result::NoSd;
    case RenderResult::ReadFail: return Result::ReadFail;
    case RenderResult::Busy: return Result::AudioBusy;
    case RenderResult::Bank:
      switch (static_cast<mt::BankResult>(rep.bank)) {
        case mt::BankResult::Full: return Result::BankFull;
        case mt::BankResult::NoBank: return Result::NoBank;
        case mt::BankResult::Busy: return Result::AudioBusy;
        default: return Result::WriteFail;
      }
    case RenderResult::NoRender:
    case RenderResult::BadOrder: return Result::NoSynth;  // the board restarted or lost the render
    default: return Result::WriteFail;
  }
}

template <class Req>
bool call(Msg t, const Req& q, RenderRep& rep, uint32_t timeoutMs) {
  uint8_t p[kMaxPayload];
  Writer w(p, sizeof p);
  encode(q, w);
  if (!w.ok()) return false;
  uint8_t reply[kMaxPayload];
  int len = 0;
  if (!slink::request(t, p, w.size(), t, reply, len, timeoutMs)) return false;
  Reader r(reply, len);
  return decode(r, rep);
}

bool callEnd(const RenderEndReq& q, RenderRep& rep) {
  uint8_t p[kMaxPayload];
  Writer w(p, sizeof p);
  encode(q, w);
  uint8_t reply[kMaxPayload];
  int len = 0;
  if (!w.ok() || !slink::requestLong(Msg::RenderEnd, p, w.size(), reply, len)) return false;
  Reader r(reply, len);
  return decode(r, rep);
}

void abortRender() {
  RenderEndReq q;
  q.abort = 1;
  RenderRep rep;
  callEnd(q, rep);
}

// One render on the board: start, every block's events, end. rep: RenderEnd's reply.
Result run(mt::Project& p, const mt::RenderSpec& spec, uint8_t target, const char* path, uint32_t maxBlocks,
           const RenderEndReq& endReq, RenderRep& rep, bool& capped, RenderProgress cb, void* ctx) {
  capped = false;
  if (!slink::synthUp() || !waitSynced()) return Result::NoSynth;
  RenderMem rm;
  if (!rm.mem) return Result::NoMemory;
  Session ses;
  mt::OfflineSequence::Guard guard(p, spec);
  RenderMem::Parts* r = rm.start(p, spec, maxBlocks);
  const uint32_t total = r->seq.blocksTotal() < maxBlocks ? r->seq.blocksTotal() : maxBlocks;
  capped = r->seq.blocksTotal() > maxBlocks;
  RenderStartReq q;
  q.target = target;
  if (path) {
    if (strlen(path) >= sizeof q.path) return Result::WriteFail;
    strcpy(q.path, path);
  }
  if (!call(Msg::RenderStart, q, rep, kStartMs)) return Result::NoSynth;
  if (rep.result != static_cast<uint8_t>(RenderResult::Ok)) return fromRender(rep);
  uint32_t lastCb = millis();
  while (r->pack.next(r->frame)) {
    if (!call(Msg::RenderBlocks, r->frame, rep, kBlocksMs)) {
      abortRender();
      return Result::NoSynth;
    }
    if (rep.result != static_cast<uint8_t>(RenderResult::Ok)) {
      abortRender();
      return fromRender(rep);
    }
    const uint32_t now = millis();
    if (cb && now - lastCb >= kProgressMs) {
      lastCb = now;
      if (!cb(r->pack.blocksPacked(), total, ctx)) {
        abortRender();
        return Result::Cancelled;
      }
    }
  }
  if (cb) cb(total, total, ctx);
  if (!callEnd(endReq, rep)) return Result::NoSynth;
  return fromRender(rep);
}

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
  RenderRep rep;
  bool capped = false;
  const Result r = run(p, spec, RenderStartReq::kFile, path, 0xFFFFFFFFu, RenderEndReq{}, rep, capped, cb, ctx);
  if (r == Result::Ok) st = {rep.frames, static_cast<int16_t>(rep.peak), rep.clips};
  return r;
}

Result resample(mt::Project& p, const mt::RenderSpec& spec, char name[mt::kSampleNameMax + 1], RenderStats& st,
                RenderProgress cb, void* ctx) {
  st = {0, 0, 0};
  name[0] = 0;
  if (!stopped()) return Result::EngineBusy;
  if (p.sampleCount >= mt::kProjSamples) return Result::BankFull;
  if (!hw::sdReady()) return Result::NoSd;  // the render goes through a file on the card
  RenderEndReq end;
  end.normalize = 1;
  end.trim = 1;
  RenderRep rep;
  bool capped = false;
  const Result r = run(p, spec, RenderStartReq::kBank, nullptr, kResampleMaxFrames / kBlock, end, rep, capped, cb, ctx);
  audio::bankViewStale();
  if (r != Result::Ok) return r;
  engine::lockProject();
  mt::nextResampleName(p, name);
  const int idx = mt::projSampleSet(p, name, rep.crc, rep.frames);
  engine::unlockProject();
  if (idx < 0) {
    name[0] = 0;
    return Result::BankFull;  // the list is full (checked above) or the name is bad (never)
  }
  st = {rep.frames, static_cast<int16_t>(rep.peak), rep.clips};
  return capped ? Result::Capped : Result::Ok;
}

}  // namespace storage
