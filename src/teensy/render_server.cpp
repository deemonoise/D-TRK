#include "render_server.h"
#include <Arduino.h>
#include <SD.h>
#include <new>
#include <string.h>
#include "audio_out.h"
#include "bank.h"
#include "export.h"
#include "fs_server.h"
#include "link_server.h"
#include "preview_stream.h"
#include "psram.h"
#include "render.h"
#include "render_link.h"
#include "stream.h"
#include "sample_set.h"
#include "synth.h"
#include "wav.h"

using namespace mt::link;

namespace render {

namespace {

constexpr const char* kBankTmp = "/samples/render/resample.tmp";
constexpr uint32_t kBufFrames = 4096;  // stereo frames of the write buffer (16 KB, from the RAM2 heap)
constexpr uint32_t kProgressMs = 100;

mt::Synth* synth = nullptr;
SynthStream* out = nullptr;
alignas(mt::RenderFeeder) uint8_t feederMem[sizeof(mt::RenderFeeder)];
mt::RenderFeeder* feeder = nullptr;

bool running = false;
uint8_t target = 0;
char path[sizeof(RenderStartReq::path)] = {};
char tmp[sizeof(RenderStartReq::path) + 4] = {};
FsFile file;
int16_t* buf = nullptr;  // interleaved L, R
uint32_t bufFrames = 0;  // filled
uint32_t lastMs = 0;

// Export (target kExport): the files on the card, the writer, its buffers. Write buffers: the PERF ring in
// PSRAM (PERF detached for the export) or the RAM2 heap; tap buffers always from the RAM2 heap (the synth
// writes them every sample).
constexpr uint32_t kPsramCap = 48 * 1024;  // per file, from the PERF ring
constexpr uint32_t kRam2Cap = 10 * 1024;   // per file, malloc
constexpr int kRam2MaxOut = 6;

struct SdIo final : mt::ExportIo {
  FsFile f[mt::kExportMaxOut];
  bool open(int i, const char* p, uint64_t prealloc) override {
    f[i] = SD.sdfs.open(p, O_RDWR | O_CREAT | O_TRUNC);
    if (!f[i]) return false;
    if (prealloc) f[i].preAllocate(prealloc);  // a hint: a fragmented FAT32 may refuse it
    return true;
  }
  bool write(int i, const void* d, uint32_t n) override { return f[i].write(d, n) == n; }
  bool seal(int i, uint64_t size, const void* hdr, uint32_t n) override {
    const bool ok = f[i].truncate(size) && f[i].seekSet(0) && f[i].write(hdr, n) == n;
    return f[i].close() && ok;
  }
  void close(int i) override {
    if (f[i]) f[i].close();
  }
  bool remove(const char* p) override { return SD.sdfs.remove(p); }
  bool rename(const char* a, const char* b) override {
    if (SD.sdfs.exists(b)) SD.sdfs.remove(b);
    return SD.sdfs.rename(a, b);
  }
};

struct ExportMem {
  SdIo io;
  mt::ExportWriter w{io};
};

ExportMem* ex = nullptr;
uint8_t* exBuf = nullptr;    // malloc'd write buffers (nullptr: the PERF ring)
float* exTaps = nullptr;     // tap buffers
int16_t* exRing = nullptr;   // the detached PERF ring
uint32_t exRingFrames = 0;
uint32_t exEst = 0;          // expected frames (preallocation)
uint8_t exBits = 24;

// The last reply, again for a request repeated with the same seq (its reply got lost).
uint8_t lastType = 0, lastSeq = 0;
bool haveLast = false;
int lastLen = 0;
uint8_t lastRep[kMaxPayload];

// Progress of a long RenderEnd.
uint8_t endSeq = 0;
uint32_t lastProgress = 0;

int put(const RenderRep& m, uint8_t* p) {
  Writer w(p, kMaxPayload);
  encode(m, w);
  return w.ok() ? w.size() : 0;
}

RenderRep rep(RenderResult r) {
  RenderRep m;
  m.result = static_cast<uint8_t>(r);
  m.blocks = feeder ? feeder->blocks() : 0;
  m.quiet = feeder ? feeder->blocks() - feeder->loudBlocks() : 0;
  return m;
}

// One-shot bounce (RenderStartReq::kSrcPreview): the preview track's tap is the render's source.
struct PrevTap {
  mt::RenderTaps t;
  float l[mt::Synth::kBlock], r[mt::Synth::kBlock];
};
PrevTap* prevTap = nullptr;  // malloc'd for the render

void freePrevTap() {
  if (!prevTap) return;
  synth->setTaps(nullptr);
  feeder->setTap(nullptr, 0);
  free(prevTap);
  prevTap = nullptr;
}

// The export's memory back: taps off, buffers freed, the PERF ring reattached (cleared).
void freeExport() {
  if (!ex) return;
  synth->setTaps(nullptr);
  delete ex;
  ex = nullptr;
  free(exBuf);
  exBuf = nullptr;
  free(exTaps);
  exTaps = nullptr;
  if (exRing && psram::state() == psram::State::On) synth->setPerfBuffer(exRing, exRingFrames);
  exRing = nullptr;
}

void keepalive(uint32_t done, uint32_t total, void*) {
  const uint32_t now = millis();
  if (now - lastProgress >= kProgressMs) {
    lastProgress = now;
    Progress pr;
    pr.op = static_cast<uint8_t>(Msg::RenderEnd);
    pr.done = done;
    pr.total = total;
    link::reply(Msg::Progress, endSeq, pr);
  }
  link::statusIfDue();
}

// The render ends either way: file closed, buffer freed, the live output back (the synth reset by it).
void finish() {
  freeExport();
  freePrevTap();
  if (file) file.close();
  free(buf);
  buf = nullptr;
  bufFrames = 0;
  running = false;
  stream::pool().sync = nullptr;
  out->park(false);
  card::spaceChanged();
}

void abortRender() {
  if (!running) return;
  if (ex) {
    ex->w.abort();
  } else {
    if (file) file.close();
    SD.sdfs.remove(tmp);
  }
  finish();
}

bool flushBuf() {
  if (!bufFrames) return true;
  const size_t bytes = bufFrames * 4;
  bufFrames = 0;
  return file.write(buf, bytes) == bytes;
}

bool writeBlock(const int16_t* l, const int16_t* r, void*) {
  for (int i = 0; i < mt::Synth::kBlock; ++i) {
    buf[2 * bufFrames] = l[i];
    buf[2 * bufFrames + 1] = r[i];
    ++bufFrames;
  }
  return bufFrames + mt::Synth::kBlock <= kBufFrames || flushBuf();
}

// The folder of p (made with its parents when missing).
bool makeParent(const char* p) {
  char dir[sizeof tmp];
  strncpy(dir, p, sizeof dir - 1);
  dir[sizeof dir - 1] = 0;
  char* slash = strrchr(dir, '/');
  if (!slash || slash == dir) return true;
  *slash = 0;
  return SD.sdfs.exists(dir) || SD.sdfs.mkdir(dir, true);
}

bool exportBlock(const int16_t*, const int16_t*, void*) { return ex->w.block(*synth); }

// The live output parked, the synth reset: the render starts at block 0.
void beginRender() {
  preview::stop();
  out->park(true);  // the interrupt leaves the synth alone from the next block on
  stream::pool().sync = &stream::reader();  // a missed block loads at once: the render equals a RAM one
  synth->reset();
  feeder->reset();
  bufFrames = 0;
  running = true;
}

// kExport: the folder made, the buffers taken (outMax files at most), the files come with RenderOut.
RenderResult startExport(const RenderStartReq& q, uint8_t& outMax) {
  if (strncmp(q.path, "/export/", 8) != 0) return RenderResult::OpenFail;
  if (!SD.sdfs.exists(q.path) && !SD.sdfs.mkdir(q.path, true)) return RenderResult::WriteFail;
  int want = q.outs < 1 ? 1 : (q.outs > mt::kExportMaxOut ? mt::kExportMaxOut : q.outs);
  uint32_t ringFrames = 0;
  int16_t* ring = psram::perfBuffer(ringFrames);
  uint8_t* mem = nullptr;
  uint32_t cap = 0;
  int n = want;
  if (ring) {
    const uint32_t per = ringFrames * 4u / static_cast<uint32_t>(n) / 512u * 512u;
    cap = per < kPsramCap ? per : kPsramCap;
    mem = reinterpret_cast<uint8_t*>(ring);
  } else {
    if (n > kRam2MaxOut) n = kRam2MaxOut;
    cap = kRam2Cap;
    while (n > 0 && !(exBuf = static_cast<uint8_t*>(malloc(static_cast<size_t>(n) * cap)))) --n;
    mem = exBuf;
  }
  while (n > 0 && !(exTaps = static_cast<float*>(malloc(static_cast<size_t>(n) * 2 * mt::Synth::kBlock * 4)))) --n;
  ex = n > 0 ? new (std::nothrow) ExportMem : nullptr;
  if (!ex || !mem) {
    delete ex;
    ex = nullptr;
    free(exBuf);
    exBuf = nullptr;
    free(exTaps);
    exTaps = nullptr;
    return RenderResult::WriteFail;
  }
  exBits = q.bits == 16 ? 16 : 24;
  exEst = q.estFrames;
  ex->w.begin(q.path, exBits, q.skipFrames, mem, cap, exTaps, n);
  outMax = static_cast<uint8_t>(n);
  target = q.target;
  strcpy(path, q.path);
  beginRender();  // parked first: the interrupt no longer touches the synth
  if (ring) {
    exRing = ring;
    exRingFrames = ringFrames;
    synth->setPerfBuffer(nullptr, 0, false);  // the ring holds the files now
  }
  synth->setTaps(&ex->w.taps());
  return RenderResult::Ok;
}

RenderResult start(Reader& r, uint8_t& outMax) {
  RenderStartReq q;
  if (!decode(r, q)) return RenderResult::OpenFail;
  abortRender();
  if (!card::ready()) return RenderResult::NoSd;
  if (q.target == RenderStartReq::kExport) return startExport(q, outMax);
  if (q.target == RenderStartReq::kBank) {
    if (bank::writable() != mt::BankResult::Ok) return RenderResult::Busy;
    strcpy(tmp, kBankTmp);
  } else {
    if (!q.path[0] || q.path[0] != '/') return RenderResult::OpenFail;
    snprintf(tmp, sizeof tmp, "%s.tmp", q.path);
  }
  target = q.target;
  strcpy(path, q.path);
  if (!makeParent(tmp)) return RenderResult::WriteFail;
  buf = static_cast<int16_t*>(malloc(kBufFrames * 4));
  if (!buf) return RenderResult::WriteFail;
  file = SD.sdfs.open(tmp, O_RDWR | O_CREAT | O_TRUNC);
  uint8_t hdr[mt::kWavHeaderBytes] = {0};
  if (!file || file.write(hdr, sizeof hdr) != sizeof hdr) {
    if (file) {
      file.close();
      SD.sdfs.remove(tmp);
    }
    free(buf);
    buf = nullptr;
    return RenderResult::WriteFail;
  }
  beginRender();
  if (q.src == RenderStartReq::kSrcPreview) {
    prevTap = static_cast<PrevTap*>(malloc(sizeof(PrevTap)));
    if (!prevTap) {
      abortRender();
      return RenderResult::WriteFail;
    }
    new (&prevTap->t) mt::RenderTaps();
    prevTap->t.mask = 1u << mt::RenderTaps::kPrev;
    prevTap->t.l[mt::RenderTaps::kPrev] = prevTap->l;
    prevTap->t.r[mt::RenderTaps::kPrev] = prevTap->r;
    synth->setTaps(&prevTap->t);
    feeder->setTap(&prevTap->t, mt::RenderTaps::kPrev);
  }
  return RenderResult::Ok;
}

// Kept frames: past the last loud block when trimming (one block at least), else all.
uint32_t keptFrames(bool trim) {
  const uint32_t all = feeder->blocks() * mt::Synth::kBlock;
  if (!trim) return all;
  const uint32_t loud = target == RenderStartReq::kBank ? feeder->loudBlocksMono() : feeder->loudBlocks();
  const uint32_t keep = (loud ? loud : 1) * mt::Synth::kBlock;
  return keep < all ? keep : all;
}

// The file target: the kept frames read back (scaled in place when normalizing), the mono mix's crc
// for "mtcr", the header, then <path>.tmp renamed to path.
// normalize (RenderEndReq): 0 off, 1 -1 dBFS, 2 -0.3 dBFS.
float normTarget(uint8_t normalize) { return normalize == 2 ? mt::kNormPeakHot : mt::kNormPeak; }

RenderResult endFile(uint8_t normalize, uint32_t frames, uint32_t& crc) {
  crc = 0;
  const int16_t peak = normalize ? feeder->peak() : 0;
  for (uint32_t at = 0; at < frames;) {
    const uint32_t n = frames - at < kBufFrames ? frames - at : kBufFrames;
    const uint64_t pos = mt::kWavHeaderBytes + static_cast<uint64_t>(at) * 4;
    if (!file.seekSet(pos) || file.read(buf, n * 4) != static_cast<int>(n * 4)) return RenderResult::ReadFail;
    if (peak > 0) {
      mt::normalizePeak(buf, n, peak, normTarget(normalize));
      if (!file.seekSet(pos) || file.write(buf, n * 4) != n * 4) return RenderResult::WriteFail;
    }
    for (uint32_t i = 0; i < n; ++i) buf[i] = static_cast<int16_t>((buf[2 * i] + buf[2 * i + 1]) / 2);
    crc = mt::sampleCrc(buf, n, crc);
    at += n;
    keepalive(at, frames, nullptr);
  }
  uint8_t hdr[mt::kWavHeaderBytes];
  mt::wavHeader(hdr, frames, mt::kSynthRate, 60, crc, 2);
  if (!file.truncate(mt::kWavHeaderBytes + static_cast<uint64_t>(frames) * 4) || !file.seekSet(0) ||
      file.write(hdr, sizeof hdr) != sizeof hdr)
    return RenderResult::WriteFail;
  if (!file.close()) return RenderResult::WriteFail;
  if (SD.sdfs.exists(path)) SD.sdfs.remove(path);
  return SD.sdfs.rename(tmp, path) ? RenderResult::Ok : RenderResult::WriteFail;
}

// The bank target: the mono mix of the kept frames, normalized to its own peak.
struct BankFill {
  int16_t peak;
  bool failed;
  float target;
};

bool fillBank(int16_t* o, uint32_t at, uint32_t n, void* ctx) {
  BankFill& bf = *static_cast<BankFill*>(ctx);
  while (n) {
    const uint32_t k = n < kBufFrames ? n : kBufFrames;
    if (!file.seekSet(mt::kWavHeaderBytes + static_cast<uint64_t>(at) * 4) ||
        file.read(buf, k * 4) != static_cast<int>(k * 4)) {
      bf.failed = true;
      return false;
    }
    for (uint32_t i = 0; i < k; ++i) o[i] = static_cast<int16_t>((buf[2 * i] + buf[2 * i + 1]) / 2);
    mt::normalizeSamples(o, k, bf.peak, bf.target);
    o += k;
    at += k;
    n -= k;
  }
  return true;
}

RenderResult endBank(uint8_t normalize, uint32_t frames, mt::ImportOut& o, mt::BankResult& br) {
  BankFill bf{normalize ? feeder->monoPeak() : static_cast<int16_t>(0), false, normTarget(normalize)};
  br = bank::writeFrames(frames, mt::kSynthRate, fillBank, &bf, o, keepalive, nullptr);
  file.close();
  SD.sdfs.remove(tmp);
  if (br == mt::BankResult::Ok) return RenderResult::Ok;
  return bf.failed ? RenderResult::ReadFail : RenderResult::Bank;
}

// kExport: the buffers out, every file cut at q.frames, headers, renames; peaks / clips per file.
RenderRep endExport(const RenderEndReq& q) {
  RenderRep m = rep(RenderResult::Ok);
  const int n = ex->w.outs();
  m.outN = static_cast<uint8_t>(n);
  for (int i = 0; i < n; ++i) {
    m.peaks[i] = ex->w.peak(i);
    m.clipsN[i] = ex->w.clips(i);
    m.clips += m.clipsN[i];
    if (m.peaks[i] > m.peak) m.peak = m.peaks[i];
  }
  const uint32_t all = feeder->blocks() * mt::Synth::kBlock;
  m.frames = q.frames && q.frames < all ? q.frames : all;
  if (!ex->w.end(q.frames)) m.result = static_cast<uint8_t>(RenderResult::WriteFail);
  finish();
  return m;
}

RenderResult addOut(Reader& r) {
  RenderOut q;
  if (!decode(r, q) || !ex) return RenderResult::OpenFail;
  const uint64_t pre = mt::kWavPcmHeaderBytes + static_cast<uint64_t>(exEst) * (exBits / 8u) * 2u;
  return ex->w.add(q.idx, q.tap, q.name, pre) ? RenderResult::Ok : RenderResult::OpenFail;
}

RenderRep end(Reader& r, uint8_t seq) {
  RenderEndReq q;
  if (!decode(r, q)) return rep(RenderResult::ReadFail);
  if (q.abort) {
    RenderRep m = rep(RenderResult::Ok);
    abortRender();
    return m;
  }
  if (ex) return endExport(q);
  endSeq = seq;
  lastProgress = millis();
  RenderRep m = rep(RenderResult::Ok);
  m.clips = feeder->clips();
  RenderResult res = flushBuf() && file.sync() ? RenderResult::Ok : RenderResult::WriteFail;
  const uint32_t frames = keptFrames(q.trim != 0);
  if (res == RenderResult::Ok) {
    if (target == RenderStartReq::kBank) {
      mt::ImportOut o;
      mt::BankResult br = mt::BankResult::Ok;
      res = endBank(q.normalize, frames, o, br);
      m.bank = static_cast<uint8_t>(br);
      m.crc = o.crc;
      m.frames = o.frames;
      m.peak = static_cast<uint16_t>(feeder->monoPeak());
    } else {
      res = endFile(q.normalize, frames, m.crc);
      m.frames = frames;
      m.peak = static_cast<uint16_t>(feeder->peak());
    }
  }
  m.result = static_cast<uint8_t>(res);
  if (res != RenderResult::Ok) {
    if (file) file.close();
    SD.sdfs.remove(tmp);
  }
  finish();
  return m;
}

}  // namespace

void begin(mt::Synth& s, SynthStream& o) {
  synth = &s;
  out = &o;
  feeder = new (feederMem) mt::RenderFeeder(s);
}

bool isRequest(Msg t) {
  return t == Msg::RenderStart || t == Msg::RenderBlocks || t == Msg::RenderEnd || t == Msg::RenderOut;
}

int handle(Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* o) {
  const uint8_t type = static_cast<uint8_t>(t);
  if (haveLast && lastType == type && lastSeq == seq) {  // a retry: its reply got lost
    memcpy(o, lastRep, lastLen);
    return lastLen;
  }
  Reader r(p, n);
  RenderRep m;
  if (t == Msg::RenderStart) {
    uint8_t outMax = 0;
    m = rep(start(r, outMax));
    m.outMax = outMax;
  } else if (!running) {
    m = rep(RenderResult::NoRender);
  } else if (t == Msg::RenderOut) {
    m = rep(addOut(r));
    if (m.result != static_cast<uint8_t>(RenderResult::Ok)) abortRender();
  } else if (t == Msg::RenderBlocks) {
    static RenderBlocks b;
    RenderResult res = RenderResult::ReadFail;
    if (decode(r, b)) {
      res = feeder->apply(b, ex ? exportBlock : writeBlock, nullptr);
      // Export: one write per file per frame (the link's and the watchdog's timeouts); a PSRAM failure
      // (the watchdog) under the ring's buffers fails the export.
      if (ex && res == RenderResult::Ok &&
          (!ex->w.flush() || (exRing && psram::state() != psram::State::On)))
        res = RenderResult::WriteFail;
    }
    m = rep(res);
    if (res == RenderResult::WriteFail) abortRender();
  } else {
    m = end(r, seq);
  }
  lastMs = millis();
  const int len = put(m, o);
  haveLast = true;
  lastType = type;
  lastSeq = seq;
  lastLen = len;
  memcpy(lastRep, o, len);
  return len;
}

bool active() { return running; }

void reset() {
  if (running) link::log("render: dropped (ESP restarted)");
  abortRender();
  haveLast = false;
}

void step() {
  if (running && millis() - lastMs >= kIdleMs) {
    link::log("render: dropped (no blocks for %lu s)", static_cast<unsigned long>(kIdleMs / 1000));
    abortRender();
  }
}

}  // namespace render
