#include "bank.h"
#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "audio.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_partition.h"
#include "file_rules.h"
#include "hw/sdcard.h"
#include "model.h"
#include "sample_set.h"
#include "wav.h"
#include "wt_builtin.h"
#include "wt_file.h"
#include "wt_mip.h"

namespace audio {
namespace {

constexpr uint8_t kSamplesSubtype = 0x40;
constexpr uint32_t kRawBytes = 2048;  // WAV read block

// The "samples" partition, mapped whole into the data address space.
class PartitionFlash final : public mt::BankFlash {
 public:
  bool open() {
    part_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(kSamplesSubtype),
                                     "samples");
    if (!part_) {
      Serial.println("audio: no samples partition");
      return false;
    }
    if (!map()) return true;  // the bank still works without a mapping, only playback is silent
    Serial.printf("audio: samples mmap ok, %u bytes at %p\n", static_cast<unsigned>(part_->size), base_);
    return true;
  }
  // After flash writes: drop and map again so no stale cache lines are read.
  void remap() {
    if (!part_) return;
    if (base_) esp_partition_munmap(handle_);
    base_ = nullptr;
    map();
  }
  uint32_t size() const override { return part_ ? part_->size : 0; }
  bool read(uint32_t off, void* d, uint32_t n) override {
    return part_ && esp_partition_read(part_, off, d, n) == ESP_OK;
  }
  bool erase(uint32_t off, uint32_t n) override {
    if (!part_) return false;
    // Sector by sector, yielding: a sector erase takes up to ~300 ms with the caches off.
    for (uint32_t o = 0; o < n; o += mt::kBankAlign) {
      if (esp_partition_erase_range(part_, off + o, mt::kBankAlign) != ESP_OK) return false;
      vTaskDelay(1);
    }
    return true;
  }
  bool write(uint32_t off, const void* d, uint32_t n) override {
    if (!part_) return false;
    if (esp_ptr_internal(d)) return esp_partition_write(part_, off, d, n) == ESP_OK;
    // PSRAM is behind the cache, which is off while flash is written: bounce through the stack.
    uint8_t tmp[256];
    const uint8_t* p = static_cast<const uint8_t*>(d);
    while (n > 0) {
      const uint32_t k = n < sizeof(tmp) ? n : sizeof(tmp);
      memcpy(tmp, p, k);
      if (esp_partition_write(part_, off, tmp, k) != ESP_OK) return false;
      off += k;
      p += k;
      n -= k;
    }
    return true;
  }
  const uint8_t* mapped() const override { return base_; }

 private:
  bool map() {
    const void* ptr = nullptr;
    const esp_err_t err = esp_partition_mmap(part_, 0, part_->size, ESP_PARTITION_MMAP_DATA, &ptr, &handle_);
    if (err != ESP_OK) {
      Serial.printf("audio: mmap of %u bytes failed: %s\n", static_cast<unsigned>(part_->size), esp_err_to_name(err));
      return false;
    }
    base_ = static_cast<const uint8_t*>(ptr);
    return true;
  }

  const esp_partition_t* part_ = nullptr;
  const uint8_t* base_ = nullptr;
  esp_partition_mmap_handle_t handle_ = 0;
};

PartitionFlash flash;
mt::SampleBank theBank(flash);
bool mounted;
const mt::Project* project;

constexpr const char* kImportName = "~import";  // entry being imported, renamed to its key at the end
constexpr const char* kLegacyIdx = "/projects/legacy.idx";

// crc as 8 lower-case hex digits + terminator at out (mt::sampleKey without snprintf: small audio
// task stack, note-on path).
void hexKey(uint32_t crc, char* out) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (int n = 0; n < 8; ++n) out[n] = kHex[(crc >> (28 - 4 * n)) & 15];
  out[8] = 0;
}

struct BankSource final : mt::SampleSource {
  const int16_t* find(const char* name, uint32_t& frames, uint32_t& rate) const override {
    if (!mounted || !project) return nullptr;
    // mt::projSampleBank without snprintf.
    const int k = mt::projSampleFind(*project, name);
    if (k < 0) return nullptr;
    const mt::ProjSample& ps = project->samples[k];
    char key[mt::kSampleNameMax + 1];
    hexKey(ps.crc, key);
    int i = theBank.find(key);
    if (i >= 0 && theBank.entry(i)->frames != ps.frames) i = -1;
    const int16_t* d = theBank.data(i);
    if (!d) return nullptr;
    frames = theBank.entry(i)->frames;
    rate = theBank.entry(i)->rate;
    return d;
  }
};
BankSource source;

// Bank index of wavetable name: built-in "*NAME" directly, otherwise through p's list (mt::wtKey
// without snprintf). -1 if absent or not a wavetable entry.
int wtIndex(const char* name, const mt::Project* p) {
  if (!name || !name[0]) return -1;
  int i;
  if (mt::isWtBuiltin(name)) {
    i = theBank.find(name);
  } else {
    if (!p) return -1;
    const int k = mt::projWtFind(*p, name);
    if (k < 0) return -1;
    char key[mt::kSampleNameMax + 1];
    key[0] = 'w';
    hexKey(p->wavetables[k].crc, key + 1);
    i = theBank.find(key);
  }
  const mt::BankEntry* e = theBank.entry(i);
  return e && e->frames == static_cast<uint32_t>(mt::kWtTableSamples) && e->rate == 0 ? i : -1;
}

struct WtBankSource final : mt::WtSource {
  const int16_t* findWt(const char* name) const override {
    if (!mounted) return nullptr;
    return theBank.data(wtIndex(name, project));
  }
};
WtBankSource wtSource;

// heap_caps_malloc'ed buffer, freed on every path.
struct HeapBuf {
  void* p;
  explicit HeapBuf(size_t bytes, uint32_t caps) : p(heap_caps_malloc(bytes, caps)) {}
  ~HeapBuf() { heap_caps_free(p); }
  HeapBuf(const HeapBuf&) = delete;
  HeapBuf& operator=(const HeapBuf&) = delete;
  int16_t* i16() const { return static_cast<int16_t*>(p); }
};

// Audio task paused (silent, not reading the bank) for the lifetime of the object.
struct FlashWork {
  bool ok;
  FlashWork() : ok(pauseForFlash()) {}
  ~FlashWork() {
    if (!ok) return;
    flash.remap();
    resumeAfterFlash();
  }
};

bool engineIdle() {
  const engine::Status s = engine::status();
  return !s.playing && !s.paused;
}

BankResult wavErr(mt::WavErr e) {
  switch (e) {
    case mt::WavErr::Ok: return BankResult::Ok;
    case mt::WavErr::NotWav: return BankResult::NotWav;
    case mt::WavErr::Unsupported: return BankResult::Unsupported;
    default: return BankResult::Truncated;
  }
}

// Receives converted frames (mono, <= 32 kHz); false aborts with WriteFail.
using FrameSink = bool (*)(const int16_t* d, uint32_t n, void* ctx);

// Converts the first maxFrames input frames of f's WAV data and passes them to sink.
BankResult convertData(fs::File& f, const mt::WavInfo& w, uint32_t maxFrames, FrameSink sink, void* sinkCtx,
                       BankProgressFn cb, void* ctx) {
  const uint32_t fb = w.frameBytes();
  const uint32_t chunk = kRawBytes / fb;
  // Internal RAM: write() sources must be readable with the caches off.
  uint8_t* raw = static_cast<uint8_t*>(heap_caps_malloc(kRawBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  int16_t* mono = static_cast<int16_t*>(heap_caps_malloc(chunk * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  int16_t* out = static_cast<int16_t*>(
      heap_caps_malloc(mt::Downsampler::outFrames(chunk, w.rate) * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  BankResult r = BankResult::Ok;
  if (!raw || !mono || !out) r = BankResult::NoMemory;
  else if (!f.seek(w.dataOffset)) r = BankResult::ReadFail;
  mt::Downsampler ds(w.rate);
  const uint32_t total = w.frames() < maxFrames ? w.frames() : maxFrames;
  for (uint32_t done = 0; r == BankResult::Ok && done < total;) {
    const uint32_t n = total - done < chunk ? total - done : chunk;
    if (f.read(raw, n * fb) != n * fb) {
      r = BankResult::ReadFail;
      break;
    }
    mt::wavToMono(raw, n, w, mono);
    const int k = ds.push(mono, static_cast<int>(n), out);
    if (k > 0 && !sink(out, static_cast<uint32_t>(k), sinkCtx)) r = BankResult::WriteFail;
    done += n;
    if (cb) cb(done, total, ctx);
  }
  heap_caps_free(raw);
  heap_caps_free(mono);
  heap_caps_free(out);
  return r;
}

// Opens path and parses its WAV header.
BankResult openWav(const char* path, fs::File& f, mt::WavInfo& w) {
  if (!hw::sdReady()) return BankResult::NoSd;
  f = hw::sdFs().open(path, FILE_READ);
  if (!f) return BankResult::OpenFail;
  {
    hw::FileSource src(f);
    const mt::WavErr e = mt::wavParse(src, w);
    if (e != mt::WavErr::Ok) return wavErr(e);
  }
  if (w.frameBytes() > kRawBytes) return BankResult::Unsupported;
  if (w.frames() == 0) return BankResult::Truncated;
  return BankResult::Ok;
}

struct BufSink {
  int16_t* d;
  uint32_t cap, n;
};

// Bank writer that also sums up the crc of what it wrote.
struct CrcSink {
  uint32_t crc;
};

// /projects/legacy.idx: one "name crc_hex frames project" line per migrated old bank entry (lines
// written before the project field have three fields).
class SdLegacyIndex final : public mt::LegacyIndex {
 public:
  bool find(const char* name, mt::LegacySample& out) override {
    return scan([&](const mt::LegacySample& s) { return strcasecmp(s.name, name) == 0; }, out);
  }
  // First line with this data.
  bool findData(uint32_t crc, uint32_t frames, mt::LegacySample& out) {
    return scan([&](const mt::LegacySample& s) { return s.crc == crc && s.frames == frames; }, out);
  }
  bool add(const mt::LegacySample& s) override {
    if (!hw::sdReady()) return false;
    fs::File f = hw::sdFs().open(kLegacyIdx, FILE_APPEND);
    if (!f) return false;
    char line[64];
    const int n = snprintf(line, sizeof(line), "%s %08x %u %s\n", s.name, static_cast<unsigned>(s.crc),
                           static_cast<unsigned>(s.frames), s.project[0] ? s.project : "-");
    const bool ok = f.write(reinterpret_cast<const uint8_t*>(line), n) == static_cast<size_t>(n);
    f.close();
    return ok;
  }

 private:
  template <typename Match>
  static bool scan(Match match, mt::LegacySample& out) {
    if (!hw::sdReady()) return false;
    fs::File f = hw::sdFs().open(kLegacyIdx, FILE_READ);
    if (!f) return false;
    char buf[256], line[64];
    int n = 0, len = 0;
    bool found = false;
    while (!found && (len = f.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf))) > 0) {
      for (int k = 0; k < len && !found; ++k) {
        if (buf[k] != '\n') {
          if (n < static_cast<int>(sizeof(line)) - 1) line[n++] = buf[k];
          continue;
        }
        line[n] = 0;
        n = 0;
        found = parse(line, out) && match(out);
      }
    }
    if (!found && n > 0) {  // last line without a newline
      line[n] = 0;
      found = parse(line, out) && match(out);
    }
    f.close();
    return found;
  }
  static bool parse(const char* line, mt::LegacySample& out) {
    char nm[mt::kSampleNameMax + 2], proj[mt::kSampleNameMax + 2] = "";
    unsigned crc, frames;
    const int k = sscanf(line, "%17s %x %u %17s", nm, &crc, &frames, proj);
    if (k < 3 || strlen(nm) > mt::kSampleNameMax) return false;
    strcpy(out.name, nm);
    out.crc = crc;
    out.frames = frames;
    out.project[0] = 0;
    if (k == 4 && mt::projectBaseValid(proj)) strcpy(out.project, proj);  // "-": unknown
    return true;
  }
};

}  // namespace

mt::SampleBank& bank() { return theBank; }
bool bankMounted() { return mounted; }
const mt::SampleSource* sampleSource() { return &source; }
const mt::WtSource* wavetableSource() { return &wtSource; }
const uint8_t* samplesBase() { return flash.mapped(); }
uint32_t samplesSize() { return flash.size(); }

void bankSetProject(const mt::Project* p) { project = p; }

namespace {

bool builtinCached(int i) {
  const int j = theBank.find(mt::wtBuiltinName(i));
  const mt::BankEntry* e = theBank.entry(j);
  return e && e->frames == static_cast<uint32_t>(mt::kWtTableSamples) && e->rate == 0;
}

// Built-in wavetables missing from the bank are generated (mt::wtBuild) and stored as "*NAME"
// (never evicted afterwards). Normally only on the first start; each takes a moment.
void addBuiltins() {
  int missing = 0;
  for (int i = 0; i < mt::kWtBuiltins; ++i) missing += builtinCached(i) ? 0 : 1;
  if (missing == 0) return;
  const HeapBuf table(mt::kWtTableSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!table.p) {
    Serial.println("audio: no PSRAM for built-in wavetables");
    return;
  }
  // Called before the audio task exists (audio::begin), so this does not wait; it still remaps.
  FlashWork work;
  if (!work.ok) return;
  for (int i = 0; i < mt::kWtBuiltins; ++i) {
    if (builtinCached(i)) continue;
    const char* name = mt::wtBuiltinName(i);
    // An entry of that name but not a wavetable (never written by this firmware): replace it.
    if (const int old = theBank.find(name); old >= 0 && !theBank.remove(old)) continue;
    mt::WtBuiltinSrc src(i);  // not reentrant: the UI task builds nothing at the same time (boot)
    if (!mt::wtBuild(src, table.i16())) {
      Serial.printf("audio: built-in %s is silent\n", name);
      continue;
    }
    if (project && !mt::bankMakeRoom(theBank, *project, mt::kWtTableSamples)) {
      Serial.printf("audio: no bank room for %s\n", name);
      continue;
    }
    if (!theBank.begin(name, mt::kWtTableSamples, 0, 60)) {
      Serial.printf("audio: no bank room for %s\n", name);
      continue;
    }
    if (!theBank.write(table.i16(), mt::kWtTableSamples) || !theBank.commit()) {
      theBank.abort();
      Serial.printf("audio: writing %s failed\n", name);
      continue;
    }
    Serial.printf("audio: built-in %s added\n", name);
    vTaskDelay(1);
  }
}

}  // namespace

void bankBegin() {
  if (!flash.open()) return;
  mounted = theBank.mount();
  if (mounted) addBuiltins();
  Serial.printf("audio: bank %s, %d samples, %u KB free\n", mounted ? "mounted" : "mount failed", theBank.count(),
                static_cast<unsigned>(theBank.freeBytes() / 1024));
}

const char* bankResultText(BankResult r) {
  switch (r) {
    case BankResult::Ok: return "OK";
    case BankResult::Busy: return "STOP PLAYBACK FIRST";
    case BankResult::NoSd: return "NO SD CARD";
    case BankResult::NoBank: return "NO SAMPLE BANK";
    case BankResult::OpenFail: return "CANNOT OPEN FILE";
    case BankResult::ReadFail: return "READ ERROR";
    case BankResult::NotWav: return "NOT A WAV FILE";
    case BankResult::Unsupported: return "UNSUPPORTED WAV";
    case BankResult::Truncated: return "WAV TRUNCATED";
    case BankResult::Full: return "BANK FULL";
    case BankResult::WriteFail: return "FLASH WRITE ERROR";
    case BankResult::NoMemory: return "NO MEMORY";
  }
  return "?";
}

BankResult importToCache(const char* path, const mt::Project& p, ImportOut& out, const uint32_t* knownCrc,
                         BankProgressFn cb, void* ctx) {
  if (!mounted) return BankResult::NoBank;
  if (!engineIdle()) return BankResult::Busy;
  fs::File f;
  mt::WavInfo w;
  if (const BankResult e = openWav(path, f, w); e != BankResult::Ok) return e;
  // Cached already: a mono 16-bit file at the bank rate converts 1:1, so key and length identify it.
  if (w.channels == 1 && w.bits == 16 && w.rate <= mt::kWavMaxRate && (w.hasCrc || knownCrc)) {
    char k[mt::kSampleNameMax + 1];
    mt::sampleKey(w.hasCrc ? w.crc : *knownCrc, k);
    const int i = theBank.find(k);
    if (i >= 0 && theBank.entry(i)->frames == w.frames()) {
      out.crc = w.hasCrc ? w.crc : *knownCrc;
      out.frames = w.frames();
      out.root = w.root;
      return BankResult::Ok;
    }
  }
  const uint32_t frames = mt::Downsampler::outFrames(w.frames(), w.rate);
  const uint32_t rate = mt::Downsampler(w.rate).outRate();
  FlashWork work;
  if (!work.ok) return BankResult::Busy;
  // Left over from a failed import (power cut before the rename): only cache.
  if (const int old = theBank.find(kImportName); old >= 0 && !theBank.remove(old)) return BankResult::WriteFail;
  if (!mt::bankMakeRoom(theBank, p, frames, cb, ctx)) return BankResult::Full;
  if (!theBank.begin(kImportName, frames, rate, w.root)) return BankResult::Full;
  CrcSink crc{0};
  const FrameSink toBank = [](const int16_t* d, uint32_t n, void* c) {
    static_cast<CrcSink*>(c)->crc = mt::sampleCrc(d, n, static_cast<CrcSink*>(c)->crc);
    return theBank.write(d, n);
  };
  BankResult r = convertData(f, w, w.frames(), toBank, &crc, cb, ctx);
  if (r == BankResult::Ok && !theBank.commit()) r = BankResult::WriteFail;
  if (r != BankResult::Ok) {
    theBank.abort();
    return r;
  }
  const int i = theBank.find(kImportName);
  if (i < 0) return BankResult::WriteFail;
  const uint32_t got = theBank.entry(i)->frames;
  char k[mt::kSampleNameMax + 1];
  mt::sampleKey(crc.crc, k);
  int dup = theBank.find(k);
  if (dup >= 0 && theBank.entry(dup)->frames != got) {
    // Same crc, other length (practically never): the old entry goes unless the project uses it.
    if (mt::bankEntryUsed(p, *theBank.entry(dup)) || !theBank.remove(dup)) {
      theBank.remove(theBank.find(kImportName));
      return BankResult::WriteFail;
    }
    dup = -1;
  }
  // Same data cached: keep that entry (with the root stored when it was imported; out.root is this
  // file's).
  const bool ok = dup >= 0 ? theBank.remove(theBank.find(kImportName))
                           : theBank.rename(theBank.find(kImportName), k);
  if (!ok) {
    theBank.remove(theBank.find(kImportName));
    return BankResult::WriteFail;
  }
  out.crc = crc.crc;
  out.frames = got;
  out.root = w.root;
  return BankResult::Ok;
}

BankResult importWtToCache(const char* path, const mt::Project& p, uint32_t& crc, const uint32_t* knownCrc,
                           BankProgressFn cb, void* ctx) {
  if (!mounted) return BankResult::NoBank;
  if (!engineIdle()) return BankResult::Busy;
  fs::File f;
  mt::WavInfo w;
  if (const BankResult e = openWav(path, f, w); e != BankResult::Ok) return e;
  const uint32_t n = w.frames();
  if (n > mt::kWtMaxSrcSamples) return BankResult::Unsupported;
  char key[mt::kSampleNameMax + 1];
  // Cached already: a canonical source (as exportWt writes it) is identified by its crc.
  if (w.channels == 1 && w.bits == 16 && n == static_cast<uint32_t>(mt::kWtSrcSamples) && (w.hasCrc || knownCrc)) {
    const uint32_t c = w.hasCrc ? w.crc : *knownCrc;
    mt::wtKey(c, key);
    const int i = theBank.find(key);
    if (i >= 0 && theBank.entry(i)->frames == static_cast<uint32_t>(mt::kWtTableSamples)) {
      crc = c;
      return BankResult::Ok;
    }
  }
  // The whole table is converted before the bank is touched (the audio task keeps playing).
  const HeapBuf table(mt::kWtTableSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!table.p) return BankResult::NoMemory;
  {
    const HeapBuf mono(static_cast<size_t>(n) * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    // Internal RAM block for SD reads.
    const HeapBuf raw(kRawBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!mono.p || !raw.p) return BankResult::NoMemory;
    if (!f.seek(w.dataOffset)) return BankResult::ReadFail;
    const uint32_t fb = w.frameBytes();
    const uint32_t chunk = kRawBytes / fb;
    uint8_t* rb = static_cast<uint8_t*>(raw.p);
    for (uint32_t done = 0; done < n;) {
      const uint32_t k = n - done < chunk ? n - done : chunk;
      if (f.read(rb, k * fb) != k * fb) return BankResult::ReadFail;
      mt::wavToMono(rb, k, w, mono.i16() + done);
      done += k;
      if (cb) cb(done, n, ctx);
    }
    f.close();
    switch (mt::wtImport(mono.i16(), n, w.clmFrame, table.i16())) {
      case mt::WtErr::Ok: break;
      case mt::WtErr::Silent: return BankResult::NotWav;
      default: return BankResult::Unsupported;
    }
  }
  {
    const HeapBuf src(mt::kWtSrcSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!src.p) return BankResult::NoMemory;
    mt::wtLevel0(table.i16(), src.i16());
    crc = mt::sampleCrc(src.i16(), mt::kWtSrcSamples);
  }
  mt::wtKey(crc, key);
  if (const int i = theBank.find(key); i >= 0 && theBank.entry(i)->frames == static_cast<uint32_t>(mt::kWtTableSamples))
    return BankResult::Ok;
  FlashWork work;
  if (!work.ok) return BankResult::Busy;
  // Left over from a failed import (power cut before the rename): only cache.
  if (const int old = theBank.find(kImportName); old >= 0 && !theBank.remove(old)) return BankResult::WriteFail;
  // Same key, not a table (practically never): unused by p (mt::bankEntryUsed checks the length).
  if (const int dup = theBank.find(key); dup >= 0 && !theBank.remove(dup)) return BankResult::WriteFail;
  if (!mt::bankMakeRoom(theBank, p, mt::kWtTableSamples, cb, ctx)) return BankResult::Full;
  if (!theBank.begin(kImportName, mt::kWtTableSamples, 0, 60)) return BankResult::Full;
  if (!theBank.write(table.i16(), mt::kWtTableSamples) || !theBank.commit()) {
    theBank.abort();
    return BankResult::WriteFail;
  }
  if (!theBank.rename(theBank.find(kImportName), key)) {
    theBank.remove(theBank.find(kImportName));
    return BankResult::WriteFail;
  }
  return BankResult::Ok;
}

namespace {

// Fills buf with n frames of the data from frame at on; false = ReadFail.
using FrameFill = bool (*)(int16_t* buf, uint32_t at, uint32_t n, void* ctx);

// Writes a mono 16-bit WAV (header with root and "mtcr" crc, frames from fill) to path via path.tmp.
BankResult writeWav(const char* path, uint32_t frames, uint32_t rate, uint8_t root, uint32_t crc, FrameFill fill,
                    void* fillCtx) {
  // Internal RAM block: SD writes from mapped flash / PSRAM are not done directly.
  constexpr uint32_t kBlock = kRawBytes / 2;
  int16_t* buf = static_cast<int16_t*>(heap_caps_malloc(kRawBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!buf) return BankResult::NoMemory;
  char tmp[96];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  fs::FS& fs = hw::sdFs();
  if (fs.exists(tmp)) fs.remove(tmp);
  fs::File f = fs.open(tmp, FILE_WRITE);
  BankResult r = f ? BankResult::Ok : BankResult::OpenFail;
  if (r == BankResult::Ok) {
    uint8_t hdr[mt::kWavHeaderBytes];
    mt::wavHeader(hdr, frames, rate, root, crc);
    if (f.write(hdr, sizeof(hdr)) != sizeof(hdr)) r = BankResult::WriteFail;
    for (uint32_t done = 0; r == BankResult::Ok && done < frames;) {
      const uint32_t n = frames - done < kBlock ? frames - done : kBlock;
      if (!fill(buf, done, n, fillCtx)) r = BankResult::ReadFail;
      if (r == BankResult::Ok && f.write(reinterpret_cast<const uint8_t*>(buf), n * 2) != n * 2)
        r = BankResult::WriteFail;
      done += n;
    }
    f.close();
  }
  heap_caps_free(buf);
  if (r == BankResult::Ok) {
    if (fs.exists(path)) fs.remove(path);
    if (!fs.rename(tmp, path)) r = BankResult::WriteFail;
  }
  if (r != BankResult::Ok && fs.exists(tmp)) fs.remove(tmp);
  return r;
}

struct EntryFill {
  int i;
  const int16_t* mapped;
};

}  // namespace

BankResult exportWav(int i, const char* path) {
  if (!mounted) return BankResult::NoBank;
  if (!hw::sdReady()) return BankResult::NoSd;
  const mt::BankEntry* ep = theBank.entry(i);
  if (!ep) return BankResult::ReadFail;
  const mt::BankEntry e = *ep;
  EntryFill src{i, theBank.data(i)};
  uint32_t crc = 0;
  if (mt::isSampleKey(e.name)) {
    crc = static_cast<uint32_t>(strtoul(e.name, nullptr, 16));
  } else if (src.mapped) {
    crc = mt::sampleCrc(src.mapped, e.frames);
  } else {
    return BankResult::ReadFail;  // not reached: every entry is renamed to its key on import / migration
  }
  const FrameFill fill = [](int16_t* buf, uint32_t at, uint32_t n, void* ctx) {
    const EntryFill& s = *static_cast<EntryFill*>(ctx);
    if (!s.mapped) return theBank.readData(s.i, at, buf, n);
    memcpy(buf, s.mapped + at, n * 2);
    return true;
  };
  return writeWav(path, e.frames, e.rate, e.root, crc, fill, &src);
}

BankResult exportWt(const char* name, const mt::Project& p, const char* path) {
  if (!mounted) return BankResult::NoBank;
  if (!hw::sdReady()) return BankResult::NoSd;
  const int i = wtIndex(name, &p);
  if (i < 0) return BankResult::ReadFail;
  const HeapBuf src(mt::kWtSrcSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!src.p) return BankResult::NoMemory;
  if (const int16_t* table = theBank.data(i)) {
    mt::wtLevel0(table, src.i16());
  } else {
    // No mapping: level 0 of each frame through an internal RAM block.
    int16_t* blk =
        static_cast<int16_t*>(heap_caps_malloc(mt::kWtFrameLen * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    BankResult r = blk ? BankResult::Ok : BankResult::NoMemory;
    for (int f = 0; r == BankResult::Ok && f < mt::kWtFrames; ++f) {
      if (!theBank.readData(i, static_cast<uint32_t>(f) * mt::kWtFramePts, blk, mt::kWtFrameLen))
        r = BankResult::ReadFail;
      else
        memcpy(src.i16() + f * mt::kWtFrameLen, blk, mt::kWtFrameLen * sizeof(int16_t));
    }
    heap_caps_free(blk);
    if (r != BankResult::Ok) return r;
  }
  const char* key = theBank.entry(i)->name;
  const uint32_t crc = mt::isWtKey(key) ? static_cast<uint32_t>(strtoul(key + 1, nullptr, 16))
                                        : mt::sampleCrc(src.i16(), mt::kWtSrcSamples);
  const FrameFill fill = [](int16_t* buf, uint32_t at, uint32_t n, void* ctx) {
    memcpy(buf, static_cast<const int16_t*>(ctx) + at, n * 2);
    return true;
  };
  return writeWav(path, mt::kWtSrcSamples, 44100, 60, crc, fill, src.i16());
}

BankResult loadWavPreview(const char* path, uint32_t maxMs, int16_t** data, uint32_t* frames, uint32_t* rate) {
  *data = nullptr;
  *frames = 0;
  fs::File f;
  mt::WavInfo w;
  if (const BankResult e = openWav(path, f, w); e != BankResult::Ok) return e;
  const uint32_t inFrames = static_cast<uint32_t>(static_cast<uint64_t>(w.rate) * maxMs / 1000);
  const uint32_t take = w.frames() < inFrames ? w.frames() : inFrames;
  BufSink b{nullptr, mt::Downsampler::outFrames(take, w.rate), 0};
  b.d = static_cast<int16_t*>(heap_caps_malloc(b.cap * 2, MALLOC_CAP_SPIRAM));
  if (!b.d) return BankResult::NoMemory;
  const FrameSink toBuf = [](const int16_t* d, uint32_t n, void* ctx) {
    BufSink& b = *static_cast<BufSink*>(ctx);
    if (n > b.cap - b.n) n = b.cap - b.n;
    memcpy(b.d + b.n, d, n * 2);
    b.n += n;
    return true;
  };
  const BankResult r = convertData(f, w, take, toBuf, &b, nullptr, nullptr);
  if (r != BankResult::Ok || b.n == 0) {
    heap_caps_free(b.d);
    return r != BankResult::Ok ? r : BankResult::Truncated;
  }
  *data = b.d;
  *frames = b.n;
  *rate = mt::Downsampler(w.rate).outRate();
  return BankResult::Ok;
}

BankResult compactBank(BankProgressFn cb, void* ctx) {
  if (!mounted) return BankResult::NoBank;
  if (!engineIdle()) return BankResult::Busy;
  FlashWork work;
  if (!work.ok) return BankResult::Busy;
  return theBank.compact(cb, ctx) ? BankResult::Ok : BankResult::WriteFail;
}

BankResult clearCache(const mt::Project& p, int* removed) {
  if (removed) *removed = 0;
  if (!mounted) return BankResult::NoBank;
  if (!engineIdle()) return BankResult::Busy;
  FlashWork work;
  if (!work.ok) return BankResult::Busy;
  const int n = mt::bankClearUnused(theBank, p);
  if (removed) *removed = n;
  return BankResult::Ok;
}

bool legacySource(uint32_t crc, uint32_t frames, char project[17], char name[17]) {
  SdLegacyIndex idx;
  mt::LegacySample s{};
  if (!idx.findData(crc, frames, s) || !s.project[0]) return false;
  strcpy(project, s.project);
  strcpy(name, s.name);
  return true;
}

BankResult migrateProject(mt::Project& p, int* missing) {
  *missing = 0;
  if (!mounted) return BankResult::NoBank;
  if (!engineIdle()) return BankResult::Busy;
  FlashWork work;
  if (!work.ok) return BankResult::Busy;
  SdLegacyIndex idx;
  *missing = mt::migrateSamples(p, theBank, idx);
  return BankResult::Ok;
}

}  // namespace audio
