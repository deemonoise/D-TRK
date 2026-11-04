// Compiles the actual ESP bank_client.cpp against an in-memory request transport and real codecs.
#include <string.h>
#include <unity.h>
#include "audio/audio.h"
#include "engine/engine.h"
#include "link/bank_client.h"
#include "link/link.h"
using namespace mt::link;
static bool playing, synced, rpcOk, wrongToken, truncated, terminalSeen, playAfterPump;
static uint32_t responseFrames, responseRate;
static uint8_t responseRoot;
static uint32_t clockMs;
static int calls, callbacks, progressCount, cancelSends, cancelFailures;
static uint8_t terminalResult;
static SampleEditReq sent;
static SampleTempoReq tempoSent;
static SampleTempoRep tempoResponse;
static Msg expectedType;
static uint32_t cancelToken;
static mt::ImportOut output;
uint32_t millis() { return clockMs; }
void delay(uint32_t ms) { clockMs += ms; }
namespace engine { Status status() { Status s{}; s.playing = playing; return s; } }
namespace audio {
void pump() { if (playAfterPump) playing = true; }
bool synced() { return ::synced; }
const mt::Project* mirrorProject() { return nullptr; }
}
namespace slink {
bool synthUp() { return true; }
bool versionOk() { return true; }
uint32_t bootId() { return 1; }
bool sendRaw(Msg type, const uint8_t* data, int size, TickType_t) {
  TEST_ASSERT_EQUAL(expectedType==Msg::SampleTempo ? Msg::SampleTempoCancel : Msg::SampleEditCancel, type);
  SampleEditCancelReq request; Reader reader(data, size);
  TEST_ASSERT_TRUE(decode(reader, request));
  cancelToken = request.token;
  ++cancelSends;
  return cancelSends > cancelFailures;
}
bool requestLong(Msg type, const uint8_t* data, int size, uint8_t* reply, int& length,
                 ProgressFn progress, void* ctx, uint32_t) {
  ++calls;
  TEST_ASSERT_EQUAL(expectedType, type);
  if(type==Msg::SampleTempo) {
    TEST_ASSERT_EQUAL(17,size); Reader rd(data,size); TEST_ASSERT_TRUE(decode(rd,tempoSent));
    for(int i=0;i<progressCount;i++){ Progress p; p.op=static_cast<uint8_t>(type);p.done=i;p.total=600;if(progress)progress(p,ctx); }
    tempoResponse.token=tempoSent.token+(wrongToken?1:0); tempoResponse.result=terminalResult;
    Writer w(reply,kMaxPayload);encode(tempoResponse,w);length=truncated?3:w.size();terminalSeen=true;return rpcOk;
  }
  TEST_ASSERT_EQUAL(23, size);
  Reader reader(data, size);
  TEST_ASSERT_TRUE(decode(reader, sent));
  for (int i = 0; i < progressCount; ++i) {
    Progress p; p.op = static_cast<uint8_t>(type); p.done = i + 1; p.total = 600;
    if (progress) progress(p, ctx);
  }
  SampleEditRep response;
  response.token = sent.token + (wrongToken ? 1 : 0);
  response.result = terminalResult;
  response.crc = 789; response.frames = responseFrames; response.rate = responseRate; response.root = responseRoot;
  Writer writer(reply, kMaxPayload); encode(response, writer);
  length = truncated ? 3 : writer.size();
  terminalSeen = true;
  return rpcOk;
}
bool request(Msg, const uint8_t*, int, Msg, uint8_t*, int&, uint32_t) { return false; }
}
void setUp() {
  playing = playAfterPump = false; synced = true; rpcOk = true; wrongToken = truncated = terminalSeen = false;
  clockMs = calls = callbacks = cancelSends = cancelFailures = 0; progressCount = 2;
  terminalResult = static_cast<uint8_t>(mt::BankResult::Ok);
  sent = SampleEditReq{}; cancelToken = 0; expectedType=Msg::SampleEdit; tempoSent={}; tempoResponse={};
  responseFrames = 600; responseRate = 44100; responseRoot = 43;
  output.crc = 1; output.frames = 2; output.rate = 3; output.root = 4;
}
void tearDown() {}
static bool keepGoing(uint32_t, uint32_t, void*) { ++callbacks; return true; }
static bool cancel(uint32_t, uint32_t, void*) { ++callbacks; return false; }
static audio::BankResult extract(bool (*progress)(uint32_t, uint32_t, void*) = keepGoing) {
  return audio::extractSample(7, 123, 100, 700, true, output, progress, nullptr);
}
static void outputUntouched() {
  TEST_ASSERT_EQUAL(1, output.crc); TEST_ASSERT_EQUAL(2, output.frames);
  TEST_ASSERT_EQUAL(3, output.rate); TEST_ASSERT_EQUAL(4, output.root);
}
static void test_success_real_serialization_and_output() {
  TEST_ASSERT_EQUAL(mt::BankResult::Ok, extract());
  TEST_ASSERT_EQUAL(1, calls); TEST_ASSERT_TRUE(terminalSeen);
  TEST_ASSERT_NOT_EQUAL(0, sent.token); TEST_ASSERT_EQUAL(7, sent.index);
  TEST_ASSERT_EQUAL(123, sent.crc); TEST_ASSERT_EQUAL(100, sent.from);
  TEST_ASSERT_EQUAL(700, sent.to); TEST_ASSERT_EQUAL(1, sent.normalize);
  TEST_ASSERT_EQUAL(789, output.crc); TEST_ASSERT_EQUAL(600, output.frames);
  TEST_ASSERT_EQUAL(44100, output.rate); TEST_ASSERT_EQUAL(43, output.root);
  TEST_ASSERT_EQUAL(0, cancelSends);
}
static void test_busy_and_unsynced_never_send() {
  playing = true;
  TEST_ASSERT_EQUAL(mt::BankResult::Busy, extract());
  playing = false; synced = false;
  TEST_ASSERT_EQUAL(mt::BankResult::Link, extract());
  TEST_ASSERT_EQUAL(0, calls); outputUntouched();
}
static void test_bad_arguments_never_send() {
  TEST_ASSERT_EQUAL(mt::BankResult::OpenFail, audio::extractSample(-1, 0, 0, 1, false, output));
  TEST_ASSERT_EQUAL(mt::BankResult::OpenFail, audio::extractSample(128, 0, 0, 1, false, output));
  TEST_ASSERT_EQUAL(mt::BankResult::OpenFail, audio::extractSample(0, 0, 1, 1, false, output));
  TEST_ASSERT_EQUAL(0, calls); outputUntouched();
}
static void test_cancel_waits_terminal_even_if_late_ok() {
  TEST_ASSERT_EQUAL(mt::BankResult::Cancelled, extract(cancel));
  TEST_ASSERT_TRUE(terminalSeen); TEST_ASSERT_EQUAL(1, cancelSends);
  TEST_ASSERT_EQUAL(sent.token, cancelToken); outputUntouched();
}
static void test_cancel_queue_full_retries_on_next_progress() {
  cancelFailures = 1;
  TEST_ASSERT_EQUAL(mt::BankResult::Cancelled, extract(cancel));
  TEST_ASSERT_TRUE(terminalSeen); TEST_ASSERT_EQUAL(2, cancelSends); outputUntouched();
}
static void test_wrong_token_and_malformed_reply_are_rejected() {
  wrongToken = true;
  TEST_ASSERT_EQUAL(mt::BankResult::Link, extract()); outputUntouched();
  TEST_ASSERT_EQUAL(1, cancelSends); TEST_ASSERT_EQUAL(sent.token, cancelToken);
  wrongToken = false; truncated = true;
  TEST_ASSERT_EQUAL(mt::BankResult::Link, extract()); outputUntouched();
  TEST_ASSERT_EQUAL(2, cancelSends); TEST_ASSERT_EQUAL(sent.token, cancelToken);
}
static void test_terminal_cancel_and_unknown_result() {
  terminalResult = static_cast<uint8_t>(mt::BankResult::Cancelled);
  TEST_ASSERT_EQUAL(mt::BankResult::Cancelled, extract()); outputUntouched();
  TEST_ASSERT_EQUAL(0, cancelSends);  // a valid non-Ok terminal reply is already done
  terminalResult = 255;
  TEST_ASSERT_EQUAL(mt::BankResult::Link, extract()); outputUntouched();
}
static void test_rpc_timeout_does_not_publish_output() {
  rpcOk = false;
  TEST_ASSERT_EQUAL(mt::BankResult::Link, extract()); outputUntouched();
  TEST_ASSERT_EQUAL(1, cancelSends); TEST_ASSERT_EQUAL(sent.token, cancelToken);
}
static void test_late_cancel_without_progress_still_cannot_bind() {
  progressCount = 0;
  TEST_ASSERT_EQUAL(mt::BankResult::Cancelled, extract(cancel));
  TEST_ASSERT_TRUE(terminalSeen); TEST_ASSERT_EQUAL(1, callbacks); outputUntouched();
}
static void test_partial_or_invalid_output_never_published() {
  responseFrames = 599;
  TEST_ASSERT_EQUAL(mt::BankResult::ReadFail, extract()); outputUntouched();
  responseFrames = 600; responseRate = 0;
  TEST_ASSERT_EQUAL(mt::BankResult::Link, extract()); outputUntouched();
  responseRate = 48000;
  TEST_ASSERT_EQUAL(mt::BankResult::Link, extract()); outputUntouched();
  responseRate = 44100; responseRoot = 128;
  TEST_ASSERT_EQUAL(mt::BankResult::Link, extract()); outputUntouched();
}
static void test_normalize_off_and_unique_operation_tokens() {
  TEST_ASSERT_EQUAL(mt::BankResult::Ok, extract());
  const uint32_t old = sent.token;
  TEST_ASSERT_EQUAL(mt::BankResult::Ok, audio::extractSample(7, 123, 100, 700, false, output));
  TEST_ASSERT_EQUAL(0, sent.normalize); TEST_ASSERT_NOT_EQUAL(old, sent.token);
}
static void test_transport_rechecked_after_mirror_wait() {
  playAfterPump = true;
  TEST_ASSERT_EQUAL(mt::BankResult::Busy, extract());
  TEST_ASSERT_EQUAL(0, calls); outputUntouched();
}
static void test_stretch_duration_and_wire_mode() {
  responseFrames=900;
  TEST_ASSERT_EQUAL(mt::BankResult::Ok,audio::stretchSample(7,123,100,700,98304,output,keepGoing));
  TEST_ASSERT_EQUAL(1,sent.mode);TEST_ASSERT_EQUAL(0,sent.normalize);TEST_ASSERT_EQUAL(98304,sent.ratioQ16);
  TEST_ASSERT_EQUAL(900,output.frames);
}
static void test_stretch_partial_cancel_and_invalid_ratio() {
  TEST_ASSERT_EQUAL(mt::BankResult::ReadFail,audio::stretchSample(7,123,100,700,98304,output)); outputUntouched();
  TEST_ASSERT_EQUAL(mt::BankResult::Cancelled,audio::stretchSample(7,123,100,700,98304,output,cancel));outputUntouched();
  const int count=calls;
  TEST_ASSERT_EQUAL(mt::BankResult::OpenFail,audio::stretchSample(7,123,100,700,131073,output));
  TEST_ASSERT_EQUAL(count,calls);
}
static void test_tempo_success_unknown_and_typed_cancel() {
  expectedType=Msg::SampleTempo;mt::TempoEstimate out;out.bpm10=777;
  tempoResponse.bpm10=1200;tempoResponse.confidence=900;tempoResponse.halfBpm10=600;tempoResponse.doubleBpm10=2400;tempoResponse.flags=1;
  TEST_ASSERT_EQUAL(mt::BankResult::Ok,audio::detectSampleTempo(7,123,100,700,out,keepGoing));
  TEST_ASSERT_EQUAL(1200,out.bpm10);TEST_ASSERT_EQUAL(900,out.confidence);TEST_ASSERT_TRUE(out.ambiguous);
  TEST_ASSERT_EQUAL(7,tempoSent.index);TEST_ASSERT_EQUAL(123,tempoSent.crc);TEST_ASSERT_EQUAL(100,tempoSent.from);TEST_ASSERT_EQUAL(700,tempoSent.to);
  tempoResponse={};TEST_ASSERT_EQUAL(mt::BankResult::Ok,audio::detectSampleTempo(7,123,100,700,out));TEST_ASSERT_EQUAL(0,out.bpm10);
  out.bpm10=777;TEST_ASSERT_EQUAL(mt::BankResult::Cancelled,audio::detectSampleTempo(7,123,100,700,out,cancel));
  TEST_ASSERT_EQUAL(777,out.bpm10);TEST_ASSERT_EQUAL(tempoSent.token,cancelToken);
}
static void test_tempo_invalid_reply_busy_timeout_never_published() {
  expectedType=Msg::SampleTempo;mt::TempoEstimate out;out.bpm10=777;
  wrongToken=true;TEST_ASSERT_EQUAL(mt::BankResult::Link,audio::detectSampleTempo(7,123,100,700,out));TEST_ASSERT_EQUAL(777,out.bpm10);
  wrongToken=false;rpcOk=false;TEST_ASSERT_EQUAL(mt::BankResult::Link,audio::detectSampleTempo(7,123,100,700,out));TEST_ASSERT_EQUAL(777,out.bpm10);
  rpcOk=true;tempoResponse.confidence=1001;TEST_ASSERT_EQUAL(mt::BankResult::Link,audio::detectSampleTempo(7,123,100,700,out));TEST_ASSERT_EQUAL(777,out.bpm10);
  playing=true;TEST_ASSERT_EQUAL(mt::BankResult::Busy,audio::detectSampleTempo(7,123,100,700,out));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stretch_duration_and_wire_mode);
  RUN_TEST(test_stretch_partial_cancel_and_invalid_ratio);
  RUN_TEST(test_tempo_success_unknown_and_typed_cancel);
  RUN_TEST(test_tempo_invalid_reply_busy_timeout_never_published);
  RUN_TEST(test_success_real_serialization_and_output);
  RUN_TEST(test_busy_and_unsynced_never_send);
  RUN_TEST(test_bad_arguments_never_send);
  RUN_TEST(test_cancel_waits_terminal_even_if_late_ok);
  RUN_TEST(test_cancel_queue_full_retries_on_next_progress);
  RUN_TEST(test_wrong_token_and_malformed_reply_are_rejected);
  RUN_TEST(test_terminal_cancel_and_unknown_result);
  RUN_TEST(test_rpc_timeout_does_not_publish_output);
  RUN_TEST(test_late_cancel_without_progress_still_cannot_bind);
  RUN_TEST(test_partial_or_invalid_output_never_published);
  RUN_TEST(test_normalize_off_and_unique_operation_tokens);
  RUN_TEST(test_transport_rechecked_after_mirror_wait);
  return UNITY_END();
}
