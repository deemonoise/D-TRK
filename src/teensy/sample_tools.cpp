#include "sample_tools.h"
#include <Arduino.h>
#include <SD.h>
#include <new>
#include <string.h>
#include "bank.h"
#include "bank_ops.h"
#include "fs_server.h"
#include "link_server.h"
#include "onset.h"
#include "sample_store.h"
#include "stream.h"
#include "wave_peaks.h"
#include "wt_make.h"

using namespace mt::link;
using mt::BankResult;

namespace tools {

namespace {

constexpr uint32_t kProgressMs = 200;
constexpr int kPeakCols = 8;          // WavePeaks columns a background portion (with an overview)
constexpr uint32_t kScanFrames = 1024;  // Onsets: frames fed a background portion (2 KB)

// Onsets of the last sample asked for: the ESP fetches them in pieces (and retries).
DMAMEM mt::Onset onsets[mt::kMaxOnsets];
int nOnsets = -1;  // -1 = none computed
uint32_t onsCrc = 0, onsFrames = 0;

template <class M>
int put(const M& m, uint8_t* p) {
  Writer w(p, kMaxPayload);
  encode(m, w);
  return w.ok() ? w.size() : 0;
}

uint8_t res(BankResult r) { return static_cast<uint8_t>(r); }

// A store sample's overview (<crc>.pk) and frames (<crc>.wav) through SdFat files.
class StoreReader final : public mt::PeakReader {
 public:
  FsFile wav, pk;
  bool open(uint32_t crc) {
    close();
    if (!card::ready()) return false;
    char path[mt::kStorePathMax];
    mt::storePath(crc, path);
    wav = SD.sdfs.open(path, O_RDONLY);
    mt::storePeaksPath(crc, path);
    pk = SD.sdfs.open(path, O_RDONLY);
    return wav.isOpen();
  }
  void close() {
    if (wav) wav.close();
    if (pk) pk.close();
  }
  bool pairs(uint32_t first, uint32_t n, int8_t* out) override {
    if (pk) return pk.seekSet(static_cast<uint64_t>(first) * 2) && pk.read(out, n * 2) == static_cast<int>(n * 2);
    // No overview: the blocks' frames (slow; one column a portion then).
    DMAMEM static int16_t blk[mt::kPeakBlock];
    for (uint32_t i = 0; i < n; ++i) {
      const uint32_t at = (first + i) * mt::kPeakBlock;
      const uint32_t len = at + mt::kPeakBlock <= total ? mt::kPeakBlock : (at < total ? total - at : 0);
      if (!len || !frames(at, len, blk)) return false;
      mt::peakOverview(blk, len, out + 2 * i);
    }
    return true;
  }
  bool frames(uint32_t first, uint32_t n, int16_t* out) override {
    return wav.seekSet(mt::kStoreDataOffset + static_cast<uint64_t>(first) * 2) &&
           wav.read(out, n * 2) == static_cast<int>(n * 2);
  }
  uint32_t total = 0;
};
StoreReader rd;

// The request being answered later (one at a time: a new one replaces it, the ESP gave up on it).
enum class Task : uint8_t { None, Peaks, Onsets, WtRender };
Task task = Task::None;
uint8_t taskType = 0, taskSeq = 0;
uint32_t lastProgress = 0;
// The last deferred reply, for a retry of it.
bool answered = false;
uint8_t ansType = 0, ansSeq = 0;
int ansLen = 0;
DMAMEM uint8_t ansBuf[kMaxPayload];

WavePeaksReq peaksQ;
DMAMEM WavePeaksRep peaksM;
int peaksDone = 0;
uint32_t peaksFrames = 0;

OnsetsReq onsQ;
alignas(mt::OnsetScan) DMAMEM uint8_t scanMem[sizeof(mt::OnsetScan)];
mt::OnsetScan* scan = nullptr;
uint32_t scanAt = 0;
uint32_t scanCrc = 0, scanFrames = 0;

// WtRender: the maker (its buffers malloc'ed in RAM2 for the job), the sample in memory or on the card.
WtRenderReq wtQ;
alignas(mt::WtMaker) DMAMEM uint8_t makerMem[sizeof(mt::WtMaker)];
mt::WtMaker* maker = nullptr;
const int16_t* wtWhole = nullptr;

void endTask() {
  task = Task::None;
  rd.close();
  if (scan) {
    scan->~OnsetScan();
    scan = nullptr;
  }
  if (maker) {
    maker->~WtMaker();
    maker = nullptr;
  }
}

void answerTask(const uint8_t* p, int n);

int wtReply(BankResult r, mt::WtMakeErr e, int pitched, uint8_t* p) {
  WtRenderRep m;
  m.result = res(r);
  m.err = static_cast<uint8_t>(e);
  m.pitched = static_cast<uint8_t>(pitched < 0 ? 0 : (pitched > 255 ? 255 : pitched));
  return put(m, p);
}

// Only /wavetables/<name>.wav (no ".." or deeper folders).
bool wtPathOk(const char* path) {
  const size_t n = strlen(path);
  return strncmp(path, "/wavetables/", 12) == 0 && n > 16 && n + 5 <= sizeof wtQ.path &&
         strstr(path, "..") == nullptr && strchr(path + 12, '/') == nullptr && strcmp(path + n - 4, ".wav") == 0;
}

struct SdSink final : mt::ByteSink {
  FsFile* f;
  bool write(const void* d, size_t n) override { return f->write(d, n) == n; }
};

// The finished table to path (through path.tmp).
BankResult wtWrite(const int16_t* src) {
  if (!card::ready()) return BankResult::NoSd;
  char tmp[sizeof wtQ.path + 4];
  snprintf(tmp, sizeof tmp, "%s.tmp", wtQ.path);
  SD.sdfs.mkdir("/wavetables");
  SD.sdfs.remove(tmp);
  FsFile f = SD.sdfs.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return BankResult::WriteFail;
  SdSink sink;
  sink.f = &f;
  const bool ok = mt::wtWavWrite(src, sink);
  f.close();
  if (!ok) {
    SD.sdfs.remove(tmp);
    return BankResult::WriteFail;
  }
  SD.sdfs.remove(wtQ.path);
  return SD.sdfs.rename(tmp, wtQ.path) ? BankResult::Ok : BankResult::WriteFail;
}

int wtRender(Reader& r, uint8_t* p) {
  if (!decode(r, wtQ) || !wtPathOk(wtQ.path)) return wtReply(BankResult::OpenFail, mt::WtMakeErr::Ok, 0, p);
  const BankResult w = bank::writable();
  if (w != BankResult::Ok) return wtReply(w, mt::WtMakeErr::Ok, 0, p);
  uint32_t crc, frames, rate;
  const int16_t* whole;
  if (!bank::sampleAccess(wtQ.index, crc, frames, rate, whole))
    return wtReply(BankResult::OpenFail, mt::WtMakeErr::Ok, 0, p);
  if (wtQ.to > frames) wtQ.to = frames;
  if (!whole) {
    if (!rd.open(crc)) return wtReply(BankResult::OpenFail, mt::WtMakeErr::Ok, 0, p);
    rd.total = frames;
  }
  wtWhole = whole;
  maker = new (makerMem) mt::WtMaker();
  mt::WtMakeParams mp;
  mp.mode = static_cast<mt::WtMode>(wtQ.mode);
  mp.norm = static_cast<mt::WtNorm>(wtQ.norm);
  mp.seam = wtQ.seam != 0;
  const mt::WtMakeErr e = maker->begin(mp, wtQ.from, wtQ.to, rate);
  if (e != mt::WtMakeErr::Ok) {
    const int n = wtReply(e == mt::WtMakeErr::NoMemory ? BankResult::NoMemory : BankResult::Ok, e, 0, p);
    endTask();
    return n;
  }
  task = Task::WtRender;
  return -1;
}

// A portion of the maker: one read (<= 2 KB) or one step; the WAV written when done.
bool wtStep(uint8_t* p) {
  uint32_t at, n;
  if (maker->nextRead(at, n)) {
    DMAMEM static int16_t buf[mt::kWtMakeRead];
    if (wtWhole) maker->feed(wtWhole + at, n);
    else if (rd.frames(at, n, buf)) maker->feed(buf, n);
    else {
      answerTask(p, wtReply(BankResult::ReadFail, mt::WtMakeErr::Ok, 0, p));
      return false;
    }
  } else {
    maker->step();
  }
  if (!maker->done()) return true;
  const mt::WtMakeErr e = maker->error();
  const int pitched = maker->pitched();
  BankResult r = BankResult::Ok;
  if (e == mt::WtMakeErr::Ok) r = wtWrite(maker->result());
  else if (e == mt::WtMakeErr::NoMemory) r = BankResult::NoMemory;
  answerTask(p, wtReply(r, e, pitched, p));
  return false;
}

void answerTask(const uint8_t* p, int n) {
  answered = true;
  ansType = taskType;
  ansSeq = taskSeq;
  ansLen = n;
  memcpy(ansBuf, p, n);
  link::reply(static_cast<Msg>(taskType), taskSeq, p, n);
  endTask();
}

void progress(uint32_t done, uint32_t total) {
  Progress pr;
  pr.op = taskType;
  pr.done = done;
  pr.total = total;
  link::reply(Msg::Progress, taskSeq, pr);
}

int onsetPiece(uint8_t skip, uint8_t* p) {
  static OnsetsRep m;
  m = OnsetsRep();
  m.total = static_cast<uint8_t>(nOnsets);
  for (int i = skip; i < nOnsets && m.n < OnsetsRep::kMax; ++i, ++m.n) {
    m.pos[m.n] = onsets[i].pos;
    m.strength[m.n] = onsets[i].strength;
  }
  return put(m, p);
}

int peaks(Reader& r, uint8_t* p) {
  peaksM = WavePeaksRep();
  WavePeaksReq q;
  if (!decode(r, q) || q.cols > WavePeaksRep::kMaxCols || !q.width) {
    peaksM.result = res(BankResult::ReadFail);
    return put(peaksM, p);
  }
  uint32_t crc, frames, rate;
  const int16_t* whole;
  if (!bank::sampleAccess(q.index, crc, frames, rate, whole)) {
    peaksM.result = res(BankResult::OpenFail);
    return put(peaksM, p);
  }
  peaksM.cols = q.cols;
  if (whole) {
    mt::wavePeaks(whole, frames, q.col0, q.span, q.width, q.cols, peaksM.mn, peaksM.mx);
    return put(peaksM, p);
  }
  if (!rd.open(crc)) {
    peaksM.cols = 0;
    peaksM.result = res(BankResult::OpenFail);
    return put(peaksM, p);
  }
  rd.total = frames;
  peaksQ = q;
  peaksDone = 0;
  peaksFrames = frames;
  task = Task::Peaks;
  return -1;
}

int onsetList(Reader& r, uint8_t* p) {
  OnsetsReq q;
  if (!decode(r, q)) {
    static OnsetsRep m;
    m = OnsetsRep();
    m.result = res(BankResult::ReadFail);
    return put(m, p);
  }
  uint32_t crc, frames, rate;
  const int16_t* whole;
  if (!bank::sampleAccess(q.index, crc, frames, rate, whole)) {
    static OnsetsRep m;
    m = OnsetsRep();
    m.result = res(BankResult::OpenFail);
    return put(m, p);
  }
  if (nOnsets >= 0 && crc == onsCrc && frames == onsFrames) return onsetPiece(q.skip, p);
  if (whole) {
    nOnsets = mt::detectOnsets(whole, frames, rate, onsets, mt::kMaxOnsets);
    onsCrc = crc;
    onsFrames = frames;
    return onsetPiece(q.skip, p);
  }
  if (!rd.open(crc)) {
    static OnsetsRep m;
    m = OnsetsRep();
    m.result = res(BankResult::OpenFail);
    return put(m, p);
  }
  rd.total = frames;
  onsQ = q;
  scan = new (scanMem) mt::OnsetScan(frames, rate, mt::kMaxOnsets);
  scanAt = 0;
  scanCrc = crc;
  scanFrames = frames;
  task = Task::Onsets;
  return -1;
}

// One portion of the running task: a few WavePeaks columns, or 2 KB of frames for the onset scan.
bool taskStep() {
  if (task == Task::None) return false;
  uint8_t p[kMaxPayload];
  if (task == Task::WtRender) {
    if (!wtStep(p)) return true;
  } else if (task == Task::Peaks) {
    const int left = peaksQ.cols - peaksDone;
    const int k = left < (rd.pk ? kPeakCols : 1) ? left : (rd.pk ? kPeakCols : 1);
    if (k > 0 && !mt::wavePeaksRead(rd, peaksFrames, peaksQ.col0 + peaksDone, peaksQ.span, peaksQ.width, k,
                                    peaksM.mn + peaksDone, peaksM.mx + peaksDone)) {
      peaksM = WavePeaksRep();
      peaksM.result = res(BankResult::ReadFail);
      answerTask(p, put(peaksM, p));
      return true;
    }
    peaksDone += k;
    if (peaksDone >= peaksQ.cols) {
      answerTask(p, put(peaksM, p));
      return true;
    }
  } else {
    DMAMEM static int16_t buf[kScanFrames];
    const uint32_t n = scanFrames - scanAt < kScanFrames ? scanFrames - scanAt : kScanFrames;
    if (n && !rd.frames(scanAt, n, buf)) {
      static OnsetsRep m;
      m = OnsetsRep();
      m.result = res(BankResult::ReadFail);
      answerTask(p, put(m, p));
      return true;
    }
    scan->feed(buf, n);
    scanAt += n;
    if (scanAt >= scanFrames) {
      nOnsets = scan->finish(onsets);
      onsCrc = scanCrc;
      onsFrames = scanFrames;
      answerTask(p, onsetPiece(onsQ.skip, p));
      return true;
    }
  }
  const uint32_t now = millis();
  if (now - lastProgress >= kProgressMs) {
    lastProgress = now;
    if (task == Task::Peaks) progress(static_cast<uint32_t>(peaksDone), peaksQ.cols);
    else if (task == Task::WtRender) progress(static_cast<uint32_t>(maker->progress()), mt::kWtFrames);
    else progress(scanAt, scanFrames);
  }
  return true;
}

struct Bg final : mt::BgWork {
  bool step() override { return taskStep(); }
} bgWork;

}  // namespace

void begin() { stream::addBackground(&bgWork); }

void reset() {
  endTask();
  answered = false;
}

bool isRequest(Msg t) { return t == Msg::WavePeaks || t == Msg::Onsets || t == Msg::WtRender; }

int handle(Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out) {
  const uint8_t type = static_cast<uint8_t>(t);
  if (task != Task::None && taskType == type && taskSeq == seq) {  // a retry of the running one
    progress(0, 0);
    return -1;
  }
  if (answered && ansType == type && ansSeq == seq) {  // a retry: its reply got lost
    memcpy(out, ansBuf, ansLen);
    return ansLen;
  }
  endTask();
  Reader r(p, n);
  taskType = type;
  taskSeq = seq;
  lastProgress = millis();
  if (t == Msg::WtRender) return wtRender(r, out);
  return t == Msg::WavePeaks ? peaks(r, out) : onsetList(r, out);
}

}  // namespace tools
