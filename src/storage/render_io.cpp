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
#include "remote_fs.h"
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
    if (r) r->~Parts();  // the next pass of an export
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
  mt::OfflineSequence::Guard guard(p);
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

namespace {

constexpr uint32_t kOutMs = 1500;  // a file opened (preallocated) on the board's card

void joinPath(const char* dir, const char* name, char* out, int n) { snprintf(out, n, "%s/%s", dir, name); }

// The files of outs[0, n) removed (a cancelled / failed run).
void removeFiles(const char* dir, const mt::ExportOut* outs, int n) {
  for (int i = 0; i < n; ++i) {
    char path[192];
    joinPath(dir, outs[i].name, path, sizeof path);
    remoteFs().remove(path);
  }
}

// The folder's .wav files that are not in outs (an older set: a track gone, FX off now).
// Up to 8 per listing, listed again while a listing was full.
void removeStale(const char* dir, const mt::ExportOut* outs, int n) {
  char stale[8][mt::kExportNameMax];
  for (int round = 0; round < 16; ++round) {
    int k = 0;
    fs::File d = remoteFs().open(dir);
    if (!d || !d.isDirectory()) return;
    for (fs::File f = d.openNextFile(); f && k < 8; f = d.openNextFile()) {
      const char* nm = strrchr(f.name(), '/');
      nm = nm ? nm + 1 : f.name();
      const size_t len = strlen(nm);
      const bool wav = !f.isDirectory() && len > 4 && strcasecmp(nm + len - 4, ".wav") == 0;
      bool mine = false;
      for (int i = 0; i < n && !mine; ++i) mine = strcmp(outs[i].name, nm) == 0;
      if (wav && !mine && len < sizeof stale[0]) strcpy(stale[k++], nm);
    }
    d.close();
    for (int i = 0; i < k; ++i) {
      char path[192];
      joinPath(dir, stale[i], path, sizeof path);
      remoteFs().remove(path);
    }
    if (k < 8) return;
  }
}

}  // namespace

Result exportSet(mt::Project& p, const mt::ExportSpec& spec, ExportStats& st, ExportProgress cb, void* ctx) {
  st = ExportStats();
  if (!hw::sdReady()) return Result::NoSd;
  if (!stopped()) return Result::EngineBusy;
  static mt::ExportOut outs[mt::kExportMaxOut];
  const int n = mt::exportOutputs(p, spec, outs);
  char dir[sizeof RenderStartReq::path];
  mt::exportDir(p, spec, dir, sizeof dir);
  const mt::RenderSpec rs = mt::exportRenderSpec(p, spec);
  const uint32_t est = mt::exportEstFrames(p, spec);
  uint32_t totalMb = 0, freeMb = 0;
  if (remoteSpace(totalMb, freeMb) && mt::exportBytes(n, est, spec.bits) / (1024u * 1024u) + 1 > freeMb)
    return Result::DiskFull;
  if (!slink::synthUp() || !waitSynced()) return Result::NoSynth;
  RenderMem rm;
  if (!rm.mem) return Result::NoMemory;
  Session ses;
  mt::OfflineSequence::Guard guard(p);
  int done = 0, pass = 0, passes = 1, touched = 0;
  uint32_t rendered = 0xFFFFFFFFu, cut = 0;
  const bool autoTail = spec.tail == mt::ExportTail::Auto;
  const uint32_t shown = (mt::exportSkipFrames(p, spec) + est) / kBlock + 1;  // AUTO: the bar's guess
  Result res = Result::Ok;
  RenderRep rep;
  while (done < n && res == Result::Ok) {
    RenderMem::Parts* r = rm.start(p, rs, rendered);
    RenderStartReq q;
    q.target = RenderStartReq::kExport;
    strcpy(q.path, dir);
    q.bits = spec.bits == 16 ? 16 : 24;
    q.outs = static_cast<uint8_t>(n - done);
    q.skipFrames = mt::exportSkipFrames(p, spec);
    q.estFrames = est;
    if (!call(Msg::RenderStart, q, rep, kStartMs)) {
      res = Result::NoSynth;
      break;
    }
    if (rep.result != static_cast<uint8_t>(RenderResult::Ok) || rep.outMax == 0) {
      res = rep.result == static_cast<uint8_t>(RenderResult::Ok) ? Result::NoMemory : fromRender(rep);
      break;
    }
    const int m = rep.outMax < n - done ? rep.outMax : n - done;
    passes = pass + mt::exportPasses(n - done, rep.outMax);
    for (int i = 0; i < m && res == Result::Ok; ++i) {
      RenderOut o;
      o.idx = static_cast<uint8_t>(i);
      o.tap = outs[done + i].tap;
      strcpy(o.name, outs[done + i].name);
      if (!call(Msg::RenderOut, o, rep, kOutMs)) res = Result::NoSynth;
      else if (rep.result != static_cast<uint8_t>(RenderResult::Ok)) res = fromRender(rep);
    }
    if (res != Result::Ok) {
      abortRender();
      break;
    }
    const uint32_t total = pass == 0 ? (autoTail ? shown : r->seq.blocksTotal()) : rendered;
    uint32_t lastCb = millis();
    while (r->pack.next(r->frame)) {
      if (!call(Msg::RenderBlocks, r->frame, rep, kBlocksMs)) {
        res = Result::NoSynth;
        break;
      }
      if (rep.result != static_cast<uint8_t>(RenderResult::Ok)) {
        res = fromRender(rep);
        break;
      }
      if (pass == 0 && autoTail && mt::exportAutoDone(p, spec, rep.blocks, rep.quiet)) break;
      const uint32_t now = millis();
      if (cb && now - lastCb >= kProgressMs) {
        lastCb = now;
        const uint32_t b = r->pack.blocksPacked();
        if (!cb(pass, passes, b < total ? b : total, total, ctx)) {
          res = Result::Cancelled;
          break;
        }
      }
    }
    if (res != Result::Ok) {
      abortRender();
      break;
    }
    if (pass == 0) {
      rendered = rep.blocks;
      cut = mt::exportCutFrames(p, spec, rendered, rep.quiet);
    }
    if (cb) cb(pass, passes, total, total, ctx);
    RenderEndReq e;
    e.frames = cut;
    touched = done + m;
    if (!callEnd(e, rep)) {
      res = Result::NoSynth;
      break;
    }
    if ((res = fromRender(rep)) != Result::Ok) break;
    for (int i = 0; i < m && i < rep.outN; ++i) {
      if (rep.peaks[i] > st.peak) st.peak = rep.peaks[i];
      st.clips += rep.clipsN[i];
      if (rep.clipsN[i] && st.clipFile < 0) st.clipFile = done + i;
    }
    done += m;
    ++pass;
  }
  if (res != Result::Ok) {
    removeFiles(dir, outs, touched);  // earlier passes, and this one's if RenderEnd got that far
    return res;
  }
  removeStale(dir, outs, n);
  st.frames = cut;
  st.files = n;
  st.passes = pass;
  return Result::Ok;
}

}  // namespace storage
