// Bounded offline operations reuse EditDisk and immutable-output assertions from test_edit.inc.
#include <cmath>

static std::vector<int16_t> operationTone(uint32_t frames, uint32_t rate=22050) {
  std::vector<int16_t> data(frames);
  for(uint32_t i=0;i<frames;++i) data[i]=static_cast<int16_t>(12000*std::sin(2*3.141592653589793*220*i/rate));
  return data;
}
static void test_stretch_unity_is_exact_copy_with_preserved_source_and_metadata() {
  EditDisk disk; const auto source=editRamp(4096); disk.files["/source.wav"]=editWav(source);
  const auto before=disk.files; StoreJob job(disk);
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startStretch("/source.wav",17,3021,65536));
  TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));
  verifyEdit(disk,job.imported(),{source.begin()+17,source.begin()+3021});
  TEST_ASSERT_TRUE(disk.files.at("/source.wav")==before.at("/source.wav")); noEditTemps(disk);
}
static void test_stretch_ratios_have_exact_duration_crc_rate_and_bounded_reads() {
  for(uint32_t ratio:{32768u,49152u,98304u,131072u}) {
    EditDisk disk; const auto source=operationTone(6000); const auto original=editWav(source);
    disk.files["/source.wav"]=original; StoreJob job(disk);
    TEST_ASSERT_EQUAL(BankResult::Ok,job.startStretch("/source.wav",101,5102,ratio));
    const uint32_t expected=static_cast<uint32_t>((uint64_t(5001)*ratio+32768)/65536);
    TEST_ASSERT_EQUAL_UINT32(expected,job.total());
    TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));
    const auto out=job.imported(); TEST_ASSERT_EQUAL_UINT32(expected,out.frames);
    char path[kStorePathMax]; storePath(out.crc,path); const auto& file=disk.files.at(path);
    std::vector<int16_t> pcm(expected); memcpy(pcm.data(),file.data()+kStoreDataOffset,expected*2);
    verifyEdit(disk,out,pcm); TEST_ASSERT_TRUE(disk.files.at("/source.wav")==original);
    TEST_ASSERT_TRUE(disk.maxRead<=2048); TEST_ASSERT_TRUE(disk.maxWrite<=2048); noEditTemps(disk);
  }
}
static void test_stretch_invalid_ratio_and_overflow_never_create_output() {
  EditDisk disk; disk.files["/source.wav"]=editWav(editRamp(4096)); const auto before=disk.files;
  StoreJob job(disk);
  for(uint32_t ratio:{0u,32767u,131073u,UINT32_MAX})
    TEST_ASSERT_EQUAL(BankResult::Unsupported,job.startStretch("/source.wav",0,4096,ratio));
  TEST_ASSERT_EQUAL(BankResult::Unsupported,job.startStretch("/source.wav",4,4,65536));
  TEST_ASSERT_EQUAL(BankResult::Unsupported,job.startStretch("/source.wav",0,5000,65536));
  TEST_ASSERT_EQUAL_INT(0,disk.writes); TEST_ASSERT_TRUE(disk.files==before); noEditTemps(disk);
  disk.reportedSize=UINT32_MAX;
  storeHeader(disk.files["/source.wav"].data(),(UINT32_MAX-kStoreDataOffset)/2,22050,48,0);
  TEST_ASSERT_EQUAL(BankResult::Unsupported,job.startStretch("/source.wav",0,(UINT32_MAX-kStoreDataOffset)/2,131072));
  TEST_ASSERT_EQUAL_INT(0,disk.writes); noEditTemps(disk);
}
static void test_stretch_cancel_and_failures_close_source_and_leave_it_unchanged() {
  EditDisk disk; const auto original=editWav(operationTone(10000)); disk.files["/source.wav"]=original;
  StoreJob job(disk);
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startStretch("/source.wav",0,10000,98304));
  TEST_ASSERT_EQUAL(BankResult::Running,job.step()); job.cancel(); noEditTemps(disk);
  TEST_ASSERT_TRUE(disk.reading.empty()); TEST_ASSERT_TRUE(disk.writing.empty());
  TEST_ASSERT_TRUE(disk.files.at("/source.wav")==original);
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startStretch("/source.wav",0,10000,32768));
  disk.failRead=disk.reads+1;
  TEST_ASSERT_EQUAL(BankResult::ReadFail,runEdit(job)); noEditTemps(disk);
  disk.failRead=-1; disk.reserveFail=true;
  TEST_ASSERT_EQUAL(BankResult::WriteFail,job.startStretch("/source.wav",0,10000,98304)); noEditTemps(disk);
  disk.reserveFail=false;
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startEdit("/source.wav",0,100,false));
  TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));
}
static void test_tempo_is_read_only_and_short_or_silent_input_is_valid_unknown() {
  for(const auto& data:{std::vector<int16_t>(1,123),std::vector<int16_t>(22050*3)}) {
    EditDisk disk; disk.files["/source.wav"]=editWav(data); disk.files["/mt-store/edit.tmp"]={1,2,3};
    const auto before=disk.files; StoreJob job(disk);
    TEST_ASSERT_EQUAL(BankResult::Ok,job.startTempo("/source.wav",0,data.size()));
    TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));
    TEST_ASSERT_EQUAL_UINT16(0,job.tempoResult().bpm10); TEST_ASSERT_EQUAL_UINT16(0,job.tempoResult().confidence);
    TEST_ASSERT_EQUAL_INT(0,disk.writes); TEST_ASSERT_TRUE(disk.files==before);
    TEST_ASSERT_TRUE(disk.reading.empty()); TEST_ASSERT_TRUE(disk.writing.empty());
  }
}
static void test_tempo_click_train_and_cancel_have_bounded_io_without_output() {
  const uint32_t rate=22050; std::vector<int16_t> data(rate*8);
  for(uint32_t at=rate/4;at+100<data.size();at+=rate/2)
    for(uint32_t j=0;j<100;++j)data[at+j]=static_cast<int16_t>(30000*(100-j)/100);
  EditDisk disk; disk.files["/source.wav"]=editWav(data); const auto before=disk.files; StoreJob job(disk);
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startTempo("/source.wav",0,data.size()));
  TEST_ASSERT_EQUAL_UINT32(data.size(),job.total());
  TEST_ASSERT_EQUAL(BankResult::Busy,job.startEdit("/source.wav",0,100,false));
  TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));
  const auto estimate=job.tempoResult();
  TEST_ASSERT_INT_WITHIN(15,1200,estimate.bpm10); TEST_ASSERT_TRUE(estimate.confidence>0);
  TEST_ASSERT_TRUE(disk.maxRead<=2048); TEST_ASSERT_EQUAL_INT(0,disk.writes); TEST_ASSERT_TRUE(disk.files==before);
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startTempo("/source.wav",0,data.size()));
  TEST_ASSERT_EQUAL(BankResult::Running,job.step()); job.cancel();
  TEST_ASSERT_TRUE(disk.reading.empty()); TEST_ASSERT_EQUAL_INT(0,disk.writes); TEST_ASSERT_TRUE(disk.files==before);
}
static void test_tempo_invalid_and_failed_read_never_touch_output_or_old_temp_files() {
  EditDisk disk; disk.files["/source.wav"]=editWav(operationTone(10000));
  disk.files["/mt-store/edit.tmp"]={1}; disk.files["/mt-store/edit.pk.tmp"]={2};
  const auto before=disk.files; StoreJob job(disk);
  TEST_ASSERT_EQUAL(BankResult::Unsupported,job.startTempo("/source.wav",3,3));
  TEST_ASSERT_EQUAL(BankResult::Unsupported,job.startTempo("/source.wav",0,10001));
  TEST_ASSERT_EQUAL(BankResult::Ok,job.startTempo("/source.wav",0,10000));
  disk.failRead=disk.reads+1;
  TEST_ASSERT_EQUAL(BankResult::ReadFail,runEdit(job));
  TEST_ASSERT_TRUE(disk.files==before); TEST_ASSERT_EQUAL_INT(0,disk.writes);
  TEST_ASSERT_TRUE(disk.reading.empty()); TEST_ASSERT_TRUE(disk.writing.empty());
}
static void test_stretch_short_silence_and_dedup_keep_existing_assets() {
  for(uint32_t n:{1u,7u,33u})for(uint32_t ratio:{32768u,131072u}) {
    EditDisk disk; disk.files["/source.wav"]=editWav(std::vector<int16_t>(n)); StoreJob job(disk);
    TEST_ASSERT_EQUAL(BankResult::Ok,job.startStretch("/source.wav",0,n,ratio));
    TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));
    const uint32_t frames=static_cast<uint32_t>((uint64_t(n)*ratio+32768)/65536);
    verifyEdit(disk,job.imported(),std::vector<int16_t>(frames));
    const auto before=disk.files;
    TEST_ASSERT_EQUAL(BankResult::Ok,job.startStretch("/source.wav",0,n,ratio));
    TEST_ASSERT_EQUAL(BankResult::Ok,runEdit(job));
    TEST_ASSERT_TRUE(disk.files==before); noEditTemps(disk);
  }
}
