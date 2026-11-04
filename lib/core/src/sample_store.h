#pragma once
#include <stdint.h>
#include "bank_ops.h"
#include "model.h"
#include "sample_source.h"
#include "sector_map.h"
#include "stream_pool.h"
#include "tempo_detect.h"

namespace mt {
class TimeStretch;

// The sample store on the synth board's card: /mt-store/<crc>.wav, mono 16-bit <= kWavMaxRate, data
// from byte kStoreDataOffset (logical file sector 1), plus /mt-store/<crc>.pk (peak overview).
// Keyed by the data CRC, so it serves every project; the project folders keep their own copies.
constexpr uint32_t kStoreDataOffset = 512;
constexpr int kStorePathMax = 28;
constexpr int kStoreEntries = 256;
constexpr int kHeadPool = 1024;
extern const char kStoreDir[];  // "/mt-store"

void storePath(uint32_t crc, char out[kStorePathMax]);       // /mt-store/0123abcd.wav
void storePeaksPath(uint32_t crc, char out[kStorePathMax]);  // /mt-store/0123abcd.pk
// RIFF + fmt + smpl + mtcr + JUNK (pad) + data header: kStoreDataOffset bytes.
void storeHeader(uint8_t out[kStoreDataOffset], uint32_t frames, uint32_t rate, uint8_t root, uint32_t crc);

// What the synth board knows of the store files. Append only (the audio interrupt reads it
// unlocked: an entry is filled first, then its crc, then the count); clear() only with the synth parked.
struct StoreEntry {
  volatile uint32_t crc = 0;  // 0 = empty
  uint32_t frames = 0, rate = 0;
  uint32_t sector = 0;        // first card sector of the file (data from sector + 1); 0 = not contiguous
  const SectorMap* sectors = nullptr;  // immutable fragmented-file map, or null for the fast path
  uint16_t head0 = 0;         // first of nHeads in the head pool
  uint8_t nHeads = 0;
  const int16_t* volatile psram = nullptr;  // verified whole copy, nullptr = none
};

class StoreTable {
 public:
  int find(uint32_t crc) const;  // -1 if absent
  // Index of crc (added if absent); -1 if the table is full.
  // A map and its extent storage must outlive the entry; both are ready before publication.
  int add(uint32_t crc, uint32_t frames, uint32_t rate, uint32_t sector, const SectorMap* sectors = nullptr);
  // Heads of entry i (once per entry); false if the head pool is full.
  bool setHeads(int i, const SampleHead* h, int n);
  const SampleHead* heads(int i) const { return head_ + e_[i].head0; }
  void setPsram(int i, const int16_t* d) { e_[i].psram = d; }
  const StoreEntry& at(int i) const { return e_[i]; }
  int count() const { return n_; }
  void clear();

 private:
  StoreEntry e_[kStoreEntries];
  volatile int n_ = 0;
  SampleHead head_[kHeadPool];
  int nHead_ = 0;
};

// The synth's samples: name -> the model's list -> the store entry of its crc (PSRAM copy while
// pool.psramOk, else streamed with its heads). A sample not in the store comes from legacy (the
// flash bank of before the store; nullptr = none).
class StoreSampleSource final : public SampleSource {
 public:
  StoreSampleSource(const StoreTable& t, const StreamPool& p, const SampleSource* legacy, const SynthModel& m)
      : t_(t), p_(p), legacy_(legacy), m_(m) {}
  bool find(const char* name, SampleData& out) const override;

 private:
  const StoreTable& t_;
  const StreamPool& p_;
  const SampleSource* legacy_;
  const SynthModel& m_;
};

extern const char kStoreTmp[];  // a store file being written
// crc's store file is there with frames of data (header and size, no data read).
bool storeFileCurrent(BankDisk& d, uint32_t crc, uint32_t frames);
// Drops every sample entry (8-hex name) of the flash bank whose store file is current, in one table
// save. The synth is parked (the bank changes). Returns how many.
int bankDropMigrated(SampleBank& b, BankDisk& d);

// Writes frames of generated data (fill as for bankWriteFrames; rate <= kWavMaxRate) as a store file
// under its crc, then its peak overview; one call (keep gets the progress). Ok also when the file was
// there already. The synth may be running (register the file afterwards).
BankResult storeWriteFrames(BankDisk& d, uint32_t frames, uint32_t rate, FrameFill fill, void* ctx, ImportOut& out,
                            BankProgress keep = nullptr, void* keepCtx = nullptr);

// One store operation on the card, a piece at a time (step()) between link and audio work. Nothing
// here changes the flash bank: the synth keeps playing (register new files through setOnAdded).
//  - Import: any supported WAV -> the store (mono 16 bit, <= kWavMaxRate, root from "smpl"). With the
//    file's mtcr crc (or knownCrc) already stored at its length nothing is read.
//  - Edit: offline copy of a canonical store WAV region, optionally normalized, into a new data
//    key. The source and pre-existing keyed WAV/overview files are never changed.
//    Time stretch uses the same commit/overview/cleanup path and preserves rate/root.
//  - Analyze: bounded read-only onset/tempo scan; no output or temporary files.
//  - Sync: every sample of m not in the store comes from /projects/<project>/<name>.wav; other data
//    (crc) or no file: missing.
//  - Save: every sample of m in the store whose project file is not current is copied there (via
//    <file>.tmp); one not in the store: missing.
//  - Migrate: every sample entry of the flash bank (8-hex name) not in the store is written there.
// Every new store file is followed by its peak overview (<crc>.pk).
class StoreJob {
 public:
  enum class Kind : uint8_t { None, Import, Sync, Save, Migrate, Edit, Analyze };
  static constexpr uint32_t kStepFrames = 1024;  // per step; a multiple of kPeakBlock
  using Added = void (*)(uint32_t crc, void* ctx);

  explicit StoreJob(BankDisk& d) : d_(d) {}
  ~StoreJob() { cancel(); }
  void setOnAdded(Added f, void* ctx) {
    added_ = f;
    addedCtx_ = ctx;
  }
  BankResult startImport(const char* path, const uint32_t* knownCrc = nullptr);
  // Offline canonical mono16 copy of [from,to), optionally peak-normalized to -1 dBFS.
  BankResult startEdit(const char* path, uint32_t from, uint32_t to, bool normalize);
  // Pitch-preserving duration multiplier Q16, 0.5..2. Output length is rounded with 64-bit
  // arithmetic; unity is an exact copy. Temporary DSP memory exists only while converting.
  BankResult startStretch(const char* path, uint32_t from, uint32_t to, uint32_t ratioQ16);
  // Read-only scan of the selection. A valid short/silent/weak selection succeeds with unknown
  // bpm10/confidence 0. No output/temp files are created, overwritten or removed by this job.
  BankResult startTempo(const char* path, uint32_t from, uint32_t to);
  const TempoEstimate& tempoResult() const { return tempoResult_; }
  BankResult startSync(const char* project, const SynthModel& m);
  BankResult startSave(const char* project, const SynthModel& m);
  BankResult startMigrate(const SampleBank& b);
  BankResult step();  // Running until it ends
  void cancel();

  Kind kind() const { return kind_; }
  bool busy() const { return kind_ != Kind::None; }
  const ImportOut& imported() const { return imp_; }
  bool sampleMissing(int i) const { return i >= 0 && i < kProjSamples && missing_[i]; }
  int missing() const;
  int item() const { return item_; }
  uint32_t done() const;
  uint32_t total() const;

 private:
  enum class Phase : uint8_t { Next, Convert, Copy, Peaks, EditScan, TempoScan };
  struct StretchReader;
  struct TempoState;
  BankResult start(Kind k);
  BankResult nextItem();
  BankResult openConvert(const char* path, const uint32_t* expect, const uint32_t* known);
  BankResult openCopy(uint32_t crc);
  BankResult beginWrite(uint32_t frames);
  BankResult convertStep();
  BankResult editScanStep();
  BankResult editCopyStep();
  BankResult openOperation(Kind kind, const char* path, uint32_t from, uint32_t to);
  BankResult stretchStep();
  BankResult tempoStep();
  void releaseWorkers();
  const char* temporaryPath() const;
  bool editStoredMetadataMatches(const char* path);
  BankResult finishConvert();
  BankResult copyStep();
  BankResult openPeaks();
  BankResult peaksStep();
  BankResult endItem(BankResult r);
  BankResult finish(BankResult r);
  void closeAll();

  BankDisk& d_;
  Added added_ = nullptr;
  void* addedCtx_ = nullptr;
  Kind kind_ = Kind::None;
  Phase phase_ = Phase::Next;
  const SynthModel* m_ = nullptr;
  const SampleBank* b_ = nullptr;
  char project_[64] = {};
  char path_[100] = {};  // the source (Convert) or the .tmp copy (Copy): dst_ + ".tmp"
  char dst_[96] = {};   // the project file (Copy) or the overview (Peaks)
  int item_ = 0, count_ = 0;
  bool missing_[kProjSamples] = {};
  ImportOut imp_;
  TempoEstimate tempoResult_;
  TimeStretch* stretch_ = nullptr;
  TempoState* tempo_ = nullptr;
  uint32_t stretchInput_ = 0;
  bool editStretch_ = false;
  ReadFile* in_ = nullptr;
  WriteFile* out_ = nullptr;
  WavInfo w_;
  Downsampler ds_{1};
  int bankEntry_ = -1;
  uint32_t at_ = 0, left_ = 0, frames_ = 0, crc_ = 0, expect_ = 0, rate_ = 0;
  bool hasExpect_ = false;
  bool editNormalize_ = false;
  uint32_t editPeak_ = 0;
  uint8_t root_ = 60;
  uint32_t subDone_ = 0, subTotal_ = 0;
  uint8_t raw_[kStepFrames * 8];  // stereo float: 8 bytes a frame
  int16_t mono_[kStepFrames];
  int16_t res_[kStepFrames + 2];
};

}  // namespace mt
