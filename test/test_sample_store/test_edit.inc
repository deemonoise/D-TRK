// Store edits exercise real PCM/WAV/CRC and the one-reader/one-writer BankDisk contract.
#include <algorithm>
#include <map>
#include <string>
#include "render.h"
#include "wave_peaks.h"

namespace {
struct EditDisk final : BankDisk {
  std::map<std::string, std::vector<uint8_t>> files;
  std::string reading, writing;
  bool present = true, closeFail = false, renameFail = false, seekFail = false, reserveFail = false;
  uint32_t reportedSize = 0;
  int failRead = -1, failWrite = -1, reads = 0, writes = 0, readerOverlap = 0;
  size_t maxRead = 0, maxWrite = 0;
  struct Reader final : ReadFile {
    EditDisk& d; size_t at = 0;
    explicit Reader(EditDisk& disk) : d(disk) {}
    bool read(void* out, size_t n) override {
      ++d.reads; d.maxRead = std::max(d.maxRead, n);
      const auto& b = d.files.at(d.reading);
      if (d.reads == d.failRead || at + n > b.size()) return false;
      memcpy(out, b.data() + at, n); at += n; return true;
    }
    bool skip(size_t n) override { return seek(static_cast<uint32_t>(at + n)); }
    uint32_t size() override { return d.reportedSize ? d.reportedSize : static_cast<uint32_t>(d.files.at(d.reading).size()); }
    bool seek(uint32_t pos) override {
      if (pos > size()) return false;
      at = pos; return true;
    }
  } reader{*this};
  struct Writer final : WriteFile {
    EditDisk& d; size_t at = 0;
    explicit Writer(EditDisk& disk) : d(disk) {}
    bool write(const void* src, size_t n) override {
      ++d.writes; d.maxWrite = std::max(d.maxWrite, n);
      if (d.writes == d.failWrite) return false;
      auto& b = d.files.at(d.writing);
      if (b.size() < at + n) b.resize(at + n);
      memcpy(b.data() + at, src, n); at += n; return true;
    }
    bool reserve(uint32_t) override { return !d.reserveFail; }
    bool seek(uint32_t pos) override { if (d.seekFail) return false; at = pos; return true; }
  } writer{*this};
  bool ready() override { return present; }
  ReadFile* openRead(const char* path) override {
    if (!reading.empty()) { ++readerOverlap; return nullptr; }
    if (!files.count(path)) return nullptr;
    reading = path; reader.at = 0; return &reader;
  }
  void closeRead() override { reading.clear(); }
  WriteFile* openWrite(const char* path) override {
    if (!writing.empty()) return nullptr;
    writing = path; files[path].clear(); writer.at = 0; return &writer;
  }
  bool closeWrite() override { writing.clear(); return !closeFail; }
  bool exists(const char* path) override { return files.count(path); }
  bool remove(const char* path) override { return files.erase(path); }
  bool rename(const char* from, const char* to) override {
    if (renameFail || !files.count(from) || files.count(to)) return false;
    files[to] = std::move(files[from]); files.erase(from); return true;
  }
  bool mkdir(const char*) override { return true; }
};

std::vector<uint8_t> editWav(const std::vector<int16_t>& data, uint32_t rate = 22050, uint8_t root = 48) {
  std::vector<uint8_t> file(kStoreDataOffset + 2 * data.size());
  storeHeader(file.data(), static_cast<uint32_t>(data.size()), rate, root,
              sampleCrc(data.data(), static_cast<uint32_t>(data.size())));
  memcpy(file.data() + kStoreDataOffset, data.data(), data.size() * 2);
  return file;
}
std::vector<int16_t> editRamp(int n) {
  std::vector<int16_t> data(n);
  for (int i = 0; i < n; ++i) data[i] = static_cast<int16_t>(i * 19 - 17000);
  return data;
}
BankResult runEdit(StoreJob& job) {
  for (int i = 0; i < 1000; ++i) {
    const auto r = job.step();
    if (r != BankResult::Running) return r;
  }
  TEST_FAIL_MESSAGE("edit did not terminate"); return BankResult::Running;
}
void verifyEdit(EditDisk& disk, const ImportOut& result, const std::vector<int16_t>& expected,
                uint32_t rate = 22050, uint8_t root = 48) {
  TEST_ASSERT_EQUAL_UINT32(expected.size(), result.frames);
  TEST_ASSERT_EQUAL_UINT32(rate, result.rate);
  TEST_ASSERT_EQUAL_UINT8(root, result.root);
  TEST_ASSERT_EQUAL_HEX32(sampleCrc(expected.data(), expected.size()), result.crc);
  char path[kStorePathMax]; storePath(result.crc, path);
  TEST_ASSERT_TRUE(disk.files.count(path));
  const auto& file = disk.files.at(path);
  TEST_ASSERT_EQUAL(kStoreDataOffset + expected.size() * 2, file.size());
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), file.data() + kStoreDataOffset, expected.size() * 2);
  VecSource src(file); WavInfo w;
  TEST_ASSERT_EQUAL(WavErr::Ok, wavParse(src, w));
  TEST_ASSERT_EQUAL(result.crc, w.crc);
  TEST_ASSERT_EQUAL(result.frames, w.frames());
  TEST_ASSERT_EQUAL(rate, w.rate); TEST_ASSERT_EQUAL(root, w.root);
  TEST_ASSERT_EQUAL(0, disk.readerOverlap);
  TEST_ASSERT_TRUE(disk.reading.empty()); TEST_ASSERT_TRUE(disk.writing.empty());
}
void noEditTemps(const EditDisk& disk) {
  TEST_ASSERT_FALSE(disk.files.count("/mt-store/edit.tmp"));
  TEST_ASSERT_FALSE(disk.files.count("/mt-store/edit.pk.tmp"));
}
}  // namespace

void test_edit_extracts_half_open_region_preserves_source_metadata_and_builds_peaks() {
  EditDisk disk; const auto samples = editRamp(7000); disk.files["/src.wav"] = editWav(samples);
  const auto original = disk.files["/src.wav"];
  StoreJob job(disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, job.startEdit("/src.wav", 123, 6100, false));
  TEST_ASSERT_EQUAL(StoreJob::Kind::Edit, job.kind());
  TEST_ASSERT_TRUE(job.busy());
  TEST_ASSERT_EQUAL(BankResult::Ok, runEdit(job));
  const std::vector<int16_t> expected(samples.begin() + 123, samples.begin() + 6100);
  verifyEdit(disk, job.imported(), expected);
  TEST_ASSERT_TRUE(disk.files["/src.wav"] == original);
  char pk[kStorePathMax]; storePeaksPath(job.imported().crc, pk);
  TEST_ASSERT_TRUE(disk.files.count(pk));
  std::vector<int8_t> peaks(2 * ((expected.size() + kPeakBlock - 1) / kPeakBlock));
  const int count = peakOverview(expected.data(), expected.size(), peaks.data());
  TEST_ASSERT_EQUAL(2 * count, disk.files[pk].size());
  TEST_ASSERT_EQUAL_MEMORY(peaks.data(), disk.files[pk].data(), peaks.size());
  TEST_ASSERT_TRUE(disk.maxRead <= StoreJob::kStepFrames * 2);
  TEST_ASSERT_TRUE(disk.maxWrite <= StoreJob::kStepFrames * 2);
  noEditTemps(disk);
}

void test_edit_normalizes_selected_pcm_with_int16_min_and_silence() {
  for (bool silence : {false, true}) {
    EditDisk disk; std::vector<int16_t> samples(2501, 0);
    if (!silence) { samples[1] = -32768; samples[2500] = 32767; samples[2400] = 1; }
    disk.files["/src.wav"] = editWav(samples, 44100, 71);
    const auto original = disk.files["/src.wav"];
    StoreJob job(disk);
    TEST_ASSERT_EQUAL(BankResult::Ok, job.startEdit("/src.wav", 0, samples.size(), true));
    TEST_ASSERT_EQUAL(BankResult::Ok, runEdit(job));
    std::vector<int16_t> expected = samples;
    const int target = static_cast<int>(kNormPeak + 0.5f);
    if (!silence) {
      expected[1] = -target;
      expected[2500] = static_cast<int16_t>((int64_t(32767) * target + 16384) / 32768);
      expected[2400] = 1;
    }
    verifyEdit(disk, job.imported(), expected, 44100, 71);
    TEST_ASSERT_TRUE(disk.files["/src.wav"] == original);
    noEditTemps(disk);
  }
}

void test_edit_normalizes_only_the_selection_and_copies_one_frame() {
  EditDisk disk; const std::vector<int16_t> samples = {-32768, 1000, 1000, 32767};
  disk.files["/src.wav"] = editWav(samples);
  StoreJob job(disk);
  TEST_ASSERT_EQUAL(BankResult::Ok, job.startEdit("/src.wav", 1, 2, true));
  TEST_ASSERT_EQUAL(BankResult::Ok, runEdit(job));
  verifyEdit(disk, job.imported(), {static_cast<int16_t>(kNormPeak + 0.5f)});
}

void test_edit_rejects_empty_reversed_out_of_bounds_and_noncanonical_input() {
  EditDisk disk; disk.files["/src.wav"] = editWav(editRamp(20));
  StoreJob job(disk);
  for (auto range : {std::pair<uint32_t,uint32_t>{2,2}, {4,3}, {0,21}, {UINT32_MAX,UINT32_MAX}}) {
    TEST_ASSERT_EQUAL(BankResult::Unsupported, job.startEdit("/src.wav", range.first, range.second, false));
    TEST_ASSERT_FALSE(job.busy()); noEditTemps(disk);
  }
  TEST_ASSERT_EQUAL(BankResult::OpenFail, job.startEdit("/absent", 0, 1, false));
  const auto valid = disk.files["/src.wav"];
  for (int mode = 0; mode < 3; ++mode) {
    disk.files["/src.wav"] = valid;
    if (mode == 0) { disk.files["/src.wav"][22] = 2; disk.files["/src.wav"][32] = 4; }
    if (mode == 1) { uint32_t rate = 48000; memcpy(disk.files["/src.wav"].data()+24, &rate, 4); }
    if (mode == 2) disk.files["/src.wav"][34] = 24;
    TEST_ASSERT_EQUAL(BankResult::Unsupported, job.startEdit("/src.wav", 0, 1, false));
    TEST_ASSERT_FALSE(job.busy()); noEditTemps(disk);
  }
}

void test_edit_actual_file_bounds_are_checked_before_copy() {
  EditDisk disk; disk.files["/src.wav"] = editWav(editRamp(20));
  disk.files["/src.wav"].resize(kStoreDataOffset + 12);
  StoreJob job(disk);
  TEST_ASSERT_EQUAL(BankResult::Truncated, job.startEdit("/src.wav", 0, 20, false));
  TEST_ASSERT_FALSE(job.busy()); noEditTemps(disk);
}

void test_edit_full_range_dedup_preserves_existing_wav_and_overview_on_cancel_or_success() {
  const auto samples = editRamp(3000); const uint32_t crc = sampleCrc(samples.data(), samples.size());
  for (bool normalize : {false, true}) {
    // For normalize use all-zero PCM, whose data key is unchanged.
    const std::vector<int16_t> pcm = normalize ? std::vector<int16_t>(3000,0) : samples;
    const uint32_t key = normalize ? sampleCrc(pcm.data(), pcm.size()) : crc;
    char wav[kStorePathMax], pk[kStorePathMax]; storePath(key,wav); storePeaksPath(key,pk);
    EditDisk disk; disk.files[wav] = editWav(pcm); disk.files[pk] = {7,4,9};
    const auto original = disk.files[wav], overview = disk.files[pk];
    StoreJob job(disk);
    TEST_ASSERT_EQUAL(BankResult::Ok, job.startEdit(wav, 0, pcm.size(), normalize));
    TEST_ASSERT_EQUAL(BankResult::Ok, runEdit(job));
    job.cancel();
    TEST_ASSERT_TRUE(disk.files[wav] == original); TEST_ASSERT_TRUE(disk.files[pk] == overview);
    verifyEdit(disk,job.imported(),pcm); noEditTemps(disk);
  }
}

void test_edit_cancel_drops_only_own_temporary_files_and_never_published_result() {
  const auto samples = editRamp(6000);
  for (int stop = 0; stop < 21; ++stop) {
    EditDisk disk; disk.files["/src.wav"] = editWav(samples); const auto source = disk.files["/src.wav"];
    disk.files[kStoreTmp] = {11,22,33};  // another operation's public import temp is untouched
    StoreJob job(disk);
    TEST_ASSERT_EQUAL(BankResult::Ok, job.startEdit("/src.wav", 1, 5900, true));
    for (int n = 0; n < stop && job.busy(); ++n) job.step();
    std::map<std::string,std::vector<uint8_t>> published;
    for (const auto& pair : disk.files)
      if (pair.first != "/src.wav" && pair.first.find(".tmp") == std::string::npos) published.insert(pair);
    job.cancel();
    TEST_ASSERT_FALSE(job.busy());
    TEST_ASSERT_TRUE(disk.files["/src.wav"] == source);
    TEST_ASSERT_TRUE(disk.files[kStoreTmp] == std::vector<uint8_t>({11,22,33}));
    for (const auto& pair : published) TEST_ASSERT_TRUE(disk.files[pair.first] == pair.second);
    noEditTemps(disk);
  }
}

void test_edit_faults_clean_temporary_output_and_are_retryable() {
  for (int fault = 0; fault < 5; ++fault) {
    EditDisk disk; disk.files["/src.wav"] = editWav(editRamp(3000)); const auto source = disk.files["/src.wav"];
    StoreJob job(disk);
    TEST_ASSERT_EQUAL(BankResult::Ok, job.startEdit("/src.wav", 0, 2400, false));
    if (fault == 0) disk.failRead = disk.reads + 1;
    if (fault == 1) disk.failWrite = disk.writes + 1;
    if (fault == 2) disk.closeFail = true;
    if (fault == 3) disk.renameFail = true;
    if (fault == 4) disk.seekFail = true;
    TEST_ASSERT_NOT_EQUAL(BankResult::Ok, runEdit(job));
    TEST_ASSERT_FALSE(job.busy()); noEditTemps(disk);
    TEST_ASSERT_TRUE(disk.files["/src.wav"] == source);
    disk.failRead = disk.failWrite = -1; disk.closeFail = disk.renameFail = disk.seekFail = false;
    TEST_ASSERT_EQUAL(BankResult::Ok, job.startEdit("/src.wav", 0, 2400, false));
    TEST_ASSERT_EQUAL(BankResult::Ok, runEdit(job));
    const auto pcm = editRamp(3000);
    verifyEdit(disk, job.imported(), std::vector<int16_t>(pcm.begin(), pcm.begin() + 2400));
  }
}

void test_edit_progress_is_bounded_per_step_and_does_not_restart_an_active_job() {
  for (bool normalize : {false,true}) {
    EditDisk disk; disk.files["/src.wav"] = editWav(editRamp(4097));
    StoreJob job(disk);
    TEST_ASSERT_EQUAL(BankResult::Ok, job.startEdit("/src.wav", 0, 4097, normalize));
    const uint32_t total = job.total();
    TEST_ASSERT_EQUAL_UINT32(4097 * (normalize ? 2 : 1), total);
    TEST_ASSERT_EQUAL(BankResult::Busy, job.startEdit("/src.wav", 0, 10, false));
    uint32_t prior = 0;
    for (int i = 0; i < 100 && job.busy(); ++i) {
      const auto r = job.step();
      if (r == BankResult::Running) {
        TEST_ASSERT_TRUE(job.done() >= prior);
        TEST_ASSERT_TRUE(job.done() - prior <= StoreJob::kStepFrames);
        TEST_ASSERT_TRUE(job.done() <= total);
        TEST_ASSERT_EQUAL_UINT32(total, job.total());
        prior = job.done();
      } else TEST_ASSERT_EQUAL(BankResult::Ok, r);
    }
    TEST_ASSERT_FALSE(job.busy()); noEditTemps(disk);
  }
}

void test_edit_cancel_and_overview_failures_keep_committed_wav_and_prior_overview() {
  for (int failure = 0; failure < 5; ++failure) {
    EditDisk disk; const auto pcm = editRamp(3000); disk.files["/src.wav"] = editWav(pcm);
    const std::vector<int16_t> expected(pcm.begin()+1, pcm.begin()+2800);
    const uint32_t key = sampleCrc(expected.data(), expected.size());
    char wav[kStorePathMax], pk[kStorePathMax]; storePath(key,wav); storePeaksPath(key,pk);
    const std::vector<uint8_t> sentinel = {12,34,56};
    if (failure == 0) disk.files[pk] = sentinel;  // existing orphan overview must survive too
    StoreJob job(disk);
    TEST_ASSERT_EQUAL(BankResult::Ok, job.startEdit("/src.wav",1,2800,false));
    for (int i=0;i<10&&!disk.files.count(wav);++i) TEST_ASSERT_EQUAL(BankResult::Running,job.step());
    TEST_ASSERT_TRUE(disk.files.count(wav)); const auto published = disk.files[wav];
    if (failure == 1) disk.failRead = disk.reads+1;
    if (failure == 2) disk.failWrite = disk.writes+1;
    if (failure == 3) disk.closeFail = true;
    if (failure == 4) { job.cancel(); }
    else TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));  // overview is optional
    TEST_ASSERT_TRUE(disk.files[wav] == published);
    if (failure == 0) TEST_ASSERT_TRUE(disk.files[pk] == sentinel);
    else TEST_ASSERT_FALSE(disk.files.count(pk));
    noEditTemps(disk);
  }
}

void test_edit_can_be_followed_by_ordinary_import_and_added_callback_has_closed_source() {
  EditDisk disk; const auto pcm = editRamp(4100); disk.files["/src.wav"] = editWav(pcm);
  struct Observer {
    EditDisk& disk; int calls = 0;
    static void added(uint32_t crc, void* context) {
      auto& self = *static_cast<Observer*>(context);
      TEST_ASSERT_TRUE(self.disk.reading.empty()); TEST_ASSERT_TRUE(self.disk.writing.empty());
      char wav[kStorePathMax]; storePath(crc,wav); TEST_ASSERT_TRUE(self.disk.files.count(wav));
      ++self.calls;
    }
  } observer{disk};
  StoreJob job(disk); job.setOnAdded(Observer::added,&observer);
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startEdit("/src.wav",2,4099,true));
  TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));
  TEST_ASSERT_EQUAL(1,observer.calls);
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startImport("/src.wav"));
  TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));
  verifyEdit(disk,job.imported(),pcm);
  TEST_ASSERT_EQUAL(2,observer.calls);
  noEditTemps(disk);
}

void test_edit_reservation_failure_removes_temp_and_leaves_source_intact() {
  for (bool normalize : {false,true}) {
    EditDisk disk; disk.files["/src.wav"] = editWav(editRamp(4000));
    const auto original = disk.files["/src.wav"]; disk.reserveFail = true;
    StoreJob job(disk);
    BankResult r = job.startEdit("/src.wav",0,4000,normalize);
    if (r == BankResult::Ok) r = runEdit(job);
    TEST_ASSERT_EQUAL(BankResult::WriteFail,r);
    TEST_ASSERT_FALSE(job.busy()); noEditTemps(disk);
    TEST_ASSERT_TRUE(disk.files["/src.wav"] == original);
    TEST_ASSERT_TRUE(disk.reading.empty()); TEST_ASSERT_TRUE(disk.writing.empty());
  }
}

void test_edit_dedup_metadata_conflict_never_changes_existing_keyed_files() {
  const auto input = editRamp(5000);
  const std::vector<int16_t> extracted(input.begin()+1,input.begin()+4500);
  const uint32_t crc = sampleCrc(extracted.data(),extracted.size());
  char wav[kStorePathMax], pk[kStorePathMax]; storePath(crc,wav); storePeaksPath(crc,pk);
  for (bool rateConflict : {false,true}) {
    EditDisk disk; disk.files["/src.wav"] = editWav(input);
    disk.files[wav] = editWav(extracted,rateConflict?44100:22050,rateConflict?48:60);
    disk.files[pk] = {11,22,33}; const auto original = disk.files[wav], peaks = disk.files[pk];
    StoreJob job(disk);
    TEST_ASSERT_EQUAL(BankResult::Ok,job.startEdit("/src.wav",1,4500,false));
    TEST_ASSERT_EQUAL(BankResult::Unsupported,runEdit(job));
    TEST_ASSERT_TRUE(disk.files[wav] == original); TEST_ASSERT_TRUE(disk.files[pk] == peaks);
    TEST_ASSERT_EQUAL_UINT32(0,job.imported().frames);
    noEditTemps(disk);
  }
}

void test_edit_large_progress_total_never_wraps_or_allocates_whole_file() {
  EditDisk disk;
  constexpr uint32_t frames = (UINT32_MAX-kStoreDataOffset)/2;
  disk.files["/src.wav"] = std::vector<uint8_t>(kStoreDataOffset);
  storeHeader(disk.files["/src.wav"].data(),frames,22050,48,1234);
  disk.reportedSize = UINT32_MAX;
  StoreJob job(disk);
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startEdit("/src.wav",0,frames,true));
  TEST_ASSERT_EQUAL_UINT32(frames*2,job.total());
  TEST_ASSERT_EQUAL_UINT32(0,job.done());
  TEST_ASSERT_TRUE(disk.maxRead <= kStoreDataOffset);
  TEST_ASSERT_EQUAL(0,disk.writes);  // normalize does not allocate output before the scan
  job.cancel(); noEditTemps(disk);
}

void test_edit_scan_read_failure_closes_source_without_creating_output() {
  EditDisk disk; disk.files["/src.wav"] = editWav(editRamp(4000));
  const auto original = disk.files["/src.wav"];
  StoreJob job(disk);
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startEdit("/src.wav",0,4000,true));
  disk.failRead = disk.reads+2;
  TEST_ASSERT_EQUAL(BankResult::Truncated,runEdit(job));
  TEST_ASSERT_EQUAL(0,disk.writes);
  TEST_ASSERT_TRUE(disk.files["/src.wav"] == original);
  TEST_ASSERT_TRUE(disk.reading.empty()); TEST_ASSERT_TRUE(disk.writing.empty());
  TEST_ASSERT_FALSE(job.busy()); noEditTemps(disk);
  disk.failRead = -1;
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startEdit("/src.wav",0,4000,true));
  TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));
}
