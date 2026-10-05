#include "bank.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include "audio.h"
#include "engine/engine.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_partition.h"
#include "hw/sdcard.h"
#include "wav.h"

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

struct BankSource final : mt::SampleSource {
  const int16_t* find(const char* name, uint32_t& frames, uint32_t& rate) const override {
    if (!mounted) return nullptr;
    const int i = theBank.find(name);
    const int16_t* d = theBank.data(i);
    if (!d) return nullptr;
    frames = theBank.entry(i)->frames;
    rate = theBank.entry(i)->rate;
    return d;
  }
};
BankSource source;

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

// Copies raw WAV data from f into the open bank entry. Caller: FlashWork held, bank begun.
BankResult copyData(fs::File& f, const mt::WavInfo& w, BankProgressFn cb, void* ctx) {
  const FrameSink toBank = [](const int16_t* d, uint32_t n, void*) { return theBank.write(d, n); };
  return convertData(f, w, w.frames(), toBank, nullptr, cb, ctx);
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

}  // namespace

mt::SampleBank& bank() { return theBank; }
bool bankMounted() { return mounted; }
const mt::SampleSource* sampleSource() { return &source; }
const uint8_t* samplesBase() { return flash.mapped(); }
uint32_t samplesSize() { return flash.size(); }

void bankBegin() {
  if (!flash.open()) return;
  mounted = theBank.mount();
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
    case BankResult::Exists: return "NAME EXISTS";
    case BankResult::Full: return "BANK FULL";
    case BankResult::WriteFail: return "FLASH WRITE ERROR";
    case BankResult::NoMemory: return "NO MEMORY";
  }
  return "?";
}

BankResult importWav(const char* path, const char* name, bool replace, BankProgressFn cb, void* ctx) {
  if (!mounted) return BankResult::NoBank;
  if (!engineIdle()) return BankResult::Busy;
  if (!hw::sdReady()) return BankResult::NoSd;
  const bool exists = theBank.find(name) >= 0;
  if (exists && !replace) return BankResult::Exists;
  fs::File f;
  mt::WavInfo w;
  if (const BankResult e = openWav(path, f, w); e != BankResult::Ok) return e;
  const uint32_t frames = mt::Downsampler::outFrames(w.frames(), w.rate);
  const uint32_t rate = mt::Downsampler(w.rate).outRate();
  // A replacement goes in under a free temporary name first.
  char tmp[mt::kSampleNameMax + 1] = "";
  if (exists) {
    for (int k = 0; k < 10 && !tmp[0]; ++k) {
      snprintf(tmp, sizeof(tmp), "~import%d", k);
      if (theBank.find(tmp) >= 0) tmp[0] = 0;
    }
    if (!tmp[0]) return BankResult::Exists;
  }
  FlashWork work;
  if (!work.ok) return BankResult::Busy;
  const char* as = exists ? tmp : name;
  if (!theBank.begin(as, frames, rate, w.root)) {
    // No room for both: the file is valid, so the old sample goes first.
    if (!exists || !theBank.remove(theBank.find(name))) return BankResult::Full;
    as = name;
    if (!theBank.begin(as, frames, rate, w.root)) return BankResult::Full;
  }
  BankResult r = copyData(f, w, cb, ctx);
  if (r == BankResult::Ok && !theBank.commit()) r = BankResult::WriteFail;
  if (r != BankResult::Ok) {
    theBank.abort();
    return r;
  }
  if (as == tmp && !theBank.replace(theBank.find(name), theBank.find(tmp))) {
    theBank.remove(theBank.find(tmp));
    return BankResult::WriteFail;
  }
  return BankResult::Ok;
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

BankResult removeSample(int i) {
  if (!mounted) return BankResult::NoBank;
  if (i < 0 || i >= theBank.count()) return BankResult::Ok;  // already gone
  if (!engineIdle()) return BankResult::Busy;
  FlashWork work;
  if (!work.ok) return BankResult::Busy;
  return theBank.remove(i) ? BankResult::Ok : BankResult::WriteFail;
}

BankResult compactBank(BankProgressFn cb, void* ctx) {
  if (!mounted) return BankResult::NoBank;
  if (!engineIdle()) return BankResult::Busy;
  FlashWork work;
  if (!work.ok) return BankResult::Busy;
  return theBank.compact(cb, ctx) ? BankResult::Ok : BankResult::WriteFail;
}

}  // namespace audio
