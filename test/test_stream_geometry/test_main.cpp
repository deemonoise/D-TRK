#include <cstring>
#include <vector>
#include <unity.h>
#include "sample_loop.h"
#include "sector_map.h"
#include "stream_loader.h"
#include "synth_resample.h"

#ifndef MT_STREAM_SLOT_BLOCKS
#define MT_STREAM_SLOT_BLOCKS 6
#endif
#ifndef MT_STREAM_LOAD_BLOCKS
#define MT_STREAM_LOAD_BLOCKS 4
#endif

using namespace mt;
namespace {
constexpr int kSlotFrames = kSlotBlocks*kStreamBlock;
constexpr int16_t kUntouched = 0x6B5A;
int16_t memory[kStreamSlots*kSlotFrames+2];
StreamPool pool;

struct Reader final : BlockReader {
  struct Call { uint32_t block; int n; };
  std::vector<Call> calls;
  bool fail = false, rekey = false;
  int slot = 0;
  bool read(uint32_t key, uint32_t block, int n, int16_t* out) override {
    TEST_ASSERT_TRUE(n > 0 && n <= kLoadMax);
#if MT_STREAM_LOAD_BLOCKS > 4
    TEST_ASSERT_EQUAL_UINT32(0,reinterpret_cast<uintptr_t>(out)&31);
#endif
    calls.push_back({block,n});
    for (int i=0;i<n*kStreamBlock;++i)
      out[i] = static_cast<int16_t>((block*kStreamBlock+i)*37+key);
    if (rekey) {
      pool.release(slot);
      SampleRegion g; g.to=g.loopHi=176400;
      TEST_ASSERT_EQUAL(slot,pool.acquire(key+1,176400,44100,nullptr,0,g,SlotOwner::Voice));
    }
    return !fail;
  }
} reader;

int acquire(const SampleRegion& region, uint32_t frames) {
  return pool.acquire(1,frames,44100,nullptr,0,region,SlotOwner::Voice);
}
void fill(int slot) {
  for (int i=0;i<2*kSlotBlocks+2&&streamLoad(pool,slot,reader);++i) {}
}
void checkOuterAndOtherSlots() {
  TEST_ASSERT_EQUAL_INT16(kUntouched,memory[0]);
  for (int i=1+kSlotFrames;i<kStreamSlots*kSlotFrames+2;++i)
    TEST_ASSERT_EQUAL_INT16(kUntouched,memory[i]);
}
}  // namespace

void setUp() {
  for (auto& value:memory) value=kUntouched;
  pool.init(memory+1);
  reader.calls.clear(); reader.fail=reader.rekey=false; reader.slot=0;
}
void tearDown() {}

void test_requested_geometry_is_used_without_changing_voice_count() {
  TEST_ASSERT_EQUAL(MT_STREAM_SLOT_BLOCKS,kSlotBlocks);
  TEST_ASSERT_EQUAL(MT_STREAM_LOAD_BLOCKS,kLoadMax);
  TEST_ASSERT_EQUAL(kStreamSlots,pool.slots());
  for (int i=0;i<kStreamSlots;++i) {
    SampleRegion g; g.to=g.loopHi=176400;
    TEST_ASSERT_EQUAL(i,pool.acquire(i+1,176400,44100,nullptr,0,g,SlotOwner::Voice));
    TEST_ASSERT_EQUAL_PTR(memory+1+i*kSlotFrames,pool.slot(i).buf);
  }
  SampleRegion g;
  TEST_ASSERT_EQUAL(-1,pool.acquire(999,176400,44100,nullptr,0,g,SlotOwner::Voice));
}

void test_guarded_initial_fill_uses_all_ways_in_bounded_batches() {
  SampleRegion g; g.to=g.loopHi=176400; g.pos=int64_t(4096+256)<<32;
  const int slot=acquire(g,176400);
  pool.publish(slot,g.pos,int64_t(4)<<32,17,18);
  StreamLoad first;
  TEST_ASSERT_TRUE(streamPlan(pool,slot,first));
  TEST_ASSERT_EQUAL(kLoadMax,first.n);
  fill(slot);
  int loaded=0;
  for (const auto& call:reader.calls) {
    TEST_ASSERT_EQUAL_UINT32(8+loaded,call.block);
    TEST_ASSERT_TRUE(call.n>0&&call.n<=kLoadMax);
    loaded+=call.n;
  }
  TEST_ASSERT_EQUAL(kSlotBlocks,loaded);
  uint32_t reserve;
  TEST_ASSERT_FALSE(streamNeeds(pool,slot,reserve));
  TEST_ASSERT_TRUE(reserve>0);
  for (int block=8;block<8+kSlotBlocks;++block) {
    const int16_t* data=pool.block(slot,block);
    TEST_ASSERT_NOT_NULL(data);
    for (int i=0;i<kStreamBlock;++i)
      TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>((block*kStreamBlock+i)*37+1),data[i]);
  }
  checkOuterAndOtherSlots();
}

void test_forward_reverse_loop_and_pingpong_guards_remain_available() {
  for (LoopMode mode:{LoopMode::Off,LoopMode::Forward,LoopMode::PingPong})
    for (int direction:{-1,1}) {
      pool.init(memory+1);
      SampleRegion g; g.from=7; g.to=3113;
      g.loopLo=mode==LoopMode::Off?g.from:511;
      g.loopHi=mode==LoopMode::Off?g.to:2567;
      g.loopMode=static_cast<uint8_t>(mode); g.dir=direction;
      int64_t pos=int64_t(direction>0?g.from:g.to-1)<<32;
      int64_t step=int64_t(direction)*4*(int64_t{1}<<32);
      g.pos=pos;
      const int slot=acquire(g,g.to);
      bool looped=false;
      for (int output=0;output<2000;++output) {
        pool.publish(slot,pos,step,17,18,looped);
        fill(slot);
        const uint32_t center=static_cast<uint32_t>(pos>>32);
        for (int tap=-17;tap<=18;++tap) {
          const uint32_t frame=sampleFirFrame(int64_t(center)+tap,g.from,g.to,g.loopLo,g.loopHi,
                                              g.loopMode,looped,step);
          TEST_ASSERT_NOT_NULL(pool.block(slot,frame/kStreamBlock));
        }
        pos+=step;
        if (sampleLoopPast(pos,step,int64_t(g.loopLo)<<32,int64_t(g.loopHi)<<32,g.loopMode)&&
            !sampleLoopEdge(pos,step,int64_t(g.loopLo)<<32,int64_t(g.loopHi)<<32,g.loopMode,looped)) break;
      }
    }
}

void test_failed_and_rekeyed_reads_never_commit_partial_cache_data() {
  pool.init(memory+1,1);
  SampleRegion g; g.to=g.loopHi=176400; g.pos=int64_t(4096+256)<<32;
  const int slot=acquire(g,176400);
  pool.publish(slot,g.pos,int64_t(4)<<32,17,18);
  reader.fail=true;
  TEST_ASSERT_FALSE(streamLoad(pool,slot,reader));
  for (uint32_t tag:pool.slot(slot).tag) TEST_ASSERT_EQUAL_UINT32(0,tag);
  for (int i=0;i<kSlotFrames;++i) TEST_ASSERT_EQUAL_INT16(kUntouched,pool.slot(slot).buf[i]);
  reader.fail=false; reader.rekey=true; reader.slot=slot;
  TEST_ASSERT_TRUE(streamLoad(pool,slot,reader));  // I/O finished; the obsolete commit is discarded
  TEST_ASSERT_EQUAL_UINT32(2,pool.slot(slot).key);
  for (uint32_t tag:pool.slot(slot).tag) TEST_ASSERT_EQUAL_UINT32(0,tag);
  checkOuterAndOtherSlots();
}

void test_partial_last_block_pads_full_sectors_without_touching_the_next_file() {
  struct Card final : SectorReader {
    uint32_t last=0, sectors=0;
    bool read(uint32_t at,uint32_t count,uint8_t* out) override {
      last=at+count-1; sectors+=count; memset(out,0x31,count*512); return true;
    }
  } card;
  struct Mapped final : BlockReader {
    SectorMap file;
    Card* card=nullptr;
    bool read(uint32_t,uint32_t block,int n,int16_t* out) override {
      return file.read(1+2*block,2*n,reinterpret_cast<uint8_t*>(out),*card);
    }
  } mapped;
  constexpr uint32_t frames=(kLoadMax-1)*kStreamBlock+37;
  mapped.file.first=1000;
  mapped.file.sectors=1+(frames*2+511)/512;
  mapped.card=&card;
  SampleRegion g; g.to=g.loopHi=frames;
  const int slot=acquire(g,frames);
  pool.publish(slot,0,int64_t(4)<<32,17,18);
  TEST_ASSERT_TRUE(streamLoad(pool,slot,mapped));
  TEST_ASSERT_EQUAL_UINT32(mapped.file.first+mapped.file.sectors-1,card.last);
  TEST_ASSERT_EQUAL_UINT32(2*kLoadMax-1,card.sectors);
  const int16_t* last=pool.block(slot,kLoadMax-1);
  TEST_ASSERT_NOT_NULL(last);
  // The last file sector includes 37 real frames; only the full following sector is zeroed.
  // Playback's region projection never asks for the unused bytes of that partial sector.
  for (int i=256;i<kStreamBlock;++i) TEST_ASSERT_EQUAL_INT16(0,last[i]);
  pool.publish(slot,int64_t(frames-1)<<32,int64_t(4)<<32,17,18);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,streamReserveUs(pool,slot));
  checkOuterAndOtherSlots();
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_requested_geometry_is_used_without_changing_voice_count);
  RUN_TEST(test_guarded_initial_fill_uses_all_ways_in_bounded_batches);
  RUN_TEST(test_forward_reverse_loop_and_pingpong_guards_remain_available);
  RUN_TEST(test_failed_and_rekeyed_reads_never_commit_partial_cache_data);
  RUN_TEST(test_partial_last_block_pads_full_sectors_without_touching_the_next_file);
  return UNITY_END();
}
