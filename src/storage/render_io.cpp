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
#include "oneshot_lib.h"
#include "render_link.h"
#include "remote_fs.h"
#include "sample_store.h"
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

// The synth board answering again: a long job on it (an FsCopy of the one-shot before) stops its
// Status frames for a while.
bool waitUp() {
  const uint32_t t0 = millis();
  for (;;) {
    audio::pump();
    if (slink::synthUp()) return true;
    if (millis() - t0 >= kSyncWaitMs) return false;
    delay(5);
  }
}

bool remoteExists(const char* path, void*) { return remoteFs().exists(path); }

// The bounced sample (the store's <crc>.wav) copied into /samples/oneshots/<dir of in>/.
bool copyOneshot(const mt::Instrument& in, const char* name, uint32_t crc) {
  char dir[mt::kOneshotDirMax], path[96], from[mt::kStorePathMax];
  mt::oneshotDir(in, dir);
  if (!mt::oneshotFilePath(dir, name, remoteExists, nullptr, path, sizeof path)) return false;
  mt::storePath(crc, from);
  return remoteCopy(from, path) == kErrOk;
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

// One render on the board, for every kind (Resample, one-shot, an Export pass): RenderStart, the job's
// frames, RenderEnd. Any failure, a cancel or silence aborts it on the board. rep: RenderEnd's reply.
struct Job {
  virtual ~Job() = default;
  // After RenderStart (Ok): Export adds its files. Not Ok: aborted.
  virtual Result opened(const RenderRep&) { return Result::Ok; }
  // The next frame (last: the reply to the one before, or RenderStart's); nullptr: done.
  virtual RenderBlocks* next(const RenderRep& last) = 0;
  // Every kProgressMs and once at the end (final); false cancels.
  virtual bool progress(bool final) = 0;
  // Right before RenderEnd: its request (last: the last block's reply).
  virtual void ending(const RenderRep& last, RenderEndReq& e) { (void)last, (void)e; }
  bool silentFails = false;  // nothing above -60 dBFS: Silent, nothing kept
};

Result drive(Job& job, const RenderStartReq& q, RenderRep& rep) {
  if (!call(Msg::RenderStart, q, rep, kStartMs)) return Result::NoSynth;
  if (rep.result != static_cast<uint8_t>(RenderResult::Ok)) return fromRender(rep);
  Result res = job.opened(rep);
  if (res != Result::Ok) {
    abortRender();
    return res;
  }
  uint32_t lastCb = millis();
  for (RenderBlocks* f = job.next(rep); f; f = job.next(rep)) {
    if (!call(Msg::RenderBlocks, *f, rep, kBlocksMs)) {
      res = Result::NoSynth;
    } else if (rep.result != static_cast<uint8_t>(RenderResult::Ok)) {
      res = fromRender(rep);
    } else if (millis() - lastCb >= kProgressMs) {
      lastCb = millis();
      if (!job.progress(false)) res = Result::Cancelled;
    }
    if (res != Result::Ok) {
      abortRender();
      return res;
    }
  }
  if (job.silentFails && rep.quiet >= rep.blocks) {  // never above -60 dBFS
    abortRender();
    return Result::Silent;
  }
  job.progress(true);
  RenderEndReq e;
  job.ending(rep, e);
  if (!callEnd(e, rep)) return Result::NoSynth;
  return fromRender(rep);
}

// The board renders over the live project's mirror: the transport stopped, the cards there, a free
// slot in the sample list (into the bank), room on the synth board's card for cardBytes (0: not
// checked), the board up and in sync.
Result precheck(const mt::Project& p, bool toBank, uint64_t cardBytes) {
  if (!stopped()) return Result::EngineBusy;
  if (!hw::sdReady()) return Result::NoSd;
  if (toBank && p.sampleCount >= mt::kProjSamples) return Result::BankFull;
  if (!waitUp()) return Result::NoSynth;
  uint32_t totalMb = 0, freeMb = 0;
  if (cardBytes && remoteSpace(totalMb, freeMb) && cardBytes / (1024u * 1024u) + 1 > freeMb) return Result::DiskFull;
  if (!waitSynced()) return Result::NoSynth;
  return Result::Ok;
}

// The bank view is stale after a render into the bank, whatever came of it.
struct StaleBankView {
  ~StaleBankView() { audio::bankViewStale(); }
};

// A sequence's blocks (Resample, an Export pass) out of RenderMem.
struct SeqJob : Job {
  RenderMem::Parts* r;
  uint32_t total;
  RenderProgress cb;
  void* ctx;
  SeqJob(RenderMem::Parts* parts, uint32_t blocks, RenderProgress c, void* x) : r(parts), total(blocks), cb(c), ctx(x) {}
  RenderBlocks* next(const RenderRep&) override { return r->pack.next(r->frame) ? &r->frame : nullptr; }
  bool progress(bool final) override { return !cb || cb(final ? total : r->pack.blocksPacked(), total, ctx); }
};

}  // namespace

Result resample(mt::Project& p, const mt::ResamplePlan& plan, char name[mt::kSampleNameMax + 1], RenderStats& st,
                RenderProgress cb, void* ctx) {
  st = {0, 0, 0, false};
  name[0] = 0;
  StaleBankView stale;
  // The temporary file holds the wrap pass too.
  Result res = precheck(p, true, (static_cast<uint64_t>(plan.skipFrames) + plan.frames) * 4 + 4096);
  if (res != Result::Ok) return res;
  RenderMem rm;
  if (!rm.mem) return Result::NoMemory;
  RenderRep rep;
  {
    Session ses;
    mt::OfflineSequence::Guard guard(p);
    struct ResampleJob : SeqJob {
      uint32_t frames;
      using SeqJob::SeqJob;
      void ending(const RenderRep&, RenderEndReq& e) override { e.frames = frames; }  // as played: no normalize, no trim
    } job(rm.start(p, plan.spec, plan.blocks), plan.blocks, cb, ctx);
    job.frames = plan.frames;
    job.silentFails = true;
    RenderStartReq q;
    q.target = RenderStartReq::kBank;
    q.skipFrames = plan.skipFrames;
    res = drive(job, q, rep);
  }
  if (res != Result::Ok) return res;
  engine::lockProject();
  mt::nextResampleName(p, name);
  const int idx = mt::projSampleSet(p, name, rep.crc, rep.frames);
  engine::unlockProject();
  if (idx < 0) {
    name[0] = 0;
    return Result::BankFull;  // the list is full (checked above) or the name is bad (never)
  }
  st = {rep.frames, static_cast<int16_t>(rep.peak), rep.clips, false};
  return plan.capped ? Result::Capped : Result::Ok;
}

Result bounce(mt::Project& p, const mt::BounceSpec& spec, const char* name, RenderStats& st, RenderProgress cb,
              void* ctx, const mt::Instrument* dirOf) {
  st = {0, 0, 0, false};
  if (!name || !validName(name)) return Result::BadFile;
  StaleBankView stale;
  Result res = precheck(p, true, static_cast<uint64_t>(spec.maxBlocks) * kBlock * 4 + 4096);
  if (res != Result::Ok) return res;
  RenderRep rep;
  {
    Session ses;
    struct BounceJob : Job {
      mt::BounceSeq seq;
      RenderProgress cb;
      void* ctx;
      RenderBlocks frame;  // ~500 B: the job lives in a static below
      bool normalize;
      explicit BounceJob(const mt::BounceSpec& s) : seq(s), cb(nullptr), ctx(nullptr), normalize(s.normalize) {}
      RenderBlocks* next(const RenderRep& last) override { return seq.next(frame, last.quiet) ? &frame : nullptr; }
      // The length is known only at the end (silence after the note-off): frames so far, total 0.
      bool progress(bool) override { return !cb || cb(seq.blocksSent() * kBlock, 0, ctx); }
      void ending(const RenderRep&, RenderEndReq& e) override {
        e.normalize = normalize ? 2 : 0;  // -0.3 dBFS / as played (mt::kPrevTapScale)
        e.trim = 1;
      }
    };
    alignas(BounceJob) static uint8_t mem[sizeof(BounceJob)];  // off the task's stack
    BounceJob* job = new (mem) BounceJob(spec);
    job->cb = cb;
    job->ctx = ctx;
    job->silentFails = true;
    RenderStartReq q;
    q.target = RenderStartReq::kBank;
    q.src = RenderStartReq::kSrcPreview;
    res = drive(*job, q, rep);
    job->~BounceJob();
  }
  if (res != Result::Ok) return res;
  engine::lockProject();
  const int idx = mt::projSampleSet(p, name, rep.crc, rep.frames);
  engine::unlockProject();
  if (idx < 0) return Result::BankFull;
  st = {rep.frames, static_cast<int16_t>(rep.peak), rep.clips, false};
  // A copy for the library: a failed one leaves the sample in the project.
  st.copied = copyOneshot(dirOf ? *dirOf : p.instruments[spec.instr % mt::kInstruments], name, rep.crc);
  return Result::Ok;
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

// One export pass: its files (RenderOut), the sequence's blocks (the first pass: AUTO stops at the tail's
// silence and decides the length), the cut.
struct ExportJob : Job {
  const mt::Project& p;
  const mt::ExportSpec& spec;
  const mt::ExportOut* outs;  // this pass's first file
  RenderMem::Parts* r = nullptr;
  int pass = 0, passes = 1, done = 0, m = 0, n = 0, touched = 0;
  uint32_t total = 0, rendered = 0xFFFFFFFFu, cut = 0;
  bool autoTail = false;
  ExportProgress cb;
  void* ctx;
  ExportJob(const mt::Project& pr, const mt::ExportSpec& s, const mt::ExportOut* o, ExportProgress c, void* x)
      : p(pr), spec(s), outs(o), cb(c), ctx(x) {}
  Result opened(const RenderRep& rep) override {
    if (rep.outMax == 0) return Result::NoMemory;
    m = rep.outMax < n - done ? rep.outMax : n - done;
    passes = pass + mt::exportPasses(n - done, rep.outMax);
    for (int i = 0; i < m; ++i) {
      RenderOut o;
      o.idx = static_cast<uint8_t>(i);
      o.tap = outs[done + i].tap;
      strcpy(o.name, outs[done + i].name);
      RenderRep or_;
      if (!call(Msg::RenderOut, o, or_, kOutMs)) return Result::NoSynth;
      if (or_.result != static_cast<uint8_t>(RenderResult::Ok)) return fromRender(or_);
    }
    return Result::Ok;
  }
  RenderBlocks* next(const RenderRep& last) override {
    if (pass == 0 && autoTail && mt::exportAutoDone(p, spec, last.blocks, last.quiet)) return nullptr;
    return r->pack.next(r->frame) ? &r->frame : nullptr;
  }
  bool progress(bool final) override {
    const uint32_t b = r->pack.blocksPacked();
    return !cb || cb(pass, passes, final || b > total ? total : b, total, ctx);
  }
  void ending(const RenderRep& last, RenderEndReq& e) override {
    if (pass == 0) {
      rendered = last.blocks;
      cut = mt::exportCutFrames(p, spec, rendered, last.quiet);
    }
    e.frames = cut;
    touched = done + m;
  }
};

}  // namespace

Result exportSet(mt::Project& p, const mt::ExportSpec& spec, ExportStats& st, ExportProgress cb, void* ctx) {
  st = ExportStats();
  static mt::ExportOut outs[mt::kExportMaxOut];
  const int n = mt::exportOutputs(p, spec, outs);
  const uint32_t est = mt::exportEstFrames(p, spec);
  Result res = precheck(p, false, mt::exportBytes(n, est, spec.bits));
  if (res != Result::Ok) return res;
  char dir[sizeof RenderStartReq::path];
  mt::exportDir(p, spec, dir, sizeof dir);
  const mt::RenderSpec rs = mt::exportRenderSpec(p, spec);
  RenderMem rm;
  if (!rm.mem) return Result::NoMemory;
  Session ses;
  mt::OfflineSequence::Guard guard(p);
  ExportJob job(p, spec, outs, cb, ctx);
  job.n = n;
  job.autoTail = spec.tail == mt::ExportTail::Auto;
  const uint32_t shown = (mt::exportSkipFrames(p, spec) + est) / kBlock + 1;  // AUTO: the bar's guess
  RenderRep rep;
  while (job.done < n && res == Result::Ok) {
    job.r = rm.start(p, rs, job.rendered);
    job.total = job.pass == 0 ? (job.autoTail ? shown : job.r->seq.blocksTotal()) : job.rendered;
    job.m = 0;
    RenderStartReq q;
    q.target = RenderStartReq::kExport;
    strcpy(q.path, dir);
    q.bits = spec.bits == 16 ? 16 : 24;
    q.outs = static_cast<uint8_t>(n - job.done);
    q.skipFrames = mt::exportSkipFrames(p, spec);
    q.estFrames = est;
    if ((res = drive(job, q, rep)) != Result::Ok) break;
    for (int i = 0; i < job.m && i < rep.outN; ++i) {
      if (rep.peaks[i] > st.peak) st.peak = rep.peaks[i];
      st.clips += rep.clipsN[i];
      if (rep.clipsN[i] && st.clipFile < 0) st.clipFile = job.done + i;
    }
    job.done += job.m;
    ++job.pass;
  }
  if (res != Result::Ok) {
    removeFiles(dir, outs, job.touched);  // earlier passes, and this one's if RenderEnd got that far
    return res;
  }
  removeStale(dir, outs, n);
  st.frames = job.cut;
  st.files = n;
  st.passes = job.pass;
  return Result::Ok;
}

}  // namespace storage
