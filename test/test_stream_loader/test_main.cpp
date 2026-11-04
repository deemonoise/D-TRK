#include <string.h>
#include <unity.h>
#include <vector>
#include "stream_loader.h"
#include "synth_resample.h"

using namespace mt;

static int16_t mem[kStreamSlots * kSlotBlocks * kStreamBlock];
static StreamPool pool;

struct VecReader : BlockReader {
  std::vector<int16_t> d;
  int reads = 0;
  bool read(uint32_t, uint32_t b, int n, int16_t* out) override {
    ++reads;
    for (int i = 0; i < n * kStreamBlock; ++i) {
      const uint32_t f = b * kStreamBlock + i;
      out[i] = f < d.size() ? d[f] : 0;
    }
    return true;
  }
};
static VecReader rd;

static PlaySnap snap(uint32_t pos, int32_t step16, uint32_t lo, uint32_t hi, LoopMode mode) {
  PlaySnap p;
  p.pos = pos;
  p.step16 = step16;
  p.from = p.loopLo = lo;
  p.to = p.loopHi = hi;
  p.loopMode = static_cast<uint8_t>(mode);
  return p;
}
static void expectWant(const PlaySnap& p, std::vector<uint32_t> want) {
  uint32_t got[kWantMax];
  const int n = streamWant(p, got);
  TEST_ASSERT_EQUAL(static_cast<int>(want.size()), n);
  for (int i = 0; i < n; ++i) TEST_ASSERT_EQUAL_UINT32(want[i], got[i]);
}
static int voice(uint32_t frames, uint32_t start) {
  SampleRegion g;
  g.from = g.loopLo = 0;
  g.to = g.loopHi = frames;
  g.pos = static_cast<int64_t>(start) << 32;
  return pool.acquire(1, frames, 44100, nullptr, 0, g, SlotOwner::Voice);
}

void setUp() {
  pool.init(mem);
  rd.d.assign(100000, 0);
  for (uint32_t i = 0; i < rd.d.size(); ++i) rd.d[i] = static_cast<int16_t>(i);
  rd.reads = 0;
}
void tearDown() {}

void test_want_forward() { expectWant(snap(1000, 65536, 0, 100000, LoopMode::Off), {1, 2, 3, 4, 5}); }
void test_want_stops_at_end_without_loop() { expectWant(snap(1000, 65536, 0, 1500, LoopMode::Off), {1, 2}); }
void test_want_reverse() { expectWant(snap(3000, -65536, 0, 100000, LoopMode::Off), {5, 4, 3, 2, 1}); }
void test_want_forward_loop_wraps() { expectWant(snap(1900, 65536, 0, 2048, LoopMode::Forward), {3, 0, 1, 2}); }
void test_want_pingpong_turns() { expectWant(snap(3000, 65536, 0, 3072, LoopMode::PingPong), {5, 4, 3, 2, 1}); }

// This probe uses the guarded publication when available, and reproduces the old planner on
// the pre-FIR API. The runtime RED must be a lost history block, not a compile failure.
template <class Pool>
static auto publishWindow(Pool& target, int slot, int64_t pos, int64_t step,
                          uint16_t before, uint16_t after, bool looped, int)
    -> decltype(target.publish(slot, pos, step, before, after, looped), void()) {
  target.publish(slot, pos, step, before, after, looped);
}
template <class Pool>
static void publishWindow(Pool& target, int slot, int64_t pos, int64_t step,
                          uint16_t, uint16_t, bool, long) {
  target.publish(slot, pos, step);
}
static void window(int slot, uint32_t pos, int dir, uint16_t before = 12,
                   uint16_t after = 12, bool looped = false) {
  publishWindow(pool, slot, int64_t(pos) << 32, int64_t(dir) * (int64_t{1} << 32),
                before, after, looped, 0);
}
void test_fir_window_prioritizes_previous_forward_block() {
  const int s = voice(100000, 512);
  window(s, 512, 1);
  PlaySnap p;
  TEST_ASSERT_TRUE(pool.snap(s, p));
  expectWant(p, {1, 0, 2, 3, 4, 5});
}
void test_fir_window_prioritizes_previous_reverse_block() {
  const int s = voice(100000, 1023);
  window(s, 1023, -1);
  PlaySnap p;
  TEST_ASSERT_TRUE(pool.snap(s, p));
  expectWant(p, {1, 2, 0});
}
void test_plan_does_not_evict_fir_history() {
  const int s = voice(100000, 512);
  StreamLoad seed;
  seed.slot=s;seed.key=pool.slot(s).key;seed.gen=pool.slot(s).gen;seed.n=1;
  static int16_t block[kStreamBlock] = {};
  const uint32_t blocks[kSlotBlocks] = {0, 1, 20, 21, 22, 23};
  for(int way=0;way<kSlotBlocks;++way){seed.block=blocks[way];seed.way[0]=way;TEST_ASSERT_TRUE(pool.commit(seed, block));}
  window(s, 512, 1);
  TEST_ASSERT_TRUE(streamLoad(pool, s, rd));
  TEST_ASSERT_NOT_NULL(pool.block(s, 0));
}

static int loopVoice(uint32_t pos, LoopMode mode) {
  SampleRegion g;
  g.from=0;g.to=2048;g.loopLo=512;g.loopHi=1536;
  g.loopMode=static_cast<uint8_t>(mode);g.pos=int64_t(pos)<<32;
  return pool.acquire(1,2048,44100,nullptr,0,g,SlotOwner::Voice);
}
static void expectSlotWant(int slot, std::vector<uint32_t> wanted) {
  PlaySnap p;TEST_ASSERT_TRUE(pool.snap(slot,p));expectWant(p,wanted);
}
static void loadOne(int slot,uint32_t block) {
  StreamLoad l;l.slot=slot;l.key=pool.slot(slot).key;l.gen=pool.slot(slot).gen;
  l.n=1;l.block=block;l.way[0]=static_cast<int>(block%kSlotBlocks);
  static int16_t data[kStreamBlock] = {};
  TEST_ASSERT_TRUE(pool.commit(l,data));
}
void test_fir_loop_guard_preserves_initial_prefix_and_suffix() {
  const int s=loopVoice(512,LoopMode::Forward);
  window(s,512,1,12,12,false);expectSlotWant(s,{1,0,2});
  window(s,512,1,12,12,true);expectSlotWant(s,{1,2});
  window(s,1535,-1,12,12,false);expectSlotWant(s,{2,3,1});
  window(s,1535,-1,12,12,true);expectSlotWant(s,{2,1});
}
void test_fir_pingpong_guard_reflects_inside_loop() {
  const int s=loopVoice(512,LoopMode::PingPong);
  window(s,512,1,96,96,true);expectSlotWant(s,{1,2});
  window(s,1535,-1,96,96,true);expectSlotWant(s,{2,1});
}
void test_fir_reserve_requires_history_without_counting_it_as_future() {
  const int s=voice(100000,512);window(s,512,1);
  loadOne(s,1);TEST_ASSERT_EQUAL_UINT32(0,streamReserveUs(pool,s));
  loadOne(s,0);TEST_ASSERT_UINT32_WITHIN(30,11337,streamReserveUs(pool,s));
  loadOne(s,36);  // replace block 0: reverse reserve must stop at its next missing block
  window(s,1023,-1);TEST_ASSERT_EQUAL_UINT32(0,streamReserveUs(pool,s));
  loadOne(s,2);TEST_ASSERT_UINT32_WITHIN(30,11337,streamReserveUs(pool,s));
}
void test_fir_window_over_capacity_has_no_false_reserve() {
  const int s=voice(100000,1536);window(s,1536,1,1535,1536);
  uint32_t wanted[kWantMax];PlaySnap p;TEST_ASSERT_TRUE(pool.snap(s,p));
  const int n=streamWant(p,wanted);TEST_ASSERT_EQUAL(kWantMax,n);
  for(int i=0;i<n;++i)loadOne(s,wanted[i]);
  TEST_ASSERT_EQUAL_UINT32(0,streamReserveUs(pool,s));
}
void test_fir_tiny_loop_and_slice_never_plan_outside_region() {
  SampleRegion g;g.from=g.loopLo=7;g.to=g.loopHi=8;g.loopMode=static_cast<uint8_t>(LoopMode::PingPong);g.pos=int64_t(7)<<32;
  const int s=pool.acquire(1,100000,44100,nullptr,0,g,SlotOwner::Voice);
  window(s,7,-1,1535,1536,true);expectSlotWant(s,{0});
  loadOne(s,0);TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,streamReserveUs(pool,s));
}
void test_fir_loader_boundary_walk_keeps_every_needed_block() {
  const int s=voice(100000,0);
  for(uint32_t pos=0;pos<12000;pos+=128){
    window(s,pos,1,96,96);for(int i=0;i<10&&streamLoad(pool,s,rd);++i){}
    const uint32_t begin=pos>96?pos-96:0,end=pos+96;
    for(uint32_t frame=begin;frame<=end;++frame)TEST_ASSERT_NOT_NULL(pool.block(s,frame/kStreamBlock));
  }
  for(int pos=11999;pos>=0;pos-=128){
    window(s,static_cast<uint32_t>(pos),-1,96,96);for(int i=0;i<10&&streamLoad(pool,s,rd);++i){}
    const uint32_t begin=pos>96?pos-96:0,end=pos+96;
    for(uint32_t frame=begin;frame<=end;++frame)TEST_ASSERT_NOT_NULL(pool.block(s,frame/kStreamBlock));
  }
}

void test_guarded_planner_uses_the_sixth_existing_way() {
  const int slot=voice(100000,8192+511);
  window(slot,8192+511,1,1280,1280);
  PlaySnap p;TEST_ASSERT_TRUE(pool.snap(slot,p));
  uint32_t storage[kWantMax+2];for(auto& value:storage)value=0xDEADBEEFu;
  TEST_ASSERT_EQUAL(kSlotBlocks,streamWant(p,storage+1));
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu,storage[0]);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu,storage[kWantMax+1]);
  for(int loads=0;loads<10&&streamLoad(pool,slot,rd);++loads){}
  for(uint32_t frame=p.pos-p.before;frame<=p.pos+p.after;++frame)
    TEST_ASSERT_NOT_NULL(pool.block(slot,frame/kStreamBlock));
  TEST_ASSERT_TRUE(streamReserveUs(pool,slot)>0);
}

void test_guarded_plan_covers_a_full_live_block_at_step16() {
  for (int direction : {-1, 1}) for (uint32_t offset : {0u, 1u, 95u, 96u, 255u, 416u, 511u}) {
    pool.init(mem);
    const uint32_t start = 8192 + offset;
    const int slot = voice(100000, start);
    pool.publish(slot, int64_t(start) << 32, int64_t(direction) * (int64_t{16} << 32),
                 96, 96, false);
    for (int loads = 0; loads < 10 && streamLoad(pool, slot, rd); ++loads) {}
    for (int output = 0; output < 128; ++output) {
      const int64_t center = int64_t(start) + direction * 16 * output;
      for (int tap = -96; tap <= 96; ++tap)
        TEST_ASSERT_NOT_NULL(pool.block(slot, static_cast<uint32_t>(center + tap) / kStreamBlock));
    }
  }
}

void test_plan_reads_consecutive_missing_blocks() {
  const int s = voice(100000, 0);
  StreamLoad l;
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(0, l.block);
  TEST_ASSERT_EQUAL(kLoadMax, l.n);
  TEST_ASSERT_TRUE(streamLoad(pool, s, rd));
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(4, l.block);
  TEST_ASSERT_EQUAL(1, l.n);
  TEST_ASSERT_TRUE(streamLoad(pool, s, rd));
  TEST_ASSERT_FALSE(streamPlan(pool, s, l));  // all wanted blocks in
  TEST_ASSERT_NULL(pool.block(s, 5));  // not wanted yet
}

void test_plan_evicts_blocks_behind() {
  const int s = voice(100000, 0);
  while (streamLoad(pool, s, rd)) {
  }
  pool.publish(s, int64_t(2600) << 32, int64_t(1) << 32);  // block 5
  StreamLoad l;
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(5, l.block);
  TEST_ASSERT_EQUAL(kLoadMax, l.n);
  TEST_ASSERT_TRUE(streamLoad(pool, s, rd));
  TEST_ASSERT_NULL(pool.block(s, 0));  // behind: evicted (any unwanted way may go, not only the farthest)
  const int16_t* d = pool.block(s, 7);
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_EQUAL_INT16(7 * 512 + 3, d[3]);
}

void test_reserve_counts_loaded_frames_ahead() {
  const int s = voice(100000, 0);
  TEST_ASSERT_EQUAL_UINT32(0, streamReserveUs(pool, s));
  streamLoad(pool, s, rd);  // blocks 0..3: 2048 frames at 1.0 = 46.44 ms
  TEST_ASSERT_UINT32_WITHIN(50, 46440, streamReserveUs(pool, s));
  pool.publish(s, 0, int64_t(2) << 32);  // an octave up: half the time
  TEST_ASSERT_UINT32_WITHIN(50, 23220, streamReserveUs(pool, s));
}

void test_heads_count_as_loaded() {
  static int16_t h[1024];
  static SampleHead head;
  head.frame = 0;
  head.len = 1024;
  head.d = h;
  SampleRegion g;
  g.to = g.loopHi = 100000;
  const int s = pool.acquire(1, 100000, 44100, &head, 1, g, SlotOwner::Voice);
  StreamLoad l;
  TEST_ASSERT_TRUE(streamPlan(pool, s, l));
  TEST_ASSERT_EQUAL_UINT32(2, l.block);  // blocks 0, 1 are in the head
  TEST_ASSERT_UINT32_WITHIN(50, 23220, streamReserveUs(pool, s));
}

void test_full_prefetch_horizon_is_finite_at_high_pitch() {
  const int s = voice(100000, 0);
  pool.publish(s, 0, int64_t(4) << 32);
  while (streamLoad(pool, s, rd)) {}
  TEST_ASSERT_UINT32_WITHIN(2, 14512, streamReserveUs(pool, s));
}

void test_full_reverse_prefetch_horizon_is_finite() {
  const int s = voice(100000, 4095);
  pool.publish(s, int64_t(4095) << 32, -int64_t(4) * (int64_t{1} << 32));
  while (streamLoad(pool, s, rd)) {}
  TEST_ASSERT_UINT32_WITHIN(2, 14512, streamReserveUs(pool, s));
}

void test_full_fir_horizon_cannot_release_background_work() {
  const int s = voice(100000, 0);
  pool.publish(s, 0, int64_t(4) << 32, 12, 12);
  while (streamLoad(pool, s, rd)) {}
  TEST_ASSERT_TRUE(streamReserveUs(pool, s) < 30000);
}

void test_cached_one_shot_end_has_no_more_io_deadline() {
  const int s = voice(1200, 1000);
  pool.publish(s, int64_t(1000) << 32, int64_t(4) << 32);
  while (streamLoad(pool, s, rd)) {}
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, streamReserveUs(pool, s));
}

void test_reserve_stops_at_partial_forward_loop_block() {
  SampleRegion g;
  g.to = 100000;
  g.loopHi = 1537;
  g.loopMode = static_cast<uint8_t>(LoopMode::Forward);
  g.pos = int64_t(1536) << 32;
  const int s = pool.acquire(1, 100000, 44100, nullptr, 0, g, SlotOwner::Voice);
  loadOne(s, 3);
  pool.publish(s, g.pos, int64_t(1) << 30);  // 1/4 speed: only one frame until uncached loop start
  TEST_ASSERT_UINT32_WITHIN(1, 90, streamReserveUs(pool, s));
}

void test_reserve_stops_at_partial_reverse_loop_block() {
  SampleRegion g;
  g.to = g.loopHi = 4096;
  g.loopLo = 511;
  g.loopMode = static_cast<uint8_t>(LoopMode::Forward);
  g.pos = int64_t(511) << 32;
  const int s = pool.acquire(1, 100000, 44100, nullptr, 0, g, SlotOwner::Voice);
  loadOne(s, 0);
  pool.publish(s, g.pos, -int64_t(1) * (int64_t{1} << 30), 0, 0, true);
  TEST_ASSERT_UINT32_WITHIN(1, 90, streamReserveUs(pool, s));
}

void test_reserve_keeps_initial_prefix_before_loop() {
  SampleRegion g;
  g.to = g.loopHi = 100000;
  g.loopLo = 10000;
  g.loopMode = static_cast<uint8_t>(LoopMode::Forward);
  const int s = pool.acquire(1, 100000, 44100, nullptr, 0, g, SlotOwner::Voice);
  pool.publish(s, 0, int64_t(1) << 32);
  while (streamLoad(pool, s, rd)) {}
  TEST_ASSERT_UINT32_WITHIN(2, 58049, streamReserveUs(pool, s));
}

void test_reserve_keeps_initial_reverse_suffix_above_loop() {
  SampleRegion g;
  g.to = 100000;
  g.loopHi = 30000;
  g.loopLo = 10000;
  g.loopMode = static_cast<uint8_t>(LoopMode::Forward);
  g.pos = int64_t(40959) << 32;
  const int s = pool.acquire(1, 100000, 44100, nullptr, 0, g, SlotOwner::Voice);
  pool.publish(s, g.pos, -int64_t(1) * (int64_t{1} << 32));
  while (streamLoad(pool, s, rd)) {}
  TEST_ASSERT_UINT32_WITHIN(2, 58049, streamReserveUs(pool, s));
}

// Independent oracle: project every source tap through the renderer's region rules. The
// planner must retain exactly this prefix before adding any future playback blocks.
static void expectRendererGuardPrefix(const PlaySnap& p) {
  std::vector<uint32_t> expected{p.pos / kStreamBlock};
  bool overflow = false;
  if (p.before || p.after) {
    for (int64_t frame = int64_t(p.pos) - p.before;
         frame <= int64_t(p.pos) + p.after; ++frame) {
      const uint32_t block = sampleFirFrame(frame, p.from, p.to, p.loopLo, p.loopHi,
                                            p.loopMode, p.looped, p.step16) / kStreamBlock;
      bool present = false;
      for (uint32_t have : expected) present |= have == block;
      if (present) continue;
      if (expected.size() == kWantMax) { overflow = true; break; }
      expected.push_back(block);
    }
  }
  uint32_t guarded[kWantMax + 2];
  for (auto& value : guarded) value = 0xDEADBEEFu;
  const int n = streamWant(p, guarded + 1);
  TEST_ASSERT_TRUE(n >= static_cast<int>(expected.size()));
  TEST_ASSERT_TRUE(n <= kWantMax);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, guarded[0]);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, guarded[kWantMax + 1]);
  for (size_t i = 0; i < expected.size(); ++i)
    TEST_ASSERT_EQUAL_UINT32(expected[i], guarded[i + 1]);
  if (overflow) TEST_ASSERT_EQUAL(kWantMax, n);
}

void test_guard_priority_matches_renderer_at_loop_and_slice_boundaries() {
  struct Region { uint32_t from, to, lo, hi; };
  const Region regions[] = {{0,100000,0,100000}, {7,8,7,8}, {511,1537,512,1536},
                            {7,2049,511,1537}, {4095,12000,6000,8193}};
  for (const Region& r : regions) for (LoopMode mode : {LoopMode::Off, LoopMode::Forward, LoopMode::PingPong})
    for (int dir : {-1,1}) for (bool looped : {false,true})
      for (uint16_t guard : {uint16_t(0),uint16_t(1),uint16_t(18),uint16_t(96),uint16_t(1280)})
        for (uint32_t pos : {r.from,r.lo,r.hi-1,r.to-1}) {
          PlaySnap p = snap(pos, dir*65536, r.lo, r.hi, mode);
          p.from = r.from; p.to = r.to; p.looped = looped;
          p.before = guard; p.after = guard ? guard-1 : 0;
          expectRendererGuardPrefix(p);
        }
}

void test_guard_priority_matches_renderer_for_random_regions_and_wide_windows() {
  uint32_t random = 0xB2A19F03u;
  auto next = [&]() { random = random*1664525u+1013904223u; return random; };
  for (int trial = 0; trial < 3000; ++trial) {
    PlaySnap p;
    p.from = next()%10000;
    p.to = p.from + 1 + next()%100000;
    p.loopLo = p.from + next()%(p.to-p.from);
    p.loopHi = p.loopLo + 1 + next()%(p.to-p.loopLo);
    p.pos = p.from + next()%(p.to-p.from);
    p.step16 = next()&1 ? 4*65536 : -4*65536;
    p.loopMode = static_cast<uint8_t>(next()%3);
    p.looped = next()&1;
    p.before = static_cast<uint16_t>(next()%2049);
    p.after = static_cast<uint16_t>(next()%2049);
    expectRendererGuardPrefix(p);
  }
}

static void expectCombinedNeeds(int slot) {
  const uint32_t expected = streamReserveUs(pool, slot);
  StreamLoad load;
  const bool needed = streamPlan(pool, slot, load);
  uint32_t reserve = 0;
  TEST_ASSERT_EQUAL(needed, streamNeeds(pool, slot, reserve));
  TEST_ASSERT_EQUAL_UINT32(expected, reserve);
}

void test_combined_needs_preserves_free_slot_and_pinned_duplicate_ways() {
  expectCombinedNeeds(0);  // a free slot has no deadline and no load
  const int slot = voice(100000, 512);
  window(slot, 512, 4, 17, 18);
  expectCombinedNeeds(slot);  // empty ways accept the first load
  StreamLoad seed;
  seed.slot = slot; seed.key = pool.slot(slot).key; seed.gen = pool.slot(slot).gen;
  seed.n = 1; seed.block = 0;
  static int16_t data[kStreamBlock] = {};
  for (int way = 0; way < kSlotBlocks; ++way) {
    seed.way[0] = way;
    TEST_ASSERT_TRUE(pool.commit(seed, data));
  }
  // Current block is missing, but every way holds a wanted history block. The combined query
  // must preserve streamPlan's inability to load, rather than merely checking for a miss.
  StreamLoad load;
  TEST_ASSERT_FALSE(streamPlan(pool, slot, load));
  expectCombinedNeeds(slot);
  const uint8_t generation = pool.slot(slot).gen;
  pool.slotRef(slot).gen = static_cast<uint8_t>(generation + 1);
  TEST_ASSERT_TRUE(streamPlan(pool, slot, load));  // old tags are reusable after a generation change
  expectCombinedNeeds(slot);
  pool.slotRef(slot).gen = generation;
  seed.block = 40;
  TEST_ASSERT_TRUE(pool.commit(seed, data));
  TEST_ASSERT_TRUE(streamPlan(pool, slot, load));
  expectCombinedNeeds(slot);
  seed.block = 1;
  TEST_ASSERT_TRUE(pool.commit(seed, data));
  window(slot, 512, 0, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, streamReserveUs(pool, slot));
  expectCombinedNeeds(slot);  // a stationary cursor still asks to fill its wanted blocks
}

void test_combined_needs_preserves_guard_overflow_and_cached_end() {
  const int slot = voice(100000, 1536);
  window(slot, 1536, 1, 1535, 1536);
  for (int loads = 0; loads < 10 && streamLoad(pool, slot, rd); ++loads) {}
  TEST_ASSERT_EQUAL_UINT32(0, streamReserveUs(pool, slot));
  expectCombinedNeeds(slot);
  pool.init(mem);
  const int end = voice(1200, 1000);
  while (streamLoad(pool, end, rd)) {}
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, streamReserveUs(pool, end));
  expectCombinedNeeds(end);
}

void test_combined_needs_matches_separate_queries_for_random_heads_and_cache() {
  uint32_t random = 0x85471AF3u;
  auto next = [&]() { random = random*1664525u+1013904223u; return random; };
  static int16_t data[kStreamBlock] = {};
  static int16_t headData[1024] = {};
  for (int trial = 0; trial < 3000; ++trial) {
    pool.init(mem, 1);
    SampleRegion g;
    g.from = next()%1000; g.to = g.from + 1 + next()%100000;
    g.loopLo = g.from + next()%(g.to-g.from);
    g.loopHi = g.loopLo + 1 + next()%(g.to-g.loopLo);
    g.loopMode = static_cast<uint8_t>(next()%3);
    uint32_t pos = g.from + next()%(g.to-g.from);
    if (trial%4 == 0) pos = g.loopLo;
    else if (trial%4 == 1) pos = g.loopHi-1;
    g.pos = int64_t(pos)<<32;
    SampleHead heads[2];
    for (auto& head : heads) {
      head.frame = (next()%(g.to/kStreamBlock+1))*kStreamBlock;
      head.len = static_cast<uint16_t>(next()%1025); head.d = headData;
    }
    const int slot = pool.acquire(1, g.to, 44100, heads, next()%3, g, SlotOwner::Voice);
    for (int way = 0; way < kSlotBlocks; ++way) {
      StreamLoad seed;
      seed.slot = slot; seed.key = 1; seed.gen = pool.slot(slot).gen;
      seed.n = 1; seed.way[0] = way;
      int64_t block = int64_t(pos/kStreamBlock) + int(next()%9)-4;
      if (block < 0) block = 0;
      if (trial%4 == 2) block = next()%(g.to/kStreamBlock+1);
      seed.block = static_cast<uint32_t>(block);
      TEST_ASSERT_TRUE(pool.commit(seed, data));
    }
    const int32_t rates[] = {0, 65536, 4*65536, -4*65536};
    const uint16_t before = static_cast<uint16_t>(next()%2049);
    const uint16_t after = static_cast<uint16_t>(next()%2049);
    pool.publish(slot, g.pos, int64_t(rates[next()%4])*65536, before, after, next()&1);
    expectCombinedNeeds(slot);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_want_forward);
  RUN_TEST(test_want_stops_at_end_without_loop);
  RUN_TEST(test_want_reverse);
  RUN_TEST(test_want_forward_loop_wraps);
  RUN_TEST(test_want_pingpong_turns);
  RUN_TEST(test_fir_window_prioritizes_previous_forward_block);
  RUN_TEST(test_fir_window_prioritizes_previous_reverse_block);
  RUN_TEST(test_plan_does_not_evict_fir_history);
  RUN_TEST(test_fir_loop_guard_preserves_initial_prefix_and_suffix);
  RUN_TEST(test_fir_pingpong_guard_reflects_inside_loop);
  RUN_TEST(test_fir_reserve_requires_history_without_counting_it_as_future);
  RUN_TEST(test_fir_window_over_capacity_has_no_false_reserve);
  RUN_TEST(test_fir_tiny_loop_and_slice_never_plan_outside_region);
  RUN_TEST(test_fir_loader_boundary_walk_keeps_every_needed_block);
  RUN_TEST(test_guarded_planner_uses_the_sixth_existing_way);
  RUN_TEST(test_guarded_plan_covers_a_full_live_block_at_step16);
  RUN_TEST(test_plan_reads_consecutive_missing_blocks);
  RUN_TEST(test_plan_evicts_blocks_behind);
  RUN_TEST(test_reserve_counts_loaded_frames_ahead);
  RUN_TEST(test_heads_count_as_loaded);
  RUN_TEST(test_full_prefetch_horizon_is_finite_at_high_pitch);
  RUN_TEST(test_full_reverse_prefetch_horizon_is_finite);
  RUN_TEST(test_full_fir_horizon_cannot_release_background_work);
  RUN_TEST(test_cached_one_shot_end_has_no_more_io_deadline);
  RUN_TEST(test_reserve_stops_at_partial_forward_loop_block);
  RUN_TEST(test_reserve_stops_at_partial_reverse_loop_block);
  RUN_TEST(test_reserve_keeps_initial_prefix_before_loop);
  RUN_TEST(test_reserve_keeps_initial_reverse_suffix_above_loop);
  RUN_TEST(test_guard_priority_matches_renderer_at_loop_and_slice_boundaries);
  RUN_TEST(test_guard_priority_matches_renderer_for_random_regions_and_wide_windows);
  RUN_TEST(test_combined_needs_preserves_free_slot_and_pinned_duplicate_ways);
  RUN_TEST(test_combined_needs_preserves_guard_overflow_and_cached_end);
  RUN_TEST(test_combined_needs_matches_separate_queries_for_random_heads_and_cache);
  return UNITY_END();
}
