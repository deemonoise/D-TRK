#include "bank_client.h"
#include <Arduino.h>
#include <string.h>
#include <strings.h>
#include "audio/audio.h"
#include "engine/engine.h"
#include "link.h"
#include "link_msg.h"
#include "model.h"
#include "project_io.h"
#include "sample_set.h"
#include "wt_builtin.h"
#include "wt_mip.h"

using namespace mt::link;

namespace audio {

namespace {

constexpr uint32_t kSyncWaitMs = 2000;

BankView view;
bool viewStale = true;
uint32_t viewFailAt = 0;  // the last fetch got no answer: not again for kRetryMs (the UI asks every frame)
constexpr uint32_t kRetryMs = 1000;
uint32_t viewKey = 0;  // crc of the lists + the synth's boot id the view was fetched for

// Small cache of wavetable frames (the picker / INST page draw a few, again and again).
struct WtCached {
  bool used = false;
  char name[17] = {};
  uint8_t frame = 0;
  uint32_t gen = 0;
  int8_t pts[WtFrameRep::kPoints];
};
constexpr int kWtCache = 6;
WtCached wtCache[kWtCache];
int wtNext = 0;

bool engineIdle() {
  const engine::Status s = engine::status();
  return !s.playing && !s.paused;
}

uint32_t listsKey() {
  const mt::Project* p = mirrorProject();
  if (!p) return 0;
  uint32_t k = mt::crc32(p->samples, sizeof p->samples);
  k = mt::crc32(p->wavetables, sizeof p->wavetables, k);
  k = mt::crc32(&p->sampleCount, sizeof p->sampleCount, k);
  k = mt::crc32(&p->wavetableCount, sizeof p->wavetableCount, k);
  const uint32_t boot = slink::bootId();
  return mt::crc32(&boot, sizeof boot, k);
}

// The synth board has the project's lists (its model is what its bank jobs go by).
bool waitSynced() {
  const uint32_t t0 = millis();
  for (;;) {
    pump();
    if (synced()) return true;
    if ((slink::synthUp() && !slink::versionOk()) || millis() - t0 >= kSyncWaitMs) return false;
    delay(5);
  }
}

BankResult result(uint8_t r) {
  return r <= static_cast<uint8_t>(BankResult::Running) ? static_cast<BankResult>(r) : BankResult::Link;
}

struct ProgCtx {
  BankProgressFn cb;
  BankItemFn itemCb;
  void* ctx;
};

void onProgress(const Progress& p, void* c) {
  const ProgCtx* pc = static_cast<const ProgCtx*>(c);
  if (pc->cb) pc->cb(p.done, p.total, pc->ctx);
  if (pc->itemCb) {
    const int item = p.item == Progress::kNoItem ? -1 : (p.item >= Progress::kWtItem ? kItemWt + p.item - Progress::kWtItem : p.item);
    pc->itemCb(item, p.done, p.total, pc->ctx);
  }
}

// A long request; reply holds its payload. Link when nothing came.
template <class Req>
BankResult longCall(Msg t, const Req& q, uint8_t* reply, int& len, BankProgressFn cb, void* ctx,
                    BankItemFn itemCb = nullptr) {
  uint8_t p[kMaxPayload];
  Writer w(p, sizeof p);
  encode(q, w);
  if (!w.ok()) return BankResult::OpenFail;
  ProgCtx pc{cb, itemCb, ctx};
  len = 0;
  const bool ok = slink::requestLong(t, p, w.size(), reply, len, onProgress, &pc);
  bankViewStale();
  return ok && len > 0 ? BankResult::Ok : BankResult::Link;
}

template <class Req>
bool shortCall(Msg t, const Req& q, uint8_t* reply, int& len, uint32_t timeoutMs = 300) {
  uint8_t p[kMaxPayload];
  Writer w(p, sizeof p);
  encode(q, w);
  len = 0;
  return w.ok() && slink::request(t, p, w.size(), t, reply, len, timeoutMs) && len > 0;
}

BankResult importRaw(const char* path, uint8_t kind, const uint32_t* knownCrc, BankImportRep& rep, BankProgressFn cb,
                     void* ctx) {
  if (!engineIdle()) return BankResult::Busy;
  if (!waitSynced()) return BankResult::Link;
  BankImportReq q;
  if (strlen(path) >= sizeof q.path) return BankResult::OpenFail;
  strcpy(q.path, path);
  q.kind = kind;
  q.hasCrc = knownCrc ? 1 : 0;
  q.crc = knownCrc ? *knownCrc : 0;
  uint8_t reply[kMaxPayload];
  int len = 0;
  BankResult r = longCall(Msg::BankImport, q, reply, len, cb, ctx);
  if (r != BankResult::Ok) return r;
  Reader rd(reply, len);
  if (!decode(rd, rep)) return len > 0 ? result(reply[0]) : BankResult::Link;
  return result(rep.result);
}

BankResult setsCall(Msg t, const char* name, BankSets* out, BankItemFn cb, void* ctx) {
  if (!engineIdle() && t == Msg::BankSync) return BankResult::Busy;
  if (!waitSynced()) return BankResult::Link;
  BankProjectReq q;
  strncpy(q.project, name ? name : "", sizeof q.project - 1);
  uint8_t reply[kMaxPayload];
  int len = 0;
  const BankResult r = longCall(t, q, reply, len, nullptr, ctx, cb);
  if (r != BankResult::Ok) return r;
  BankSetsRep rep;
  Reader rd(reply, len);
  if (!decode(rd, rep)) return result(reply[0]);
  if (out) {
    out->missing = rep.missing;
    out->failed = rep.failed;
    memcpy(out->samples, rep.samples, sizeof out->samples);
    memcpy(out->wavetables, rep.wavetables, sizeof out->wavetables);
  }
  return result(rep.result);
}

BankResult clearCall(uint8_t compact, BankClearRep& rep, BankProgressFn cb, void* ctx) {
  if (!engineIdle()) return BankResult::Busy;
  if (!waitSynced()) return BankResult::Link;
  BankClearReq q;
  q.compact = compact;
  uint8_t reply[kMaxPayload];
  int len = 0;
  const BankResult r = longCall(Msg::BankClear, q, reply, len, cb, ctx);
  if (r != BankResult::Ok) return r;
  Reader rd(reply, len);
  if (!decode(rd, rep)) return result(reply[0]);
  return result(rep.result);
}

}  // namespace

const char* bankResultText(BankResult r) {
  switch (r) {
    case BankResult::Ok: return "OK";
    case BankResult::Busy: return "STOP PLAYBACK FIRST";
    case BankResult::NoSd: return "NO SD CARD";
    case BankResult::NoBank: return "NO SAMPLE BANK";
    case BankResult::OpenFail: return "CANNOT OPEN FILE";
    case BankResult::ReadFail: return "READ ERROR";
    case BankResult::NotWav: return "NOT A WAV FILE";
    case BankResult::Unsupported: return "UNSUPPORTED WAV";
    case BankResult::Truncated: return "WAV TRUNCATED";
    case BankResult::Full: return "BANK FULL";
    case BankResult::WriteFail: return "FLASH WRITE ERROR";
    case BankResult::NoMemory: return "NO MEMORY";
    case BankResult::Link: return "SYNTH NOT ANSWERING";
    case BankResult::Running: return "BUSY";
  }
  return "?";
}

BankResult importToCache(const char* path, ImportOut& out, const uint32_t* knownCrc, BankProgressFn cb, void* ctx) {
  BankImportRep rep;
  const BankResult r = importRaw(path, 0, knownCrc, rep, cb, ctx);
  if (r != BankResult::Ok) return r;
  out.crc = rep.crc;
  out.frames = rep.frames;
  out.rate = rep.rate;
  out.root = rep.root;
  return r;
}

BankResult importWtToCache(const char* path, uint32_t& crc, const uint32_t* knownCrc, BankProgressFn cb, void* ctx) {
  BankImportRep rep;
  const BankResult r = importRaw(path, 1, knownCrc, rep, cb, ctx);
  if (r == BankResult::Ok) crc = rep.crc;
  return r;
}

BankResult syncProject(const char* name, BankSets* out, BankItemFn cb, void* ctx) {
  return setsCall(Msg::BankSync, name, out, cb, ctx);
}

BankResult saveAssets(const char* name, BankSets* out, BankItemFn cb, void* ctx) {
  return setsCall(Msg::AssetsSave, name, out, cb, ctx);
}

BankResult clearCache(int* removed) {
  BankClearRep rep;
  const BankResult r = clearCall(0, rep, nullptr, nullptr);
  if (removed) *removed = r == BankResult::Ok ? rep.removed : 0;
  return r;
}

BankResult compactBank(BankProgressFn cb, void* ctx) {
  BankClearRep rep;
  return clearCall(1, rep, cb, ctx);
}

const BankView& bankView() {
  const uint32_t key = listsKey();
  if (!viewStale && key == viewKey && view.ok) return view;
  if (viewFailAt && millis() - viewFailAt < kRetryMs) return view;
  view = BankView();
  viewFailAt = millis() | 1;
  if (!slink::synthUp() || !waitSynced()) return view;
  uint8_t reply[kMaxPayload];
  int len = 0;
  BankIndexRep rep;
  if (!slink::request(Msg::BankIndex, nullptr, 0, Msg::BankIndex, reply, len, 300) || len == 0) return view;
  Reader rd(reply, len);
  if (!decode(rd, rep)) return view;
  viewFailAt = 0;
  view.ok = true;
  viewStale = false;
  viewKey = key;
  view.mounted = rep.result == static_cast<uint8_t>(BankResult::Ok);
  if (!view.mounted) return view;
  view.count = rep.count;
  view.capacity = rep.capacity;
  view.free = rep.free;
  view.unused = rep.unused;
  view.gen = rep.gen;
  memcpy(view.samples, rep.samples, sizeof view.samples);
  memcpy(view.wavetables, rep.wavetables, sizeof view.wavetables);
  view.builtins = rep.builtins;
  memcpy(view.rate, rep.rate, sizeof view.rate);
  return view;
}

void bankViewStale() {
  viewStale = true;
  viewFailAt = 0;
}

BankResult sampleInfo(int index, SampleInfo& out) {
  out = SampleInfo();
  if (index < 0 || index >= mt::kProjSamples) return BankResult::OpenFail;
  if (!waitSynced()) return BankResult::Link;
  SampleInfoReq q;
  q.index = static_cast<uint8_t>(index);
  uint8_t reply[kMaxPayload];
  int len = 0;
  if (!shortCall(Msg::SampleInfo, q, reply, len)) return BankResult::Link;
  SampleInfoRep rep;
  Reader rd(reply, len);
  if (!decode(rd, rep)) return result(reply[0]);
  if (rep.result != 0) return result(rep.result);
  out.cached = rep.cached != 0;
  out.frames = rep.frames;
  out.rate = rep.rate;
  out.root = rep.root;
  out.loop = rep.loop;
  return BankResult::Ok;
}

bool wtCached(const char* name) {
  if (!name || !name[0]) return false;
  const BankView& v = bankView();
  if (!v.mounted) return false;
  if (mt::isWtBuiltin(name)) {
    for (int i = 0; i < mt::kWtBuiltins && i < 8; ++i)
      if (strcasecmp(mt::wtBuiltinName(i), name) == 0) return (v.builtins >> i) & 1;
    return false;
  }
  const mt::Project* p = mirrorProject();
  return p && v.wtCached(mt::projWtFind(*p, name));
}

bool wtFrame(const char* name, int f, int8_t* out) {
  if (!name || !name[0] || strlen(name) > 16) return false;
  const uint8_t frame = static_cast<uint8_t>(f < 0 ? 0 : (f > 63 ? 63 : f));
  if (!wtCached(name)) return false;  // no request for a missing one (the UI asks every frame)
  const BankView& v = bankView();
  for (const WtCached& c : wtCache)
    if (c.used && c.frame == frame && c.gen == viewKey + v.gen && strcmp(c.name, name) == 0) {
      memcpy(out, c.pts, sizeof c.pts);
      return true;
    }
  WtFrameReq q;
  q.frame = frame;
  strcpy(q.name, name);
  uint8_t reply[kMaxPayload];
  int len = 0;
  if (!shortCall(Msg::WtFrame, q, reply, len)) return false;
  WtFrameRep rep;
  Reader rd(reply, len);
  if (!decode(rd, rep) || rep.result != 0) return false;
  WtCached& c = wtCache[wtNext];
  wtNext = (wtNext + 1) % kWtCache;
  c.used = true;
  strcpy(c.name, name);
  c.frame = frame;
  c.gen = viewKey + v.gen;
  memcpy(c.pts, rep.pts, sizeof c.pts);
  memcpy(out, rep.pts, sizeof rep.pts);
  return true;
}

BankResult wavePeaks(int index, uint32_t col0, uint32_t span, int width, int cols, int8_t* mn, int8_t* mx) {
  if (index < 0 || index >= mt::kProjSamples || width <= 0 || width > 0xFFFF) return BankResult::OpenFail;
  if (!waitSynced()) return BankResult::Link;
  static WavePeaksRep rep;  // UI task only
  for (int done = 0; done < cols;) {
    WavePeaksReq q;
    q.index = static_cast<uint8_t>(index);
    q.col0 = col0 + done;
    q.span = span;
    q.width = static_cast<uint16_t>(width);
    q.cols = static_cast<uint8_t>(cols - done < WavePeaksRep::kMaxCols ? cols - done : WavePeaksRep::kMaxCols);
    uint8_t reply[kMaxPayload];
    int len = 0;
    if (!shortCall(Msg::WavePeaks, q, reply, len)) return BankResult::Link;
    Reader rd(reply, len);
    if (!decode(rd, rep)) return result(reply[0]);
    if (rep.result != 0) return result(rep.result);
    if (rep.cols != q.cols) return BankResult::Link;
    memcpy(mn + done, rep.mn, rep.cols);
    memcpy(mx + done, rep.mx, rep.cols);
    done += rep.cols;
  }
  return BankResult::Ok;
}

int onsets(int index, mt::Onset* out, int max, BankResult* err) {
  BankResult r = BankResult::Ok;
  int n = 0;
  if (index < 0 || index >= mt::kProjSamples) r = BankResult::OpenFail;
  else if (!waitSynced()) r = BankResult::Link;
  static OnsetsRep rep;  // UI task only
  while (r == BankResult::Ok && n < max) {
    OnsetsReq q;
    q.index = static_cast<uint8_t>(index);
    q.skip = static_cast<uint8_t>(n);
    uint8_t reply[kMaxPayload];
    int len = 0;
    // The first piece may wait for the detection on the whole sample.
    if (!shortCall(Msg::Onsets, q, reply, len, n ? 300 : 1000)) {
      r = BankResult::Link;
      break;
    }
    Reader rd(reply, len);
    if (!decode(rd, rep)) r = result(reply[0]);
    else if (rep.result != 0) r = result(rep.result);
    if (r != BankResult::Ok) break;
    for (int i = 0; i < rep.n && n < max; ++i, ++n) out[n] = mt::Onset{rep.pos[i], rep.strength[i]};
    if (!rep.n || n >= rep.total) break;
  }
  if (err) *err = r;
  return r == BankResult::Ok ? n : -1;
}

BankResult previewFile(const char* path, uint32_t* frames, uint32_t* rate) {
  PreviewFileReq q;
  if (!path || strlen(path) >= sizeof q.path) return BankResult::OpenFail;
  strcpy(q.path, path);
  uint8_t reply[kMaxPayload];
  int len = 0;
  if (!shortCall(Msg::PreviewFile, q, reply, len, 500)) return BankResult::Link;
  PreviewFileRep rep;
  Reader rd(reply, len);
  if (!decode(rd, rep)) return result(reply[0]);
  if (rep.result != 0) return result(rep.result);
  if (frames) *frames = rep.frames;
  if (rate) *rate = rep.rate;
  return BankResult::Ok;
}

void previewStop() {
  static const uint8_t none = 0;
  slink::sendRaw(Msg::PreviewStop, &none, 0, pdMS_TO_TICKS(20));
}

}  // namespace audio
