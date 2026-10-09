#include "bank.h"
#include <Arduino.h>
#include <SD.h>
#include <new>
#include <string.h>
#include "audio_out.h"
#include "bank_ops.h"
#include "flash_bank.h"
#include "fs_server.h"
#include "link_server.h"
#include "model.h"
#include "sample_set.h"
#include "synth.h"
#include "wt_builtin.h"
#include "wt_mip.h"

using namespace mt::link;
using mt::BankResult;

namespace bank {

namespace {

// ---- the card for bank jobs (SdFat; the Fs server's files are separate FsFile objects) ----

class SdRead final : public mt::ReadFile {
 public:
  FsFile f;
  bool read(void* d, size_t n) override { return f.read(d, n) == static_cast<int>(n); }
  bool skip(size_t n) override { return f.seekCur(static_cast<int64_t>(n)); }
  uint32_t size() override {
    const uint64_t s = f.fileSize();
    return s > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(s);
  }
  bool seek(uint32_t pos) override { return f.seekSet(pos); }
};

class SdWrite final : public mt::WriteFile {
 public:
  FsFile f;
  bool write(const void* d, size_t n) override { return f.write(d, n) == n; }
};

class SdDisk final : public mt::BankDisk {
 public:
  bool ready() override { return card::ready(); }
  mt::ReadFile* openRead(const char* path) override {
    r_.f = SD.sdfs.open(path, O_RDONLY);
    if (r_.f && r_.f.isDir()) r_.f.close();
    return r_.f ? &r_ : nullptr;
  }
  void closeRead() override { r_.f.close(); }
  mt::WriteFile* openWrite(const char* path) override {
    w_.f = SD.sdfs.open(path, O_RDWR | O_CREAT | O_TRUNC);
    return w_.f ? &w_ : nullptr;
  }
  bool closeWrite() override {
    card::spaceChanged();
    return w_.f.close();
  }
  bool exists(const char* path) override { return SD.sdfs.exists(path); }
  bool remove(const char* path) override { return SD.sdfs.remove(path); }
  bool rename(const char* from, const char* to) override { return SD.sdfs.rename(from, to); }
  bool mkdir(const char* path) override { return SD.sdfs.mkdir(path, false); }

 private:
  SdRead r_;
  SdWrite w_;
};

constexpr uint32_t kProgressMs = 100;

mt::SynthModel* model = nullptr;
SynthStream* out = nullptr;
FlashBank flash;
mt::SampleBank theBank(flash);
SdDisk disk;
mt::BankJob job(theBank, disk);
bool mounted = false;
int builtinsAdded = 0;
// Constructed in begin() (they hold a reference to the model).
alignas(mt::BankSampleSource) uint8_t srcMem[sizeof(mt::BankSampleSource)];
alignas(mt::BankWtSource) uint8_t wtMem[sizeof(mt::BankWtSource)];

// The request being worked on (a job, or BankClear in one go) and the last one answered.
bool working = false;
uint8_t curType = 0, curSeq = 0;
bool parkedByUs = false;
uint32_t lastProgress = 0;
bool answered = false;
uint8_t ansType = 0, ansSeq = 0;
int ansLen = 0;
uint8_t ansBuf[kMaxPayload];

template <class M>
int put(const M& m, uint8_t* p) {
  Writer w(p, kMaxPayload);
  encode(m, w);
  return w.ok() ? w.size() : 0;
}

uint8_t res(BankResult r) { return static_cast<uint8_t>(r); }

void park(bool on) {
  if (on == parkedByUs) return;
  parkedByUs = on;
  out->park(on);
}

void sendProgress(uint32_t done, uint32_t total, int item) {
  Progress pr;
  pr.op = curType;
  pr.done = done;
  pr.total = total;
  pr.item = item < 0 ? Progress::kNoItem
                     : static_cast<uint8_t>(item >= mt::BankJob::kWtItem ? Progress::kWtItem + item - mt::BankJob::kWtItem : item);
  link::reply(Msg::Progress, curSeq, pr);
}

void jobProgress() { sendProgress(job.done(), job.total(), job.item()); }

// Long parts done in one go (compacting, a wavetable's analysis): Progress and Status keep the ESP
// waiting.
void keepalive(uint32_t done, uint32_t total, void*) {
  const uint32_t now = millis();
  if (now - lastProgress >= kProgressMs) {
    lastProgress = now;
    if (job.busy()) jobProgress();
    else sendProgress(done, total, -1);
  }
  link::statusIfDue();
}

// Ends the request being worked on: its reply out now and kept for a retry.
void answer(const uint8_t* p, int n) {
  working = false;
  park(false);
  answered = true;
  ansType = curType;
  ansSeq = curSeq;
  ansLen = n;
  memcpy(ansBuf, p, n);
  link::reply(static_cast<Msg>(curType), curSeq, p, n);
}

void jobDone(BankResult r) {
  uint8_t p[kMaxPayload];
  int n = 0;
  if (job.kind() == mt::BankJob::Kind::Import) {
    const mt::ImportOut& o = job.imported();
    BankImportRep m;
    m.result = res(r);
    m.crc = o.crc;
    m.frames = o.frames;
    m.rate = o.rate;
    m.root = o.root;
    n = put(m, p);
  } else {
    BankSetsRep m;
    m.result = res(r);
    m.missing = static_cast<uint8_t>(job.missing());
    m.failed = static_cast<uint8_t>(job.failed());
    for (int i = 0; i < mt::kProjSamples; ++i)
      if (job.sampleMissing(i)) maskSet(m.samples, i);
    for (int i = 0; i < mt::kProjWavetables; ++i)
      if (job.wtMissing(i)) maskSet(m.wavetables, i);
    n = put(m, p);
  }
  answer(p, n);
}

// Reply with just a result: the request's own reply struct.
int resultOnly(Msg t, BankResult r, uint8_t* p) {
  switch (t) {
    case Msg::BankSync:
    case Msg::AssetsSave: {
      BankSetsRep m;
      m.result = res(r);
      return put(m, p);
    }
    case Msg::BankImport: {
      BankImportRep m;
      m.result = res(r);
      return put(m, p);
    }
    case Msg::SampleInfo: {
      SampleInfoRep m;
      m.result = res(r);
      return put(m, p);
    }
    case Msg::BankIndex: {
      BankIndexRep m;
      m.result = res(r);
      m.n = 0;
      return put(m, p);
    }
    case Msg::BankClear: {
      BankClearRep m;
      m.result = res(r);
      return put(m, p);
    }
    case Msg::WtFrame: {
      WtFrameRep m;
      m.result = res(r);
      return put(m, p);
    }
    default: return 0;
  }
}

int sampleInfo(Reader& r, uint8_t* p) {
  SampleInfoReq q;
  if (!decode(r, q)) return resultOnly(Msg::SampleInfo, BankResult::ReadFail, p);
  SampleInfoRep m;
  const int j = q.index < model->sampleCount ? mt::projSampleBank(*model, theBank, q.index) : -1;
  if (const mt::BankEntry* e = theBank.entry(j)) {
    m.cached = 1;
    m.frames = e->frames;
    m.rate = e->rate;
    m.root = e->root;
    m.loop = e->loop;
  }
  return put(m, p);
}

int bankIndex(uint8_t* p) {
  BankIndexRep m;
  m.count = static_cast<uint8_t>(theBank.count());
  m.capacity = theBank.capacity();
  m.free = theBank.freeBytes();
  m.unused = mt::bankUnusedBytes(theBank, *model);
  m.gen = theBank.generation();
  const int ns = model->sampleCount < mt::kProjSamples ? model->sampleCount : mt::kProjSamples;
  for (int i = 0; i < ns; ++i)
    if (const mt::BankEntry* e = theBank.entry(mt::projSampleBank(*model, theBank, i))) {
      maskSet(m.samples, i);
      m.rate[i] = static_cast<uint16_t>(e->rate);
    }
  m.n = static_cast<uint8_t>(ns);
  const int nw = model->wavetableCount < mt::kProjWavetables ? model->wavetableCount : mt::kProjWavetables;
  for (int i = 0; i < nw; ++i)
    if (mt::projWtBank(*model, theBank, i) >= 0) maskSet(m.wavetables, i);
  for (int i = 0; i < mt::kWtBuiltins && i < 8; ++i)
    if (mt::bankWtIndex(theBank, *model, mt::wtBuiltinName(i)) >= 0) m.builtins |= static_cast<uint8_t>(1 << i);
  return put(m, p);
}

int wtFrame(Reader& r, uint8_t* p) {
  WtFrameReq q;
  if (!decode(r, q)) return resultOnly(Msg::WtFrame, BankResult::ReadFail, p);
  WtFrameRep m;
  if (!mt::bankWtFrame(theBank, *model, q.name, q.frame, m.pts)) m.result = res(BankResult::OpenFail);
  return put(m, p);
}

// BankClear: quick (removing entries rewrites the table) or long (compacting: keepalive).
int clear(Reader& r, uint8_t* p) {
  BankClearReq q;
  if (!decode(r, q)) return resultOnly(Msg::BankClear, BankResult::ReadFail, p);
  park(true);
  BankClearRep m;
  if (q.compact) {
    lastProgress = millis();
    if (!theBank.compact(keepalive, nullptr)) m.result = res(BankResult::WriteFail);
  } else {
    const int n = mt::bankClearUnused(theBank, *model);
    m.removed = static_cast<uint8_t>(n < 0 ? 0 : n);
  }
  return put(m, p);
}

BankResult startJob(Msg t, Reader& r) {
  if (t == Msg::BankImport) {
    BankImportReq q;
    if (!decode(r, q)) return BankResult::ReadFail;
    park(true);
    return job.startImport(q.path, q.kind == 1, *model, q.hasCrc ? &q.crc : nullptr);
  }
  BankProjectReq q;
  if (!decode(r, q)) return BankResult::ReadFail;
  if (t == Msg::BankSync) {
    park(true);
    return job.startSync(q.project, *model);
  }
  return job.startSave(q.project, *model);
}

}  // namespace

void begin(mt::SynthModel& m, mt::Synth& synth, SynthStream& o) {
  model = &m;
  out = &o;
  auto* src = new (srcMem) mt::BankSampleSource(theBank, m);
  auto* wts = new (wtMem) mt::BankWtSource(theBank, m);
  job.setKeepalive(keepalive, nullptr);
  if (!flashmap::firmwareFits()) return;  // the bank would overlap the OTA area
  mounted = theBank.mount();
  if (!mounted) return;
  if (auto* table = static_cast<int16_t*>(malloc(mt::kWtTableSamples * sizeof(int16_t)))) {
    builtinsAdded = mt::bankAddBuiltins(theBank, m, table);
    free(table);
  }
  synth.setBank(src);
  synth.setWavetables(wts);
}

void logState() {
  if (!flashmap::firmwareFits()) {
    link::log("bank: firmware %lu B over %lu B, bank off", static_cast<unsigned long>(flashmap::firmwareBytes()),
              static_cast<unsigned long>(flashmap::kFwMax));
    return;
  }
  if (!mounted) {
    link::log("bank: mount failed");
    return;
  }
  link::log("bank: %d entries, %lu KB free of %lu KB, %d built-ins added", theBank.count(),
            static_cast<unsigned long>(theBank.freeBytes() / 1024), static_cast<unsigned long>(theBank.capacity() / 1024),
            builtinsAdded);
}

bool isRequest(Msg t) {
  switch (t) {
    case Msg::BankSync:
    case Msg::AssetsSave:
    case Msg::BankImport:
    case Msg::SampleInfo:
    case Msg::BankIndex:
    case Msg::BankClear:
    case Msg::WtFrame: return true;
    default: return false;
  }
}

int handle(Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* o) {
  const uint8_t type = static_cast<uint8_t>(t);
  Reader r(p, n);
  // Lookups: answered at once, any time.
  if (t == Msg::SampleInfo || t == Msg::BankIndex || t == Msg::WtFrame) {
    if (!mounted) return resultOnly(t, BankResult::NoBank, o);
    if (t == Msg::SampleInfo) return sampleInfo(r, o);
    if (t == Msg::BankIndex) return bankIndex(o);
    return wtFrame(r, o);
  }
  if (working && curType == type && curSeq == seq) {  // a retry of the running job
    jobProgress();
    return -1;
  }
  if (answered && ansType == type && ansSeq == seq) {  // a retry: its reply got lost
    memcpy(o, ansBuf, ansLen);
    return ansLen;
  }
  if (working) return resultOnly(t, BankResult::Busy, o);
  if (!mounted) return resultOnly(t, BankResult::NoBank, o);
  curType = type;
  curSeq = seq;
  working = true;
  lastProgress = millis();
  if (t == Msg::BankClear) {
    const int len = clear(r, o);
    working = false;
    park(false);
    answered = true;
    ansType = type;
    ansSeq = seq;
    ansLen = len;
    memcpy(ansBuf, o, len);
    return len;
  }
  const BankResult s = startJob(t, r);
  if (s != BankResult::Running) {
    working = false;
    park(false);
    return resultOnly(t, s, o);
  }
  return -1;
}

const int16_t* sampleData(int index, uint32_t& frames, uint32_t& rate, uint32_t& gen) {
  frames = rate = 0;
  gen = theBank.generation();
  if (!mounted || parkedByUs || index < 0 || index >= model->sampleCount) return nullptr;
  const int j = mt::projSampleBank(*model, theBank, index);
  const mt::BankEntry* e = theBank.entry(j);
  const int16_t* d = e ? theBank.data(j) : nullptr;
  if (!d) return nullptr;
  frames = e->frames;
  rate = e->rate;
  return d;
}

mt::BankResult writable() {
  if (!mounted) return BankResult::NoBank;
  return working ? BankResult::Busy : BankResult::Ok;
}

mt::BankResult writeFrames(uint32_t frames, uint32_t rate, mt::FrameFill fill, void* ctx, mt::ImportOut& o,
                           mt::BankProgress keep, void* keepCtx) {
  const BankResult w = writable();
  if (w != BankResult::Ok) return w;
  return mt::bankWriteFrames(theBank, *model, frames, rate, fill, ctx, o, keep, keepCtx);
}

void step() {
  if (!working) return;
  const BankResult r = job.step();
  if (r != BankResult::Running) {
    jobDone(r);
    return;
  }
  const uint32_t now = millis();
  if (now - lastProgress >= kProgressMs) {
    lastProgress = now;
    jobProgress();
  }
}

}  // namespace bank
