#include "render_server.h"
#include <Arduino.h>
#include <SD.h>
#include <new>
#include <string.h>
#include "audio_out.h"
#include "bank.h"
#include "fs_server.h"
#include "link_server.h"
#include "preview_stream.h"
#include "render.h"
#include "render_link.h"
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
  return m;
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
  if (file) file.close();
  free(buf);
  buf = nullptr;
  bufFrames = 0;
  running = false;
  out->park(false);
  card::spaceChanged();
}

void abortRender() {
  if (!running) return;
  if (file) file.close();
  SD.sdfs.remove(tmp);
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

RenderResult start(Reader& r) {
  RenderStartReq q;
  if (!decode(r, q)) return RenderResult::OpenFail;
  abortRender();
  if (!card::ready()) return RenderResult::NoSd;
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
  preview::stop();
  out->park(true);  // the interrupt leaves the synth alone from the next block on
  synth->reset();
  feeder->reset();
  bufFrames = 0;
  running = true;
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
RenderResult endFile(bool normalize, uint32_t frames, uint32_t& crc) {
  crc = 0;
  const int16_t peak = normalize ? feeder->peak() : 0;
  for (uint32_t at = 0; at < frames;) {
    const uint32_t n = frames - at < kBufFrames ? frames - at : kBufFrames;
    const uint64_t pos = mt::kWavHeaderBytes + static_cast<uint64_t>(at) * 4;
    if (!file.seekSet(pos) || file.read(buf, n * 4) != static_cast<int>(n * 4)) return RenderResult::ReadFail;
    if (peak > 0) {
      mt::normalizePeak(buf, n, peak);
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
    mt::normalizeSamples(o, k, bf.peak);
    o += k;
    at += k;
    n -= k;
  }
  return true;
}

RenderResult endBank(bool normalize, uint32_t frames, mt::ImportOut& o, mt::BankResult& br) {
  BankFill bf{normalize ? feeder->monoPeak() : static_cast<int16_t>(0), false};
  br = bank::writeFrames(frames, mt::kSynthRate, fillBank, &bf, o, keepalive, nullptr);
  file.close();
  SD.sdfs.remove(tmp);
  if (br == mt::BankResult::Ok) return RenderResult::Ok;
  return bf.failed ? RenderResult::ReadFail : RenderResult::Bank;
}

RenderRep end(Reader& r, uint8_t seq) {
  RenderEndReq q;
  if (!decode(r, q)) return rep(RenderResult::ReadFail);
  if (q.abort) {
    RenderRep m = rep(RenderResult::Ok);
    abortRender();
    return m;
  }
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
      res = endBank(q.normalize != 0, frames, o, br);
      m.bank = static_cast<uint8_t>(br);
      m.crc = o.crc;
      m.frames = o.frames;
      m.peak = static_cast<uint16_t>(feeder->monoPeak());
    } else {
      res = endFile(q.normalize != 0, frames, m.crc);
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

bool isRequest(Msg t) { return t == Msg::RenderStart || t == Msg::RenderBlocks || t == Msg::RenderEnd; }

int handle(Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* o) {
  const uint8_t type = static_cast<uint8_t>(t);
  if (haveLast && lastType == type && lastSeq == seq) {  // a retry: its reply got lost
    memcpy(o, lastRep, lastLen);
    return lastLen;
  }
  Reader r(p, n);
  RenderRep m;
  if (t == Msg::RenderStart) {
    m = rep(start(r));
  } else if (!running) {
    m = rep(RenderResult::NoRender);
  } else if (t == Msg::RenderBlocks) {
    static RenderBlocks b;
    RenderResult res = decode(r, b) ? feeder->apply(b, writeBlock, nullptr) : RenderResult::ReadFail;
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
