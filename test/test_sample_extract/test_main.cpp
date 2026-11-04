#include <memory>
#include <string.h>
#include <unity.h>
#include "sample_extract.h"
#include "slices.h"

using namespace mt;
static std::unique_ptr<Project> p, before;
static SampleExtractTarget target;
static ImportOut output;
void setUp() {
  p.reset(new Project{});
  p->sampleCount = 1;
  strcpy(p->samples[0].name, "SOURCE");
  p->samples[0].crc = 123;
  p->samples[0].frames = 1000;
  Instrument& in = p->instruments[3];
  in.type = InstrType::Sample;
  strcpy(in.sample, "SOURCE");
  in.root = 47; in.vol = 81; in.attack = 19; in.reverse = true;
  in.start = 10000; in.end = 50000; in.loop = 1; in.loopStart = 23456;
  in.sliceMode = 1; in.sliceCount = 2; in.slices[0] = 12345; in.slices[1] = 34567;
  p->instruments[4] = in;
  target = SampleExtractTarget{};
  target.instrument = 3; target.index = 0; target.source = p->samples[0];
  target.from = 100; target.to = 700;
  output = ImportOut{}; output.crc = 456; output.frames = 600; output.rate = 44100; output.root = 60;
  before.reset(new Project(*p));
}
void tearDown() { p.reset(); before.reset(); }
static void unchanged() { TEST_ASSERT_EQUAL_MEMORY(before.get(), p.get(), sizeof(Project)); }
static void snapshot() { *before = *p; }

static void test_region_is_start_end_even_with_slices() {
  uint32_t from = 0, to = 0;
  const Instrument& in = p->instruments[3];
  TEST_ASSERT_TRUE(sampleExtractRegion(in, 1000, -1, from, to));
  TEST_ASSERT_EQUAL(fracToFrame(in.start, 1000), from);
  TEST_ASSERT_EQUAL(fracToFrame(in.end, 1000), to);
}
static void test_slice_clamped_to_start_end() {
  Instrument& in = p->instruments[3];
  in.start = 20000; in.end = 40000;
  in.slices[0] = 10000; in.slices[1] = 50000;
  uint32_t from = 0, to = 0;
  TEST_ASSERT_TRUE(sampleExtractRegion(in, 1000, 0, from, to));
  TEST_ASSERT_EQUAL(fracToFrame(in.start, 1000), from);
  TEST_ASSERT_EQUAL(fracToFrame(in.end, 1000), to);
  TEST_ASSERT_FALSE(sampleExtractRegion(in, 1000, 1, from, to));
  TEST_ASSERT_FALSE(sampleExtractRegion(in, 1000, 2, from, to));
  TEST_ASSERT_FALSE(sampleExtractRegion(in, 0, -1, from, to));
}
static void test_name_suffix_is_free_case_insensitive_and_bounded() {
  p->sampleCount = 3;
  strcpy(p->samples[1].name, "source_1"); strcpy(p->samples[2].name, "SOURCE_2");
  char name[kSampleNameMax + 1];
  TEST_ASSERT_TRUE(sampleExtractName(*p, "SOURCE", name));
  TEST_ASSERT_EQUAL_STRING("SOURCE_3", name);
  strcpy(p->samples[0].name, "1234567890123456");
  TEST_ASSERT_TRUE(sampleExtractName(*p, "1234567890123456", name));
  TEST_ASSERT_EQUAL_STRING("12345678901234_1", name);
}
static void test_commit_only_changes_new_entry_and_target_binding() {
  TEST_ASSERT_EQUAL(SampleExtractResult::Ok,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Ok, output));
  before->sampleCount = 2;
  strcpy(before->samples[1].name, "EXTRACT");
  before->samples[1].crc = output.crc; before->samples[1].frames = output.frames;
  Instrument& expected = before->instruments[3];
  strcpy(expected.sample, "EXTRACT"); expected.start = 0; expected.end = 0xFFFF;
  expected.loop = 0; expected.loopStart = 0; expected.sliceMode = 0; sliceClear(expected);
  unchanged();  // root, reverse, envelopes, filters and every other instrument remain exact.
}
static void test_cancel_and_job_failure_leave_project_exact() {
  TEST_ASSERT_EQUAL(SampleExtractResult::Cancelled,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Ok, output, true));
  unchanged();
  TEST_ASSERT_EQUAL(SampleExtractResult::JobFailed,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::WriteFail, output));
  unchanged();
}
static void test_partial_success_is_not_committed() {
  --output.frames;
  TEST_ASSERT_EQUAL(SampleExtractResult::JobFailed,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Ok, output));
  unchanged();
}
static void test_collision_and_full_are_transactional() {
  TEST_ASSERT_EQUAL(SampleExtractResult::NameTaken,
                    sampleExtractCommit(*p, target, "source", BankResult::Ok, output));
  unchanged();
  p->sampleCount = kProjSamples; snapshot();
  TEST_ASSERT_EQUAL(SampleExtractResult::Full,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Ok, output));
  unchanged();
}
static void test_stale_source_or_binding_is_not_committed() {
  ++p->samples[0].crc; snapshot();
  TEST_ASSERT_EQUAL(SampleExtractResult::BadSource,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Ok, output));
  unchanged();
  p->samples[0] = target.source; strcpy(p->instruments[3].sample, "ELSE"); snapshot();
  TEST_ASSERT_EQUAL(SampleExtractResult::BadSource,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Ok, output));
  unchanged();
}
static void test_invalid_name_or_range_never_mutates() {
  TEST_ASSERT_EQUAL(SampleExtractResult::BadName,
                    sampleExtractCommit(*p, target, "../bad", BankResult::Ok, output));
  unchanged();
  target.to = target.from;
  TEST_ASSERT_EQUAL(SampleExtractResult::BadSource,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Ok, output));
  unchanged();
}
static void test_cancelled_result_and_bad_rate_never_mutate() {
  TEST_ASSERT_EQUAL(SampleExtractResult::Cancelled,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Cancelled, output));
  unchanged();
  output.rate = 0;
  TEST_ASSERT_EQUAL(SampleExtractResult::JobFailed,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Ok, output));
  unchanged();
  output.rate = 48000;
  TEST_ASSERT_EQUAL(SampleExtractResult::JobFailed,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Ok, output));
  unchanged();
}
static void test_stale_length_type_and_invalid_indices_are_refused() {
  ++p->samples[0].frames;
  TEST_ASSERT_EQUAL(SampleExtractResult::BadSource, sampleExtractCheck(*p, target, "EXTRACT"));
  p->samples[0] = target.source; p->instruments[3].type = InstrType::Chip;
  TEST_ASSERT_EQUAL(SampleExtractResult::BadSource, sampleExtractCheck(*p, target, "EXTRACT"));
  p->instruments[3].type = InstrType::Sample;
  target.instrument = kInstruments;
  TEST_ASSERT_EQUAL(SampleExtractResult::BadSource, sampleExtractCheck(*p, target, "EXTRACT"));
  target.instrument = 3; target.index = p->sampleCount;
  TEST_ASSERT_EQUAL(SampleExtractResult::BadSource, sampleExtractCheck(*p, target, "EXTRACT"));
}
static void test_single_frame_region_and_zero_crc_are_valid() {
  uint32_t from, to;
  TEST_ASSERT_TRUE(sampleExtractRegion(p->instruments[3], 1, -1, from, to));
  TEST_ASSERT_EQUAL(0, from); TEST_ASSERT_EQUAL(1, to);
  output.crc = 0;
  TEST_ASSERT_EQUAL(SampleExtractResult::Ok,
                    sampleExtractCommit(*p, target, "EXTRACT", BankResult::Ok, output));
  TEST_ASSERT_EQUAL(0, p->samples[1].crc);
}
static void test_stretch_commit_checks_expected_duration() {
  target.ratioQ16=98304; output.frames=900;
  TEST_ASSERT_EQUAL(SampleExtractResult::Ok,sampleExtractCommit(*p,target,"STRETCH",BankResult::Ok,output));
  TEST_ASSERT_EQUAL(900,p->samples[1].frames);
}
static void test_stretch_wrong_duration_and_invalid_ratio_leave_project_exact() {
  target.ratioQ16=32768;
  TEST_ASSERT_EQUAL(SampleExtractResult::JobFailed,sampleExtractCommit(*p,target,"STRETCH",BankResult::Ok,output)); unchanged();
  target.ratioQ16=131073; output.frames=1200;
  TEST_ASSERT_EQUAL(SampleExtractResult::BadSource,sampleExtractCheck(*p,target,"STRETCH")); unchanged();
}
static void test_tempo_ratio_bounds_and_rounding() {
  uint32_t q=77;
  TEST_ASSERT_TRUE(sampleStretchRatio(1200,800,q)); TEST_ASSERT_EQUAL(98304,q);
  TEST_ASSERT_TRUE(sampleStretchRatio(600,1200,q)); TEST_ASSERT_EQUAL(32768,q);
  TEST_ASSERT_TRUE(sampleStretchRatio(2400,1200,q)); TEST_ASSERT_EQUAL(131072,q);
  TEST_ASSERT_FALSE(sampleStretchRatio(599,1200,q)); TEST_ASSERT_EQUAL(131072,q);
  TEST_ASSERT_FALSE(sampleStretchRatio(2401,1200,q)); TEST_ASSERT_FALSE(sampleStretchRatio(0,1200,q));
  TEST_ASSERT_EQUAL(2,sampleEditFrames(3,32768)); TEST_ASSERT_EQUAL(1,sampleEditFrames(1,32768));
  TEST_ASSERT_EQUAL(0,sampleEditFrames(UINT32_MAX,131072));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tempo_ratio_bounds_and_rounding);
  RUN_TEST(test_stretch_commit_checks_expected_duration);
  RUN_TEST(test_stretch_wrong_duration_and_invalid_ratio_leave_project_exact);
  RUN_TEST(test_region_is_start_end_even_with_slices);
  RUN_TEST(test_slice_clamped_to_start_end);
  RUN_TEST(test_name_suffix_is_free_case_insensitive_and_bounded);
  RUN_TEST(test_commit_only_changes_new_entry_and_target_binding);
  RUN_TEST(test_cancel_and_job_failure_leave_project_exact);
  RUN_TEST(test_partial_success_is_not_committed);
  RUN_TEST(test_collision_and_full_are_transactional);
  RUN_TEST(test_stale_source_or_binding_is_not_committed);
  RUN_TEST(test_invalid_name_or_range_never_mutates);
  RUN_TEST(test_cancelled_result_and_bad_rate_never_mutate);
  RUN_TEST(test_stale_length_type_and_invalid_indices_are_refused);
  RUN_TEST(test_single_frame_region_and_zero_crc_are_valid);
  return UNITY_END();
}
