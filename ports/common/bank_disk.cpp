#include <FS.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include "bank_ops.h"
#include "brick_runtime.h"
#include "engine_native.h"
#include "wt_mip.h"

namespace {
class Reader final : public mt::ReadFile {
 public:
  FILE* f{};
  uint32_t length{};
  bool seek(uint32_t at) override { return fseeko(f, at, SEEK_SET) == 0; }
  bool read(void* buf, size_t n) override { return fread(buf, 1, n, f) == n; }
  uint32_t size() override { return length; }
  bool skip(size_t n) override {
    auto p = ftello(f);
    return p >= 0 && uint64_t(p) + n <= length && fseeko(f, n, SEEK_CUR) == 0;
  }
};
class Writer final : public mt::WriteFile {
 public:
  FILE* f{};
  bool write(const void* buf, size_t n) override { return fwrite(buf, 1, n, f) == n; }
  bool seek(uint32_t at) override { return fseeko(f, at, SEEK_SET) == 0; }
};
class Disk final : public mt::BankDisk {
  Reader reader;
  Writer writer;
  fs::FS fs;

 public:
  ~Disk() {
    closeRead();
    closeWrite();
  }
  bool ready() override {
    std::error_code ec;
    return std::filesystem::is_directory(fs::root(), ec);
  }
  mt::ReadFile* openRead(const char* path) override {
    if (reader.f) return nullptr;
    reader.f = fopen(fs::resolve(path).c_str(), "rb");
    if (!reader.f) return nullptr;
    struct stat st{};
    if (fstat(fileno(reader.f), &st) || !S_ISREG(st.st_mode) || st.st_size > UINT32_MAX) {
      closeRead();
      return nullptr;
    }
    reader.length = st.st_size;
    return &reader;
  }
  void closeRead() override {
    if (reader.f) fclose(reader.f);
    reader.f = nullptr;
  }
  mt::WriteFile* openWrite(const char* path) override {
    if (writer.f) return nullptr;
    writer.f = fopen(fs::resolve(path).c_str(), "wb");
    return writer.f ? &writer : nullptr;
  }
  bool closeWrite() override {
    if (!writer.f) return true;
    bool ok = fflush(writer.f) == 0 && fsync(fileno(writer.f)) == 0;
    if (fclose(writer.f)) ok = false;
    writer.f = nullptr;
    return ok;
  }
  bool exists(const char* p) override { return fs.exists(p); }
  bool remove(const char* p) override { return fs.remove(p); }
  bool mkdir(const char* p) override { return fs.mkdir(p); }
  bool rename(const char* a, const char* b) override { return fs.rename(a, b); }
} card;

// MAP_SHARED gives the DSP stable, read-only sample pointers. All writes go via pwrite so
// a full SD card returns a normal import error rather than a SIGBUS on a dirty mapped page.
class Cache final : public mt::BankFlash {
  static constexpr uint32_t bytes = 256u << 20;
  int fd = -1;
  uint8_t* map = nullptr;

 public:
  ~Cache() {
    if (map) munmap(map, bytes);
    if (fd >= 0) close(fd);
  }
  bool init() {
    if (map) return true;
    std::error_code ec;
    std::filesystem::create_directories(fs::root(), ec);
    if (ec) return false;
    fd = open(fs::resolve("/sample-cache-v1.bank").c_str(), O_RDWR | O_CREAT, 0600);
    if (fd < 0) return false;
    if (!flock(fd, LOCK_EX | LOCK_NB) && !ftruncate(fd, bytes)) {
      void* p = mmap(nullptr, bytes, PROT_READ, MAP_SHARED, fd, 0);
      if (p != MAP_FAILED) {
        map = static_cast<uint8_t*>(p);
        return true;
      }
    }
    close(fd);
    fd = -1;
    return false;
  }
  uint32_t size() const override { return map ? bytes : 0; }
  const uint8_t* mapped() const override { return map; }
  bool read(uint32_t off, void* buf, uint32_t n) override {
    if (!map || uint64_t(off) + n > bytes) return false;
    std::memcpy(buf, map + off, n);
    return true;
  }
  // POSIX permits overwriting every byte. SampleBank rewrites all bytes of each table or sample.
  bool erase(uint32_t off, uint32_t n) override { return map && uint64_t(off) + n <= bytes; }
  bool write(uint32_t off, const void* buf, uint32_t n) override {
    if (!map || uint64_t(off) + n > bytes || pwrite(fd, buf, n, off) != n) return false;
    return off >= mt::kBankHeader || fsync(fd) == 0;
  }
} cache;
mt::SampleBank samples(cache);
std::unique_ptr<mt::BankSampleSource> source;
std::unique_ptr<mt::BankWtSource> waves;
}  // namespace

namespace brick {
mt::BankDisk& disk() { return card; }
mt::SampleBank& bank() { return samples; }
void beginBank(mt::Project& p) {
  if (!cache.init() || !samples.mount()) return;
  source = std::make_unique<mt::BankSampleSource>(samples, p);
  waves = std::make_unique<mt::BankWtSource>(samples, p);
  std::vector<int16_t> table(mt::kWtTableSamples);
  mt::bankAddBuiltins(samples, p, table.data());
  synth().setBank(source.get());
  synth().setWavetables(waves.get());
}
}  // namespace brick
