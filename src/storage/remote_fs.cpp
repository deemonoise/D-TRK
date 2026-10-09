#include "remote_fs.h"
#include <Arduino.h>
#include <FSImpl.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "link/link.h"
#include "link_fs.h"

using namespace mt::link;

namespace storage {

namespace {

constexpr uint32_t kFsTimeoutMs = 2000;  // per try: a write may wait on the card
constexpr size_t kBuf = 4096;
constexpr uint8_t kListPage = 32;

bool xfer(void*, Msg t, const uint8_t* p, int n, uint8_t* reply, int& replyLen) {
  if (!slink::synthUp()) return false;  // fail at once instead of three timeouts
  return slink::request(t, p, n, t, reply, replyLen, kFsTimeoutMs);
}

FsClientCore client(xfer, nullptr);

FsMode modeOf(const char* m) {
  if (m && m[0] == 'w') return FsMode::Write;
  if (m && m[0] == 'a') return FsMode::Append;
  return FsMode::Read;
}

class RemoteFileImpl : public fs::FileImpl {
 public:
  RemoteFileImpl(const char* path, FsMode mode, uint8_t h, bool isDir, uint32_t size)
      : mode_(mode), h_(h), isDir_(isDir), size_(size), pos_(mode == FsMode::Append ? size : 0) {
    strlcpy(path_, path, sizeof path_);
    const char* slash = strrchr(path_, '/');
    name_ = slash ? slash + 1 : path_;
    if (!isDir_) {  // PSRAM: nothing does DMA from it, and internal RAM is short
      buf_ = static_cast<uint8_t*>(heap_caps_malloc(kBuf, MALLOC_CAP_SPIRAM));
      if (!buf_) buf_ = static_cast<uint8_t*>(malloc(kBuf));
    }
  }
  ~RemoteFileImpl() override {
    close();
    heap_caps_free(buf_);
    delete page_;
  }

  size_t write(const uint8_t* d, size_t n) override {
    if (!open_ || isDir_ || mode_ == FsMode::Read || failed_) return 0;
    if (!buf_) return direct(d, n);
    if (bufLen_ && state_ == State::Reading) bufLen_ = 0;
    state_ = State::Writing;
    size_t done = 0;
    while (done < n) {
      if (bufLen_ == 0) bufAt_ = pos_;
      const size_t k = n - done < kBuf - bufLen_ ? n - done : kBuf - bufLen_;
      memcpy(buf_ + bufLen_, d + done, k);
      bufLen_ += k;
      done += k;
      pos_ += k;
      if (pos_ > size_) size_ = pos_;
      if (bufLen_ == kBuf && !flushWrites()) return done - k;
    }
    return done;
  }

  size_t read(uint8_t* d, size_t n) override {
    if (!open_ || isDir_ || mode_ != FsMode::Read) return 0;
    size_t done = 0;
    while (done < n && pos_ < size_) {
      if (buf_ && bufLen_ && pos_ >= bufAt_ && pos_ < bufAt_ + bufLen_) {
        const size_t off = pos_ - bufAt_;
        const size_t k = n - done < bufLen_ - off ? n - done : bufLen_ - off;
        memcpy(d + done, buf_ + off, k);
        done += k;
        pos_ += k;
        continue;
      }
      // A long read goes straight into d; a short one fills the look-ahead.
      if (!buf_ || n - done >= kBuf) {
        const int k = client.read(h_, pos_, d + done, static_cast<int>(n - done));
        if (k <= 0) break;
        done += k;
        pos_ += k;
        continue;
      }
      if (!fill()) break;
    }
    return done;
  }

  void flush() override { flushWrites(); }

  bool seek(uint32_t pos, fs::SeekMode mode) override {
    if (!open_ || isDir_) return false;
    int64_t to = pos;
    if (mode == fs::SeekCur) to += pos_;
    else if (mode == fs::SeekEnd) to += size_;
    if (to < 0 || to > size_ || !flushWrites()) return false;
    pos_ = static_cast<uint32_t>(to);
    return true;
  }

  size_t position() const override { return pos_; }
  size_t size() const override { return size_; }
  bool setBufferSize(size_t) override { return true; }

  void close() override {
    if (!open_) return;
    open_ = false;
    if (isDir_) return;
    flushWrites();
    client.close(h_);
  }

  time_t getLastWrite() override { return 0; }
  const char* path() const override { return path_; }
  const char* name() const override { return name_; }
  boolean isDirectory() override { return isDir_; }

  fs::FileImplPtr openNextFile(const char* mode) override {
    FsEntry e;
    char p[kFsPathMax];
    if (!nextEntry(e) || !child(e.name, p)) return fs::FileImplPtr();
    return openRemote(p, mode);
  }

  boolean seekDir(long position) override {
    if (!isDir_ || position < 0) return false;
    listAt_ = static_cast<uint16_t>(position);
    pageLoaded_ = false;
    return true;
  }

  String getNextFileName() override { return getNextFileName(nullptr); }

  String getNextFileName(bool* isDir) override {
    FsEntry e;
    char p[kFsPathMax];
    if (!nextEntry(e) || !child(e.name, p)) return String();
    if (isDir) *isDir = e.isDir;
    return String(p);
  }

  void rewindDirectory() override { seekDir(0); }

  operator bool() override { return open_; }

  static fs::FileImplPtr openRemote(const char* path, const char* mode) {
    const FsMode m = modeOf(mode);
    uint8_t h = 0;
    bool isDir = false;
    uint32_t size = 0;
    if (client.open(path, m, h, isDir, size) != kErrOk) return fs::FileImplPtr();
    return std::make_shared<RemoteFileImpl>(path, m, h, isDir, size);
  }

 private:
  enum class State : uint8_t { Reading, Writing };

  // The look-ahead from pos_: up to kBuf bytes in frame-sized reads.
  bool fill() {
    state_ = State::Reading;
    bufAt_ = pos_;
    bufLen_ = 0;
    while (bufLen_ < kBuf && bufAt_ + bufLen_ < size_) {
      const int k = client.read(h_, bufAt_ + bufLen_, buf_ + bufLen_, static_cast<int>(kBuf - bufLen_));
      if (k <= 0) break;
      bufLen_ += k;
    }
    return bufLen_ > 0;
  }

  bool flushWrites() {
    if (state_ != State::Writing || bufLen_ == 0) return !failed_;
    const size_t n = bufLen_;
    bufLen_ = 0;
    if (!failed_ && direct(buf_, n, bufAt_) != n) failed_ = true;
    return !failed_;
  }

  // Writes d at at (default: pos_, which then moves on) in frame-sized requests; bytes written.
  size_t direct(const uint8_t* d, size_t n, uint32_t at = UINT32_MAX) {
    const bool advance = at == UINT32_MAX;
    if (advance) at = pos_;
    size_t done = 0;
    while (done < n) {
      const int k = client.write(h_, at + done, d + done, static_cast<int>(n - done));
      if (k <= 0) break;
      done += k;
    }
    if (advance) {
      pos_ += done;
      if (pos_ > size_) size_ = pos_;
    }
    if (done < n) failed_ = true;
    return done;
  }

  bool nextEntry(FsEntry& e) {
    if (!isDir_ || !open_) return false;
    if (!page_) page_ = new FsListPage();
    for (;;) {
      if (pageLoaded_ && page_->next(e)) {
        ++listAt_;
        return true;
      }
      if (pageLoaded_ && !page_->more()) return false;
      if (client.list(path_, listAt_, kListPage, *page_) != kErrOk) return false;
      pageLoaded_ = true;
      if (page_->count() == 0) return false;
    }
  }

  bool child(const char* name, char* out) const {
    const size_t d = strlen(path_);
    const bool slash = d > 0 && path_[d - 1] == '/';
    return snprintf(out, kFsPathMax, slash ? "%s%s" : "%s/%s", path_, name) < kFsPathMax;
  }

  char path_[kFsPathMax];
  const char* name_;
  FsMode mode_;
  uint8_t h_;
  bool isDir_;
  bool open_ = true;
  bool failed_ = false;  // a write was lost: later ones are refused
  uint32_t size_, pos_;
  // Read look-ahead or pending writes (state_), bytes bufAt_..bufAt_ + bufLen_ of the file.
  uint8_t* buf_ = nullptr;
  uint32_t bufAt_ = 0;
  size_t bufLen_ = 0;
  State state_ = State::Reading;
  // Folder listing: the page holding entry listAt_ - 1, and the index of the next entry.
  FsListPage* page_ = nullptr;
  bool pageLoaded_ = false;
  uint16_t listAt_ = 0;
};

class RemoteFSImpl : public fs::FSImpl {
 public:
  fs::FileImplPtr open(const char* path, const char* mode, const bool) override {
    return RemoteFileImpl::openRemote(path, mode);
  }
  bool exists(const char* path) override {
    bool isDir;
    uint32_t size;
    return client.stat(path, isDir, size) == kErrOk;
  }
  bool rename(const char* from, const char* to) override { return client.rename(from, to) == kErrOk; }
  bool remove(const char* path) override { return client.remove(path) == kErrOk; }
  bool mkdir(const char* path) override { return client.mkdir(path) == kErrOk; }
  // The folder and everything in it (project folders).
  bool rmdir(const char* path) override { return client.rmdir(path) == kErrOk; }
};

}  // namespace

fs::FS& remoteFs() {
  static fs::FS fs(std::make_shared<RemoteFSImpl>());
  return fs;
}

int16_t remoteLastError() { return client.lastError(); }

bool remoteSpace(uint32_t& totalMb, uint32_t& freeMb) {
  constexpr uint32_t kCacheMs = 10000;
  static bool ok = false;
  static uint32_t at = 0, total = 0, free = 0;
  if (at == 0 || millis() - at >= kCacheMs) {
    ok = client.space(total, free) == kErrOk;
    at = millis() | 1;
  }
  totalMb = total;
  freeMb = free;
  return ok;
}

}  // namespace storage
