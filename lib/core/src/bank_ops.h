#pragma once
#include <stdint.h>
#include "model.h"
#include "project_io.h"
#include "sample_bank.h"
#include "synth.h"
#include "wav.h"

namespace mt {

// The sample bank's work on the synth board: the synth's sources, imports from the card, exports to
// the project folders, built-in wavetables. Card access goes through BankDisk (SdFat there, POSIX in
// the tests); the bank through its BankFlash.

enum class BankResult : uint8_t {
  Ok, Busy, NoSd, NoBank, OpenFail, ReadFail, NotWav, Unsupported, Truncated, Full, WriteFail, NoMemory,
  Link,     // ESP side: no answer from the synth board
  Running,  // BankJob::step: not done yet
};

// A file being read: ByteSource read / skip take exactly n bytes or fail.
struct ReadFile : ByteSource {
  virtual uint32_t size() = 0;
  virtual bool seek(uint32_t pos) = 0;
};
// A file being written: ByteSink write takes exactly n bytes or fails.
struct WriteFile : ByteSink {};

struct BankDisk {
  virtual bool ready() = 0;  // a card is in and mounted
  // One file of each kind open at a time; nullptr when it cannot be opened (absent / a folder).
  virtual ReadFile* openRead(const char* path) = 0;
  virtual void closeRead() = 0;
  virtual WriteFile* openWrite(const char* path) = 0;  // creates / truncates
  virtual bool closeWrite() = 0;                       // false: not all of it reached the card
  virtual bool exists(const char* path) = 0;
  virtual bool remove(const char* path) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
  virtual bool mkdir(const char* path) = 0;
};

// Lookup by name for the synth (audio interrupt): the name goes through the model's sample list to the
// bank entry of its data key. The list and the bank change only with the synth parked.
class BankSampleSource final : public SampleSource {
 public:
  BankSampleSource(const SampleBank& b, const SynthModel& m) : b_(b), m_(m) {}
  const int16_t* find(const char* name, uint32_t& frames, uint32_t& rate) const override;

 private:
  const SampleBank& b_;
  const SynthModel& m_;
};

// Wavetables for the synth (note-on): a built-in "*NAME" is its bank entry, any other name goes
// through the model's wavetable list to the entry of its key (wtKey).
class BankWtSource final : public WtSource {
 public:
  BankWtSource(const SampleBank& b, const SynthModel& m) : b_(b), m_(m) {}
  const int16_t* findWt(const char* name) const override;

 private:
  const SampleBank& b_;
  const SynthModel& m_;
};

// Bank index of wavetable name (built-in or through m's list), -1 if absent or not a wavetable entry.
int bankWtIndex(const SampleBank& b, const SynthModel& m, const char* name);
// Level 0 of frame f of wavetable name, >> 8 (kWtFrameLen points). False if absent.
bool bankWtFrame(const SampleBank& b, const SynthModel& m, const char* name, int f, int8_t* out);

// Built-in wavetables missing from the bank are generated (wtBuild into table, kWtTableSamples) and
// stored as "*NAME" (never evicted afterwards), making room by evicting entries m does not use.
// keep is called between them. Returns how many were added.
int bankAddBuiltins(SampleBank& b, const SynthModel& m, int16_t* table, BankProgress keep = nullptr,
                    void* ctx = nullptr);

struct ImportOut {
  uint32_t crc = 0, frames = 0, rate = 0;  // bank data (a wavetable: frames = rate = 0)
  uint8_t root = 60;
};

// Writes frames of generated data (fill: consecutive pieces, at = frames done so far; false cancels:
// ReadFail) into the cache under its data key, making room by evicting entries m does not use.
using FrameFill = bool (*)(int16_t* buf, uint32_t at, uint32_t n, void* ctx);
BankResult bankWriteFrames(SampleBank& b, const SynthModel& m, uint32_t frames, uint32_t rate, FrameFill fill,
                           void* ctx, ImportOut& out, BankProgress keep = nullptr, void* keepCtx = nullptr);

// True if path holds frames of mono 16-bit data with this crc: checked by its "mtcr" chunk and the
// file size (no data read).
bool bankFileCurrent(BankDisk& d, const char* path, uint32_t crc, uint32_t frames);

// One bank operation on the card, done a piece at a time (step()) so the caller can serve the link in
// between. Import and Sync change the bank: the synth must stay parked until the job ends.
//  - Import: a WAV into the cache under its data key (sampleKey), mono, <= kWavMaxRate, root from the
//    "smpl" chunk or 60; a wavetable WAV (wtImport layouts, "clm " frame size) under wtKey of its
//    canonical source. When the file's "mtcr" crc (or knownCrc) is cached with the same length, the
//    data is not read. Room is made by evicting entries m does not use.
//  - Sync: every entry of m's lists that is not cached is imported from /projects/<project>/<name>.wav
//    (wavetables: .../wt/<name>.wav); an absent file or other data (crc / length) leaves it missing.
//  - Save: every cached entry of m's lists whose file there is not current (bankFileCurrent) is
//    written (via <file>.tmp); the others are missing (their files are left alone).
class BankJob {
 public:
  enum class Kind : uint8_t { None, Import, Sync, Save };
  static constexpr uint32_t kStepBytes = 2048;  // card I/O per step
  static constexpr int kWtFrameMax = 4096;      // longest wavetable source frame read

  BankJob(SampleBank& b, BankDisk& d) : b_(b), d_(d) {}
  ~BankJob() { cancel(); }
  // Called now and then by long parts done in one step (compacting to make room, a wavetable's
  // analysis), so the caller can show it is alive.
  void setKeepalive(BankProgress keep, void* ctx) {
    keep_ = keep;
    keepCtx_ = ctx;
  }

  BankResult startImport(const char* path, bool wt, const SynthModel& m, const uint32_t* knownCrc = nullptr);
  BankResult startSync(const char* project, const SynthModel& m);
  BankResult startSave(const char* project, const SynthModel& m);
  // Running until the job is over, then its result (again on every call until the next start).
  BankResult step();
  bool busy() const { return kind_ != Kind::None && !over_; }
  Kind kind() const { return kind_; }
  // Progress: an import in bytes of its data, a sync / save in items x 1000.
  uint32_t done() const;
  uint32_t total() const;
  // Sync / save: the item being worked on (samples 0.., wavetables kWtItem + i), -1 if none.
  static constexpr int kWtItem = kProjSamples;
  int item() const { return item_ < count_ ? (item_ < nS_ ? item_ : kWtItem + item_ - nS_) : -1; }
  const ImportOut& imported() const { return out_; }
  bool sampleMissing(int i) const { return i >= 0 && i < kProjSamples && (missS_[i >> 3] >> (i & 7)) & 1; }
  bool wtMissing(int i) const { return i >= 0 && i < kProjWavetables && (missW_[i >> 3] >> (i & 7)) & 1; }
  int missing() const { return missing_; }
  int failed() const { return failed_; }
  // Ends the job: closes its files, drops a half added bank entry.
  void cancel();

 private:
  enum class Sub : uint8_t { None, Import, Export };
  static constexpr int kPathMax = 256;
  BankResult begin(Kind k, const SynthModel& m);
  BankResult finish(BankResult r);
  // Sync / save: decides item_ (may start a sub-operation); Running while items are left.
  BankResult nextItem();
  void itemDone(bool ok);
  void markMissing();

  BankResult itemPath(bool wt, int i);  // path_ of sync / save item i
  bool itemMatches(bool wt, int i) const;  // out_ is the listed data
  BankResult importOpen(const char* path, bool wt, const uint32_t* knownCrc);
  BankResult importStep();
  BankResult importWt(const uint32_t* knownCrc);
  BankResult importEnd(BankResult r);
  BankResult exportOpen(int entry, bool wt);
  BankResult exportStep();
  BankResult exportEnd(BankResult r);
  BankResult openWav(const char* path);

  SampleBank& b_;
  BankDisk& d_;
  BankProgress keep_ = nullptr;
  void* keepCtx_ = nullptr;
  const SynthModel* m_ = nullptr;
  Kind kind_ = Kind::None;
  bool over_ = false;
  BankResult result_ = BankResult::Ok;
  Sub sub_ = Sub::None;
  char project_[kSampleNameMax + 1] = {};
  // Sync / save items: samples first, then wavetables.
  int item_ = 0, count_ = 0, nS_ = 0;
  bool itemOpen_ = false;  // item_ has a sub-operation started
  uint8_t missS_[kProjSamples / 8] = {}, missW_[kProjWavetables / 8] = {};
  int missing_ = 0, failed_ = 0;
  ImportOut out_;
  // Sub-operation state.
  ReadFile* rf_ = nullptr;
  WriteFile* wf_ = nullptr;
  WavInfo w_;
  uint32_t subDone_ = 0, subTotal_ = 0;
  uint32_t crc_ = 0;
  bool adding_ = false;  // a bank add is open
  uint8_t* raw_ = nullptr;
  int16_t* mono_ = nullptr;
  int16_t* conv_ = nullptr;
  bool wtOut_ = false;      // export: a wavetable (its canonical source: level 0 of every frame)
  int entry_ = -1;          // export: the bank entry
  Downsampler ds_{0};
  // Import job: what to open on the first step.
  bool wt_ = false, hasKnown_ = false;
  uint32_t known_ = 0;
  bool started_ = false;  // the first step ran (import: file opened; save: folders made)
  char path_[kPathMax] = {};
  char tmp_[kPathMax + 4] = {};
};

}  // namespace mt
