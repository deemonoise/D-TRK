#include <unity.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>
#include <vector>
#include "time_stretch.h"

using namespace mt;
namespace { bool failWorkAllocation = false; }
// Only the processor's explicitly nothrow workspace allocation is faulted, never test vectors.
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  if (failWorkAllocation) return nullptr;
  try { return ::operator new[](n); } catch (...) { return nullptr; }
}

namespace {
struct Source : TimeStretchReader {
  const std::vector<int16_t>& data;
  int calls = 0, failCall = -1;
  explicit Source(const std::vector<int16_t>& d) : data(d) {}
  bool read(uint32_t first, uint32_t n, int16_t* out) override {
    ++calls;
    TEST_ASSERT_TRUE(n > 0 && n <= TimeStretch::kMaxStepFrames);
    TEST_ASSERT_TRUE(uint64_t(first) + n <= data.size());
    if (calls == failCall) return false;
    memcpy(out, data.data() + first, n * 2);
    return true;
  }
};
std::vector<int16_t> render(const std::vector<int16_t>& input, uint32_t outputFrames,
                          uint32_t rate = 44100, uint32_t capacity = 1024) {
  TimeStretch engine;
  Source source(input);
  TEST_ASSERT_EQUAL(TimeStretchResult::Running, engine.start(input.size(), rate, outputFrames));
  TEST_ASSERT_EQUAL_UINT32(0, engine.done());
  TEST_ASSERT_EQUAL_UINT32(outputFrames, engine.total());
  std::vector<int16_t> output;
  std::vector<int16_t> block(capacity + 2, 0x1357);
  for (uint32_t iteration = 0; iteration < outputFrames + 2; ++iteration) {
    const uint32_t before = engine.done();
    uint32_t produced = UINT32_MAX;
    const auto r = engine.step(source, block.data() + 1, capacity, produced);
    TEST_ASSERT_TRUE(produced > 0 && produced <= std::min(capacity, TimeStretch::kMaxStepFrames));
    TEST_ASSERT_TRUE(output.size() + produced <= outputFrames);
    TEST_ASSERT_EQUAL_INT16(0x1357, block.front());
    TEST_ASSERT_EQUAL_INT16(0x1357, block.back());
    TEST_ASSERT_EQUAL_UINT32(before + produced, engine.done());
    output.insert(output.end(), block.begin() + 1, block.begin() + 1 + produced);
    if (r == TimeStretchResult::Done) {
      TEST_ASSERT_EQUAL_UINT32(outputFrames, output.size());
      const int reads = source.calls;
      TEST_ASSERT_EQUAL(TimeStretchResult::Done, engine.step(source, block.data()+1, capacity, produced));
      TEST_ASSERT_EQUAL_UINT32(0, produced);
      TEST_ASSERT_EQUAL(reads, source.calls);
      return output;
    }
    TEST_ASSERT_EQUAL(TimeStretchResult::Running, r);
  }
  TEST_FAIL_MESSAGE("stretch failed to terminate"); return {};
}
std::vector<int16_t> tone(uint32_t frames, double frequency, uint32_t rate = 44100) {
  std::vector<int16_t> input(frames);
  for (uint32_t i=0;i<frames;++i) input[i]=int16_t(std::lround(20000*std::sin(6.283185307179586*frequency*i/rate)));
  return input;
}
double powerAt(const std::vector<int16_t>& signal, double frequency, uint32_t rate=44100) {
  const size_t trim=std::min<size_t>(1024,signal.size()/8), n=signal.size()-2*trim;
  const double coeff=2*std::cos(6.283185307179586*frequency/rate);
  double a=0,b=0;
  for(size_t i=0;i<n;++i) {
    const double window=.5-.5*std::cos(6.283185307179586*i/(n-1));
    const double next=signal[i+trim]*window+coeff*a-b;
    b=a;a=next;
  }
  return a*a+b*b-coeff*a*b;
}
double dominantNear(const std::vector<int16_t>& signal,double frequency,uint32_t rate=44100) {
  double best=0,found=0;
  for(int k=-12;k<=12;++k) {
    const double f=frequency+k*.25,p=powerAt(signal,f,rate);
    if(p>best){best=p;found=f;}
  }
  return found;
}
std::vector<int16_t> resampledControl(const std::vector<int16_t>& input,uint32_t length) {
  std::vector<int16_t> output(length);
  for(uint32_t i=0;i<length;++i) {
    const double p=double(i)*input.size()/length;
    const uint32_t first=static_cast<uint32_t>(p),next=std::min<uint32_t>(first+1,input.size()-1);
    output[i]=int16_t(std::lround(input[first]+(input[next]-input[first])*(p-first)));
  }
  return output;
}
}  // namespace

void setUp(){ failWorkAllocation=false; }
void tearDown(){ failWorkAllocation=false; }

void test_unity_is_bitexact_for_ragged_reads_and_needs_no_workspace() {
  auto input=tone(5003,441);
  input[0]=-32768;input[4999]=32767;
  failWorkAllocation=true;
  const auto output=render(input,input.size(),44100,317);
  TEST_ASSERT_TRUE(output==input);
}

void test_duration_changes_preserve_sine_pitch_unlike_resampling() {
  for(double frequency:{100.,440.,1000.}) {
    const auto input=tone(15435,frequency);
    for(uint32_t length:{uint32_t(input.size()/2+1),uint32_t(input.size()*3/2),uint32_t(input.size()*2)}) {
      const auto output=render(input,length);
      const double peak=dominantNear(output,frequency);
      TEST_ASSERT_TRUE(std::fabs(peak-frequency)<=1.0);
      const double wrong=frequency*input.size()/length;
      TEST_ASSERT_TRUE(powerAt(output,frequency)>8*powerAt(output,wrong));
      const auto control=resampledControl(input,length);
      TEST_ASSERT_TRUE(powerAt(control,wrong)>8*powerAt(control,frequency));
    }
  }
}

void test_two_tones_stay_at_original_frequencies() {
  auto input=tone(18000,220);
  const auto upper=tone(18000,880);
  for(size_t i=0;i<input.size();++i)input[i]=int16_t((int32_t(input[i])+upper[i])/2);
  const auto output=render(input,30000);
  TEST_ASSERT_TRUE(std::fabs(dominantNear(output,220)-220)<=1.5);
  TEST_ASSERT_TRUE(std::fabs(dominantNear(output,880)-880)<=1.5);
  TEST_ASSERT_TRUE(powerAt(output,220)>5*powerAt(output,132));
  TEST_ASSERT_TRUE(powerAt(output,880)>5*powerAt(output,528));
}

void test_constant_extrema_silence_and_dc_are_preserved_without_tail_artifacts() {
  for(int value:{-32768,-123,0,17,32767}) {
    const std::vector<int16_t> input(3001,int16_t(value));
    for(uint32_t length:{1501u,4500u,6002u}) {
      const auto output=render(input,length);
      for(int16_t sample:output)TEST_ASSERT_EQUAL_INT16(value,sample);
    }
  }
}

void test_short_inputs_and_fractional_last_hop_are_bounded() {
  for(uint32_t n:{1u,2u,3u,7u,31u,64u,65u,1023u,1025u}) {
    std::vector<int16_t> input(n);
    for(uint32_t i=0;i<n;++i)input[i]=(i&1)?32767:-32768;
    for(uint32_t length:{(n+1)/2,2*n}) {
      const auto a=render(input,length,44100,37);
      const auto b=render(input,length,44100,1024);
      TEST_ASSERT_TRUE(a==b);
    }
  }
}

void test_output_is_deterministic_across_chunk_sizes_and_reset() {
  std::vector<int16_t> input(7001);
  uint32_t state=0x12345678;
  for(auto& sample:input){state^=state<<13;state^=state>>17;state^=state<<5;sample=int16_t(state);}
  const auto a=render(input,10631,22050,1024);
  const auto b=render(input,10631,22050,17);
  const auto c=render(input,10631,22050,511);
  TEST_ASSERT_TRUE(a==b);TEST_ASSERT_TRUE(a==c);
  // Sparse attacks exercise low-energy references and search transitions.
  std::fill(input.begin(),input.end(),0);
  for(size_t i=500;i<input.size();i+=777)input[i]=32767;
  TEST_ASSERT_TRUE(render(input,3501,22050,113)==render(input,3501,22050,1024));
}

void test_reader_failure_is_sticky_without_exposing_partial_output() {
  const auto input=tone(7000,440);
  for(int failCall:{1,2,3,4}) {
    TimeStretch engine;Source source(input);source.failCall=failCall;
    TEST_ASSERT_EQUAL(TimeStretchResult::Running,engine.start(input.size(),44100,10000));
    int16_t out[1026];std::fill(std::begin(out),std::end(out),1234);
    bool failed=false;
    for(int i=0;i<10;++i){
      const auto before=std::vector<int16_t>(out,out+1026);uint32_t produced=0;
      const auto r=engine.step(source,out+1,1024,produced);
      if(r==TimeStretchResult::ReadFail){
        TEST_ASSERT_EQUAL_UINT32(0,produced);
        TEST_ASSERT_EQUAL_MEMORY(before.data(),out,sizeof out);
        failed=true;break;
      }
    }
    TEST_ASSERT_TRUE(failed);
    const int calls=source.calls;source.failCall=-1;uint32_t produced=99;
    TEST_ASSERT_EQUAL(TimeStretchResult::ReadFail,engine.step(source,out+1,1024,produced));
    TEST_ASSERT_EQUAL_UINT32(0,produced);TEST_ASSERT_EQUAL(calls,source.calls);
    engine.reset();
    TEST_ASSERT_EQUAL(TimeStretchResult::Running,engine.start(input.size(),44100,input.size()));
    TEST_ASSERT_EQUAL(TimeStretchResult::Running,engine.step(source,out+1,1024,produced));
    TEST_ASSERT_EQUAL_UINT32(1024,produced);
  }
}

void test_invalid_parameters_and_workspace_failure_do_no_reader_io() {
  TimeStretch engine;const std::vector<int16_t> input(50,1);Source reader(input);int16_t out[1024];uint32_t n=9;
  TEST_ASSERT_EQUAL(TimeStretchResult::BadArg,engine.step(reader,out,1024,n));
  TEST_ASSERT_EQUAL_UINT32(0,n);
  for(auto invalid:{std::vector<uint32_t>{0,44100,1},{1,0,1},{1,48000,1},{10,44100,0},
                   {10,44100,4},{10,44100,21}})
    TEST_ASSERT_EQUAL(TimeStretchResult::BadArg,engine.start(invalid[0],invalid[1],invalid[2]));
  failWorkAllocation=true;
  TEST_ASSERT_EQUAL(TimeStretchResult::NoMemory,engine.start(50,44100,100));
  TEST_ASSERT_EQUAL(TimeStretchResult::NoMemory,engine.step(reader,out,1024,n));
  TEST_ASSERT_EQUAL(0,reader.calls);
  failWorkAllocation=false;
  TEST_ASSERT_EQUAL(TimeStretchResult::Running,engine.start(50,44100,100));
  TEST_ASSERT_EQUAL(TimeStretchResult::BadArg,engine.step(reader,nullptr,1024,n));
  TEST_ASSERT_EQUAL_UINT32(0,n);
  TEST_ASSERT_EQUAL(TimeStretchResult::Running,engine.start(50,44100,100));
  TEST_ASSERT_EQUAL(TimeStretchResult::BadArg,engine.step(reader,out,0,n));
}

void test_state_and_workspace_are_bounded_for_extreme_lengths() {
  TEST_ASSERT_TRUE(sizeof(TimeStretch)<200);
  TEST_ASSERT_TRUE(TimeStretch::kWorkBytes<=24*1024);
  TimeStretch engine;
  TEST_ASSERT_EQUAL(TimeStretchResult::Running,engine.start(UINT32_MAX,44100,UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,engine.total());
  engine.reset();TEST_ASSERT_EQUAL_UINT32(0,engine.total());
  TEST_ASSERT_EQUAL(TimeStretchResult::Running,engine.start(2147483648u,44100,UINT32_MAX));
  engine.reset();
  TEST_ASSERT_EQUAL(TimeStretchResult::BadArg,engine.start(UINT32_MAX,44100,1));
}

void test_finite_tail_preserves_the_final_source_attack_for_compression_and_expansion() {
  for(uint32_t size:{2u,3u,7u,2050u,4096u,8193u}) {
    for(uint32_t length:{(size+1)/2,size*2}) {
      std::vector<int16_t> input(size,0);input.back()=-32768;
      const auto a=render(input,length,44100,37);
      const auto b=render(input,length,44100,1024);
      TEST_ASSERT_TRUE(a==b);
      TEST_ASSERT_EQUAL_INT16(input.back(),a.back());
    }
  }
}

int main(){
  UNITY_BEGIN();
  RUN_TEST(test_unity_is_bitexact_for_ragged_reads_and_needs_no_workspace);
  RUN_TEST(test_duration_changes_preserve_sine_pitch_unlike_resampling);
  RUN_TEST(test_two_tones_stay_at_original_frequencies);
  RUN_TEST(test_constant_extrema_silence_and_dc_are_preserved_without_tail_artifacts);
  RUN_TEST(test_short_inputs_and_fractional_last_hop_are_bounded);
  RUN_TEST(test_output_is_deterministic_across_chunk_sizes_and_reset);
  RUN_TEST(test_reader_failure_is_sticky_without_exposing_partial_output);
  RUN_TEST(test_invalid_parameters_and_workspace_failure_do_no_reader_io);
  RUN_TEST(test_state_and_workspace_are_bounded_for_extreme_lengths);
  RUN_TEST(test_finite_tail_preserves_the_final_source_attack_for_compression_and_expansion);
  return UNITY_END();
}
