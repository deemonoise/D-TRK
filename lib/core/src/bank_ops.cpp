#include "bank_ops.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file_rules.h"
#include "sample_set.h"
#include "wt_builtin.h"
#include "wt_file.h"
#include "wt_mip.h"

namespace mt {
namespace {

constexpr uint32_t kRawBytes = BankJob::kStepBytes;  // WAV read block
constexpr const char* kImportName = "~import";       // entry being imported, renamed to its key at the end

// crc as 8 lower-case hex digits + terminator at out (sampleKey without snprintf: note-on path in
// the audio interrupt).
void hexKey(uint32_t crc, char* out) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (int n = 0; n < 8; ++n) out[n] = kHex[(crc >> (28 - 4 * n)) & 15];
  out[8] = 0;
}

BankResult wavErr(WavErr e) {
  switch (e) {
    case WavErr::Ok: return BankResult::Ok;
    case WavErr::NotWav: return BankResult::NotWav;
    case WavErr::Unsupported: return BankResult::Unsupported;
    default: return BankResult::Truncated;
  }
}

bool isWtEntry(const BankEntry* e) { return e && e->frames == static_cast<uint32_t>(kWtTableSamples) && e->rate == 0; }

// Writes n frames in pieces, calling keep between them (each piece may erase flash sectors).
bool writePieces(SampleBank& b, const int16_t* d, uint32_t n, BankProgress keep, void* ctx) {
  constexpr uint32_t kPiece = 2048;
  for (uint32_t at = 0; at < n; at += kPiece) {
    if (!b.write(d + at, n - at < kPiece ? n - at : kPiece)) return false;
    if (keep) keep(at, n, ctx);
  }
  return true;
}

// The committed kImportName entry becomes the entry of its data key (an existing one with the same
// data is kept instead). out.crc / out.frames set on success.
BankResult finishImport(SampleBank& b, const SynthModel& m, uint32_t crc, ImportOut& out) {
  const int i = b.find(kImportName);
  if (i < 0) return BankResult::WriteFail;
  const uint32_t got = b.entry(i)->frames;
  char k[kSampleNameMax + 1];
  sampleKey(crc, k);
  int dup = b.find(k);
  if (dup >= 0 && b.entry(dup)->frames != got) {
    // Same crc, other length (practically never): the old entry goes unless the project uses it.
    if (bankEntryUsed(m, *b.entry(dup)) || !b.remove(dup)) {
      b.remove(b.find(kImportName));
      return BankResult::WriteFail;
    }
    dup = -1;
  }
  // Same data cached: keep that entry (with the root stored when it was imported).
  const bool ok = dup >= 0 ? b.remove(b.find(kImportName)) : b.rename(b.find(kImportName), k);
  if (!ok) {
    b.remove(b.find(kImportName));
    return BankResult::WriteFail;
  }
  out.crc = crc;
  out.frames = got;
  return BankResult::Ok;
}

// Source frames of a wavetable WAV, read from the card when wtBuild asks for them.
struct FileFrames final : WtFrameReader {
  ReadFile* f;
  const WavInfo* w;
  int16_t* buf;  // kWtFrameMax
  uint8_t* raw;  // kRawBytes
  int cur = -1;
  bool err = false;
  uint32_t reads = 0, total = 0;
  BankProgress keep;
  void* ctx;
  const int16_t* frame(int k, const WtFormat& fmt) override {
    if (k == cur) return buf;
    cur = k;
    const uint32_t fb = w->frameBytes();
    const uint32_t chunk = kRawBytes / fb;
    const uint32_t len = static_cast<uint32_t>(fmt.frameLen);
    if (!err && !f->seek(w->dataOffset + static_cast<uint32_t>(k) * len * fb)) err = true;
    for (uint32_t done = 0; !err && done < len;) {
      const uint32_t n = len - done < chunk ? len - done : chunk;
      if (!f->read(raw, n * fb)) {
        err = true;
        break;
      }
      wavToMono(raw, n, *w, buf + done);
      done += n;
    }
    if (err) memset(buf, 0, len * sizeof(int16_t));
    if (keep) keep(++reads, total, ctx);
    return buf;
  }
};

}  // namespace

// ---- sources ----

const int16_t* BankSampleSource::find(const char* name, uint32_t& frames, uint32_t& rate) const {
  // projSampleBank without snprintf.
  const int k = projSampleFind(m_, name);
  if (k < 0) return nullptr;
  const ProjSample& ps = m_.samples[k];
  char key[kSampleNameMax + 1];
  hexKey(ps.crc, key);
  int i = b_.find(key);
  if (i >= 0 && b_.entry(i)->frames != ps.frames) i = -1;
  const int16_t* d = b_.data(i);
  if (!d) return nullptr;
  frames = b_.entry(i)->frames;
  rate = b_.entry(i)->rate;
  return d;
}

const int16_t* BankWtSource::findWt(const char* name) const { return b_.data(bankWtIndex(b_, m_, name)); }

int bankWtIndex(const SampleBank& b, const SynthModel& m, const char* name) {
  if (!name || !name[0]) return -1;
  int i;
  if (isWtBuiltin(name)) {
    i = b.find(name);
  } else {
    const int k = projWtFind(m, name);
    if (k < 0) return -1;
    char key[kSampleNameMax + 1];
    key[0] = 'w';
    hexKey(m.wavetables[k].crc, key + 1);
    i = b.find(key);
  }
  return isWtEntry(b.entry(i)) ? i : -1;
}

bool bankWtFrame(const SampleBank& b, const SynthModel& m, const char* name, int f, int8_t* out) {
  const int i = bankWtIndex(b, m, name);
  if (i < 0) return false;
  f = f < 0 ? 0 : (f >= kWtFrames ? kWtFrames - 1 : f);
  int16_t pts[kWtFrameLen];
  if (const int16_t* t = b.data(i)) memcpy(pts, t + f * kWtFramePts, sizeof pts);  // level 0
  else if (!b.readData(i, static_cast<uint32_t>(f) * kWtFramePts, pts, kWtFrameLen)) return false;
  for (int k = 0; k < kWtFrameLen; ++k) out[k] = static_cast<int8_t>(pts[k] >> 8);
  return true;
}

// ---- built-ins, generated data ----

int bankAddBuiltins(SampleBank& b, const SynthModel& m, int16_t* table, BankProgress keep, void* ctx) {
  int added = 0;
  for (int i = 0; i < kWtBuiltins; ++i) {
    const char* name = wtBuiltinName(i);
    const int old = b.find(name);
    if (isWtEntry(b.entry(old))) continue;
    // An entry of that name but not a wavetable (never written by this firmware): replace it.
    if (old >= 0 && !b.remove(old)) continue;
    WtBuiltinSrc src(i);
    if (!wtBuild(src, table)) continue;
    if (keep) keep(i, kWtBuiltins, ctx);
    if (!bankMakeRoom(b, m, kWtTableSamples, keep, ctx) || !b.begin(name, kWtTableSamples, 0, 60)) continue;
    if (!writePieces(b, table, kWtTableSamples, keep, ctx) || !b.commit()) {
      b.abort();
      continue;
    }
    ++added;
  }
  return added;
}

BankResult bankWriteFrames(SampleBank& b, const SynthModel& m, uint32_t frames, uint32_t rate, FrameFill fill,
                           void* ctx, ImportOut& out, BankProgress keep, void* keepCtx) {
  if (frames == 0) return BankResult::Unsupported;
  constexpr uint32_t kPiece = kRawBytes / 2;
  int16_t* buf = new (std::nothrow) int16_t[kPiece];
  if (!buf) return BankResult::NoMemory;
  BankResult r = BankResult::Ok;
  uint32_t crc = 0;
  if (const int old = b.find(kImportName); old >= 0 && !b.remove(old)) r = BankResult::WriteFail;
  if (r == BankResult::Ok && !bankMakeRoom(b, m, frames, keep, keepCtx)) r = BankResult::Full;
  if (r == BankResult::Ok && !b.begin(kImportName, frames, rate, 60)) r = BankResult::Full;
  for (uint32_t done = 0; r == BankResult::Ok && done < frames;) {
    const uint32_t n = frames - done < kPiece ? frames - done : kPiece;
    if (!fill(buf, done, n, ctx)) r = BankResult::ReadFail;  // cancelled
    else if (!b.write(buf, n)) r = BankResult::WriteFail;
    crc = sampleCrc(buf, n, crc);
    done += n;
    if (keep) keep(done, frames, keepCtx);
  }
  delete[] buf;
  if (r == BankResult::Ok && !b.commit()) r = BankResult::WriteFail;
  if (r != BankResult::Ok) {
    b.abort();
    return r;
  }
  out.root = 60;
  out.rate = rate;
  return finishImport(b, m, crc, out);
}

bool bankFileCurrent(BankDisk& d, const char* path, uint32_t crc, uint32_t frames) {
  ReadFile* f = d.openRead(path);
  if (!f) return false;
  WavInfo w;
  const bool ok = wavParse(*f, w) == WavErr::Ok && w.channels == 1 && w.bits == 16 && w.hasCrc && w.crc == crc &&
                  w.frames() == frames && f->size() >= w.dataOffset + static_cast<uint64_t>(frames) * 2;
  d.closeRead();
  return ok;
}

// ---- BankJob ----

BankResult BankJob::begin(Kind k, const SynthModel& m) {
  cancel();
  kind_ = k;
  m_ = &m;
  over_ = false;
  started_ = false;
  result_ = BankResult::Running;
  item_ = count_ = nS_ = 0;
  itemOpen_ = false;
  memset(missS_, 0, sizeof missS_);
  memset(missW_, 0, sizeof missW_);
  missing_ = failed_ = 0;
  out_ = ImportOut();
  subDone_ = subTotal_ = 0;
  return BankResult::Running;
}

BankResult BankJob::finish(BankResult r) {
  result_ = r;
  over_ = true;
  return r;
}

void BankJob::cancel() {
  if (sub_ == Sub::Import) importEnd(BankResult::ReadFail);
  if (sub_ == Sub::Export) exportEnd(BankResult::WriteFail);
  if (kind_ != Kind::None && !over_) finish(BankResult::ReadFail);
  kind_ = Kind::None;
}

BankResult BankJob::startImport(const char* path, bool wt, const SynthModel& m, const uint32_t* knownCrc) {
  begin(Kind::Import, m);
  if (strlen(path) >= sizeof path_) return finish(BankResult::OpenFail);
  strcpy(path_, path);
  wt_ = wt;
  hasKnown_ = knownCrc != nullptr;
  known_ = knownCrc ? *knownCrc : 0;
  return BankResult::Running;
}

BankResult BankJob::startSync(const char* project, const SynthModel& m) {
  begin(Kind::Sync, m);
  strncpy(project_, project, kSampleNameMax);
  project_[kSampleNameMax] = 0;
  nS_ = m.sampleCount < kProjSamples ? m.sampleCount : kProjSamples;
  count_ = nS_ + (m.wavetableCount < kProjWavetables ? m.wavetableCount : kProjWavetables);
  return BankResult::Running;
}

BankResult BankJob::startSave(const char* project, const SynthModel& m) {
  startSync(project, m);
  kind_ = Kind::Save;
  if (!projectBaseValid(project_)) return finish(BankResult::OpenFail);
  return BankResult::Running;
}

uint32_t BankJob::done() const {
  if (kind_ == Kind::Import) return subDone_;
  const uint32_t part = itemOpen_ && subTotal_ ? static_cast<uint32_t>(static_cast<uint64_t>(subDone_) * 1000 / subTotal_) : 0;
  return static_cast<uint32_t>(item_ < count_ ? item_ : count_) * 1000 + part;
}

uint32_t BankJob::total() const { return kind_ == Kind::Import ? subTotal_ : static_cast<uint32_t>(count_) * 1000; }

BankResult BankJob::step() {
  if (kind_ == Kind::None || over_) return result_;
  if (sub_ != Sub::None) {
    BankResult r = sub_ == Sub::Import ? importStep() : exportStep();
    if (r == BankResult::Running) return r;
    r = sub_ == Sub::Import ? importEnd(r) : exportEnd(r);
    if (kind_ == Kind::Import) return finish(r);
    const bool wt = item_ >= nS_;
    itemDone(kind_ == Kind::Sync ? r == BankResult::Ok && itemMatches(wt, wt ? item_ - nS_ : item_) : r == BankResult::Ok);
    return BankResult::Running;
  }
  if (kind_ == Kind::Import) {
    started_ = true;
    const BankResult r = importOpen(path_, wt_, hasKnown_ ? &known_ : nullptr);
    return r == BankResult::Running ? r : finish(importEnd(r));
  }
  if (!started_) {
    started_ = true;
    if (kind_ == Kind::Save && count_ > 0) {
      if (!d_.ready()) return finish(BankResult::NoSd);
      char dir[48];
      snprintf(dir, sizeof dir, "/projects/%s", project_);
      if (!d_.exists(dir) && !d_.mkdir(dir)) return finish(BankResult::WriteFail);
      snprintf(dir, sizeof dir, "/projects/%s/wt", project_);
      if (count_ > nS_ && !d_.exists(dir) && !d_.mkdir(dir)) return finish(BankResult::WriteFail);
    }
    return BankResult::Running;
  }
  const BankResult r = nextItem();
  if (r == BankResult::Running) return r;
  return finish(kind_ == Kind::Save && failed_ ? BankResult::WriteFail : r);
}

BankResult BankJob::itemPath(bool wt, int i) {
  const char* name = wt ? m_->wavetables[i].name : m_->samples[i].name;
  const int n = snprintf(path_, sizeof path_, "/projects/%s/%s%s.wav", project_, wt ? "wt/" : "", name);
  return n > 0 && n < static_cast<int>(sizeof path_) ? BankResult::Ok : BankResult::OpenFail;
}

bool BankJob::itemMatches(bool wt, int i) const {
  if (wt) return out_.crc == m_->wavetables[i].crc;
  return out_.crc == m_->samples[i].crc && out_.frames == m_->samples[i].frames;
}

void BankJob::markMissing() {
  const bool wt = item_ >= nS_;
  const int i = wt ? item_ - nS_ : item_;
  uint8_t* m = wt ? missW_ : missS_;
  m[i >> 3] |= static_cast<uint8_t>(1 << (i & 7));
  ++missing_;
}

void BankJob::itemDone(bool ok) {
  if (!ok) {
    if (kind_ == Kind::Sync) markMissing();
    else ++failed_;
  }
  itemOpen_ = false;
  ++item_;
}

BankResult BankJob::nextItem() {
  while (item_ < count_) {
    const bool wt = item_ >= nS_;
    const int i = wt ? item_ - nS_ : item_;
    const int j = wt ? projWtBank(*m_, b_, i) : projSampleBank(*m_, b_, i);
    if (kind_ == Kind::Sync) {
      if (j >= 0) {
        ++item_;
        continue;
      }
      if (!project_[0] || itemPath(wt, i) != BankResult::Ok) {
        markMissing();
        ++item_;
        continue;
      }
      const uint32_t want = wt ? m_->wavetables[i].crc : m_->samples[i].crc;
      BankResult r = importOpen(path_, wt, &want);
      if (r == BankResult::Running) {
        itemOpen_ = true;
        return r;
      }
      r = importEnd(r);
      itemDone(r == BankResult::Ok && itemMatches(wt, i));
      return BankResult::Running;  // one file per step
    }
    if (j < 0) {
      markMissing();
      ++item_;
      continue;
    }
    if (itemPath(wt, i) != BankResult::Ok) {
      itemDone(false);
      continue;
    }
    const uint32_t frames = wt ? static_cast<uint32_t>(kWtSrcSamples) : m_->samples[i].frames;
    if (bankFileCurrent(d_, path_, wt ? m_->wavetables[i].crc : m_->samples[i].crc, frames)) {
      itemDone(true);
      return BankResult::Running;
    }
    BankResult r = exportOpen(j, wt);
    if (r == BankResult::Running) {
      itemOpen_ = true;
      return r;
    }
    itemDone(exportEnd(r) == BankResult::Ok);
    return BankResult::Running;
  }
  return BankResult::Ok;
}

// ---- import ----

BankResult BankJob::openWav(const char* path) {
  if (!d_.ready()) return BankResult::NoSd;
  rf_ = d_.openRead(path);
  if (!rf_) return BankResult::OpenFail;
  const WavErr e = wavParse(*rf_, w_);
  if (e != WavErr::Ok) return wavErr(e);
  if (w_.frameBytes() > kRawBytes) return BankResult::Unsupported;
  // A data chunk longer than the file (cut copy, a writer that never fixed the size): only what is
  // there plays.
  const uint32_t size = rf_->size();
  const uint32_t avail = size > w_.dataOffset ? size - w_.dataOffset : 0;
  if (w_.dataBytes > avail) w_.dataBytes = avail - avail % w_.frameBytes();
  if (w_.frames() == 0) return BankResult::Truncated;
  return BankResult::Ok;
}

BankResult BankJob::importOpen(const char* path, bool wt, const uint32_t* knownCrc) {
  sub_ = Sub::Import;
  subDone_ = subTotal_ = 0;
  out_ = ImportOut();
  if (const BankResult e = openWav(path); e != BankResult::Ok) return e;
  out_.root = w_.root;
  if (wt) return importWt(knownCrc);
  // Cached already: a mono 16-bit file at the bank rate converts 1:1, so key and length identify it.
  if (w_.channels == 1 && w_.bits == 16 && w_.rate <= kWavMaxRate && (w_.hasCrc || knownCrc)) {
    const uint32_t c = w_.hasCrc ? w_.crc : *knownCrc;
    char k[kSampleNameMax + 1];
    sampleKey(c, k);
    const int i = b_.find(k);
    if (i >= 0 && b_.entry(i)->frames == w_.frames()) {
      out_.crc = c;
      out_.frames = w_.frames();
      out_.rate = b_.entry(i)->rate;
      return BankResult::Ok;
    }
  }
  const uint32_t frames = Downsampler::outFrames(w_.frames(), w_.rate);
  ds_ = Downsampler(w_.rate);
  out_.rate = ds_.outRate();
  // Left over from a failed import (power cut before the rename): only cache.
  if (const int old = b_.find(kImportName); old >= 0 && !b_.remove(old)) return BankResult::WriteFail;
  if (!bankMakeRoom(b_, *m_, frames, keep_, keepCtx_)) return BankResult::Full;
  const uint32_t chunk = kRawBytes / w_.frameBytes();
  raw_ = new (std::nothrow) uint8_t[kRawBytes];
  mono_ = new (std::nothrow) int16_t[chunk];
  conv_ = new (std::nothrow) int16_t[Downsampler::outFrames(chunk, w_.rate)];
  if (!raw_ || !mono_ || !conv_) return BankResult::NoMemory;
  if (!rf_->seek(w_.dataOffset)) return BankResult::ReadFail;
  if (!b_.begin(kImportName, frames, out_.rate, w_.root)) return BankResult::Full;
  adding_ = true;
  crc_ = 0;
  subTotal_ = w_.frames();
  return BankResult::Running;
}

BankResult BankJob::importStep() {
  const uint32_t fb = w_.frameBytes();
  const uint32_t chunk = kRawBytes / fb;
  const uint32_t n = subTotal_ - subDone_ < chunk ? subTotal_ - subDone_ : chunk;
  if (!rf_->read(raw_, n * fb)) return BankResult::ReadFail;
  wavToMono(raw_, n, w_, mono_);
  const int k = ds_.push(mono_, static_cast<int>(n), conv_);
  if (k > 0) {
    crc_ = sampleCrc(conv_, static_cast<uint32_t>(k), crc_);
    if (!b_.write(conv_, static_cast<uint32_t>(k))) return BankResult::WriteFail;
  }
  subDone_ += n;
  if (subDone_ < subTotal_) return BankResult::Running;
  adding_ = false;
  if (!b_.commit()) return BankResult::WriteFail;
  return finishImport(b_, *m_, crc_, out_);
}

BankResult BankJob::importWt(const uint32_t* knownCrc) {
  const uint32_t n = w_.frames();
  if (n > kWtMaxSrcSamples) return BankResult::Unsupported;
  char key[kSampleNameMax + 1];
  // Cached already: a canonical source (as the save writes it) is identified by its crc.
  if (w_.channels == 1 && w_.bits == 16 && n == static_cast<uint32_t>(kWtSrcSamples) && (w_.hasCrc || knownCrc)) {
    const uint32_t c = w_.hasCrc ? w_.crc : *knownCrc;
    wtKey(c, key);
    if (isWtEntry(b_.entry(b_.find(key)))) {
      out_.crc = c;
      return BankResult::Ok;
    }
  }
  WtFormat fmt;
  if (wtDetect(n, w_.clmFrame, fmt) != WtErr::Ok || fmt.frameLen > kWtFrameMax) return BankResult::Unsupported;
  // The whole table is built before the bank is touched.
  int16_t* table = new (std::nothrow) int16_t[kWtTableSamples];
  raw_ = new (std::nothrow) uint8_t[kRawBytes];
  mono_ = new (std::nothrow) int16_t[kWtFrameMax];
  if (!table || !raw_ || !mono_) {
    delete[] table;
    return BankResult::NoMemory;
  }
  FileFrames rd;
  rd.f = rf_;
  rd.w = &w_;
  rd.buf = mono_;
  rd.raw = raw_;
  rd.total = static_cast<uint32_t>(2 * kWtFrames);
  rd.keep = keep_;
  rd.ctx = keepCtx_;
  BankResult r = BankResult::Ok;
  switch (wtImport(rd, n, w_.clmFrame, table)) {
    case WtErr::Ok: break;
    case WtErr::Silent: r = BankResult::NotWav; break;
    default: r = BankResult::Unsupported; break;
  }
  if (rd.err) r = BankResult::ReadFail;
  // crc of the canonical source: level 0 of every frame, in order (wtLevel0 without its 32 KB).
  uint32_t crc = 0;
  if (r == BankResult::Ok)
    for (int f = 0; f < kWtFrames; ++f) crc = sampleCrc(table + f * kWtFramePts, kWtFrameLen, crc);
  if (r == BankResult::Ok) {
    wtKey(crc, key);
    out_.crc = crc;
    if (!isWtEntry(b_.entry(b_.find(key)))) {
      // Left over from a failed import; the same key but not a table (practically never: unused by m).
      if (const int old = b_.find(kImportName); old >= 0 && !b_.remove(old)) r = BankResult::WriteFail;
      else if (const int dup = b_.find(key); dup >= 0 && !b_.remove(dup)) r = BankResult::WriteFail;
      else if (!bankMakeRoom(b_, *m_, kWtTableSamples, keep_, keepCtx_) || !b_.begin(kImportName, kWtTableSamples, 0, 60))
        r = BankResult::Full;
      else if (!writePieces(b_, table, kWtTableSamples, keep_, keepCtx_) || !b_.commit())
        r = BankResult::WriteFail;
      else if (!b_.rename(b_.find(kImportName), key))
        r = BankResult::WriteFail;
      if (r != BankResult::Ok) {
        b_.abort();
        if (const int left = b_.find(kImportName); left >= 0) b_.remove(left);
      }
    }
  }
  delete[] table;
  return r;
}

BankResult BankJob::importEnd(BankResult r) {
  if (adding_) b_.abort();
  adding_ = false;
  if (rf_) d_.closeRead();
  rf_ = nullptr;
  delete[] raw_;
  delete[] mono_;
  delete[] conv_;
  raw_ = nullptr;
  mono_ = conv_ = nullptr;
  sub_ = Sub::None;
  return r;
}

// ---- export ----

BankResult BankJob::exportOpen(int entry, bool wt) {
  sub_ = Sub::Export;
  subDone_ = subTotal_ = 0;
  entry_ = entry;
  const BankEntry e = *b_.entry(entry);
  snprintf(tmp_, sizeof tmp_, "%s.tmp", path_);
  if (d_.exists(tmp_)) d_.remove(tmp_);
  uint32_t crc = 0, rate = e.rate;
  uint8_t root = e.root;
  // Entries are renamed to their key on import: the key is the crc of what gets written.
  wtOut_ = wt;
  if (wt ? !isWtKey(e.name) : !isSampleKey(e.name)) return BankResult::ReadFail;
  crc = static_cast<uint32_t>(strtoul(e.name + (wt ? 1 : 0), nullptr, 16));
  if (wt) {
    subTotal_ = kWtSrcSamples;
    rate = 44100;
    root = 60;
  } else {
    subTotal_ = e.frames;
  }
  conv_ = new (std::nothrow) int16_t[kRawBytes / 2];
  if (!conv_) return BankResult::NoMemory;
  if (!d_.ready()) return BankResult::NoSd;
  wf_ = d_.openWrite(tmp_);
  if (!wf_) return BankResult::OpenFail;
  uint8_t hdr[kWavHeaderBytes];
  wavHeader(hdr, subTotal_, rate, root, crc);
  if (!wf_->write(hdr, sizeof hdr)) return BankResult::WriteFail;
  return BankResult::Running;
}

BankResult BankJob::exportStep() {
  constexpr uint32_t kBlock = kRawBytes / 2;
  static_assert(kBlock % kWtFrameLen == 0 && kWtSrcSamples % kBlock == 0, "whole frames per block");
  const uint32_t n = subTotal_ - subDone_ < kBlock ? subTotal_ - subDone_ : kBlock;
  if (wtOut_) {
    // Level 0 of frames: kBlock is whole frames.
    for (uint32_t k = 0; k < n; k += kWtFrameLen) {
      const uint32_t f = (subDone_ + k) / kWtFrameLen;
      if (const int16_t* t = b_.data(entry_)) memcpy(conv_ + k, t + f * kWtFramePts, kWtFrameLen * 2);
      else if (!b_.readData(entry_, f * kWtFramePts, conv_ + k, kWtFrameLen)) return BankResult::ReadFail;
    }
  } else if (const int16_t* d = b_.data(entry_)) {
    memcpy(conv_, d + subDone_, n * 2);  // a RAM block: no card writes straight from flash
  } else if (!b_.readData(entry_, subDone_, conv_, n)) {
    return BankResult::ReadFail;
  }
  if (!wf_->write(conv_, n * 2)) return BankResult::WriteFail;
  subDone_ += n;
  if (subDone_ < subTotal_) return BankResult::Running;
  wf_ = nullptr;
  if (!d_.closeWrite()) return BankResult::WriteFail;
  if (d_.exists(path_) && !d_.remove(path_)) return BankResult::WriteFail;
  return d_.rename(tmp_, path_) ? BankResult::Ok : BankResult::WriteFail;
}

BankResult BankJob::exportEnd(BankResult r) {
  if (wf_) d_.closeWrite();
  wf_ = nullptr;
  if (r != BankResult::Ok && tmp_[0] && d_.exists(tmp_)) d_.remove(tmp_);
  delete[] conv_;
  conv_ = nullptr;
  tmp_[0] = 0;
  sub_ = Sub::None;
  return r;
}

}  // namespace mt
