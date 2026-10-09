#include "fs_server.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include "link_fs.h"
#include "model.h"
#include "preset_paths.h"

using namespace mt::link;

namespace card {

namespace {

class SdBackend : public FsBackend {
 public:
  int16_t ready() override {
    // mediaPresent remounts a card put back in; the folders are made on every new mount.
    const bool in = began_ ? SD.mediaPresent() : SD.begin(BUILTIN_SDCARD);
    began_ = true;
    if (in && !mounted_) {
      makeFolders();
      spaceValid_ = false;
    }
    mounted_ = in;
    return in ? kErrOk : kErrNoCard;
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
  }

  bool began_ = false, mounted_ = false;
  bool spaceValid_ = false;
  uint32_t spaceAt_ = 0, totalMb_ = 0, freeMb_ = 0;
  FsFile files_[kFsMaxOpen];
  FsFile dir_;
};

SdBackend backend;
FsServerCore server(backend);

}  // namespace

void begin() { backend.ready(); }

bool ready() { return backend.ready() == kErrOk; }

void spaceChanged() { backend.dropSpace(); }

int handle(Msg t, uint8_t seq, const uint8_t* p, int n, uint8_t* out) { return server.handle(t, seq, p, n, out); }

void reset() { server.reset(); }

}  // namespace card
