// Actual bank.cpp SampleEditSession, extracted by run.sh. Filesystem/DSP/transport are fakes;
// real protocol codecs and queue are used. StoreJob DSP/file behavior has its own core tests.
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "bank_ops.h"
#include "link_msg.h"
#include "sample_store.h"
#include "ev_queue.h"
using namespace mt::link;
using mt::BankResult;
static int guards=0;
static uint32_t now=0;
static void AudioNoInterrupts() { ++guards; }
static void AudioInterrupts() { --guards; }
static uint32_t millis() { return now; }
struct FakeOut {
  EvQueue<256> q;
  bool parked_=false;
  EvQueue<256>& queue() { return q; }
  bool parked() const { return parked_; }
  void park(bool v) { parked_=v; }
} output;
struct FakeSynth {
  int gate=-1, resets=0;
  int gatePos() const { return gate; }
  void reset() { TEST_ASSERT_TRUE(output.parked()); TEST_ASSERT_TRUE(guards>0); ++resets; }
} synth;
static bool recBusy=false, renderBusy=false, toolsBusy=false;
namespace rec { bool busy() { return recBusy; } }
namespace render { bool active() { return renderBusy; } }
namespace tools { bool busy() { return toolsBusy; } }
namespace preview { static unsigned stops=0; void stop() { TEST_ASSERT_TRUE(output.parked()); ++stops; } }
namespace stream {
static bool addOk=true;
static std::vector<uint32_t> added;
bool add(uint32_t crc) { TEST_ASSERT_TRUE(output.parked()); added.push_back(crc); return addOk; }
}
namespace mt { void storePath(uint32_t crc,char out[kStorePathMax]) { snprintf(out,kStorePathMax,"/mt-store/%08lx.wav",static_cast<unsigned long>(crc)); } }
struct FakeStore {
  int starts=0, cancels=0, steps=0;
  BankResult startResult=BankResult::Ok, nextResult=BankResult::Running;
  mt::ImportOut result;
  std::string path;
  uint32_t first=0,last=0;
  bool normalized=false;
  uint32_t ratio=0;
  bool analyzing=false;
  mt::TempoEstimate estimate{1200,700,600,2400,true};
  BankResult startEdit(const char* p,uint32_t f,uint32_t t,bool norm) {
    TEST_ASSERT_TRUE(output.parked()); TEST_ASSERT_EQUAL_INT(starts+1,synth.resets);
    TEST_ASSERT_NULL(output.queue().peek()); TEST_ASSERT_EQUAL_INT(0,guards);
    ++starts; path=p; first=f; last=t; normalized=norm; return startResult;
  }
  void cancel() { TEST_ASSERT_TRUE(output.parked()); ++cancels; }
  BankResult startStretch(const char* p,uint32_t f,uint32_t t,uint32_t r) {
    const auto result=startEdit(p,f,t,false); ratio=r; return result;
  }
  BankResult startTempo(const char* p,uint32_t f,uint32_t t) {
    const auto result=startEdit(p,f,t,false); analyzing=true; return result;
  }
  const mt::TempoEstimate& tempoResult() const { return estimate; }
  BankResult step() { TEST_ASSERT_TRUE(output.parked()); TEST_ASSERT_NULL(output.queue().peek()); ++steps; return nextResult; }
  const mt::ImportOut& imported() const { return result; }
  uint32_t done() const { return 512; }
  uint32_t total() const { return 2048; }
} store;
struct Reply { Msg type; uint8_t seq; std::vector<uint8_t> bytes; };
static std::vector<Reply> replies;
static unsigned progressCalls=0;
namespace link { void reply(Msg t,uint8_t s,const uint8_t* p,int n) { replies.push_back({t,s,{p,p+n}}); } }
static void sendProgress(uint32_t,uint32_t,int) { ++progressCalls; }
template<class M> static int put(const M& m,uint8_t* p) { Writer w(p,kMaxPayload); encode(m,w); return w.ok()?w.size():0; }
static uint8_t res(BankResult r) { return uint8_t(r); }
static mt::SynthModel testModel;
static mt::SynthModel* model=&testModel;
static FakeSynth* theSynth=&synth;
static FakeOut* out=&output;
static bool mounted=true, working=false, migrating=false, storeActive=false, jobActive=false, parkedByUs=false;
static uint8_t curType=0, curSeq=0;
static uint32_t lastProgress=0;
static constexpr uint32_t kProgressMs=100;
static void park(bool on) { parkedByUs=on; output.park(on); }
#include BANK_EDIT_EXTRACT

void setUp() {
  guards=0; now=0; synth=FakeSynth{};
  output.queue().clear(); output.parked_=false;
  recBusy=renderBusy=toolsBusy=false;
  preview::stops=0; stream::addOk=true; stream::added.clear();
  store=FakeStore{}; store.result={0x11223344,100,44100,64};
  replies.clear(); progressCalls=0;
  mounted=true; working=migrating=storeActive=jobActive=parkedByUs=false;
  model=&testModel; theSynth=&synth; out=&output;
  model->sampleCount=1; model->samples[0].crc=0xaabbccdd; model->samples[0].frames=10000;
  edit=SampleEditSession{};
}
void tearDown() { TEST_ASSERT_EQUAL_INT(0,guards); }
static SampleEditReq request(uint32_t token=1) { SampleEditReq q; q.token=token; q.index=0; q.crc=0xaabbccdd; q.from=100; q.to=200; return q; }
static int call(const SampleEditReq& q,uint8_t seq=11,uint8_t* outputBytes=nullptr,uint8_t mode=0,uint32_t ratio=65536) {
  uint8_t p[23],o[kMaxPayload]; Writer w(p,sizeof p);
  // Explicit encoding keeps the baseline missing-handler proof independent of codec scaffolding.
  w.u32(q.token); w.u8(q.index); w.u32(q.crc); w.u32(q.from); w.u32(q.to); w.u8(q.normalize);
  w.u8(mode); w.u32(ratio);
  const int n=edit.handle(seq,p,w.size(),o);
  if(n>=0 && outputBytes) memcpy(outputBytes,o,n);
  return n;
}
static SampleEditRep parsed(const uint8_t* p,int n) {
  TEST_ASSERT_EQUAL_INT(18,n);
  SampleEditRep rep; Reader r(p,n);
  rep.token=r.u32(); rep.result=r.u8(); rep.crc=r.u32(); rep.frames=r.u32(); rep.rate=r.u32(); rep.root=r.u8();
  TEST_ASSERT_TRUE(r.ok()); return rep;
}
static void futureEvent() { TEST_ASSERT_TRUE(output.queue().push(QEv{UINT64_MAX,0,3,{0x90,60,100}})); }
static void test_starts_offline_only_after_park_reset_and_queue_clear() {
  futureEvent(); auto q=request(); q.normalize=1;
  TEST_ASSERT_EQUAL_INT(-1,call(q)); TEST_ASSERT_TRUE(edit.active); TEST_ASSERT_TRUE(working); TEST_ASSERT_TRUE(storeActive);
  TEST_ASSERT_TRUE(output.parked()); TEST_ASSERT_EQUAL_INT(1,store.starts);
  TEST_ASSERT_EQUAL_STRING("/mt-store/aabbccdd.wav",store.path.c_str());
  TEST_ASSERT_EQUAL_UINT32(100,store.first); TEST_ASSERT_EQUAL_UINT32(200,store.last); TEST_ASSERT_TRUE(store.normalized);
}
static void test_retries_are_exact_and_other_requests_cannot_cancel_active_edit() {
  auto q=request(); TEST_ASSERT_EQUAL_INT(-1,call(q)); TEST_ASSERT_EQUAL_INT(-1,call(q));
  TEST_ASSERT_EQUAL_INT(1,store.starts); TEST_ASSERT_EQUAL_UINT(1,progressCalls);
  uint8_t o[kMaxPayload]; q.from++;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Busy),parsed(o,call(q,11,o)).result);
  q=request(2); TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Busy),parsed(o,call(q,12,o)).result);
  TEST_ASSERT_EQUAL_INT(0,store.cancels); TEST_ASSERT_EQUAL_INT(1,store.starts);
}
static void test_completion_caches_full_identity_and_new_token_reuses_wire_seq() {
  auto q=request(); TEST_ASSERT_EQUAL_INT(-1,call(q)); futureEvent();
  store.nextResult=BankResult::Ok; TEST_ASSERT_TRUE(edit.step());
  TEST_ASSERT_FALSE(edit.active); TEST_ASSERT_FALSE(working); TEST_ASSERT_FALSE(output.parked());
  TEST_ASSERT_NULL(output.queue().peek()); TEST_ASSERT_EQUAL_UINT(1,replies.size());
  const auto rep=parsed(replies[0].bytes.data(),replies[0].bytes.size());
  TEST_ASSERT_EQUAL_UINT32(q.token,rep.token); TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Ok),rep.result);
  TEST_ASSERT_EQUAL_UINT32(store.result.crc,rep.crc); TEST_ASSERT_EQUAL_UINT32(100,rep.frames);
  TEST_ASSERT_EQUAL_UINT32(44100,rep.rate); TEST_ASSERT_EQUAL_UINT8(64,rep.root);
  uint8_t o[kMaxPayload]; TEST_ASSERT_EQUAL_INT(18,call(q,11,o)); TEST_ASSERT_EQUAL_INT(1,store.starts);
  q.from++; TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::ReadFail),parsed(o,call(q,11,o)).result);
  q=request(2); TEST_ASSERT_EQUAL_INT(-1,call(q)); TEST_ASSERT_EQUAL_INT(2,store.starts);
}
static void test_stale_identity_and_invalid_ranges_do_no_store_io() {
  uint8_t o[kMaxPayload];
  for(int variation=0;variation<7;++variation) {
    auto q=request(); if(variation==0)q.token=0; if(variation==1)q.index=1; if(variation==2)q.crc++;
    if(variation==3)q.to=q.from; if(variation==4)q.to=10001; if(variation==5)q.normalize=2; if(variation==6)q.from=UINT32_MAX;
    const auto rep=parsed(o,call(q,11,o)); TEST_ASSERT_NOT_EQUAL(uint8_t(BankResult::Ok),rep.result);
    TEST_ASSERT_EQUAL_UINT32(q.token,rep.token); TEST_ASSERT_EQUAL_INT(0,store.starts); TEST_ASSERT_EQUAL_INT(0,synth.resets);
  }
}
static void test_busy_transport_rec_render_and_other_jobs_reject_before_park() {
  uint8_t o[kMaxPayload];
  for(int variation=0;variation<7;++variation) {
    synth.gate=variation==0?0:-1; recBusy=variation==1; renderBusy=variation==2; toolsBusy=variation==3;
    working=variation==4; migrating=variation==5; output.parked_=variation==6;
    TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Busy),parsed(o,call(request(),11,o)).result);
    TEST_ASSERT_EQUAL_INT(0,store.starts); TEST_ASSERT_EQUAL_INT(0,synth.resets);
  }
}
static int cancel(uint32_t token) { uint8_t p[4]; Writer w(p,sizeof p); w.u32(token); return edit.cancel(p,w.size()); }
static void test_matching_cancel_is_terminal_original_reply_and_drops_future_events() {
  TEST_ASSERT_EQUAL_INT(-1,call(request())); futureEvent();
  TEST_ASSERT_EQUAL_INT(-1,cancel(2)); TEST_ASSERT_TRUE(edit.active); TEST_ASSERT_TRUE(replies.empty());
  TEST_ASSERT_EQUAL_INT(-1,cancel(1)); TEST_ASSERT_FALSE(edit.active); TEST_ASSERT_FALSE(output.parked());
  TEST_ASSERT_NULL(output.queue().peek()); TEST_ASSERT_EQUAL_INT(1,store.cancels);
  TEST_ASSERT_EQUAL_UINT(1,replies.size()); TEST_ASSERT_EQUAL(Msg::SampleEdit,replies[0].type); TEST_ASSERT_EQUAL_UINT8(11,replies[0].seq);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Cancelled),parsed(replies[0].bytes.data(),replies[0].bytes.size()).result);
  TEST_ASSERT_EQUAL_INT(-1,cancel(1)); TEST_ASSERT_EQUAL_UINT(1,replies.size());
}
static void test_hello_reset_cancels_edit_silently_and_forgets_reply_cache() {
  TEST_ASSERT_EQUAL_INT(-1,call(request())); futureEvent(); edit.reset();
  TEST_ASSERT_FALSE(edit.active); TEST_ASSERT_FALSE(edit.cached); TEST_ASSERT_FALSE(working); TEST_ASSERT_FALSE(output.parked());
  TEST_ASSERT_NULL(output.queue().peek()); TEST_ASSERT_EQUAL_INT(1,store.cancels); TEST_ASSERT_TRUE(replies.empty());
  TEST_ASSERT_EQUAL_INT(-1,call(request())); TEST_ASSERT_EQUAL_INT(2,store.starts);
}
static void test_start_failure_and_registration_failure_unpark_with_cached_error() {
  store.startResult=BankResult::OpenFail; uint8_t o[kMaxPayload];
  TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::OpenFail),parsed(o,call(request(),11,o)).result);
  TEST_ASSERT_FALSE(output.parked()); TEST_ASSERT_FALSE(working); TEST_ASSERT_EQUAL_INT(1,store.starts);
  TEST_ASSERT_EQUAL_INT(18,call(request(),11,o)); TEST_ASSERT_EQUAL_INT(1,store.starts);
  setUp(); TEST_ASSERT_EQUAL_INT(-1,call(request())); stream::addOk=false; store.nextResult=BankResult::Ok; edit.step();
  TEST_ASSERT_FALSE(output.parked()); TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::ReadFail),parsed(replies[0].bytes.data(),replies[0].bytes.size()).result);
}
static void test_running_step_discards_future_events_and_reports_progress() {
  TEST_ASSERT_EQUAL_INT(-1,call(request())); futureEvent(); now=101;
  TEST_ASSERT_TRUE(edit.step()); TEST_ASSERT_TRUE(edit.active); TEST_ASSERT_TRUE(output.parked());
  TEST_ASSERT_NULL(output.queue().peek()); TEST_ASSERT_EQUAL_UINT(1,progressCalls);
  TEST_ASSERT_EQUAL_INT(1,store.steps); TEST_ASSERT_TRUE(replies.empty());
}
static void test_malformed_payloads_are_rejected_before_identity_memory_access() {
  uint8_t p[24]={},o[kMaxPayload];
  for(int length:{-1,0,3,17,18,22,24}) {
    const auto rep=parsed(o,edit.handle(11,p,length,o));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Unsupported),rep.result);
  }
  TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Unsupported),parsed(o,edit.handle(11,nullptr,23,o)).result);
  TEST_ASSERT_EQUAL_INT(-1,edit.cancel(nullptr,4)); TEST_ASSERT_EQUAL_INT(-1,edit.cancel(p,3));
  TEST_ASSERT_EQUAL_INT(0,store.starts); TEST_ASSERT_EQUAL_INT(0,store.cancels);
}
static void test_completed_token_cannot_be_reexecuted_under_a_different_seq() {
  TEST_ASSERT_EQUAL_INT(-1,call(request())); store.nextResult=BankResult::Ok; edit.step();
  uint8_t o[kMaxPayload]; TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::ReadFail),parsed(o,call(request(),12,o)).result);
  TEST_ASSERT_EQUAL_INT(1,store.starts); TEST_ASSERT_EQUAL_INT(18,call(request(),11,o));
}
static void test_late_cancel_does_not_overwrite_success_or_register_twice() {
  TEST_ASSERT_EQUAL_INT(-1,call(request())); store.nextResult=BankResult::Ok; edit.step();
  TEST_ASSERT_EQUAL_INT(-1,cancel(1)); TEST_ASSERT_EQUAL_INT(0,store.cancels);
  uint8_t o[kMaxPayload]; TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Ok),parsed(o,call(request(),11,o)).result);
  TEST_ASSERT_EQUAL_UINT(1,replies.size()); TEST_ASSERT_EQUAL_UINT(1,stream::added.size());
}
static void test_hello_does_not_cancel_or_unpark_unrelated_bank_work() {
  working=true; storeActive=true; parkedByUs=true; output.parked_=true;
  edit.reset();
  TEST_ASSERT_TRUE(working); TEST_ASSERT_TRUE(storeActive); TEST_ASSERT_TRUE(output.parked());
  TEST_ASSERT_EQUAL_INT(0,store.cancels); TEST_ASSERT_TRUE(replies.empty());
}
static int analyze(uint32_t token=1,uint8_t seq=11,uint8_t* outputBytes=nullptr) {
  uint8_t p[17],o[kMaxPayload]; Writer w(p,sizeof p); const auto q=request(token);
  w.u32(q.token); w.u8(q.index); w.u32(q.crc); w.u32(q.from); w.u32(q.to);
  const int n=edit.analyze(seq,p,w.size(),o); if(n>=0&&outputBytes)memcpy(outputBytes,o,n); return n;
}
static void test_stretch_mode_has_exact_identity_and_rejects_invalid_combinations() {
  auto q=request(); TEST_ASSERT_EQUAL_INT(-1,call(q,11,nullptr,1,98304));
  TEST_ASSERT_EQUAL_UINT32(98304,store.ratio); TEST_ASSERT_FALSE(store.normalized);
  uint8_t o[kMaxPayload];
  TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Busy),parsed(o,call(q,11,o,1,131072)).result);
  edit.reset();
  for(int variation=0;variation<5;++variation) {
    q=request(variation+2); q.normalize=variation==0?1:0;
    const uint8_t mode=variation==4?2:variation==3?0:1;
    const uint32_t ratio=variation==1?32767:variation==2?131073:98304;
    TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Unsupported),parsed(o,call(q,11,o,mode,ratio)).result);
  }
  TEST_ASSERT_EQUAL_INT(1,store.starts);
}
static void test_tempo_typed_cache_never_replays_edit_reply_or_registers_output() {
  TEST_ASSERT_EQUAL_INT(-1,call(request())); store.nextResult=BankResult::Ok; edit.step();
  stream::added.clear(); replies.clear();
  TEST_ASSERT_EQUAL_INT(-1,analyze(1,11)); TEST_ASSERT_TRUE(store.analyzing);
  store.nextResult=BankResult::Ok; edit.step();
  TEST_ASSERT_EQUAL_UINT(1,replies.size()); TEST_ASSERT_EQUAL_UINT8(54,uint8_t(replies[0].type));
  TEST_ASSERT_EQUAL_UINT(14,replies[0].bytes.size());
  Reader r(replies[0].bytes.data(),14);
  TEST_ASSERT_EQUAL_UINT32(1,r.u32()); TEST_ASSERT_EQUAL_UINT8(0,r.u8());
  TEST_ASSERT_EQUAL_UINT16(1200,r.u16()); TEST_ASSERT_EQUAL_UINT16(700,r.u16());
  TEST_ASSERT_EQUAL_UINT16(600,r.u16()); TEST_ASSERT_EQUAL_UINT16(2400,r.u16()); TEST_ASSERT_EQUAL_UINT8(1,r.u8());
  TEST_ASSERT_TRUE(stream::added.empty()); uint8_t o[kMaxPayload];
  TEST_ASSERT_EQUAL_INT(14,analyze(1,11,o)); TEST_ASSERT_EQUAL_INT(2,store.starts);
  TEST_ASSERT_EQUAL_INT(-1,call(request())); TEST_ASSERT_EQUAL_INT(3,store.starts);
}
static void test_tempo_valid_unknown_and_hello_cleanup_are_read_only() {
  TEST_ASSERT_EQUAL_INT(-1,analyze()); store.estimate=mt::TempoEstimate{};
  store.nextResult=BankResult::Ok; edit.step();
  TEST_ASSERT_EQUAL_UINT(14,replies[0].bytes.size());
  for(size_t i=5;i<14;++i)TEST_ASSERT_EQUAL_UINT8(0,replies[0].bytes[i]);
  TEST_ASSERT_TRUE(stream::added.empty());
  TEST_ASSERT_EQUAL_INT(-1,analyze(2)); futureEvent(); edit.reset();
  TEST_ASSERT_NULL(output.queue().peek()); TEST_ASSERT_FALSE(output.parked()); TEST_ASSERT_FALSE(working);
}
static void test_tempo_cancel_is_typed_and_retries_keep_original_reply_identity() {
  TEST_ASSERT_EQUAL_INT(-1,analyze(7,19));
  TEST_ASSERT_EQUAL_INT(-1,analyze(7,19)); TEST_ASSERT_EQUAL_UINT(1,progressCalls);
  TEST_ASSERT_EQUAL_UINT8(54,curType);
  uint8_t p[4]; Writer w(p,sizeof p); w.u32(7); futureEvent();
  TEST_ASSERT_EQUAL_INT(-1,edit.cancel(p,4));  // EditCancel must not cancel a Tempo job.
  TEST_ASSERT_TRUE(edit.active); TEST_ASSERT_EQUAL_INT(0,store.cancels);
  TEST_ASSERT_EQUAL_INT(-1,edit.cancel(p,4,static_cast<Msg>(54)));
  TEST_ASSERT_FALSE(edit.active); TEST_ASSERT_FALSE(output.parked()); TEST_ASSERT_NULL(output.queue().peek());
  TEST_ASSERT_EQUAL_INT(1,store.cancels); TEST_ASSERT_EQUAL_UINT(1,replies.size());
  TEST_ASSERT_EQUAL_UINT8(54,uint8_t(replies[0].type)); TEST_ASSERT_EQUAL_UINT8(19,replies[0].seq);
  TEST_ASSERT_EQUAL_UINT(14,replies[0].bytes.size());
  TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Cancelled),replies[0].bytes[4]);
  uint8_t o[kMaxPayload]; TEST_ASSERT_EQUAL_INT(14,analyze(7,19,o));
  TEST_ASSERT_EQUAL_MEMORY(replies[0].bytes.data(),o,14); TEST_ASSERT_EQUAL_INT(1,store.starts);
}
static void test_other_typed_request_is_busy_and_stretch_overflow_is_preflighted() {
  TEST_ASSERT_EQUAL_INT(-1,analyze()); uint8_t o[kMaxPayload];
  TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Busy),parsed(o,call(request(),11,o)).result);
  TEST_ASSERT_EQUAL_INT(1,store.starts); edit.reset();
  model->samples[0].frames=UINT32_MAX;
  auto q=request(2); q.from=0; q.to=UINT32_MAX;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(BankResult::Unsupported),parsed(o,call(q,11,o,1,131072)).result);
  TEST_ASSERT_EQUAL_INT(1,store.starts);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_offline_only_after_park_reset_and_queue_clear);
  RUN_TEST(test_retries_are_exact_and_other_requests_cannot_cancel_active_edit);
  RUN_TEST(test_completion_caches_full_identity_and_new_token_reuses_wire_seq);
  RUN_TEST(test_stale_identity_and_invalid_ranges_do_no_store_io);
  RUN_TEST(test_busy_transport_rec_render_and_other_jobs_reject_before_park);
  RUN_TEST(test_matching_cancel_is_terminal_original_reply_and_drops_future_events);
  RUN_TEST(test_hello_reset_cancels_edit_silently_and_forgets_reply_cache);
  RUN_TEST(test_start_failure_and_registration_failure_unpark_with_cached_error);
  RUN_TEST(test_running_step_discards_future_events_and_reports_progress);
  RUN_TEST(test_malformed_payloads_are_rejected_before_identity_memory_access);
  RUN_TEST(test_completed_token_cannot_be_reexecuted_under_a_different_seq);
  RUN_TEST(test_late_cancel_does_not_overwrite_success_or_register_twice);
  RUN_TEST(test_hello_does_not_cancel_or_unpark_unrelated_bank_work);
  RUN_TEST(test_stretch_mode_has_exact_identity_and_rejects_invalid_combinations);
  RUN_TEST(test_tempo_typed_cache_never_replays_edit_reply_or_registers_output);
  RUN_TEST(test_tempo_valid_unknown_and_hello_cleanup_are_read_only);
  RUN_TEST(test_tempo_cancel_is_typed_and_retries_keep_original_reply_identity);
  RUN_TEST(test_other_typed_request_is_busy_and_stretch_overflow_is_preflighted);
  return UNITY_END();
}
