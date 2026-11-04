#include <unity.h>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "sector_io.h"

using namespace mt;
namespace {
struct Device final : SectorIoDevice {
  struct Call { uint32_t sector, count; };
  int failures = 0, recoveries = 0;
  int finishes = 0, busyReadWaits = 0, failWriteAt = -1;
  bool recoverable = true, aligned = true;
  bool programming = false, finishReady = true;
  std::vector<Call> reads, writes;
  std::string order;
  std::map<uint32_t, std::vector<uint8_t>> sectors;
  bool read(uint32_t sector, uint32_t count, uint8_t* out) override {
    order += 'r'; reads.push_back({sector,count});
    if (programming) { ++busyReadWaits; programming = false; }
    aligned &= (reinterpret_cast<uintptr_t>(out) & 31) == 0;
    if (failures) { --failures; memset(out, 0xA5, count*512); return false; }
    for (uint32_t i = 0; i < count; ++i) {
      if (sectors.count(sector+i)) memcpy(out+512*i,sectors[sector+i].data(),512);
      else memset(out+512*i,static_cast<uint8_t>(sector+i),512);
    }
    return true;
  }
  bool write(uint32_t sector, uint32_t count, const uint8_t* in) override {
    order += 'w'; writes.push_back({sector,count});
    aligned &= (reinterpret_cast<uintptr_t>(in) & 31) == 0;
    if (failures || static_cast<int>(writes.size()) == failWriteAt) {
      if (failures) --failures;
      return false;
    }
    for (uint32_t i = 0; i < count; ++i) sectors[sector+i] = {in+512*i,in+512*(i+1)};
    programming = true;  // DMA TC can precede card programming completion.
    return true;
  }
  bool finishWrite() override {
    order += 'd'; ++finishes;
    if (!finishReady) return false;
    programming = false;
    return true;
  }
  bool recover() override { order += 's'; ++recoveries; return recoverable; }
  void prepareRead(uint8_t*, uint32_t bytes) override {
    TEST_ASSERT_EQUAL(0,bytes&31); order += 'b';
  }
  void finishRead(uint8_t*, uint32_t bytes) override {
    TEST_ASSERT_EQUAL(0,bytes&31); order += 'a';
  }
  void prepareWrite(uint8_t*, uint32_t bytes) override {
    TEST_ASSERT_EQUAL(0,bytes&31); order += 'f';
  }
};
alignas(32) uint8_t bounce[4096];
}  // namespace
void setUp() {}
void tearDown() {}

void test_reads_split_and_support_unaligned_destinations() {
  Device device; SectorIo io(device,bounce,8);
  std::vector<uint8_t> data(20*512+2,0x55);
  TEST_ASSERT_TRUE(io.read(11,20,data.data()+1));
  TEST_ASSERT_EQUAL(3,device.reads.size());
  TEST_ASSERT_EQUAL(11,device.reads[0].sector); TEST_ASSERT_EQUAL(8,device.reads[0].count);
  TEST_ASSERT_EQUAL(19,device.reads[1].sector); TEST_ASSERT_EQUAL(8,device.reads[1].count);
  TEST_ASSERT_EQUAL(27,device.reads[2].sector); TEST_ASSERT_EQUAL(4,device.reads[2].count);
  for (int s=0;s<20;++s) for(int i=0;i<512;++i) TEST_ASSERT_EQUAL(11+s,data[1+512*s+i]);
  TEST_ASSERT_EQUAL(0x55,data.front()); TEST_ASSERT_EQUAL(0x55,data.back());
  TEST_ASSERT_TRUE(device.aligned); TEST_ASSERT_EQUAL_STRING("brabrabra",device.order.c_str());
}
void test_writes_split_flush_the_bounce_and_preserve_unaligned_source() {
  Device device; SectorIo io(device,bounce,8);
  std::vector<uint8_t> data(19*512+2,0x55);
  for (size_t i=1;i<data.size()-1;++i) data[i]=static_cast<uint8_t>(i*37);
  const auto before=data;
  TEST_ASSERT_TRUE(io.write(30,19,data.data()+1));
  TEST_ASSERT_EQUAL(3,device.writes.size());
  TEST_ASSERT_EQUAL(30,device.writes[0].sector); TEST_ASSERT_EQUAL(8,device.writes[0].count);
  TEST_ASSERT_EQUAL(46,device.writes[2].sector); TEST_ASSERT_EQUAL(3,device.writes[2].count);
  for (int s=0;s<19;++s) TEST_ASSERT_EQUAL_MEMORY(data.data()+1+512*s,device.sectors[30+s].data(),512);
  TEST_ASSERT_TRUE(data==before); TEST_ASSERT_TRUE(device.aligned);
  TEST_ASSERT_EQUAL_STRING("fwfwfwd",device.order.c_str());
}
void test_failed_read_does_not_replace_cache_and_retries_are_bounded() {
  Device device; device.failures=10; SectorIo io(device,bounce,8);
  uint8_t cache[4096]; memset(cache,0x37,sizeof cache);
  TEST_ASSERT_FALSE(io.read(1,8,cache));
  for (uint8_t value:cache) TEST_ASSERT_EQUAL(0x37,value);
  TEST_ASSERT_EQUAL(3,device.reads.size()); TEST_ASSERT_EQUAL(2,device.recoveries);
  TEST_ASSERT_EQUAL_STRING("brasbrasbra",device.order.c_str());
}
void test_read_retries_only_after_recovery_then_commits_success() {
  Device device; device.failures=2; SectorIo io(device,bounce,8);
  uint8_t out[512]; memset(out,0x37,sizeof out);
  TEST_ASSERT_TRUE(io.read(44,1,out));
  for (uint8_t value:out) TEST_ASSERT_EQUAL(44,value);
  TEST_ASSERT_EQUAL(3,device.reads.size()); TEST_ASSERT_EQUAL(2,device.recoveries);
  TEST_ASSERT_EQUAL_STRING("brasbrasbra",device.order.c_str());
}
void test_unrecoverable_failure_does_not_start_another_dma_transfer() {
  Device device; device.failures=10; device.recoverable=false; SectorIo io(device,bounce,8);
  uint8_t out[512]; memset(out,0x37,sizeof out);
  TEST_ASSERT_FALSE(io.read(0,1,out));
  TEST_ASSERT_EQUAL(1,device.reads.size()); TEST_ASSERT_EQUAL(1,device.recoveries);
  TEST_ASSERT_EQUAL_STRING("bras",device.order.c_str());
  for (uint8_t value:out) TEST_ASSERT_EQUAL(0x37,value);
}
void test_write_retry_reuses_identical_data_and_reports_persistent_failure() {
  Device device; device.failures=1; SectorIo io(device,bounce,8);
  uint8_t data[512]; for(size_t i=0;i<sizeof data;++i)data[i]=static_cast<uint8_t>(i*17);
  TEST_ASSERT_TRUE(io.write(1,1,data));
  TEST_ASSERT_EQUAL(2,device.writes.size()); TEST_ASSERT_EQUAL(1,device.recoveries);
  TEST_ASSERT_EQUAL_MEMORY(data,device.sectors[1].data(),512);
  TEST_ASSERT_EQUAL_STRING("fwsfwd",device.order.c_str());
  device.failures=10;
  TEST_ASSERT_FALSE(io.write(2,1,data));
  TEST_ASSERT_FALSE(device.sectors.count(2));
  TEST_ASSERT_EQUAL(5,device.writes.size());
}
void test_sector_range_and_byte_count_overflow_fail_before_device_access() {
  Device device; SectorIo io(device,bounce,8); uint8_t out[512];
  TEST_ASSERT_FALSE(io.read(UINT32_MAX,2,out));
  TEST_ASSERT_FALSE(io.write(UINT32_MAX,2,out));
  TEST_ASSERT_FALSE(io.read(0,UINT32_MAX/512+1,out));
  TEST_ASSERT_FALSE(io.write(0,UINT32_MAX/512+1,out));
  TEST_ASSERT_TRUE(device.reads.empty()); TEST_ASSERT_TRUE(device.writes.empty());
  TEST_ASSERT_TRUE(io.read(UINT32_MAX,1,out));
  for(uint8_t value:out)TEST_ASSERT_EQUAL(255,value);
}
void test_empty_io_is_noop_and_invalid_buffer_is_rejected() {
  Device device; SectorIo good(device,bounce,8), absent(device,nullptr,8), empty(device,bounce,0);
  SectorIo unaligned(device,bounce+1,1); uint8_t out[512];
  TEST_ASSERT_TRUE(good.read(0,0,nullptr)); TEST_ASSERT_TRUE(good.write(0,0,nullptr));
  TEST_ASSERT_FALSE(good.read(0,1,nullptr)); TEST_ASSERT_FALSE(good.write(0,1,nullptr));
  TEST_ASSERT_FALSE(absent.read(0,1,out)); TEST_ASSERT_FALSE(empty.read(0,1,out));
  TEST_ASSERT_FALSE(unaligned.read(0,1,out));
  TEST_ASSERT_TRUE(device.reads.empty()); TEST_ASSERT_TRUE(device.writes.empty());
  TEST_ASSERT_EQUAL(0,device.finishes);
}

void test_write_finishes_card_programming_before_returning_success() {
  Device device; SectorIo io(device,bounce,8); uint8_t data[512] = {}, out[512];
  TEST_ASSERT_TRUE(io.write(3,1,data));
  TEST_ASSERT_FALSE(device.programming);
  TEST_ASSERT_EQUAL(1,device.finishes);
  TEST_ASSERT_TRUE(io.read(3,1,out));
  TEST_ASSERT_EQUAL(0,device.busyReadWaits);  // the next sample read inherits no hidden write wait
  TEST_ASSERT_EQUAL_STRING("fwdbra",device.order.c_str());
}
void test_finish_failure_is_not_success_and_does_not_retry_the_write() {
  Device device; device.finishReady=false; SectorIo io(device,bounce,8); uint8_t data[512] = {};
  TEST_ASSERT_FALSE(io.write(3,1,data));
  TEST_ASSERT_EQUAL(1,device.writes.size());
  TEST_ASSERT_EQUAL(1,device.finishes);
  TEST_ASSERT_EQUAL(0,device.recoveries);
  TEST_ASSERT_EQUAL_STRING("fwd",device.order.c_str());
}
void test_failed_dma_write_never_polls_card_ready() {
  Device device; device.failures=1; device.recoverable=false;
  SectorIo io(device,bounce,8); uint8_t data[512] = {};
  TEST_ASSERT_FALSE(io.write(3,1,data));
  TEST_ASSERT_EQUAL(1,device.writes.size());
  TEST_ASSERT_EQUAL(0,device.finishes);
  TEST_ASSERT_EQUAL_STRING("fws",device.order.c_str());
}
void test_split_write_finishes_once_only_after_the_final_chunk() {
  Device device; SectorIo io(device,bounce,8); std::vector<uint8_t> data(19*512);
  TEST_ASSERT_TRUE(io.write(3,19,data.data()));
  TEST_ASSERT_EQUAL(3,device.writes.size());
  TEST_ASSERT_EQUAL(1,device.finishes);
  TEST_ASSERT_EQUAL_STRING("fwfwfwd",device.order.c_str());
  Device failed; failed.failWriteAt=2; failed.recoverable=false;
  SectorIo failIo(failed,bounce,8);
  TEST_ASSERT_FALSE(failIo.write(3,19,data.data()));
  TEST_ASSERT_EQUAL(2,failed.writes.size());
  TEST_ASSERT_EQUAL(0,failed.finishes);
  TEST_ASSERT_EQUAL_STRING("fwfws",failed.order.c_str());
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reads_split_and_support_unaligned_destinations);
  RUN_TEST(test_writes_split_flush_the_bounce_and_preserve_unaligned_source);
  RUN_TEST(test_failed_read_does_not_replace_cache_and_retries_are_bounded);
  RUN_TEST(test_read_retries_only_after_recovery_then_commits_success);
  RUN_TEST(test_unrecoverable_failure_does_not_start_another_dma_transfer);
  RUN_TEST(test_write_retry_reuses_identical_data_and_reports_persistent_failure);
  RUN_TEST(test_sector_range_and_byte_count_overflow_fail_before_device_access);
  RUN_TEST(test_empty_io_is_noop_and_invalid_buffer_is_rejected);
  RUN_TEST(test_write_finishes_card_programming_before_returning_success);
  RUN_TEST(test_finish_failure_is_not_success_and_does_not_retry_the_write);
  RUN_TEST(test_failed_dma_write_never_polls_card_ready);
  RUN_TEST(test_split_write_finishes_once_only_after_the_final_chunk);
  return UNITY_END();
}
