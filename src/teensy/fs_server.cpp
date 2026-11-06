#include "fs_server.h"
#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include <new>
#include <string.h>
#include "fs_deferred.h"
#include "link_fs.h"
#include "link_server.h"
#include "model.h"
#include "preset_paths.h"
#include "rec_server.h"
#include "sector_io.h"
#include "stream.h"

#ifndef DTRK_SDIO_DMA
#define DTRK_SDIO_DMA 1
#endif

using namespace mt::link;

namespace card {

namespace {
bool streamIdle();

// One exclusive transfer area for all filesystem and raw sample reads. DMA never sees caller
// stack / cache buffers; each cache maintenance operation owns whole cache lines.
DMAMEM uint8_t sectorBounce[8192] __attribute__((aligned(32)));

// A filesystem cache never receives a failed sector read. The same bounce path serves the
// sample reader, so enabling DMA cannot bypass coherency for one kind of transfer.
class SafeCard final : public FsBlockDevice, private mt::SectorIoDevice {
 public:
  SafeCard() : io_(*this, sectorBounce, sizeof sectorBounce / 512) {}
  SdCard* c = nullptr;
  bool fault = false;  // a read or write failed for good: the volume is mounted again before use

  bool isBusy() override { return c && !fault && c->isBusy(); }
  uint32_t sectorCount() override { return c ? c->sectorCount() : 0; }
  bool syncDevice() override { return c && !fault && c->syncDevice(); }
  bool readSector(uint32_t s, uint8_t* d) override {
    return readSectors(s, d, 1);
  }
  bool readSectors(uint32_t s, uint8_t* d, size_t n) override {
    if (fault) return false;
    if (n <= UINT32_MAX && io_.read(s, static_cast<uint32_t>(n), d)) return true;
    return failed("read", s, n);
  }
  bool writeSector(uint32_t s, const uint8_t* d) override {
    return writeSectors(s, d, 1);
  }
  bool writeSectors(uint32_t s, const uint8_t* d, size_t n) override {
    if (fault) return false;
    if (n <= UINT32_MAX && io_.write(s, static_cast<uint32_t>(n), d)) return true;
    return failed("write", s, n);
  }

 private:
  bool read(uint32_t sector, uint32_t count, uint8_t* out) override {
    return c && c->readSectors(sector, out, count);
  }
  bool write(uint32_t sector, uint32_t count, const uint8_t* in) override {
    return c && c->writeSectors(sector, in, count);
  }
  bool recover() override {
#if DTRK_SDIO_DMA
    // The SDK's syncDevice does not abort a timed-out DMA. Latch the fault; no bounce reuse
    // or further transfers until an idle full controller restart has quiesced it.
    return false;
#else
    return c && c->syncDevice();
#endif
  }
  void prepareRead(uint8_t* data, uint32_t bytes) override {
#if DTRK_SDIO_DMA
    arm_dcache_flush_delete(data, bytes);
#endif
    (void)data; (void)bytes;
  }
  void finishRead(uint8_t* data, uint32_t bytes) override {
#if DTRK_SDIO_DMA
    arm_dcache_delete(data, bytes);
#endif
    (void)data; (void)bytes;
  }
  void prepareWrite(uint8_t* data, uint32_t bytes) override {
#if DTRK_SDIO_DMA
    arm_dcache_flush_delete(data, bytes);
#endif
    (void)data; (void)bytes;
  }
  bool finishWrite() override {
#if DTRK_SDIO_DMA
    if (!c) return false;
    constexpr uint32_t kReadyBudgetUs = 1000000;
    constexpr uint32_t kStatusErrors =
        CARD_STATUS_OUT_OF_RANGE | CARD_STATUS_ADDRESS_ERROR | CARD_STATUS_SECTOR_LEN_ERROR |
        CARD_STATUS_ERASE_SEQ_ERROR | CARD_STATUS_ERASE_PARAM | CARD_STATUS_WP_VIOLATION |
        CARD_STATUS_CARD_IS_LOCKED | CARD_STATUS_LOCK_UNLOCK_FAILED | CARD_STATUS_COM_CRC_ERROR |
        CARD_STATUS_ILLEGAL_COMMAND | CARD_STATUS_CARD_ECC_FAILED | CARD_STATUS_CC_ERROR |
        CARD_STATUS_ERROR | CARD_STATUS_CSD_OVERWRITE | CARD_STATUS_WP_ERASE_SKIP | CARD_STATUS_AKE_SEQ_ERROR;
    const uint32_t started = micros();
    for (;;) {
      if (micros() - started >= kReadyBudgetUs) return false;
      // DMA TC only completed the bus transfer. The next read must not inherit programming
      // busy from this write. status() also distinguishes a failed CMD13 (0) from normal busy;
      // its SDK command has its own timeout, so this poll budget is not a hard wall-time bound.
      const uint32_t status = c->status();
      if (micros() - started >= kReadyBudgetUs || status == 0 || status == UINT32_MAX ||
          (status & kStatusErrors)) return false;
      if ((status & CARD_STATUS_READY_FOR_DATA) &&
          (status & CARD_STATUS_CURRENT_STATE) == (4u << CARD_STATUS_CURRENT_STATE_SHIFT)) return true;
      yield();
    }
#else
    return true;
#endif
  }
  bool failed(const char* operation, uint32_t sector, size_t count) {
    fault = true;
    ::link::log("sd: %s failed sector %lu count %lu error %u data %08lx", operation,
                static_cast<unsigned long>(sector), static_cast<unsigned long>(count),
                c ? c->errorCode() : 0, static_cast<unsigned long>(c ? c->errorData() : 0));
    return false;
  }
  mt::SectorIo io_;
};

class SdBackend : public FsBackend {
 public:
  int16_t ready() override {
    // A card put back in is remounted; the folders are made on every new mount. Polled before every
    // card access: the status command at most every kCheckMs, after the open multi-sector transfer
    // is ended (a status command inside one shifted the data that followed it).
    constexpr uint32_t kCheckMs = 500;
    if (mounted_ && !dev_.fault && millis() - checkAt_ < kCheckMs) return kErrOk;
    for (FsFile& f : files_)
      if (f.isOpen() && !dev_.fault) return mounted_ ? kErrOk : kErrNoCard;
    bool in;
#if DTRK_SDIO_DMA
    if (!began_) {
      FsDateTime::setCallback(SDClass::dateTime);
      attemptAt_ = millis();
      ++attempts_;
      // Public SdFat entry: remembers DMA for restart(). The SDK's initialization and CMD6
      // buffer remain native; the project mounts through SafeCard before any file operation.
      in = SD.sdfs.begin(SdioConfig(DMA_SDIO));
    } else if (!mounted_ || dev_.fault) {
      const uint32_t wait = attempts_ < 3 ? 1000 : 30000;
      if (millis() - attemptAt_ < wait || rec::busy() || !streamIdle()) return kErrNoCard;
      if (attempts_ >= 3) attempts_ = 0;
      // Close stale handles while the faulted block device still rejects writes, before
      // restarting the card / volume. FatFile::close marks them closed even if sync fails.
      dev_.fault = true;
      for (FsFile& f : files_) f.close();
      dir_.close();
      attemptAt_ = millis();
      ++attempts_;
      in = SD.sdfs.restart();
    } else {
      const uint32_t status = SD.sdfs.card() && SD.sdfs.card()->syncDevice()
                                  ? SD.sdfs.card()->status() : 0;
      in = status != 0 && status != UINT32_MAX;
    }
#else
    if (!began_) {
      in = SD.begin(BUILTIN_SDCARD);
    } else {
      if (SD.sdfs.card()) SD.sdfs.card()->syncDevice();
      in = SD.mediaPresent();
    }
#endif
    began_ = true;
    checkAt_ = millis();
    const bool fresh = in && (!mounted_ || dev_.fault);
    if (fresh) {
      // SD.begin and the remount in mediaPresent mount on the bare card: mount again through
      // SafeCard (a fault: the cache is dropped this way too).
      dev_.c = SD.sdfs.card();
      dev_.fault = false;
      in = static_cast<FsVolume&>(SD.sdfs).begin(&dev_);
      if (in) makeFolders();
      spaceValid_ = false;
    }
    mounted_ = in;
#if DTRK_SDIO_DMA
    if (in) attempts_ = 0;
#endif
    return in ? kErrOk : kErrNoCard;
  }

  bool readSectors(uint32_t sector, uint32_t count, uint8_t* out) {
    return mounted_ && dev_.readSectors(sector, out, count);
  }

  void logState() {
    SdCard* c = SD.sdfs.card();
    ::link::log("sd: %s mounted %u fault %u attempts %u error %u data %08lx kHz %lu",
                DTRK_SDIO_DMA ? "DMA" : "FIFO", mounted_, dev_.fault, attempts_,
                c ? c->errorCode() : 0, static_cast<unsigned long>(c ? c->errorData() : 0),
                static_cast<unsigned long>(c ? static_cast<SdioCard*>(c)->kHzSdClk() : 0));
  }

  int16_t stat(const char* path, bool& isDir, uint32_t& size) override {
    FsFile f = SD.sdfs.open(path, O_RDONLY);
    if (!f) return kErrNotFound;
    isDir = f.isDir();
    size = isDir ? 0 : clip(f.fileSize());
    f.close();
    return kErrOk;
  }

  int16_t open(int slot, const char* path, FsMode mode, uint32_t& size) override {
    const oflag_t flags = mode == FsMode::Read ? O_RDONLY : (mode == FsMode::Write ? O_RDWR | O_CREAT | O_TRUNC : O_RDWR | O_CREAT);
    files_[slot] = SD.sdfs.open(path, flags);
    if (!files_[slot]) return mode == FsMode::Read ? kErrNotFound : kErrIo;
    size = clip(files_[slot].fileSize());
    return kErrOk;
  }

  int read(int slot, uint32_t pos, uint8_t* d, int n) override {
    FsFile& f = files_[slot];
    if (f.curPosition() != pos && !f.seekSet(pos)) return kErrIo;
    const int k = f.read(d, n);
    return k < 0 ? kErrIo : k;
  }

  int write(int slot, uint32_t pos, const uint8_t* d, int n) override {
    FsFile& f = files_[slot];
    if (f.curPosition() != pos && !f.seekSet(pos)) return kErrIo;
    const size_t k = f.write(d, n);
    return k == 0 && n > 0 ? kErrIo : static_cast<int>(k);  // short: the card is full
  }

  int16_t close(int slot) override { return files_[slot].close() ? kErrOk : kErrIo; }

  int16_t dirOpen(const char* path) override {
    dir_ = SD.sdfs.open(path, O_RDONLY);
    if (dir_ && dir_.isDir()) return kErrOk;
    dir_.close();
    return kErrNotFound;
  }

  bool dirNext(FsEntry& e) override {
    FsFile f;
    while (f.openNext(&dir_, O_RDONLY)) {
      f.getName(e.name, sizeof e.name);
      e.isDir = f.isDir();
      e.size = e.isDir ? 0 : clip(f.fileSize());
      f.close();
      if (strcmp(e.name, ".") != 0 && strcmp(e.name, "..") != 0) return true;
    }
    return false;
  }

  int16_t dirError() const override {
    return dev_.fault || dir_.getError() ? kErrIo : kErrOk;
  }

  void dirClose() override { dir_.close(); }

  int16_t remove(const char* path) override {
    if (!SD.sdfs.exists(path)) return kErrNotFound;
    return SD.sdfs.remove(path) ? kErrOk : kErrIo;
  }

  int16_t rename(const char* from, const char* to) override {
    if (!SD.sdfs.exists(from)) return kErrNotFound;
    if (SD.sdfs.exists(to)) return kErrExists;
    return SD.sdfs.rename(from, to) ? kErrOk : kErrIo;
  }

  int16_t mkdir(const char* path) override {
    if (SD.sdfs.exists(path)) return kErrExists;
    return SD.sdfs.mkdir(path, false) ? kErrOk : kErrIo;
  }

  int16_t rmdir(const char* path) override { return SD.sdfs.rmdir(path) ? kErrOk : kErrNotEmpty; }

  // FAT32 counts its free clusters by reading the whole FAT (a large card: a few hundred ms in
  // loop()): done at most every kSpaceMs, the ESP caches it as well.
  void dropSpace() { spaceValid_ = false; }

  // FsCopy between chunks: a Status when due and the watchdog fed (sends only; no frame is read).
  void poll() override { ::link::statusIfDue(); }

  int16_t space(uint32_t& totalMb, uint32_t& freeMb) override {
    constexpr uint32_t kSpaceMs = 10000;
    if (!spaceValid_ || millis() - spaceAt_ >= kSpaceMs) {
      const uint64_t cl = SD.sdfs.bytesPerCluster();
      const int32_t freeCl = SD.sdfs.freeClusterCount();
      if (cl == 0 || freeCl < 0) return kErrIo;
      totalMb_ = static_cast<uint32_t>(cl * SD.sdfs.clusterCount() >> 20);
      freeMb_ = static_cast<uint32_t>(cl * static_cast<uint32_t>(freeCl) >> 20);
      spaceAt_ = millis();
      spaceValid_ = true;
    }
    totalMb = totalMb_;
    freeMb = freeMb_;
    return kErrOk;
  }

 private:
  static uint32_t clip(uint64_t v) { return v > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(v); }

  static void folder(const char* p) {
    if (!SD.sdfs.exists(p)) SD.sdfs.mkdir(p, false);
  }

  // The tree the tracker expects (the ESP's own card had it).
  static void makeFolders() {
    static const char* const kDirs[] = {"/projects", "/midi",      "/samples", "/wavetables",
                                        "/presets",  "/templates", "/diag",    "/firmware"};
    for (const char* d : kDirs) folder(d);
    for (int t = 0; t < static_cast<int>(mt::InstrType::Count); ++t)
      if (mt::presetTypeHas(static_cast<mt::InstrType>(t))) folder(mt::presetRoot(static_cast<mt::InstrType>(t)));
    folder("/presets/TUN");  // user tunings (.scl), read by the PROJ Tuning row
  }

  SafeCard dev_;
  bool began_ = false, mounted_ = false;
  uint32_t checkAt_ = 0;
  uint32_t attemptAt_ = 0;
  uint8_t attempts_ = 0;
  bool spaceValid_ = false;
  uint32_t spaceAt_ = 0, totalMb_ = 0, freeMb_ = 0;
  FsFile files_[kFsMaxOpen];
  FsFile dir_;
};

SdBackend backend;
FsServerCore server(backend);
void sendReply(void*, Msg type, uint8_t seq, const uint8_t* payload, int size) {
  ::link::reply(type, seq, payload, size);
}
// The copied wire request and last reply must not consume the tight audio stack / RAM1.
DMAMEM FsDeferred deferred(server, sendReply, nullptr);
bool (*audioIdle)() = nullptr;

bool streamIdle() {
  // Owners and the event queue form one snapshot: otherwise the audio interrupt could consume
  // a queued SAMPLE event between the slot scan and the empty-queue check. No link frames are
  // read while a long operation runs, so the single loop producer cannot add another event.
  AudioNoInterrupts();
  bool idle = audioIdle && audioIdle();
  const mt::StreamPool& pool = stream::pool();
  for (int i = 0; idle && i < pool.slots(); ++i)
    if (pool.slot(i).owner != mt::SlotOwner::Free) idle = false;
  AudioInterrupts();
  return idle;
}

struct DeferredWork final : mt::BgWork {
  uint32_t minReserveUs() const override { return FsDeferred::kReserveUs; }
  bool step() override {
    if (!deferred.waiting()) return false;
    // Recording owns the card; requests still expire through card::step without touching it.
    if (rec::busy()) return false;
    return deferred.step(millis(), streamIdle());
  }
} deferredWork;

}  // namespace

void begin() {
  // DMAMEM is NOLOAD: establish all guards explicitly before a first Hello or filesystem job.
  new (&deferred) FsDeferred(server, sendReply, nullptr);
  backend.ready();
}

void setAudioIdle(bool (*check)()) { audioIdle = check; }

bool ready() { return backend.ready() == kErrOk; }

bool readSectors(uint32_t sector, uint32_t count, uint8_t* out) {
  return backend.readSectors(sector, count, out);
}

void logState() { backend.logState(); }

void spaceChanged() { backend.dropSpace(); }

int handle(Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out) {
  return deferred.submit(t, seq, p, n, millis(), out);
}

mt::BgWork* background() { return &deferredWork; }

void step() { deferred.expire(millis()); }

void reset() { deferred.reset(); }

}  // namespace card
