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

// The raw I/O bounce owns DMA coherence; the audio pool is written only by the CPU.
constexpr uint32_t kPoolStride = mt::kSlotBlocks * mt::kStreamBlock;
DMAMEM int16_t poolMem[mt::kStreamSlots * kPoolStride] __attribute__((aligned(32)));
mt::StreamPool thePool;
mt::StoreTable theTable;

// Only fragmented files need runs. Metadata is built in loop(), then published immutably;
// neither the audio interrupt nor a stream refill has to traverse FAT / exFAT cluster chains.
constexpr int kExtentCapacity = 1024;
DMAMEM mt::SectorExtent extents[kExtentCapacity];
DMAMEM mt::SectorMap maps[mt::kStoreEntries];
int usedExtents = 0;
#ifdef LINK_DESK
Profile profile;
#endif

class RawReader final : public mt::SectorReader {
 public:
  bool read(uint32_t sector, uint32_t count, uint8_t* out) override {
#ifdef LINK_DESK
    const uint32_t start = ARM_DWT_CYCCNT;
#endif
    const bool ok = card::readSectors(sector, count, out);
#ifdef LINK_DESK
    const uint32_t elapsed = ARM_DWT_CYCCNT - start;
    profile.readCycles += elapsed;
    ++profile.reads;
    profile.readBlocks += count;
    profile.readFailures += !ok;
    if (elapsed > profile.readPeakCycles) profile.readPeakCycles = elapsed;
#endif
    return ok;
  }
};
RawReader raw;

class SdReader final : public mt::BlockReader {
 public:
  bool read(uint32_t key, uint32_t block, int n, int16_t* out) override {
    const int i = theTable.find(key);
    if (i < 0 || n <= 0 || n > mt::kLoadMax || !card::ready()) return false;
    const mt::StoreEntry& entry = theTable.at(i);
    mt::SectorMap contiguous;
    contiguous.first = entry.sector;
    contiguous.sectors = 1 + static_cast<uint32_t>((uint64_t(entry.frames) * 2 + 511) / 512);
    const mt::SectorMap& file = entry.sectors ? *entry.sectors : contiguous;
    const uint64_t at = 1 + uint64_t(block) * 2;
    return at <= UINT32_MAX && file.read(static_cast<uint32_t>(at), 2 * n,
                                        reinterpret_cast<uint8_t*>(out), raw);
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
#ifdef LINK_DESK
    const uint32_t start = ARM_DWT_CYCCNT;
#endif
    const bool need = mt::streamNeeds(thePool, s, r);
#ifdef LINK_DESK
    profile.needsCycles += ARM_DWT_CYCCNT - start;
    ++profile.needsCalls;
#endif
    return need;
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

bool mapFile(FsFile& file, uint32_t sectors, mt::SectorMap& map, int& count) {
  const uint32_t clusterSectors = SD.sdfs.sectorsPerCluster();
  if (!clusterSectors) return false;
  mt::SectorExtent* runs = extents + usedExtents;
  count = 0;
  for (uint32_t at = 0; at < sectors;) {
    // SdFat's saved position records the cluster containing position-1, in both file systems.
    if (!file.seekSet(uint64_t(at) * 512 + 1)) return false;
    fspos_t position;
    file.fgetpos(&position);
    if (position.cluster < 2 || position.cluster - 2 >= SD.sdfs.clusterCount()) return false;
    uint32_t take = sectors - at;
    if (take > clusterSectors) take = clusterSectors;
    const uint64_t physical = uint64_t(SD.sdfs.dataStartSector()) +
                              uint64_t(position.cluster - 2) * clusterSectors;
    if (physical > UINT32_MAX ||
        !mt::appendSectorExtent(runs, count, kExtentCapacity - usedExtents, at,
                                static_cast<uint32_t>(physical), take)) return false;
    at += take;
  }
  map.first = 0;
  map.sectors = sectors;
  map.extents = runs;
  map.count = static_cast<uint16_t>(count);
  return count != 0;
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
#ifdef LINK_DESK
  const uint32_t start = ARM_DWT_CYCCNT;
#endif
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
#ifdef LINK_DESK
  const uint32_t elapsed = ARM_DWT_CYCCNT - start;
  profile.stepCycles += elapsed;
  if (elapsed > profile.stepPeakCycles) profile.stepPeakCycles = elapsed;
#endif
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
                  w.bits == 16 && w.dataOffset == mt::kStoreDataOffset && w.frames() &&
                  f.fileSize() >= uint64_t(w.dataOffset) + w.dataBytes;
  const uint32_t sector = ok ? firstSector(f) : 0;
  if (!ok) return false;
  if (theTable.find(crc) >= 0) return true;
  if (theTable.count() >= mt::kStoreEntries) return false;
  const mt::SectorMap* map = nullptr;
  int count = 0;
  if (!sector) {
    mt::SectorMap& mapped = maps[theTable.count()];
    const uint32_t sectors = 1 + static_cast<uint32_t>((uint64_t(w.frames()) * 2 + 511) / 512);
    if (!mapFile(f, sectors, mapped, count)) {
      if (quietAdds == 0) link::log("store: %08lx invalid chain or extent pool full", static_cast<unsigned long>(crc));
      return false;
    }
    map = &mapped;
    if (quietAdds == 0) link::log("store: %08lx mapped %d extents", static_cast<unsigned long>(crc), count);
  }
  const bool added = theTable.add(crc, w.frames(), w.rate, sector, map) >= 0;
  if (added) usedExtents += count;
  return added;
}

int rescan(const mt::SynthModel& m, bool log) {
  theTable.clear();
  usedExtents = 0;
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

#ifdef LINK_DESK
Profile takeProfile() {
  const Profile taken = profile;
  profile = Profile{};
  return taken;
}
#endif

}  // namespace stream
