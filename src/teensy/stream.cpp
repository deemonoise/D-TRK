#include "stream.h"
#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include "bank.h"
#include "fs_server.h"
#include "link_server.h"
#include "preview_stream.h"
#include "psram.h"
#include "sample_heads.h"
#include "sample_set.h"
#include "stream_loader.h"
#include "synth.h"
#include "wav.h"

namespace stream {

namespace {

// 96 KB in RAM2 (OCRAM, cached): SdFat SDIO runs in FIFO mode, the CPU copies, no DMA coherence.
DMAMEM int16_t poolMem[mt::kStreamSlots * mt::kSlotBlocks * mt::kStreamBlock] __attribute__((aligned(32)));
mt::StreamPool thePool;
mt::StoreTable theTable;

class SdReader final : public mt::BlockReader {
 public:
  bool read(uint32_t key, uint32_t block, int n, int16_t* out) override {
    const int i = theTable.find(key);
    if (i < 0 || !card::ready()) return false;
    const uint32_t s = theTable.at(i).sector;
    if (!s) return false;
    uint8_t* d = reinterpret_cast<uint8_t*>(out);
    if (SD.sdfs.card()->readSectors(s + 1 + 2 * block, d, 2 * n)) return true;
    // The multi-sector read SdFat SDIO keeps open between calls may have timed out: start over once.
    SD.sdfs.card()->syncDevice();
    return SD.sdfs.card()->readSectors(s + 1 + 2 * block, d, 2 * n);
  }
};
SdReader rd;

class SlotConsumer final : public mt::StreamConsumer {
 public:
  int s = 0;
  bool needs(uint32_t& r) override {
    const mt::StreamSlot& x = thePool.slot(s);
    if (x.owner != mt::SlotOwner::Voice && x.owner != mt::SlotOwner::Prefetch) {
      r = UINT32_MAX;
      return false;
    }
    r = mt::streamReserveUs(thePool, s);
    mt::StreamLoad l;
    return mt::streamPlan(thePool, s, l);
  }
  void load() override { mt::streamLoad(thePool, s, rd); }
};
SlotConsumer slots[mt::kStreamSlots];
mt::StreamConsumer* consumers[mt::kStreamSlots + 1];
int nConsumers = 0;

constexpr int kBgMax = 6;
mt::BgWork* bg[kBgMax];
int nBg = 0, bgNext = 0;

int quietAdds = 0;  // > 0: no log lines (boot, before link::begin)
uint32_t underrunsSeen[kStatReaders] = {};
uint32_t minReserve[kStatReaders] = {UINT32_MAX, UINT32_MAX};
mt::Synth* synthP = nullptr;

constexpr uint32_t kPrefetchMaxMs = 1000;  // a hint whose note never came (CND, PRB, stop)
uint16_t hintsSeen[kStatReaders] = {}, hintSlotsSeen[kStatReaders] = {};

// Sector of a store file's first byte if it is one contiguous run, else 0.
uint32_t firstSector(FsFile& f) {
  uint32_t bgn = 0, end = 0;
  if (!f.contiguousRange(&bgn, &end)) return 0;
  return bgn;
}

}  // namespace

void begin(mt::Synth& s) {
  thePool.init(poolMem);
  for (int i = 0; i < mt::kStreamSlots; ++i) {
    slots[i].s = i;
    consumers[nConsumers++] = &slots[i];
  }
  consumers[nConsumers++] = preview::consumer();
  s.setPool(&thePool);
  synthP = &s;
}

void addBackground(mt::BgWork* w) {
  if (nBg < kBgMax) bg[nBg++] = w;
}

void step() {
  AudioNoInterrupts();
  thePool.expirePrefetch(millis(), kPrefetchMaxMs);
  AudioInterrupts();
  mt::schedulePass(consumers, nConsumers, bg, nBg, bgNext);
  for (int i = 0; i < mt::kStreamSlots; ++i)
    if (thePool.slot(i).owner == mt::SlotOwner::Voice) {
      const uint32_t r = mt::streamReserveUs(thePool, i);
      for (int k = 0; k < kStatReaders; ++k)
        if (r < minReserve[k]) minReserve[k] = r;
    }
}

bool add(uint32_t crc) {
  if (!card::ready()) return false;
  char path[mt::kStorePathMax];
  mt::storePath(crc, path);
  FsFile f = SD.sdfs.open(path, O_RDONLY);
  if (!f) return false;
  struct Src final : mt::ByteSource {
    FsFile* f;
    bool read(void* d, size_t n) override { return f->read(d, n) == static_cast<int>(n); }
    bool skip(size_t n) override { return f->seekCur(static_cast<int64_t>(n)); }
  } src;
  src.f = &f;
  mt::WavInfo w;
  const bool ok = mt::wavParse(src, w) == mt::WavErr::Ok && w.hasCrc && w.crc == crc && w.channels == 1 &&
                  w.bits == 16 && w.dataOffset == mt::kStoreDataOffset;
  const uint32_t sector = ok ? firstSector(f) : 0;
  f.close();
  if (!ok) return false;
  if (!sector && quietAdds == 0) link::log("store: %08lx is not contiguous", static_cast<unsigned long>(crc));
  return theTable.add(crc, w.frames(), w.rate, sector) >= 0;
}

int rescan(const mt::SynthModel& m, bool log) {
  theTable.clear();
  if (!log) ++quietAdds;
  int n = 0;
  const mt::SampleBank& fb = bank::flash();
  for (int k = 0; k < m.sampleCount; ++k) {
    const uint32_t crc = m.samples[k].crc;
    if (!add(crc)) continue;
    ++n;
    // Its heads from the flash bank (entry "h" + crc, written by bank's BankSync / BankImport).
    const int i = theTable.find(crc);
    char key[mt::kSampleNameMax + 1];
    mt::headKey(crc, key);
    const int b = fb.find(key);
    static mt::SampleHead hs[mt::kMaxHeads];
    int nh = 0;
    if (i >= 0 && b >= 0 && theTable.at(i).nHeads == 0 && mt::headParse(fb.data(b), fb.entry(b)->frames, hs, nh) && nh)
      theTable.setHeads(i, hs, nh);
  }
  if (!log) --quietAdds;
  if (log) link::log("store: %d of %d samples", n, m.sampleCount);
  psram::restart(m);  // theTable.clear() dropped the old copies; Off before psram::begin
  return n;
}

void hint(uint8_t track, uint8_t note, uint8_t ofs) {
  if (!synthP) return;
  AudioNoInterrupts();
  const int s = synthP->prefetch(track, note, ofs);
  if (s >= 0) thePool.slotRef(s).stamp = millis();
  AudioInterrupts();
  for (int k = 0; k < kStatReaders; ++k) {
    ++hintsSeen[k];
    hintSlotsSeen[k] += s >= 0;
  }
#ifdef LINK_DESK
  link::log("hint: track %u note %u ofs %u -> slot %d", track, note, ofs, s);
#endif
}

mt::StreamPool& pool() { return thePool; }
mt::StoreTable& table() { return theTable; }
mt::BlockReader& reader() { return rd; }

Stats takeStats(int who) {
  Stats s;
  if (who < 0 || who >= kStatReaders) return s;
  for (int i = 0; i < mt::kStreamSlots; ++i) s.voices += thePool.slot(i).owner == mt::SlotOwner::Voice;
  const uint32_t u = thePool.underruns;
  s.underruns = static_cast<uint16_t>(u - underrunsSeen[who] > 0xFFFF ? 0xFFFF : u - underrunsSeen[who]);
  underrunsSeen[who] = u;
  const uint32_t mr = minReserve[who];
  s.minReserveMs = mr == UINT32_MAX ? 0xFFFF : static_cast<uint16_t>(mr / 1000 > 0xFFFE ? 0xFFFE : mr / 1000);
  minReserve[who] = UINT32_MAX;
  s.hints = hintsSeen[who];
  s.hintSlots = hintSlotsSeen[who];
  hintsSeen[who] = hintSlotsSeen[who] = 0;
  return s;
}

}  // namespace stream
