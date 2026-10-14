#include "sample_store.h"
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sample_set.h"
#include "wav.h"
#include "wave_peaks.h"

namespace mt {

const char kStoreDir[] = "/mt-store";

namespace {

inline void fence() { std::atomic_signal_fence(std::memory_order_seq_cst); }
void le32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
void path(uint32_t crc, const char* ext, char* out) {
  static const char hex[] = "0123456789abcdef";
  strcpy(out, kStoreDir);
  char* p = out + strlen(out);
  *p++ = '/';
  for (int i = 7; i >= 0; --i) *p++ = hex[(crc >> (i * 4)) & 15];
  strcpy(p, ext);
}

}  // namespace

void storePath(uint32_t crc, char out[kStorePathMax]) { path(crc, ".wav", out); }
void storePeaksPath(uint32_t crc, char out[kStorePathMax]) { path(crc, ".pk", out); }

void storeHeader(uint8_t out[kStoreDataOffset], uint32_t frames, uint32_t rate, uint8_t root, uint32_t crc) {
  uint8_t h[kWavHeaderBytes];
  wavHeader(h, frames, rate, root, crc, 1);
  constexpr uint32_t pre = kWavHeaderBytes - 8;  // wavHeader ends with the "data" chunk header
  constexpr uint32_t junk = kStoreDataOffset - pre - 8 - 8;
  static_assert(junk % 2 == 0, "chunks are word aligned");
  memcpy(out, h, pre);
  memcpy(out + pre, "JUNK", 4);
  le32(out + pre + 4, junk);
  memset(out + pre + 8, 0, junk);
  memcpy(out + kStoreDataOffset - 8, h + pre, 8);
  le32(out + 4, kStoreDataOffset - 8 + frames * 2);
}

int StoreTable::find(uint32_t crc) const {
  const int n = n_;
  for (int i = 0; i < n; ++i)
    if (e_[i].crc == crc) return i;
  return -1;
}

int StoreTable::add(uint32_t crc, uint32_t frames, uint32_t rate, uint32_t sector) {
  const int have = find(crc);
  if (have >= 0) return have;
  if (n_ >= kStoreEntries || !crc) return -1;
  StoreEntry& e = e_[n_];
  e.frames = frames;
  e.rate = rate;
  e.sector = sector;
  e.head0 = 0;
  e.nHeads = 0;
  e.psram = nullptr;
  fence();
  e.crc = crc;
  fence();
  n_ = n_ + 1;
  return n_ - 1;
}

bool StoreTable::setHeads(int i, const SampleHead* h, int n) {
  if (i < 0 || i >= n_ || e_[i].nHeads || n > kMaxHeads || nHead_ + n > kHeadPool) return false;
  for (int k = 0; k < n; ++k) head_[nHead_ + k] = h[k];
  e_[i].head0 = static_cast<uint16_t>(nHead_);
  nHead_ += n;
  fence();
  e_[i].nHeads = static_cast<uint8_t>(n);
  return true;
}

void StoreTable::clear() {
  n_ = 0;
  nHead_ = 0;
  for (auto& e : e_) e = StoreEntry();
}

bool StoreSampleSource::find(const char* name, SampleData& out) const {
  const int k = projSampleFind(m_, name);
  if (k < 0) return false;
  const ProjSample& ps = m_.samples[k];
  const int i = t_.find(ps.crc);
  if (i >= 0 && t_.at(i).frames == ps.frames) {
    const StoreEntry& e = t_.at(i);
    out = SampleData();
    out.key = ps.crc;
    out.frames = e.frames;
    out.rate = e.rate;
    out.heads = e.nHeads ? t_.heads(i) : nullptr;
    out.nHeads = e.nHeads;
    out.streamable = e.sector != 0;
    const int16_t* ps2 = e.psram;
    if (ps2 && p_.psramOk) {
      out.whole = ps2;
      out.psram = true;
    }
    if (out.whole || out.streamable) return true;
  }
  return legacy_ && legacy_->find(name, out);
}

// ---- StoreJob ----

const char kStoreTmp[] = "/mt-store/import.tmp";

namespace {
BankResult fromWav(WavErr e) {
  return e == WavErr::NotWav ? BankResult::NotWav
                             : (e == WavErr::Truncated ? BankResult::Truncated : BankResult::Unsupported);
}
}  // namespace

bool storeFileCurrent(BankDisk& d, uint32_t crc, uint32_t frames) {
  char p[kStorePathMax];
  storePath(crc, p);
  return bankFileCurrent(d, p, crc, frames);
}

int bankDropMigrated(SampleBank& b, BankDisk& d) {
  bool drop[kBankEntries] = {};
  int n = 0;
  for (int i = 0; i < b.count(); ++i) {
    const BankEntry* e = b.entry(i);
    if (!isSampleKey(e->name)) continue;
    const uint32_t crc = static_cast<uint32_t>(strtoul(e->name, nullptr, 16));
    if (storeFileCurrent(d, crc, e->frames)) {
      drop[i] = true;
      ++n;
    }
  }
  return n && b.removeMany(drop) ? n : 0;
}

BankResult storeWriteFrames(BankDisk& d, uint32_t frames, uint32_t rate, FrameFill fill, void* ctx, ImportOut& out,
                            BankProgress keep, void* keepCtx) {
  if (frames == 0 || rate == 0 || rate > kWavMaxRate) return BankResult::Unsupported;
  if (!d.ready()) return BankResult::NoSd;
  d.mkdir(kStoreDir);
  constexpr uint32_t kPiece = 1024;  // a multiple of kPeakBlock
  int16_t buf[kPiece];
  d.remove(kStoreTmp);
  WriteFile* w = d.openWrite(kStoreTmp);
  if (!w) return BankResult::WriteFail;
  w->reserve(kStoreDataOffset + frames * 2);
  uint8_t h[kStoreDataOffset] = {};
  BankResult r = w->write(h, sizeof h) ? BankResult::Ok : BankResult::WriteFail;  // the header at the end
  uint32_t crc = 0;
  for (uint32_t done = 0; r == BankResult::Ok && done < frames;) {
    const uint32_t n = frames - done < kPiece ? frames - done : kPiece;
    if (!fill(buf, done, n, ctx)) r = BankResult::ReadFail;  // cancelled
    else if (!w->write(buf, n * 2)) r = BankResult::WriteFail;
    crc = sampleCrc(buf, n, crc);
    done += n;
    if (keep) keep(done, frames, keepCtx);
  }
  if (r == BankResult::Ok) {
    storeHeader(h, frames, rate, 60, crc);
    if (!w->seek(0) || !w->write(h, sizeof h)) r = BankResult::WriteFail;
  }
  if (!d.closeWrite() && r == BankResult::Ok) r = BankResult::WriteFail;
  char p[kStorePathMax];
  storePath(crc, p);
  if (r == BankResult::Ok && d.exists(p)) {
    d.remove(kStoreTmp);  // the same data is there already (with its overview)
  } else if (r != BankResult::Ok || !d.rename(kStoreTmp, p)) {
    d.remove(kStoreTmp);
    return r == BankResult::Ok ? BankResult::WriteFail : r;
  } else {
    // The overview, read back from the new file (no overview: the tools read frames instead).
    char pk[kStorePathMax];
    storePeaksPath(crc, pk);
    ReadFile* in = d.openRead(p);
    WriteFile* o = in && in->seek(kStoreDataOffset) ? d.openWrite(pk) : nullptr;
    bool ok = o != nullptr;
    for (uint32_t left = frames; ok && left;) {
      const uint32_t n = left < kPiece ? left : kPiece;
      int8_t v[2 * kPiece / kPeakBlock];
      ok = in->read(buf, n * 2);
      const int np = ok ? peakOverview(buf, n, v) : 0;
      ok = ok && o->write(v, static_cast<size_t>(np) * 2);
      left -= n;
    }
    if (in) d.closeRead();
    if (o) ok = d.closeWrite() && ok;
    if (o && !ok) d.remove(pk);  // a cut overview is worse than none
  }
  out.crc = crc;
  out.frames = frames;
  out.rate = rate;
  out.root = 60;
  return BankResult::Ok;
}

int StoreJob::missing() const {
  int n = 0;
  for (bool m : missing_) n += m;
  return n;
}
uint32_t StoreJob::done() const {
  if (kind_ == Kind::Import) return subDone_;
  return static_cast<uint32_t>(item_) * 1000 +
         (subTotal_ ? static_cast<uint32_t>(static_cast<uint64_t>(subDone_) * 1000 / subTotal_) : 0);
}
uint32_t StoreJob::total() const {
  return kind_ == Kind::Import ? subTotal_ : static_cast<uint32_t>(count_) * 1000;
}

void StoreJob::closeAll() {
  if (in_) d_.closeRead();
  in_ = nullptr;
  if (out_) d_.closeWrite();
  out_ = nullptr;
}

BankResult StoreJob::start(Kind k) {
  if (busy()) return BankResult::Busy;
  if (!d_.ready()) return BankResult::NoSd;
  d_.mkdir(kStoreDir);  // false when it is there
  kind_ = k;
  phase_ = Phase::Next;
  item_ = count_ = 0;
  subDone_ = subTotal_ = 0;
  for (auto& x : missing_) x = false;
  return BankResult::Ok;
}

BankResult StoreJob::finish(BankResult r) {
  closeAll();
  kind_ = Kind::None;
  phase_ = Phase::Next;
  return r;
}

void StoreJob::cancel() {
  if (!busy()) return;
  const Phase ph = phase_;
  closeAll();
  if (ph == Phase::Convert) d_.remove(kStoreTmp);
  if (ph == Phase::Copy) d_.remove(path_);
  if (ph == Phase::Peaks) d_.remove(dst_);
  kind_ = Kind::None;
  phase_ = Phase::Next;
}

BankResult StoreJob::startImport(const char* path, const uint32_t* knownCrc) {
  BankResult r = start(Kind::Import);
  if (r != BankResult::Ok) return r;
  imp_ = ImportOut();
  r = openConvert(path, nullptr, knownCrc);
  if (r != BankResult::Ok) finish(r);
  return r;
}

BankResult StoreJob::startSync(const char* project, const SynthModel& m) {
  const BankResult r = start(Kind::Sync);
  if (r != BankResult::Ok) return r;
  m_ = &m;
  strncpy(project_, project, sizeof project_ - 1);
  project_[sizeof project_ - 1] = 0;
  count_ = m.sampleCount;
  return r;
}

BankResult StoreJob::startSave(const char* project, const SynthModel& m) {
  const BankResult r = start(Kind::Save);
  if (r != BankResult::Ok) return r;
  m_ = &m;
  strncpy(project_, project, sizeof project_ - 1);
  project_[sizeof project_ - 1] = 0;
  count_ = m.sampleCount;
  if (count_ > 0 && project_[0]) {  // the project folder, as BankJob makes it (a new project has none)
    char dir[sizeof project_ + 16];
    snprintf(dir, sizeof dir, "/projects/%s", project_);
    if (!d_.exists(dir) && !d_.mkdir(dir)) return finish(BankResult::WriteFail);
  }
  return r;
}

BankResult StoreJob::startMigrate(const SampleBank& b) {
  const BankResult r = start(Kind::Migrate);
  if (r != BankResult::Ok) return r;
  b_ = &b;
  count_ = b.count();
  return r;
}

BankResult StoreJob::step() {
  if (!busy()) return BankResult::Ok;
  switch (phase_) {
    case Phase::Convert: return convertStep();
    case Phase::Copy: return copyStep();
    case Phase::Peaks: return peaksStep();
    default: return kind_ == Kind::Import ? finish(BankResult::Ok) : nextItem();
  }
}

BankResult StoreJob::endItem(BankResult r) {
  const Phase ph = phase_;
  closeAll();
  if (ph == Phase::Convert) d_.remove(kStoreTmp);
  if (ph == Phase::Copy && r != BankResult::Ok) d_.remove(path_);
  if (ph == Phase::Peaks && left_) d_.remove(dst_);  // a cut overview is worse than none
  phase_ = Phase::Next;
  bankEntry_ = -1;
  subDone_ = subTotal_ = 0;
  if (kind_ == Kind::Import) return finish(r);
  if (r != BankResult::Ok && kind_ != Kind::Migrate && item_ < kProjSamples) missing_[item_] = true;
  ++item_;
  return BankResult::Running;
}

BankResult StoreJob::nextItem() {
  for (; item_ < count_; ++item_) {
    if (kind_ == Kind::Migrate) {
      const BankEntry* e = b_->entry(item_);
      if (!e || !isSampleKey(e->name)) continue;
      const uint32_t crc = static_cast<uint32_t>(strtoul(e->name, nullptr, 16));
      if (storeFileCurrent(d_, crc, e->frames)) continue;
      bankEntry_ = item_;
      at_ = 0;
      left_ = subTotal_ = e->frames;
      subDone_ = 0;
      hasExpect_ = true;
      expect_ = crc;
      rate_ = e->rate;
      root_ = e->root;
      ds_ = Downsampler(e->rate);
      if (beginWrite(e->frames) != BankResult::Ok) return finish(BankResult::WriteFail);
      return BankResult::Running;
    }
    const ProjSample& ps = m_->samples[item_];
    if (kind_ == Kind::Sync) {
      if (storeFileCurrent(d_, ps.crc, ps.frames)) continue;
      const int n = snprintf(path_, sizeof path_, "/projects/%s/%s.wav", project_, ps.name);
      if (!project_[0] || n <= 0 || n >= static_cast<int>(sizeof path_) ||
          openConvert(path_, &ps.crc, nullptr) != BankResult::Ok) {
        missing_[item_] = true;
        continue;
      }
      if (phase_ == Phase::Next) continue;  // was stored after all
      return BankResult::Running;
    }
    // Save
    if (!storeFileCurrent(d_, ps.crc, ps.frames)) {
      missing_[item_] = true;
      continue;
    }
    const int n = snprintf(dst_, sizeof dst_, "/projects/%s/%s.wav", project_, ps.name);
    if (!project_[0] || n <= 0 || n + 4 >= static_cast<int>(sizeof dst_)) {
      missing_[item_] = true;
      continue;
    }
    if (bankFileCurrent(d_, dst_, ps.crc, ps.frames)) continue;
    if (openCopy(ps.crc) != BankResult::Ok) {
      missing_[item_] = true;
      continue;
    }
    return BankResult::Running;
  }
  return finish(BankResult::Ok);
}

BankResult StoreJob::openConvert(const char* path, const uint32_t* expect, const uint32_t* known) {
  in_ = d_.openRead(path);
  if (!in_) return BankResult::OpenFail;
  w_ = WavInfo();
  const WavErr e = wavParse(*in_, w_);
  if (e != WavErr::Ok) {
    closeAll();
    return fromWav(e);
  }
  const uint32_t fb = w_.frameBytes();
  if (!fb || fb > 8) {
    closeAll();
    return BankResult::Unsupported;
  }
  uint32_t frames = w_.frames();
  const uint32_t size = in_->size();
  if (w_.dataOffset >= size) frames = 0;
  else if (frames > (size - w_.dataOffset) / fb) frames = (size - w_.dataOffset) / fb;  // a cut file
  if (!frames) {
    closeAll();
    return BankResult::Truncated;
  }
  hasExpect_ = expect != nullptr;
  expect_ = expect ? *expect : 0;
  root_ = w_.root;
  ds_ = Downsampler(w_.rate);
  rate_ = ds_.outRate();
  const uint32_t* k = expect ? expect : (known ? known : (w_.hasCrc ? &w_.crc : nullptr));
  if (k && rate_ == w_.rate) {
    const uint32_t kc = *k;
    closeAll();  // one read file at a time: the check opens the store file
    if (storeFileCurrent(d_, kc, frames)) {  // stored already: nothing to read
      imp_.crc = kc;
      imp_.frames = frames;
      imp_.rate = rate_;
      imp_.root = root_;
      phase_ = Phase::Next;
      return BankResult::Ok;
    }
    in_ = d_.openRead(path);
    if (!in_) return BankResult::OpenFail;
  }
  if (!in_->seek(w_.dataOffset)) {
    closeAll();
    return BankResult::ReadFail;
  }
  bankEntry_ = -1;
  left_ = subTotal_ = frames;
  subDone_ = 0;
  return beginWrite(static_cast<uint32_t>(static_cast<uint64_t>(frames) * rate_ / w_.rate) + 2);
}

BankResult StoreJob::beginWrite(uint32_t frames) {
  d_.remove(kStoreTmp);
  out_ = d_.openWrite(kStoreTmp);
  if (!out_) {
    closeAll();
    return BankResult::WriteFail;
  }
  out_->reserve(kStoreDataOffset + frames * 2);
  static const uint8_t zero[kStoreDataOffset] = {};
  if (!out_->write(zero, sizeof zero)) {  // the header goes in at the end (crc)
    closeAll();
    d_.remove(kStoreTmp);
    return BankResult::WriteFail;
  }
  frames_ = crc_ = 0;
  phase_ = Phase::Convert;
  return BankResult::Ok;
}

BankResult StoreJob::convertStep() {
  uint32_t n = left_ < kStepFrames ? left_ : kStepFrames;
  int k;
  if (bankEntry_ >= 0) {
    if (!b_->readData(bankEntry_, at_, res_, n)) return endItem(BankResult::ReadFail);
    at_ += n;
    k = static_cast<int>(n);
  } else {
    const uint32_t fb = w_.frameBytes();
    if (n > sizeof raw_ / fb) n = sizeof raw_ / fb;
    if (!in_->read(raw_, n * fb)) return endItem(BankResult::Truncated);
    wavToMono(raw_, n, w_, mono_);
    k = ds_.push(mono_, static_cast<int>(n), res_);
  }
  left_ -= n;
  subDone_ += n;
  crc_ = sampleCrc(res_, static_cast<uint32_t>(k), crc_);
  frames_ += static_cast<uint32_t>(k);
  if (k && !out_->write(res_, static_cast<size_t>(k) * 2)) return endItem(BankResult::WriteFail);
  return left_ ? BankResult::Running : finishConvert();
}

BankResult StoreJob::finishConvert() {
  if (in_) d_.closeRead();
  in_ = nullptr;
  if (hasExpect_ && crc_ != expect_) return endItem(BankResult::ReadFail);  // other data than listed
  uint8_t h[kStoreDataOffset];
  storeHeader(h, frames_, rate_, root_, crc_);
  const bool ok = out_->seek(0) && out_->write(h, sizeof h);
  out_ = nullptr;
  if (!d_.closeWrite() || !ok) {
    d_.remove(kStoreTmp);
    return endItem(BankResult::WriteFail);
  }
  char p[kStorePathMax];
  storePath(crc_, p);
  if (d_.exists(p)) {
    d_.remove(kStoreTmp);  // the same data is there already
  } else if (!d_.rename(kStoreTmp, p)) {
    d_.remove(kStoreTmp);
    phase_ = Phase::Next;
    return endItem(BankResult::WriteFail);
  }
  if (kind_ == Kind::Import) {
    imp_.crc = crc_;
    imp_.frames = frames_;
    imp_.rate = rate_;
    imp_.root = root_;
  }
  if (added_) added_(crc_, addedCtx_);
  return openPeaks();
}

BankResult StoreJob::openPeaks() {
  char p[kStorePathMax];
  storePath(crc_, p);
  phase_ = Phase::Next;
  in_ = d_.openRead(p);
  storePeaksPath(crc_, dst_);
  out_ = in_ ? d_.openWrite(dst_) : nullptr;
  left_ = frames_;
  if (!in_ || !out_ || !in_->seek(kStoreDataOffset)) {
    phase_ = Phase::Peaks;
    return endItem(BankResult::Ok);  // no overview: the tools read frames instead
  }
  phase_ = Phase::Peaks;
  return BankResult::Running;
}

BankResult StoreJob::peaksStep() {
  const uint32_t n = left_ < kStepFrames ? left_ : kStepFrames;
  if (!in_->read(res_, n * 2)) return endItem(BankResult::Ok);
  int8_t pk[2 * kStepFrames / kPeakBlock];
  const int np = peakOverview(res_, n, pk);
  if (!out_->write(pk, static_cast<size_t>(np) * 2)) return endItem(BankResult::Ok);
  left_ -= n;
  return left_ ? BankResult::Running : endItem(BankResult::Ok);
}

BankResult StoreJob::openCopy(uint32_t crc) {
  char src[kStorePathMax];
  storePath(crc, src);
  snprintf(path_, sizeof path_, "%s.tmp", dst_);
  in_ = d_.openRead(src);
  out_ = in_ ? d_.openWrite(path_) : nullptr;
  if (!in_ || !out_) {
    closeAll();
    return BankResult::OpenFail;
  }
  left_ = subTotal_ = in_->size();
  subDone_ = 0;
  phase_ = Phase::Copy;
  return BankResult::Ok;
}

BankResult StoreJob::copyStep() {
  const uint32_t n = left_ < sizeof raw_ ? left_ : sizeof raw_;
  if (!in_->read(raw_, n)) return endItem(BankResult::ReadFail);
  if (!out_->write(raw_, n)) return endItem(BankResult::WriteFail);
  left_ -= n;
  subDone_ += n;
  if (left_) return BankResult::Running;
  d_.closeRead();
  in_ = nullptr;
  out_ = nullptr;
  if (!d_.closeWrite()) return endItem(BankResult::WriteFail);
  d_.remove(dst_);
  if (!d_.rename(path_, dst_)) return endItem(BankResult::WriteFail);
  return endItem(BankResult::Ok);
}

}  // namespace mt
