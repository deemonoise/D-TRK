#include "rec.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sample_set.h"
#include "slices.h"

namespace mt {

// --- RecRing ---

void RecRing::init(int16_t* mem, uint32_t frames) {
  uint32_t cap = 1;
  while (cap <= frames / 2) cap <<= 1;
  mem_ = mem;
  mask_ = mem && frames ? cap - 1 : 0;
  reset();
}

void RecRing::reset() {
  head_.store(0);
  tail_.store(0);
  overflow_.store(false);
}

bool RecRing::push(const int16_t* l, const int16_t* r, uint32_t n) {
  const uint32_t h = head_.load();
  if (!mem_ || n > capacity() - (h - tail_.load())) {
    overflow_.store(true);
    return false;
  }
  for (uint32_t i = 0; i < n; ++i) {
    int16_t* d = mem_ + ((h + i) & mask_) * 2;
    d[0] = l[i];
    d[1] = r[i];
  }
  head_.store(h + n);  // data before the counter (seq_cst store)
  return true;
}

uint32_t RecRing::pop(int16_t* interleaved, uint32_t max) {
  const uint32_t t = tail_.load();
  const uint32_t avail = head_.load() - t;
  const uint32_t n = avail < max ? avail : max;
  for (uint32_t i = 0; i < n; ++i) {
    const int16_t* s = mem_ + ((t + i) & mask_) * 2;
    interleaved[i * 2] = s[0];
    interleaved[i * 2 + 1] = s[1];
  }
  tail_.store(t + n);
  return n;
}

void RecRing::keepLast(uint32_t n) {
  const uint32_t h = head_.load();
  if (h - tail_.load() > n) tail_.store(h - n);
}

// --- meter, threshold, gain ---

void RecMeter::feed(const int16_t* s, uint32_t n) {
  int32_t pk = peak.load();
  uint32_t cl = 0;
  for (uint32_t i = 0; i < n; ++i) {
    int32_t a = s[i] < 0 ? -static_cast<int32_t>(s[i]) : s[i];
    if (a >= 32767) {
      a = 32767;
      ++cl;
    }
    if (a > pk) pk = a;
  }
  peak.store(static_cast<uint16_t>(pk));
  if (cl) clips.fetch_add(cl);
}

void RecMeter::take(uint16_t& p, uint32_t& c) {
  p = peak.exchange(0);
  c = clips.exchange(0);
}

int16_t recThreshLevel(int8_t db) {
  if (db == kRecThreshOff) return 0;
  if (db > 0) db = 0;
  return static_cast<int16_t>(lroundf(32767.f * powf(10.f, db / 20.f)));
}

int recGainClamp(RecSource src, int gain) {
  const int hi = src == RecSource::Mic ? 63 : src == RecSource::Line ? 15 : 0;
  return gain < 0 ? 0 : gain > hi ? hi : gain;
}

// --- RecCapture ---

void RecCapture::arm(RecSource src, int8_t threshDb, uint32_t maxFrames) {
  ring_.reset();
  src_ = src;
  level_ = recThreshLevel(threshDb);
  maxFrames_ = maxFrames;
  frames_.store(0);
  overflow_.store(false);
  autoStopped_.store(false);
  state_.store(threshDb == kRecThreshOff ? RecState::Rec : RecState::Armed);
}

static bool reaches(const int16_t* s, uint32_t n, int32_t level) MT_HOT;
static bool reaches(const int16_t* s, uint32_t n, int32_t level) {
  for (uint32_t i = 0; i < n; ++i)
    if (s[i] >= level || -static_cast<int32_t>(s[i]) >= level) return true;
  return false;
}

void RecCapture::feed(const int16_t* l, const int16_t* r, uint32_t n) {
  const bool mono = src_ == RecSource::Mic;
  if (mono) r = l;
  meter_.feed(l, n);
  if (!mono) meter_.feed(r, n);
  const RecState st = state_.load();
  if (st == RecState::Armed) {
    if (!reaches(l, n, level_) && (mono || !reaches(r, n, level_))) {
      ring_.push(l, r, n);  // the consumer does not read yet: keepLast keeps room
      ring_.keepLast(kRecPreRollFrames);
      return;
    }
    ring_.keepLast(kRecPreRollFrames);
    frames_.store(ring_.size());
    state_.store(RecState::Rec);
  } else if (st != RecState::Rec) {
    return;
  }
  const uint32_t done = frames_.load();
  const uint32_t room = maxFrames_ > done ? maxFrames_ - done : 0;
  if (n > room) n = room;
  if (n && !ring_.push(l, r, n)) {
    overflow_.store(true);
    state_.store(RecState::Idle);
    return;
  }
  frames_.store(done + n);
  if (done + n >= maxFrames_) {
    autoStopped_.store(true);
    state_.store(RecState::Idle);
  }
}

uint32_t recSdLimitFrames(uint64_t freeBytes, uint8_t channels) {
  const uint64_t frameBytes = channels == 1 ? 2 : 4;
  if (freeBytes <= kRecSdReserveBytes) return 0;
  uint64_t file = freeBytes - kRecSdReserveBytes;
  if (file > kRecSdMaxFileBytes) file = kRecSdMaxFileBytes;
  if (file <= kWavHeaderBytes) return 0;
  return static_cast<uint32_t>((file - kWavHeaderBytes) / frameBytes);  // < 2^31 frames
}

// --- mono mix, writer ---

void recMonoMix(const int16_t* lr, uint32_t n, int16_t* out) {
  for (uint32_t i = 0; i < n; ++i)
    out[i] = static_cast<int16_t>((static_cast<int32_t>(lr[i * 2]) + lr[i * 2 + 1]) / 2);
}

void RecWriter::begin(uint16_t channels, Sink sink, void* ctx) {
  sink_ = sink;
  ctx_ = ctx;
  ch_ = channels == 1 ? 1 : 2;
  frames_ = crc_ = 0;
  failed_ = false;
}

bool RecWriter::write(const int16_t* lr, uint32_t n) {
  if (failed_) return false;
  if (ch_ == 2 && n && !sink_(lr, n * 4, ctx_)) {
    failed_ = true;
    return false;
  }
  int16_t mono[256];
  for (uint32_t done = 0; done < n;) {
    const uint32_t k = n - done < 256 ? n - done : 256;
    const int16_t* s = lr + done * 2;
    if (ch_ == 2) {
      recMonoMix(s, k, mono);
    } else {
      for (uint32_t i = 0; i < k; ++i) mono[i] = s[i * 2];
      if (!sink_(mono, k * 2, ctx_)) {
        failed_ = true;
        return false;
      }
    }
    crc_ = sampleCrc(mono, k, crc_);
    frames_ += k;
    done += k;
  }
  return true;
}

void RecWriter::header(uint8_t out[kWavHeaderBytes]) const { wavHeader(out, frames_, kRecRate, 60, crc_, ch_); }

// --- names, binding ---

void recName(int n, char out[kSampleNameMax + 1]) { snprintf(out, kSampleNameMax + 1, "REC%d", n); }
void recPath(int n, char out[kRecPathMax]) { snprintf(out, kRecPathMax, "/samples/rec/REC%d.wav", n); }
void recTmpPath(int n, char out[kRecPathMax]) { snprintf(out, kRecPathMax, "/samples/rec/REC%d.wav.tmp", n); }

int nextRecNumber(const SynthModel& p, int from) {
  if (from < 1 || from > kRecNumberMax) from = 1;
  char name[kSampleNameMax + 1];
  for (int i = 0; i < kRecNumberMax; ++i) {
    const int n = (from - 1 + i) % kRecNumberMax + 1;
    recName(n, name);
    if (projSampleFind(p, name) < 0) return n;
  }
  return 0;
}

int recBind(Project& p, int instr, const char* name, uint32_t crc, uint32_t frames, uint8_t root) {
  if (instr < 0 || instr >= kInstruments) return -1;
  const int i = projSampleSet(p, name, crc, frames);
  if (i < 0) return -1;
  Instrument& m = p.instruments[instr];
  sliceClear(m);
  memcpy(m.sample, p.samples[i].name, sizeof(m.sample));  // both kSampleNameMax + 1, terminated
  m.root = root > 127 ? 127 : root;
  return i;
}

}  // namespace mt
