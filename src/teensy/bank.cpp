#include "bank.h"
#include <Arduino.h>
#include <SD.h>
#include <new>
#include <string.h>
#include "audio_out.h"
#include "bank_ops.h"
#include "file_rules.h"
#include "flash_bank.h"
#include "fs_server.h"
#include "link_server.h"
#include "model.h"
#include "sample_heads.h"
#include "sample_set.h"
#include "sample_store.h"
#include "stream.h"
#include "synth.h"
#include "wav.h"
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
  uint64_t top = 0;  // largest position written: the size at close
  bool write(const void* d, size_t n) override { return f.write(d, n) == n; }
  bool reserve(uint32_t bytes) override { return f.preAllocate(bytes); }
  bool seek(uint32_t pos) override {
    if (f.curPosition() > top) top = f.curPosition();
    return f.seekSet(pos);
  }
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
    w_.top = 0;
    return w_.f ? &w_ : nullptr;
  }
  bool closeWrite() override {
    card::spaceChanged();
    // Cuts what preAllocate reserved past the data; the file stays one contiguous run.
    if (w_.f.curPosition() > w_.top) w_.top = w_.f.curPosition();
    const bool cut = !w_.f.isOpen() || w_.f.truncate(w_.top);
    w_.top = 0;
    return w_.f.close() && cut;
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
// ~14 KB of buffers: in RAM2 (DTCM is full), constructed in begin().
alignas(mt::StoreJob) DMAMEM uint8_t storeJobMem[sizeof(mt::StoreJob)];
mt::StoreJob& store = *reinterpret_cast<mt::StoreJob*>(storeJobMem);
bool mounted = false;
int builtinsAdded = 0;
int storeFound = 0;  // store files of the model's samples at boot
// Constructed in begin() (they hold a reference to the model).
alignas(mt::BankSampleSource) uint8_t srcMem[sizeof(mt::BankSampleSource)];
alignas(mt::BankWtSource) uint8_t wtMem[sizeof(mt::BankWtSource)];
alignas(mt::StoreSampleSource) uint8_t storeMem[sizeof(mt::StoreSampleSource)];

// The request being worked on (a job, or BankClear in one go) and the last one answered. A request
// runs a store job (storeActive), then (sync / save) a bank job for the wavetables (jobActive).
bool working = false;
bool storeActive = false, jobActive = false;
BankResult storeRes = BankResult::Ok;
char project[mt::kSampleNameMax + 1] = {};
bool migrating = false;  // the boot's move of bank samples to the store (no request)
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

// Sync / save: the store's samples, then the bank job's wavetables, on one scale (items x 1000).
uint32_t setsTotal() {
  const int ns = model->sampleCount < mt::kProjSamples ? model->sampleCount : mt::kProjSamples;
  const int nw = model->wavetableCount < mt::kProjWavetables ? model->wavetableCount : mt::kProjWavetables;
  return static_cast<uint32_t>(ns + nw) * 1000;
}

void jobProgress() {
  if (storeActive) {
    if (curType == static_cast<uint8_t>(Msg::BankImport)) sendProgress(store.done(), store.total(), -1);
    else sendProgress(store.done(), setsTotal(), store.item());
  } else {
    sendProgress(job.done(), job.total(), job.item());
  }
}

// Long parts done in one go (compacting, a wavetable's analysis): Progress and Status keep the ESP
// waiting.
void keepalive(uint32_t done, uint32_t total, void*) {
  const uint32_t now = millis();
  if (now - lastProgress >= kProgressMs) {
    lastProgress = now;
    if (working && (storeActive || jobActive)) jobProgress();
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

// Frames [frame, frame + len) of store sample crc into the bank entry being added (raw card reads,
// 2 KB at a time).
bool copyFrames(uint32_t crc, uint32_t frame, uint32_t len) {
  DMAMEM static int16_t buf[2 * mt::kStreamBlock];
  uint32_t b = frame / mt::kStreamBlock, off = frame % mt::kStreamBlock;
  while (len) {
    const int nb = off + len > mt::kStreamBlock ? 2 : 1;
    // SdFat SDIO keeps a multi-sector read open between calls; continuing it after flash writes
    // times out: end it first.
    SD.sdfs.card()->syncDevice();
    if (!stream::reader().read(crc, b, nb, buf)) return false;
    const uint32_t have = nb * mt::kStreamBlock - off;
    const uint32_t k = len < have ? len : have;
    if (!theBank.write(buf + off, k)) return false;
    len -= k;
    b += nb;
    off = 0;
    keepalive(0, 0, nullptr);
  }
  return true;
}

// Writes / refreshes the heads of the model's store samples (the synth parked); stream::rescan then
// hands them to the table.
void writeHeads() {
  if (!mounted) return;
  int written = 0;
  for (int k = 0; k < model->sampleCount; ++k) {
    const mt::ProjSample& ps = model->samples[k];
    const int t = stream::table().find(ps.crc);
    if (t < 0) continue;
    const mt::StoreEntry& e = stream::table().at(t);
    if (!e.sector) continue;  // not streamable: no heads
    mt::HeadPoint pts[mt::kMaxHeads];
    const int n = mt::headPoints(*model, ps.crc, e.frames, e.rate, pts, mt::kMaxHeads);
    char key[mt::kSampleNameMax + 1];
    mt::headKey(ps.crc, key);
    const int b = theBank.find(key);
    if (b >= 0 && mt::headSame(theBank.data(b), theBank.entry(b)->frames, pts, n)) continue;
    if (b >= 0) theBank.remove(b);
    if (!n) continue;
    static int16_t index[mt::kHeadIndexFrames];
    const uint32_t len = mt::headLayout(pts, n, index);
    if (!mt::bankMakeRoom(theBank, *model, len, keepalive, nullptr) || !theBank.begin(key, len, 0, 60)) {
      link::log("heads: no room for %s (%lu frames)", key, static_cast<unsigned long>(len));
      continue;
    }
    bool ok = theBank.write(index, mt::kHeadIndexFrames);
    for (int i = 0; ok && i < n; ++i) ok = copyFrames(ps.crc, pts[i].frame, pts[i].len);  // store -> bank
    if (!ok || !theBank.commit()) {
      theBank.abort();
      link::log("heads: %s failed", key);
    } else {
      ++written;
    }
  }
  if (written) link::log("heads: %d samples written", written);
}

bool inModel(uint32_t crc) {
  for (int i = 0; i < model->sampleCount; ++i)
    if (model->samples[i].crc == crc) return true;
  return false;
}

// The reply of a sync / save: the store's samples, the bank job's wavetables (jobR: its result).
void setsDone(BankResult jobR) {
  uint8_t p[kMaxPayload];
  BankSetsRep m;
  m.result = res(storeRes != BankResult::Ok ? storeRes : jobR);
  int missing = store.missing(), failed = 0;
  for (int i = 0; i < mt::kProjSamples; ++i)
    if (store.sampleMissing(i)) maskSet(m.samples, i);
  missing += job.missing();
  failed += job.failed();
  for (int i = 0; i < mt::kProjWavetables; ++i)
    if (job.wtMissing(i)) maskSet(m.wavetables, i);
  m.missing = static_cast<uint8_t>(missing > 255 ? 255 : missing);
  m.failed = static_cast<uint8_t>(failed > 255 ? 255 : failed);
  const int n = put(m, p);
  if (curType == static_cast<uint8_t>(Msg::BankSync)) {  // still parked: answer() unparks
    stream::rescan(*model, false);  // the model's store files (stored before: not registered yet)
    writeHeads();
    storeFound = stream::rescan(*model);
  }
  answer(p, n);
}

// A sample import into the store ended; its heads are written now if the model plays it already.
void storeImportDone(BankResult r) {
  uint8_t p[kMaxPayload];
  const mt::ImportOut& o = store.imported();
  BankImportRep m;
  m.result = res(r);
  if (r == BankResult::Ok) {
    m.crc = o.crc;
    m.frames = o.frames;
    m.rate = o.rate;
    m.root = o.root;
    stream::add(o.crc);  // stored already (nothing written): register it all the same
    if (inModel(o.crc)) {
      park(true);
      writeHeads();
      stream::rescan(*model);
    }
  }
  answer(p, put(m, p));
}

// A bank job ended: a wavetable import, or the wavetable part of a sync / save.
void jobDone(BankResult r) {
  jobActive = false;
  if (job.kind() != mt::BankJob::Kind::Import) {
    setsDone(r);
    return;
  }
  uint8_t p[kMaxPayload];
  const mt::ImportOut& o = job.imported();
  BankImportRep m;
  m.result = res(r);
  m.crc = o.crc;
  m.frames = o.frames;
  m.rate = o.rate;
  m.root = o.root;
  answer(p, put(m, p));
}

// The store part of a sync / save ended: the wavetables next (bank job, samples left to the store).
void storeSetsDone(BankResult r) {
  storeRes = r;
  const bool sync = curType == static_cast<uint8_t>(Msg::BankSync);
  const BankResult s = sync ? job.startSync(project, *model, false) : job.startSave(project, *model, false);
  if (s == BankResult::Running) jobActive = true;
  else setsDone(s);
}

void migrateDone(BankResult r) {
  migrating = false;
  park(true);
  const int n = mt::bankDropMigrated(theBank, disk);
  link::log("migrated %d samples to the store (%s)", n, r == BankResult::Ok ? "ok" : "failed");
  stream::rescan(*model, false);
  writeHeads();
  storeFound = stream::rescan(*model);
  park(false);
}

// The root note of a store file (its smpl chunk), 60 if it has none or cannot be read.
uint8_t storeRoot(uint32_t crc) {
  char path[mt::kStorePathMax];
  mt::storePath(crc, path);
  mt::ReadFile* f = disk.openRead(path);
  if (!f) return 60;
  mt::WavInfo w;
  const uint8_t root = mt::wavParse(*f, w) == mt::WavErr::Ok ? w.root : 60;
  disk.closeRead();
  return root;
}

// Store entry of project sample i if its file holds the listed data, else -1.
int storeOf(int i) {
  if (i < 0 || i >= model->sampleCount) return -1;
  const int t = stream::table().find(model->samples[i].crc);
  return t >= 0 && stream::table().at(t).frames == model->samples[i].frames ? t : -1;
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
  if (const int t = storeOf(q.index); t >= 0) {
    const mt::StoreEntry& e = stream::table().at(t);
    m.cached = 1;
    m.frames = e.frames;
    m.rate = e.rate;
    m.root = storeRoot(e.crc);
    return put(m, p);
  }
  // Not moved to the store yet: the flash bank (legacy).
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
  for (int i = 0; i < ns; ++i) {
    uint32_t rate = 0;
    if (const int t = storeOf(i); t >= 0) rate = stream::table().at(t).rate;
    else if (const mt::BankEntry* e = theBank.entry(mt::projSampleBank(*model, theBank, i))) rate = e->rate;
    if (!rate) continue;
    maskSet(m.samples, i);
    m.rate[i] = static_cast<uint16_t>(rate);
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

// Store files (<8 hex>.wav / .pk) of samples the model does not list are removed; returns the
// samples (.wav) removed. The synth is parked.
int clearStore() {
  if (!card::ready()) return 0;
  int removed = 0;
  for (int pass = 0; pass < 64; ++pass) {  // a batch of names a pass: no removing while listing
    constexpr int kBatch = 32;
    char names[kBatch][16];
    int n = 0;
    FsFile dir = SD.sdfs.open(mt::kStoreDir, O_RDONLY);
    if (!dir || !dir.isDir()) return removed;
    FsFile f;
    while (n < kBatch && f.openNext(&dir, O_RDONLY)) {
      char nm[16] = {};
      f.getName(nm, sizeof nm);
      const bool file = !f.isDir();
      f.close();
      const char* dot = strchr(nm, '.');
      if (!file || !dot || dot - nm != 8 || (strcmp(dot, ".wav") != 0 && strcmp(dot, ".pk") != 0)) continue;
      char hex[9];
      memcpy(hex, nm, 8);
      hex[8] = 0;
      if (!mt::isSampleKey(hex) || inModel(static_cast<uint32_t>(strtoul(hex, nullptr, 16)))) continue;
      strcpy(names[n++], nm);
    }
    dir.close();
    if (!n) break;
    for (int i = 0; i < n; ++i) {
      char path[mt::kStorePathMax + 4];
      strcpy(path, mt::kStoreDir);  // "/mt-store"
      strcat(path, "/");
      strcat(path, names[i]);
      if (SD.sdfs.remove(path) && strstr(names[i], ".wav")) ++removed;
    }
  }
  card::spaceChanged();
  return removed;
}

// BankClear: quick (removing entries rewrites the table; the store files of samples the project
// does not list go too) or long (compacting: keepalive).
int clear(Reader& r, uint8_t* p) {
  BankClearReq q;
  if (!decode(r, q)) return resultOnly(Msg::BankClear, BankResult::ReadFail, p);
  park(true);
  BankClearRep m;
  if (q.compact) {
    lastProgress = millis();
    if (!theBank.compact(keepalive, nullptr)) m.result = res(BankResult::WriteFail);
  } else {
    int n = mt::bankClearUnused(theBank, *model);
    const int files = clearStore();
    if (files) link::log("store: %d unused samples removed", files);
    n = (n < 0 ? 0 : n) + files;
    m.removed = static_cast<uint8_t>(n > 255 ? 255 : n);
  }
  stream::rescan(*model, false);  // heads may have moved, files gone
  return put(m, p);
}

// Starts the request's work: a store job first (samples), a bank job (wavetables) after it or
// instead of it. Running: bgStep() goes on.
BankResult startJob(Msg t, Reader& r) {
  storeRes = BankResult::Ok;
  if (t == Msg::BankImport) {
    BankImportReq q;
    if (!decode(r, q)) return BankResult::ReadFail;
    if (q.kind == 1) {  // a wavetable: into the flash bank, the synth parked
      park(true);
      const BankResult s = job.startImport(q.path, true, *model, q.hasCrc ? &q.crc : nullptr);
      jobActive = s == BankResult::Running;
      return s;
    }
    // A sample: into the store while the synth plays (onAdded registers it).
    const BankResult s = store.startImport(q.path, q.hasCrc ? &q.crc : nullptr);
    storeActive = s == BankResult::Ok;
    return storeActive ? BankResult::Running : s;
  }
  BankProjectReq q;
  if (!decode(r, q)) return BankResult::ReadFail;
  strncpy(project, q.project, sizeof project - 1);
  project[sizeof project - 1] = 0;
  BankResult s;
  if (t == Msg::BankSync) {
    park(true);
    s = store.startSync(project, *model);
  } else {
    if (!mt::projectBaseValid(project)) return BankResult::OpenFail;
    s = store.startSave(project, *model);
  }
  storeActive = s == BankResult::Ok;
  return storeActive ? BankResult::Running : s;
}

// The running job as the stream scheduler's background work.
struct Bg final : mt::BgWork {
  bool step() override { return bgStep(); }
} bgWork;

}  // namespace

void begin(mt::SynthModel& m, mt::Synth& synth, SynthStream& o) {
  model = &m;
  out = &o;
  auto* legacy = new (srcMem) mt::BankSampleSource(theBank, m);
  auto* wts = new (wtMem) mt::BankWtSource(theBank, m);
  stream::addBackground(&bgWork);
  job.setKeepalive(keepalive, nullptr);
  new (storeJobMem) mt::StoreJob(disk);
  store.setOnAdded([](uint32_t crc, void*) { stream::add(crc); }, nullptr);
  mounted = flashmap::firmwareFits() && theBank.mount();  // a bank past the firmware would overlap the OTA area
  // Samples from the store, those not in it from the flash bank (legacy; none without a bank).
  auto* src = new (storeMem) mt::StoreSampleSource(stream::table(), stream::pool(), mounted ? legacy : nullptr, m);
  synth.setBank(src);
  storeFound = stream::rescan(m, false);  // the synth does not run yet; logState() tells
  if (!mounted) return;
  if (auto* table = static_cast<int16_t*>(malloc(mt::kWtTableSamples * sizeof(int16_t)))) {
    builtinsAdded = mt::bankAddBuiltins(theBank, m, table);
    free(table);
  }
  synth.setWavetables(wts);
  // Samples of the bank from before the store move there in the background (they play from the
  // bank until then); migrateDone() drops them from the bank.
  bool legacySamples = false;
  for (int i = 0; i < theBank.count() && !legacySamples; ++i) legacySamples = mt::isSampleKey(theBank.entry(i)->name);
  if (legacySamples) migrating = store.startMigrate(theBank) == BankResult::Ok;
}

void logState() {
  link::log("store: %d of %d samples", storeFound, model->sampleCount);
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
  if (migrating) link::log("bank: moving its samples to the store");
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
  if (working || migrating) return resultOnly(t, BankResult::Busy, o);
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

bool sampleAccess(int index, uint32_t& crc, uint32_t& frames, uint32_t& rate, const int16_t*& whole) {
  crc = frames = rate = 0;
  whole = nullptr;
  if (index < 0 || index >= model->sampleCount) return false;
  if (const int t = storeOf(index); t >= 0) {
    const mt::StoreEntry& e = stream::table().at(t);
    crc = e.crc;
    frames = e.frames;
    rate = e.rate;
    whole = e.psram;
    return true;
  }
  // Not moved to the store yet: its flash bank entry (stable unless a job runs).
  if (!mounted || parkedByUs) return false;
  const int j = mt::projSampleBank(*model, theBank, index);
  const mt::BankEntry* e = theBank.entry(j);
  const int16_t* d = e ? theBank.data(j) : nullptr;
  if (!d) return false;
  crc = model->samples[index].crc;
  frames = e->frames;
  rate = e->rate;
  whole = d;
  return true;
}

const mt::SampleBank& flash() { return theBank; }

void reset() { answered = false; }

mt::BankResult writable() {
  if (!card::ready()) return BankResult::NoSd;
  return working || migrating ? BankResult::Busy : BankResult::Ok;  // the jobs' card files are shared
}

mt::BankResult writeFrames(uint32_t frames, uint32_t rate, mt::FrameFill fill, void* ctx, mt::ImportOut& o,
                           mt::BankProgress keep, void* keepCtx) {
  const BankResult w = writable();
  if (w != BankResult::Ok) return w;
  const BankResult r = mt::storeWriteFrames(disk, frames, rate, fill, ctx, o, keep, keepCtx);
  if (r == BankResult::Ok) stream::add(o.crc);
  return r;
}

bool bgStep() {
  if (migrating) {
    const BankResult r = store.step();
    if (r != BankResult::Running) migrateDone(r);
    return true;
  }
  if (!working) return false;
  if (storeActive) {
    const BankResult r = store.step();
    if (r != BankResult::Running) {
      storeActive = false;
      if (curType == static_cast<uint8_t>(Msg::BankImport)) storeImportDone(r);
      else storeSetsDone(r);
      return true;
    }
  } else if (jobActive) {
    const BankResult r = job.step();
    if (r != BankResult::Running) {
      jobDone(r);
      return true;
    }
  } else {
    return false;
  }
  const uint32_t now = millis();
  if (now - lastProgress >= kProgressMs) {
    lastProgress = now;
    jobProgress();
  }
  return true;
}

}  // namespace bank
