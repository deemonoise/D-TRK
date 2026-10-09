#include "link_msg.h"
#include <string.h>
#include "model.h"
#include "wt_mip.h"

namespace mt::link {

void Writer::put(const void* d, int n) {
  if (!ok_ || n < 0 || n_ + n > cap_) {
    ok_ = false;
    return;
  }
  memcpy(b_ + n_, d, n);
  n_ += n;
}

void Writer::u16(uint16_t v) {
  uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
  put(b, 2);
}

void Writer::u32(uint32_t v) {
  u16(static_cast<uint16_t>(v));
  u16(static_cast<uint16_t>(v >> 16));
}

void Writer::u64(uint64_t v) {
  u32(static_cast<uint32_t>(v));
  u32(static_cast<uint32_t>(v >> 32));
}

void Writer::str(const char* s) {
  size_t n = strlen(s);
  if (n > 255) n = 255;
  u8(static_cast<uint8_t>(n));
  put(s, static_cast<int>(n));
}

bool Reader::take(int n) {
  if (!ok_ || n < 0 || pos_ + n > n_) {
    ok_ = false;
    return false;
  }
  return true;
}

uint8_t Reader::u8() {
  if (!take(1)) return 0;
  return b_[pos_++];
}

uint16_t Reader::u16() {
  if (!take(2)) return 0;
  uint16_t v = static_cast<uint16_t>(b_[pos_] | (b_[pos_ + 1] << 8));
  pos_ += 2;
  return v;
}

uint32_t Reader::u32() {
  uint32_t lo = u16();
  return lo | (static_cast<uint32_t>(u16()) << 16);
}

uint64_t Reader::u64() {
  uint64_t lo = u32();
  return lo | (static_cast<uint64_t>(u32()) << 32);
}

void Reader::bytes(void* d, int n) {
  if (!take(n)) return;
  memcpy(d, b_ + pos_, n);
  pos_ += n;
}

void Reader::str(char* out, int cap) {
  int n = u8();
  if (!take(n)) {
    if (cap > 0) out[0] = 0;
    return;
  }
  int k = n < cap - 1 ? n : cap - 1;
  memcpy(out, b_ + pos_, k);
  out[k] = 0;
  pos_ += n;
}

void encode(const Hello& m, Writer& w) {
  w.u16(m.proto);
  w.bytes(m.fw, sizeof m.fw);
  w.u32(m.bootId);
  w.u32(m.modelSize);
}

bool decode(Reader& r, Hello& m) {
  m.proto = r.u16();
  r.bytes(m.fw, sizeof m.fw);
  m.fw[sizeof m.fw - 1] = 0;
  m.bootId = r.u32();
  m.modelSize = r.u32();
  return r.ok();
}

void encode(const Time& m, Writer& w) { w.u64(m.tUs); }

bool decode(Reader& r, Time& m) {
  m.tUs = r.u64();
  return r.ok();
}

void encode(const EvBatch& m, Writer& w) {
  uint64_t base = 0;
  for (int i = 0; i < m.n; ++i)
    if (i == 0 || m.ev[i].tUs < base) base = m.ev[i].tUs;
  w.u64(base);
  w.u8(m.n);
  for (int i = 0; i < m.n; ++i) {
    const Ev& e = m.ev[i];
    uint64_t dt = e.tUs - base;
    w.u32(dt > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(dt));
    w.u8(e.track);
    uint8_t len = e.len > 3 ? 3 : e.len;
    w.u8(len);
    w.bytes(e.b, len);
  }
}

bool decode(Reader& r, EvBatch& m) {
  uint64_t base = r.u64();
  m.n = r.u8();
  if (m.n > EvBatch::kMax) return false;
  for (int i = 0; i < m.n; ++i) {
    Ev& e = m.ev[i];
    e.tUs = base + r.u32();
    e.track = r.u8();
    e.len = r.u8();
    if (e.len > 3) return false;
    r.bytes(e.b, e.len);
  }
  return r.ok();
}

void encode(const StateSet& m, Writer& w) {
  w.u16(m.chunk);
  w.u16(m.total);
  w.u16(m.offset);
  w.u16(m.len);
  w.bytes(m.data, m.len);
}

bool decode(Reader& r, StateSet& m) {
  m.chunk = r.u16();
  m.total = r.u16();
  m.offset = r.u16();
  m.len = r.u16();
  if (m.len > StateSet::kMaxData) return false;
  r.bytes(m.data, m.len);
  return r.ok();
}

void encode(const Ack& m, Writer& w) { w.u16(m.id); }

bool decode(Reader& r, Ack& m) {
  m.id = r.u16();
  return r.ok();
}

void encode(const Nack& m, Writer& w) {
  w.u16(m.id);
  w.u16(static_cast<uint16_t>(m.err));
}

bool decode(Reader& r, Nack& m) {
  m.id = r.u16();
  m.err = static_cast<int16_t>(r.u16());
  return r.ok();
}

void encode(const Status& m, Writer& w) {
  w.u8(m.cpuPct);
  w.u16(m.stalls);
  w.u16(m.late);
  w.u8(m.voices);
  w.u16(m.outPeak);
  w.bytes(m.trackPeak, sizeof m.trackPeak);
  w.u16(m.lost);
  uint16_t n = m.scopeN > Status::kScopeMax ? Status::kScopeMax : m.scopeN;
  w.u16(n);
  w.bytes(m.scope, n);
}

bool decode(Reader& r, Status& m) {
  m.cpuPct = r.u8();
  m.stalls = r.u16();
  m.late = r.u16();
  m.voices = r.u8();
  m.outPeak = r.u16();
  r.bytes(m.trackPeak, sizeof m.trackPeak);
  m.lost = r.u16();
  m.scopeN = r.u16();
  if (m.scopeN > Status::kScopeMax) return false;
  r.bytes(m.scope, m.scopeN);
  return r.ok();
}

void encode(const Progress& m, Writer& w) {
  w.u8(m.op);
  w.u32(m.done);
  w.u32(m.total);
  w.u8(m.item);
}

bool decode(Reader& r, Progress& m) {
  m.op = r.u8();
  m.done = r.u32();
  m.total = r.u32();
  m.item = r.u8();
  return r.ok();
}

void encode(const Log& m, Writer& w) { w.str(m.text); }

bool decode(Reader& r, Log& m) {
  r.str(m.text, sizeof m.text);
  return r.ok();
}

void encode(const Byte& m, Writer& w) { w.u8(m.v); }

bool decode(Reader& r, Byte& m) {
  m.v = r.u8();
  return r.ok();
}

void encode(const PreviewNote& m, Writer& w) {
  w.u8(m.instr);
  w.u8(m.note);
  w.u32(m.holdMs);
}

bool decode(Reader& r, PreviewNote& m) {
  m.instr = r.u8();
  m.note = r.u8();
  m.holdMs = r.u32();
  return r.ok();
}

void encode(const PreviewSlice& m, Writer& w) {
  w.u8(m.instr);
  w.u8(m.slice);
  w.u8(m.root);
  w.u32(m.holdMs);
}

bool decode(Reader& r, PreviewSlice& m) {
  m.instr = r.u8();
  m.slice = r.u8();
  m.root = r.u8();
  m.holdMs = r.u32();
  return r.ok();
}

static_assert(kSampleMaskBytes * 8 == kProjSamples && kWtMaskBytes * 8 == kProjWavetables, "list masks");
static_assert(BankIndexRep::kRates == kProjSamples, "a rate per listed sample");
static_assert(WtFrameRep::kPoints == kWtFrameLen, "a whole wavetable frame");
static_assert(sizeof(BankProjectReq::project) == kSampleNameMax + 1 && sizeof(WtFrameReq::name) == kSampleNameMax + 1,
              "project / wavetable names");

void encode(const BankProjectReq& m, Writer& w) { w.str(m.project); }

bool decode(Reader& r, BankProjectReq& m) {
  r.str(m.project, sizeof m.project);
  return r.ok();
}

void encode(const BankSetsRep& m, Writer& w) {
  w.u8(m.result);
  w.u8(m.missing);
  w.u8(m.failed);
  w.bytes(m.samples, sizeof m.samples);
  w.bytes(m.wavetables, sizeof m.wavetables);
}

bool decode(Reader& r, BankSetsRep& m) {
  m.result = r.u8();
  m.missing = r.u8();
  m.failed = r.u8();
  r.bytes(m.samples, sizeof m.samples);
  r.bytes(m.wavetables, sizeof m.wavetables);
  return r.ok();
}

void encode(const BankImportReq& m, Writer& w) {
  w.u8(m.kind);
  w.u8(m.hasCrc);
  w.u32(m.crc);
  w.str(m.path);
}

bool decode(Reader& r, BankImportReq& m) {
  m.kind = r.u8();
  m.hasCrc = r.u8();
  m.crc = r.u32();
  r.str(m.path, sizeof m.path);
  return r.ok();
}

void encode(const BankImportRep& m, Writer& w) {
  w.u8(m.result);
  w.u32(m.crc);
  w.u32(m.frames);
  w.u32(m.rate);
  w.u8(m.root);
}

bool decode(Reader& r, BankImportRep& m) {
  m.result = r.u8();
  m.crc = r.u32();
  m.frames = r.u32();
  m.rate = r.u32();
  m.root = r.u8();
  return r.ok();
}

void encode(const SampleInfoReq& m, Writer& w) { w.u8(m.index); }

bool decode(Reader& r, SampleInfoReq& m) {
  m.index = r.u8();
  return r.ok();
}

void encode(const SampleInfoRep& m, Writer& w) {
  w.u8(m.result);
  w.u8(m.cached);
  w.u32(m.frames);
  w.u32(m.rate);
  w.u8(m.root);
  w.u8(m.loop);
}

bool decode(Reader& r, SampleInfoRep& m) {
  m.result = r.u8();
  m.cached = r.u8();
  m.frames = r.u32();
  m.rate = r.u32();
  m.root = r.u8();
  m.loop = r.u8();
  return r.ok();
}

void encode(const BankIndexRep& m, Writer& w) {
  w.u8(m.result);
  w.u8(m.count);
  w.u32(m.capacity);
  w.u32(m.free);
  w.u32(m.unused);
  w.u32(m.gen);
  w.bytes(m.samples, sizeof m.samples);
  w.bytes(m.wavetables, sizeof m.wavetables);
  w.u8(m.builtins);
  const uint8_t n = m.n > BankIndexRep::kRates ? BankIndexRep::kRates : m.n;
  w.u8(n);
  for (int i = 0; i < n; ++i) w.u16(m.rate[i]);
}

bool decode(Reader& r, BankIndexRep& m) {
  m.result = r.u8();
  m.count = r.u8();
  m.capacity = r.u32();
  m.free = r.u32();
  m.unused = r.u32();
  m.gen = r.u32();
  r.bytes(m.samples, sizeof m.samples);
  r.bytes(m.wavetables, sizeof m.wavetables);
  m.builtins = r.u8();
  m.n = r.u8();
  if (m.n > BankIndexRep::kRates) return false;
  for (int i = 0; i < m.n; ++i) m.rate[i] = r.u16();
  for (int i = m.n; i < BankIndexRep::kRates; ++i) m.rate[i] = 0;
  return r.ok();
}

void encode(const BankClearReq& m, Writer& w) { w.u8(m.compact); }

bool decode(Reader& r, BankClearReq& m) {
  m.compact = r.u8();
  return r.ok();
}

void encode(const BankClearRep& m, Writer& w) {
  w.u8(m.result);
  w.u8(m.removed);
}

bool decode(Reader& r, BankClearRep& m) {
  m.result = r.u8();
  m.removed = r.u8();
  return r.ok();
}

void encode(const WtFrameReq& m, Writer& w) {
  w.u8(m.frame);
  w.str(m.name);
}

bool decode(Reader& r, WtFrameReq& m) {
  m.frame = r.u8();
  r.str(m.name, sizeof m.name);
  return r.ok();
}

void encode(const WtFrameRep& m, Writer& w) {
  w.u8(m.result);
  w.bytes(m.pts, sizeof m.pts);
}

bool decode(Reader& r, WtFrameRep& m) {
  m.result = r.u8();
  r.bytes(m.pts, sizeof m.pts);
  return r.ok();
}

static_assert(2 + 2 * WavePeaksRep::kMaxCols <= kMaxPayload && 3 + 6 * OnsetsRep::kMax <= kMaxPayload,
              "peaks / onsets replies fit a frame");

void encode(const WavePeaksReq& m, Writer& w) {
  w.u8(m.index);
  w.u32(m.col0);
  w.u32(m.span);
  w.u16(m.width);
  w.u8(m.cols);
}

bool decode(Reader& r, WavePeaksReq& m) {
  m.index = r.u8();
  m.col0 = r.u32();
  m.span = r.u32();
  m.width = r.u16();
  m.cols = r.u8();
  return r.ok();
}

void encode(const WavePeaksRep& m, Writer& w) {
  const int n = m.cols < WavePeaksRep::kMaxCols ? m.cols : WavePeaksRep::kMaxCols;
  w.u8(m.result);
  w.u8(static_cast<uint8_t>(n));
  w.bytes(m.mn, n);
  w.bytes(m.mx, n);
}

bool decode(Reader& r, WavePeaksRep& m) {
  m.result = r.u8();
  m.cols = r.u8();
  if (m.cols > WavePeaksRep::kMaxCols) return false;
  r.bytes(m.mn, m.cols);
  r.bytes(m.mx, m.cols);
  return r.ok();
}

void encode(const OnsetsReq& m, Writer& w) {
  w.u8(m.index);
  w.u8(m.skip);
}

bool decode(Reader& r, OnsetsReq& m) {
  m.index = r.u8();
  m.skip = r.u8();
  return r.ok();
}

void encode(const OnsetsRep& m, Writer& w) {
  const int n = m.n < OnsetsRep::kMax ? m.n : OnsetsRep::kMax;
  w.u8(m.result);
  w.u8(m.total);
  w.u8(static_cast<uint8_t>(n));
  for (int i = 0; i < n; ++i) {
    w.u32(m.pos[i]);
    w.u16(m.strength[i]);
  }
}

bool decode(Reader& r, OnsetsRep& m) {
  m.result = r.u8();
  m.total = r.u8();
  m.n = r.u8();
  if (m.n > OnsetsRep::kMax) return false;
  for (int i = 0; i < m.n; ++i) {
    m.pos[i] = r.u32();
    m.strength[i] = r.u16();
  }
  return r.ok();
}

void encode(const PreviewFileReq& m, Writer& w) { w.str(m.path); }

bool decode(Reader& r, PreviewFileReq& m) {
  r.str(m.path, sizeof m.path);
  return r.ok();
}

void encode(const PreviewFileRep& m, Writer& w) {
  w.u8(m.result);
  w.u32(m.frames);
  w.u32(m.rate);
}

bool decode(Reader& r, PreviewFileRep& m) {
  m.result = r.u8();
  m.frames = r.u32();
  m.rate = r.u32();
  return r.ok();
}

void encode(const ProfileRep& m, Writer& w) {
  w.u8(m.running);
  w.u32(m.blocks);
  w.u32(m.cyclesPerUs);
  for (uint64_t c : m.cycles) w.u64(c);
}

bool decode(Reader& r, ProfileRep& m) {
  m.running = r.u8();
  m.blocks = r.u32();
  m.cyclesPerUs = r.u32();
  for (uint64_t& c : m.cycles) c = r.u64();
  return r.ok();
}

void encode(const RenderStartReq& m, Writer& w) {
  w.u8(m.target);
  w.str(m.path);
}

bool decode(Reader& r, RenderStartReq& m) {
  m.target = r.u8();
  r.str(m.path, sizeof m.path);
  return r.ok() && m.target <= RenderStartReq::kBank;
}

void encode(const RenderBlocks& m, Writer& w) {
  const int n = m.n < RenderBlocks::kMaxBlocks ? m.n : RenderBlocks::kMaxBlocks;
  w.u32(m.first);
  w.u8(static_cast<uint8_t>(n));
  int total = 0;
  for (int i = 0; i < n; ++i) {
    w.u8(m.evN[i]);
    total += m.evN[i] & ~RenderBlocks::kCont;
  }
  if (total > RenderBlocks::kMaxEvents) total = RenderBlocks::kMaxEvents;
  for (int i = 0; i < total; ++i) {
    const RenderEv& e = m.ev[i];
    const uint8_t len = e.len >= 1 && e.len <= 3 ? e.len : 1;
    w.u8(e.off);
    w.u8(e.track);
    w.u8(len);
    w.bytes(e.b, len);
  }
}

bool decode(Reader& r, RenderBlocks& m) {
  m.first = r.u32();
  m.n = r.u8();
  if (m.n > RenderBlocks::kMaxBlocks) return false;
  int total = 0;
  for (int i = 0; i < m.n; ++i) {
    m.evN[i] = r.u8();
    if ((m.evN[i] & RenderBlocks::kCont) && i != m.n - 1) return false;  // only the last record goes on
    total += m.evN[i] & ~RenderBlocks::kCont;
  }
  if (total > RenderBlocks::kMaxEvents) return false;
  for (int i = 0; i < total; ++i) {
    RenderEv& e = m.ev[i];
    e.off = r.u8();
    e.track = r.u8();
    e.len = r.u8();
    if (e.len < 1 || e.len > 3) return false;
    r.bytes(e.b, e.len);
  }
  return r.ok();
}

void encode(const RenderEndReq& m, Writer& w) {
  w.u8(m.abort);
  w.u8(m.normalize);
  w.u8(m.trim);
}

bool decode(Reader& r, RenderEndReq& m) {
  m.abort = r.u8();
  m.normalize = r.u8();
  m.trim = r.u8();
  return r.ok();
}

void encode(const RenderRep& m, Writer& w) {
  w.u8(m.result);
  w.u8(m.bank);
  w.u32(m.blocks);
  w.u32(m.frames);
  w.u16(m.peak);
  w.u32(m.clips);
  w.u32(m.crc);
}

bool decode(Reader& r, RenderRep& m) {
  m.result = r.u8();
  m.bank = r.u8();
  m.blocks = r.u32();
  m.frames = r.u32();
  m.peak = r.u16();
  m.clips = r.u32();
  m.crc = r.u32();
  return r.ok();
}

void encode(const FwFromFileReq& m, Writer& w) { w.str(m.path); }

bool decode(Reader& r, FwFromFileReq& m) {
  r.str(m.path, sizeof m.path);
  return r.ok() && m.path[0];
}

void encode(const FwRep& m, Writer& w) {
  w.u8(m.result);
  w.u32(m.bytes);
}

bool decode(Reader& r, FwRep& m) {
  m.result = r.u8();
  m.bytes = r.u32();
  return r.ok();
}

}  // namespace mt::link
