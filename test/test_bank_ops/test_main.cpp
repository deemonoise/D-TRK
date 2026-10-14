#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unity.h>
#include <string>
#include <vector>
#include "bank_ops.h"
#include "sample_set.h"
#include "sample_store.h"
#include "wave_peaks.h"
#include "wt_builtin.h"
#include "wt_file.h"
#include "wt_mip.h"

using namespace mt;

// NOR flash in RAM (as test_sample_bank).
struct RamFlash final : BankFlash {
  std::vector<uint8_t> mem;
  bool badWrite = false;
  explicit RamFlash(uint32_t n) : mem(n, 0x00) {}
  uint32_t size() const override { return static_cast<uint32_t>(mem.size()); }
  bool read(uint32_t off, void* d, uint32_t n) override {
    if (off + n > mem.size()) return false;
    memcpy(d, mem.data() + off, n);
    return true;
  }
  bool erase(uint32_t off, uint32_t n) override {
    if (off % kBankAlign || n % kBankAlign || off + n > mem.size()) {
      badWrite = true;
      return false;
    }
    memset(mem.data() + off, 0xFF, n);
    return true;
  }
  bool write(uint32_t off, const void* d, uint32_t n) override {
    if (off + n > mem.size()) return false;
    for (uint32_t i = 0; i < n; ++i)
      if (mem[off + i] != 0xFF) badWrite = true;
    memcpy(mem.data() + off, d, n);
    return true;
  }
  const uint8_t* mapped() const override { return map ? mem.data() : nullptr; }
  bool map = true;
};

// The card: a temporary folder.
struct PosixRead final : ReadFile {
  FILE* f = nullptr;
  bool read(void* d, size_t n) override { return fread(d, 1, n, f) == n; }
  bool skip(size_t n) override { return fseek(f, static_cast<long>(n), SEEK_CUR) == 0; }
  uint32_t size() override {
    const long at = ftell(f);
    fseek(f, 0, SEEK_END);
    const long s = ftell(f);
    fseek(f, at, SEEK_SET);
    return static_cast<uint32_t>(s);
  }
  bool seek(uint32_t pos) override { return fseek(f, static_cast<long>(pos), SEEK_SET) == 0; }
};
struct PosixWrite final : WriteFile {
  FILE* f = nullptr;
  bool write(const void* d, size_t n) override { return fwrite(d, 1, n, f) == n; }
  bool seek(uint32_t pos) override { return fseek(f, static_cast<long>(pos), SEEK_SET) == 0; }
};
struct PosixDisk final : BankDisk {
  std::string root;
  PosixRead r;
  PosixWrite w;
  bool present = true;
  std::string full(const char* p) const { return root + p; }
  bool ready() override { return present; }
  ReadFile* openRead(const char* path) override {
    struct stat st;
    if (stat(full(path).c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return nullptr;
    r.f = fopen(full(path).c_str(), "rb");
    return r.f ? &r : nullptr;
  }
  void closeRead() override {
    if (r.f) fclose(r.f);
    r.f = nullptr;
  }
  WriteFile* openWrite(const char* path) override {
    w.f = fopen(full(path).c_str(), "wb");
    return w.f ? &w : nullptr;
  }
  bool closeWrite() override {
    const bool ok = w.f && fclose(w.f) == 0;
    w.f = nullptr;
    return ok;
  }
  bool exists(const char* path) override {
    struct stat st;
    return stat(full(path).c_str(), &st) == 0;
  }
  bool remove(const char* path) override { return ::remove(full(path).c_str()) == 0; }
  bool rename(const char* from, const char* to) override { return ::rename(full(from).c_str(), full(to).c_str()) == 0; }
  bool mkdir(const char* path) override { return ::mkdir(full(path).c_str(), 0755) == 0; }
};

static RamFlash* flash;
static SampleBank* bank;
static PosixDisk* disk;
static Project* proj;

void setUp() {
  flash = new RamFlash(4 * 1024 * 1024);
  bank = new SampleBank(*flash);
  TEST_ASSERT_TRUE(bank->mount());
  disk = new PosixDisk;
  char tmpl[] = "/tmp/mt_bank_ops_XXXXXX";
  TEST_ASSERT_NOT_NULL(mkdtemp(tmpl));
  disk->root = tmpl;
  disk->mkdir("/projects");
  proj = new Project;
}
void tearDown() {
  TEST_ASSERT_FALSE_MESSAGE(flash->badWrite, "write to a non-erased byte or unaligned erase");
  const std::string cmd = "rm -rf '" + disk->root + "'";
  TEST_ASSERT_EQUAL(0, system(cmd.c_str()));
  delete proj;
  delete disk;
  delete bank;
  delete flash;
}

static std::vector<int16_t> ramp(uint32_t frames, int seed) {
  std::vector<int16_t> v(frames);
  for (uint32_t i = 0; i < frames; ++i) v[i] = static_cast<int16_t>(seed * 1000 + i * 7);
  return v;
}

// A plain 16-bit WAV (no mtcr / smpl chunks) of channels x frames.
static void writePlainWav(const char* path, const std::vector<int16_t>& d, int channels, uint32_t rate) {
  FILE* f = fopen(disk->full(path).c_str(), "wb");
  TEST_ASSERT_NOT_NULL(f);
  const uint32_t bytes = static_cast<uint32_t>(d.size() * 2);
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f);
  u32(36 + bytes);
  fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);
  u16(static_cast<uint16_t>(channels));
  u32(rate);
  u32(rate * 2 * channels);
  u16(static_cast<uint16_t>(2 * channels));
  u16(16);
  fwrite("data", 1, 4, f);
  u32(bytes);
  fwrite(d.data(), 2, d.size(), f);
  fclose(f);
}

static BankResult run(BankJob& j) {
  BankResult r;
  int guard = 0;
  while ((r = j.step()) == BankResult::Running) TEST_ASSERT_TRUE(++guard < 1000000);
  return r;
}

static std::vector<int16_t> entryData(int i) {
  std::vector<int16_t> v(bank->entry(i)->frames);
  TEST_ASSERT_TRUE(bank->readData(i, 0, v.data(), static_cast<uint32_t>(v.size())));
  return v;
}

void test_import_sample() {
  const auto d = ramp(10000, 1);
  writePlainWav("/a.wav", d, 1, 22050);
  BankJob j(*bank, *disk);
  j.startImport("/a.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  const ImportOut& o = j.imported();
  TEST_ASSERT_EQUAL_UINT32(sampleCrc(d.data(), 10000), o.crc);
  TEST_ASSERT_EQUAL_UINT32(10000, o.frames);
  TEST_ASSERT_EQUAL_UINT32(22050, o.rate);
  TEST_ASSERT_EQUAL(60, o.root);
  char k[kSampleNameMax + 1];
  sampleKey(o.crc, k);
  const int i = bank->find(k);
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_INT16_ARRAY(d.data(), entryData(i).data(), 10000);
  TEST_ASSERT_EQUAL(-1, bank->find("~import"));
  TEST_ASSERT_EQUAL(1, bank->count());
  // Again: same data, still one entry.
  j.startImport("/a.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  TEST_ASSERT_EQUAL(1, bank->count());
}

void test_import_stereo_48k_downsampled() {
  std::vector<int16_t> d(2 * 4800);
  for (int i = 0; i < 4800; ++i) d[2 * i] = d[2 * i + 1] = static_cast<int16_t>(8000 * sinf(i * 0.05f));
  writePlainWav("/s.wav", d, 2, 48000);
  BankJob j(*bank, *disk);
  j.startImport("/s.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  TEST_ASSERT_EQUAL_UINT32(44100, j.imported().rate);
  TEST_ASSERT_INT_WITHIN(2, 4410, j.imported().frames);
  TEST_ASSERT_EQUAL_UINT32(4800, j.total());
}

void test_import_errors() {
  BankJob j(*bank, *disk);
  j.startImport("/none.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::OpenFail, run(j));
  FILE* f = fopen(disk->full("/junk.wav").c_str(), "wb");
  fputs("not a wave file at all, sorry", f);
  fclose(f);
  j.startImport("/junk.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::NotWav, run(j));
  disk->present = false;
  j.startImport("/junk.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::NoSd, run(j));
  TEST_ASSERT_EQUAL(0, bank->count());
}

void test_save_then_sync() {
  const auto a = ramp(5000, 2), b = ramp(7000, 3);
  writePlainWav("/a.wav", a, 1, 44100);
  writePlainWav("/b.wav", b, 1, 44100);
  BankJob j(*bank, *disk);
  j.startImport("/a.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  projSampleSet(*proj, "KICK", j.imported().crc, j.imported().frames);
  j.startImport("/b.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  projSampleSet(*proj, "SNARE", j.imported().crc, j.imported().frames);
  projSampleSet(*proj, "GONE", 0x12345678, 100);  // never cached

  j.startSave("P1", *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  TEST_ASSERT_EQUAL(1, j.missing());
  TEST_ASSERT_TRUE(j.sampleMissing(2));
  TEST_ASSERT_TRUE(bankFileCurrent(*disk, "/projects/P1/KICK.wav", proj->samples[0].crc, 5000));
  TEST_ASSERT_TRUE(bankFileCurrent(*disk, "/projects/P1/SNARE.wav", proj->samples[1].crc, 7000));
  TEST_ASSERT_FALSE(bankFileCurrent(*disk, "/projects/P1/KICK.wav", proj->samples[1].crc, 5000));
  TEST_ASSERT_FALSE(disk->exists("/projects/P1/KICK.wav.tmp"));

  // A fresh bank: sync brings both back from the folder.
  delete bank;
  flash->mem.assign(flash->mem.size(), 0);
  bank = new SampleBank(*flash);
  TEST_ASSERT_TRUE(bank->mount());
  BankJob s(*bank, *disk);
  s.startSync("P1", *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(s));
  TEST_ASSERT_EQUAL(1, s.missing());
  TEST_ASSERT_FALSE(s.sampleMissing(0));
  TEST_ASSERT_FALSE(s.sampleMissing(1));
  TEST_ASSERT_TRUE(s.sampleMissing(2));
  TEST_ASSERT_TRUE(projSampleBank(*proj, *bank, 0) >= 0);
  TEST_ASSERT_TRUE(projSampleBank(*proj, *bank, 1) >= 0);
  TEST_ASSERT_EQUAL_INT16_ARRAY(b.data(), entryData(projSampleBank(*proj, *bank, 1)).data(), 7000);

  // Other data under a listed name: missing.
  writePlainWav("/projects/P1/KICK.wav", ramp(5000, 9), 1, 44100);
  delete bank;
  flash->mem.assign(flash->mem.size(), 0);
  bank = new SampleBank(*flash);
  TEST_ASSERT_TRUE(bank->mount());
  BankJob s2(*bank, *disk);
  s2.startSync("P1", *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(s2));
  TEST_ASSERT_TRUE(s2.sampleMissing(0));
  TEST_ASSERT_FALSE(s2.sampleMissing(1));
}

void test_save_skips_current_files() {
  writePlainWav("/a.wav", ramp(3000, 4), 1, 44100);
  BankJob j(*bank, *disk);
  j.startImport("/a.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  projSampleSet(*proj, "A", j.imported().crc, j.imported().frames);
  j.startSave("P2", *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  struct stat st1;
  TEST_ASSERT_EQUAL(0, stat(disk->full("/projects/P2/A.wav").c_str(), &st1));
  // Touch the file's mtime marker: a current file is not rewritten.
  TEST_ASSERT_EQUAL(0, chmod(disk->full("/projects/P2/A.wav").c_str(), 0444));
  j.startSave("P2", *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  TEST_ASSERT_EQUAL(0, j.failed());
  chmod(disk->full("/projects/P2/A.wav").c_str(), 0644);
}

static void sineFrames(std::vector<int16_t>& v, int frames, int len) {
  v.resize(static_cast<size_t>(frames) * len);
  for (int f = 0; f < frames; ++f)
    for (int i = 0; i < len; ++i)
      v[f * len + i] = static_cast<int16_t>(20000 * sinf(6.2831853f * (f % 8 + 1) * i / len));
}

void test_import_wavetable_matches_buffer_import() {
  std::vector<int16_t> v;
  sineFrames(v, 16, 2048);  // 16 x 2048: read frame by frame from the file
  writePlainWav("/w.wav", v, 1, 44100);
  static int16_t ref[kWtTableSamples];
  TEST_ASSERT_EQUAL(WtErr::Ok, wtImport(v.data(), static_cast<uint32_t>(v.size()), 0, ref));
  BankJob j(*bank, *disk);
  int keeps = 0;
  j.setKeepalive([](uint32_t, uint32_t, void* c) { ++*static_cast<int*>(c); }, &keeps);
  j.startImport("/w.wav", true, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  TEST_ASSERT_TRUE(keeps > 0);
  static int16_t src[kWtSrcSamples];
  wtLevel0(ref, src);
  const uint32_t crc = sampleCrc(src, kWtSrcSamples);
  TEST_ASSERT_EQUAL_UINT32(crc, j.imported().crc);
  char k[kSampleNameMax + 1];
  wtKey(crc, k);
  const int i = bank->find(k);
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_INT16_ARRAY(ref, bank->data(i), kWtTableSamples);
  projWtSet(*proj, "PAD", crc);
  TEST_ASSERT_EQUAL(i, bankWtIndex(*bank, *proj, "PAD"));
  BankWtSource ws(*bank, *proj);
  TEST_ASSERT_EQUAL_PTR(bank->data(i), ws.findWt("PAD"));
  TEST_ASSERT_NULL(ws.findWt("NOPE"));
  int8_t pts[kWtFrameLen];
  TEST_ASSERT_TRUE(bankWtFrame(*bank, *proj, "PAD", 3, pts));
  for (int n = 0; n < kWtFrameLen; ++n) TEST_ASSERT_EQUAL_INT8(ref[3 * kWtFramePts + n] >> 8, pts[n]);
  flash->map = false;  // through readData
  int8_t pts2[kWtFrameLen];
  TEST_ASSERT_TRUE(bankWtFrame(*bank, *proj, "PAD", 3, pts2));
  TEST_ASSERT_EQUAL_INT8_ARRAY(pts, pts2, kWtFrameLen);
  flash->map = true;

  // Save writes the canonical source; a sync from it finds the same key.
  j.startSave("P3", *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  TEST_ASSERT_TRUE(bankFileCurrent(*disk, "/projects/P3/wt/PAD.wav", crc, kWtSrcSamples));
  delete bank;
  flash->mem.assign(flash->mem.size(), 0);
  bank = new SampleBank(*flash);
  TEST_ASSERT_TRUE(bank->mount());
  BankJob s(*bank, *disk);
  s.startSync("P3", *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(s));
  TEST_ASSERT_EQUAL(0, s.missing());
  TEST_ASSERT_TRUE(bankWtIndex(*bank, *proj, "PAD") >= 0);
}

void test_wavetable_bad_length() {
  writePlainWav("/w.wav", ramp(1000, 1), 1, 44100);
  BankJob j(*bank, *disk);
  j.startImport("/w.wav", true, *proj);
  TEST_ASSERT_EQUAL(BankResult::Unsupported, run(j));
  TEST_ASSERT_EQUAL(0, bank->count());
}

void test_sample_source() {
  writePlainWav("/a.wav", ramp(2000, 5), 1, 32000);
  BankJob j(*bank, *disk);
  j.startImport("/a.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  projSampleSet(*proj, "HAT", j.imported().crc, j.imported().frames);
  BankSampleSource s(*bank, *proj);
  SampleData d;
  TEST_ASSERT_TRUE(s.find("hat", d));
  TEST_ASSERT_NOT_NULL(d.whole);
  TEST_ASSERT_EQUAL_UINT32(j.imported().crc, d.key);
  TEST_ASSERT_EQUAL_UINT32(2000, d.frames);
  TEST_ASSERT_EQUAL_UINT32(32000, d.rate);
  TEST_ASSERT_FALSE(d.streamable);
  TEST_ASSERT_EQUAL(0, d.nHeads);
  TEST_ASSERT_FALSE(s.find("NONE", d));
}
struct FillCtx {
  int calls = 0;
};
static bool fillRamp(int16_t* buf, uint32_t at, uint32_t n, void* ctx) {
  ++static_cast<FillCtx*>(ctx)->calls;
  for (uint32_t i = 0; i < n; ++i) buf[i] = static_cast<int16_t>((at + i) * 3);
  return true;
}

void test_write_frames() {
  FillCtx c;
  ImportOut o;
  TEST_ASSERT_EQUAL(BankResult::Ok, bankWriteFrames(*bank, *proj, 5000, 44100, fillRamp, &c, o));
  TEST_ASSERT_TRUE(c.calls > 1);
  std::vector<int16_t> want(5000);
  for (int i = 0; i < 5000; ++i) want[i] = static_cast<int16_t>(i * 3);
  TEST_ASSERT_EQUAL_UINT32(sampleCrc(want.data(), 5000), o.crc);
  TEST_ASSERT_EQUAL_UINT32(5000, o.frames);
  char k[kSampleNameMax + 1];
  sampleKey(o.crc, k);
  TEST_ASSERT_EQUAL_INT16_ARRAY(want.data(), entryData(bank->find(k)).data(), 5000);
  // Same data again: still one entry.
  TEST_ASSERT_EQUAL(BankResult::Ok, bankWriteFrames(*bank, *proj, 5000, 44100, fillRamp, &c, o));
  TEST_ASSERT_EQUAL(1, bank->count());
  auto cancel = [](int16_t*, uint32_t, uint32_t, void*) { return false; };
  TEST_ASSERT_EQUAL(BankResult::ReadFail, bankWriteFrames(*bank, *proj, 100, 44100, cancel, nullptr, o));
  TEST_ASSERT_EQUAL(1, bank->count());
}

void test_builtins() {
  static int16_t table[kWtTableSamples];
  TEST_ASSERT_EQUAL(kWtBuiltins, bankAddBuiltins(*bank, *proj, table));
  TEST_ASSERT_EQUAL(0, bankAddBuiltins(*bank, *proj, table));
  BankWtSource ws(*bank, *proj);
  TEST_ASSERT_NOT_NULL(ws.findWt(wtBuiltinName(0)));
  int8_t pts[kWtFrameLen];
  TEST_ASSERT_TRUE(bankWtFrame(*bank, *proj, wtBuiltinName(1), 0, pts));
}

void test_cancel_drops_half_entry() {
  writePlainWav("/a.wav", ramp(20000, 6), 1, 44100);
  BankJob j(*bank, *disk);
  j.startImport("/a.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Running, j.step());
  TEST_ASSERT_EQUAL(BankResult::Running, j.step());
  TEST_ASSERT_TRUE(j.busy());
  j.cancel();
  TEST_ASSERT_FALSE(j.busy());
  TEST_ASSERT_EQUAL(0, bank->count());
  j.startImport("/a.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  TEST_ASSERT_EQUAL(1, bank->count());
}

// ---- StoreJob ----

static BankResult runStore(StoreJob& j) {
  BankResult r;
  int guard = 0;
  while ((r = j.step()) == BankResult::Running) TEST_ASSERT_TRUE(++guard < 1000000);
  return r;
}
static std::vector<uint8_t> readAll(const char* path) {
  std::vector<uint8_t> v;
  FILE* f = fopen(disk->full(path).c_str(), "rb");
  if (!f) return v;
  uint8_t b[4096];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) v.insert(v.end(), b, b + n);
  fclose(f);
  return v;
}

void test_store_import_writes_canonical_file() {
  const auto d = ramp(3000, 3);
  writePlainWav("/a.wav", d, 1, 32000);
  StoreJob j(*disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startImport("/a.wav"));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  const uint32_t crc = sampleCrc(d.data(), 3000, 0);
  TEST_ASSERT_EQUAL_HEX32(crc, j.imported().crc);
  TEST_ASSERT_EQUAL_UINT32(3000, j.imported().frames);
  TEST_ASSERT_EQUAL_UINT32(32000, j.imported().rate);
  TEST_ASSERT_TRUE(storeFileCurrent(*disk, crc, 3000));
  char p[kStorePathMax];
  storePath(crc, p);
  const auto f = readAll(p);
  TEST_ASSERT_EQUAL_UINT32(kStoreDataOffset + 6000, f.size());
  TEST_ASSERT_EQUAL_MEMORY(d.data(), f.data() + kStoreDataOffset, 6000);
  storePeaksPath(crc, p);
  const auto pk = readAll(p);
  TEST_ASSERT_EQUAL_UINT32(2 * 12, pk.size());  // ceil(3000 / 256) pairs
  TEST_ASSERT_EQUAL_INT8(3000 >> 8, static_cast<int8_t>(pk[0]));
  TEST_ASSERT_EQUAL_INT8((3000 + 255 * 7) >> 8, static_cast<int8_t>(pk[1]));
  TEST_ASSERT_FALSE(disk->exists(kStoreTmp));
}

void test_store_import_stereo_48k_downmixes_and_downsamples() {
  writePlainWav("/s.wav", ramp(9600, 1), 2, 48000);  // 4800 stereo frames
  StoreJob j(*disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startImport("/s.wav"));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL_UINT32(44100, j.imported().rate);
  TEST_ASSERT_UINT32_WITHIN(2, 4410, j.imported().frames);
  TEST_ASSERT_TRUE(storeFileCurrent(*disk, j.imported().crc, j.imported().frames));
}

void test_store_import_again_keeps_one_file() {
  writePlainWav("/a.wav", ramp(1000, 2), 1, 44100);
  StoreJob j(*disk);
  j.startImport("/a.wav");
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  const uint32_t crc = j.imported().crc;
  j.startImport("/a.wav");
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL_HEX32(crc, j.imported().crc);
  TEST_ASSERT_FALSE(disk->exists(kStoreTmp));
}

void test_store_import_known_crc_skips_reading() {
  writePlainWav("/a.wav", ramp(1000, 2), 1, 44100);
  StoreJob j(*disk);
  j.startImport("/a.wav");
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  const uint32_t crc = j.imported().crc;
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startImport("/a.wav", &crc));
  TEST_ASSERT_FALSE(j.busy() && j.done() > 0);
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL_HEX32(crc, j.imported().crc);
}

void test_store_sync_takes_project_files() {
  disk->mkdir("/projects/P");
  const auto a = ramp(2000, 4), b = ramp(1500, 6);
  writePlainWav("/projects/P/KICK.wav", a, 1, 44100);
  writePlainWav("/projects/P/SNARE.wav", b, 1, 44100);
  projSampleSet(*proj, "KICK", sampleCrc(a.data(), 2000, 0), 2000);
  projSampleSet(*proj, "SNARE", 0xDEADBEEFu, 1500);  // the folder holds other data
  projSampleSet(*proj, "HAT", 0x12345678u, 100);     // no file
  StoreJob j(*disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startSync("P", *proj));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_TRUE(storeFileCurrent(*disk, sampleCrc(a.data(), 2000, 0), 2000));
  TEST_ASSERT_EQUAL(2, j.missing());
  TEST_ASSERT_FALSE(j.sampleMissing(projSampleFind(*proj, "KICK")));
  TEST_ASSERT_TRUE(j.sampleMissing(projSampleFind(*proj, "SNARE")));
  TEST_ASSERT_TRUE(j.sampleMissing(projSampleFind(*proj, "HAT")));
  TEST_ASSERT_FALSE(disk->exists(kStoreTmp));
}

void test_store_sync_without_project_marks_missing() {
  projSampleSet(*proj, "KICK", 0x11111111u, 100);
  StoreJob j(*disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startSync("", *proj));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL(1, j.missing());
}

void test_store_save_copies_into_project() {
  const auto a = ramp(2500, 5);
  writePlainWav("/a.wav", a, 1, 22050);
  StoreJob j(*disk);
  j.startImport("/a.wav");
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  const uint32_t crc = j.imported().crc;
  projSampleSet(*proj, "PAD", crc, 2500);
  projSampleSet(*proj, "GONE", 0x22222222u, 10);  // not in the store
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startSave("Q", *proj));  // a new project: its folder is made
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL(1, j.missing());
  TEST_ASSERT_TRUE(bankFileCurrent(*disk, "/projects/Q/PAD.wav", crc, 2500));
  TEST_ASSERT_FALSE(disk->exists("/projects/Q/PAD.wav.tmp"));
}

struct Added {
  std::vector<uint32_t> crc;
};
static void onAdded(uint32_t crc, void* ctx) { static_cast<Added*>(ctx)->crc.push_back(crc); }

void test_store_reports_added_files() {
  writePlainWav("/a.wav", ramp(700, 8), 1, 44100);
  Added got;
  StoreJob j(*disk);
  j.setOnAdded(onAdded, &got);
  j.startImport("/a.wav");
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_EQUAL(1, got.crc.size());
  TEST_ASSERT_EQUAL_HEX32(j.imported().crc, got.crc[0]);
}

void test_store_migrates_bank_samples() {
  const auto a = ramp(3000, 9);
  writePlainWav("/a.wav", a, 1, 32000);
  BankJob bj(*bank, *disk);  // the old way: into the flash bank
  bj.startImport("/a.wav", false, *proj);
  TEST_ASSERT_EQUAL(BankResult::Ok, run(bj));
  const uint32_t crc = bj.imported().crc;
  StoreJob j(*disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, j.startMigrate(*bank));
  TEST_ASSERT_EQUAL(BankResult::Ok, runStore(j));
  TEST_ASSERT_TRUE(storeFileCurrent(*disk, crc, 3000));
  const int before = bank->count();
  TEST_ASSERT_EQUAL(1, bankDropMigrated(*bank, *disk));
  TEST_ASSERT_EQUAL(before - 1, bank->count());
  char key[kSampleNameMax + 1];
  sampleKey(crc, key);
  TEST_ASSERT_EQUAL(-1, bank->find(key));
}

void test_sync_wavetables_only_leaves_samples() {
  disk->mkdir("/projects/P");
  const auto a = ramp(2000, 4);
  writePlainWav("/projects/P/KICK.wav", a, 1, 44100);
  projSampleSet(*proj, "KICK", sampleCrc(a.data(), 2000, 0), 2000);
  BankJob j(*bank, *disk);
  TEST_ASSERT_EQUAL(BankResult::Running, j.startSync("P", *proj, false));
  TEST_ASSERT_EQUAL(BankResult::Ok, run(j));
  TEST_ASSERT_EQUAL(0, j.missing());  // samples are not its business
  char key[kSampleNameMax + 1];
  sampleKey(sampleCrc(a.data(), 2000, 0), key);
  TEST_ASSERT_EQUAL(-1, bank->find(key));
}

void test_store_write_frames() {
  FillCtx c;
  ImportOut o;
  TEST_ASSERT_EQUAL(BankResult::Ok, storeWriteFrames(*disk, 5000, 44100, fillRamp, &c, o));
  std::vector<int16_t> want(5000);
  for (int i = 0; i < 5000; ++i) want[i] = static_cast<int16_t>(i * 3);
  TEST_ASSERT_EQUAL_HEX32(sampleCrc(want.data(), 5000, 0), o.crc);
  TEST_ASSERT_TRUE(storeFileCurrent(*disk, o.crc, 5000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_import_sample);
  RUN_TEST(test_import_stereo_48k_downsampled);
  RUN_TEST(test_import_errors);
  RUN_TEST(test_save_then_sync);
  RUN_TEST(test_save_skips_current_files);
  RUN_TEST(test_import_wavetable_matches_buffer_import);
  RUN_TEST(test_wavetable_bad_length);
  RUN_TEST(test_sample_source);
  RUN_TEST(test_write_frames);
  RUN_TEST(test_builtins);
  RUN_TEST(test_cancel_drops_half_entry);
  RUN_TEST(test_store_import_writes_canonical_file);
  RUN_TEST(test_store_import_stereo_48k_downmixes_and_downsamples);
  RUN_TEST(test_store_import_again_keeps_one_file);
  RUN_TEST(test_store_import_known_crc_skips_reading);
  RUN_TEST(test_store_sync_takes_project_files);
  RUN_TEST(test_store_sync_without_project_marks_missing);
  RUN_TEST(test_store_save_copies_into_project);
  RUN_TEST(test_store_reports_added_files);
  RUN_TEST(test_store_migrates_bank_samples);
  RUN_TEST(test_sync_wavetables_only_leaves_samples);
  RUN_TEST(test_store_write_frames);
  return UNITY_END();
}
