#include <unity.h>
#include <initializer_list>
#include <string.h>
#include "link_msg.h"
#include "model.h"
#include "bank_ops.h"

using namespace mt::link;

void setUp() {}
void tearDown() {}

namespace {

uint8_t buf[kMaxPayload];

template <class M>
int put(const M& m) {
  Writer w(buf, kMaxPayload);
  encode(m, w);
  TEST_ASSERT_TRUE(w.ok());
  return w.size();
}

// Every proper prefix of the encoding must fail to decode.
template <class M>
void checkTruncated(int n) {
  for (int k = 0; k < n; ++k) {
    Reader r(buf, k);
    M m;
    TEST_ASSERT_FALSE_MESSAGE(decode(r, m), "truncated payload decoded");
  }
}

}  // namespace

void test_writer_reader_primitives() {
  Writer w(buf, kMaxPayload);
  w.u8(0xAB);
  w.u16(0x1234);
  w.u32(0xDEADBEEF);
  w.u64(0x0102030405060708ull);
  w.str("hi");
  TEST_ASSERT_EQUAL(1 + 2 + 4 + 8 + 3, w.size());
  TEST_ASSERT_EQUAL_HEX8(0x34, buf[1]);  // little-endian
  Reader r(buf, w.size());
  TEST_ASSERT_EQUAL_HEX8(0xAB, r.u8());
  TEST_ASSERT_EQUAL_HEX16(0x1234, r.u16());
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, r.u32());
  TEST_ASSERT_TRUE(r.u64() == 0x0102030405060708ull);
  char s[8];
  r.str(s, sizeof s);
  TEST_ASSERT_EQUAL_STRING("hi", s);
  TEST_ASSERT_TRUE(r.ok());
  r.u8();
  TEST_ASSERT_FALSE(r.ok());
}

void test_writer_overflow() {
  uint8_t small[3];
  Writer w(small, 3);
  w.u16(1);
  TEST_ASSERT_TRUE(w.ok());
  w.u16(2);
  TEST_ASSERT_FALSE(w.ok());
}

void test_hello() {
  Hello a;
  strcpy(a.fw, "1.2.3-abc");
  a.bootId = 0xCAFEF00D;
  a.modelSize = 12345;
  a.perfSec = 6;
  int n = put(a);
  Hello b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(kProtocol, b.proto);
  TEST_ASSERT_EQUAL_STRING("1.2.3-abc", b.fw);
  TEST_ASSERT_EQUAL_HEX32(0xCAFEF00D, b.bootId);
  TEST_ASSERT_EQUAL_UINT32(12345, b.modelSize);
  TEST_ASSERT_EQUAL_UINT8(6, b.perfSec);
  // Without perfSec (protocol 1 firmware) it still decodes: the version screen can offer the update.
  Hello old;
  Reader ro(buf, n - 1);
  TEST_ASSERT_TRUE(decode(ro, old));
  TEST_ASSERT_EQUAL_HEX32(0xCAFEF00D, old.bootId);
  TEST_ASSERT_EQUAL_UINT8(0, old.perfSec);
  checkTruncated<Hello>(n - 1);
}

void test_time() {
  Time a{0x123456789ABull};
  int n = put(a);
  Time b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_TRUE(b.tUs == a.tUs);
  checkTruncated<Time>(n);
}

void test_ev_batch_round_trip_out_of_order() {
  EvBatch a;
  a.n = 3;
  a.ev[0] = {5000000000ull, 2, 3, {0x90, 60, 100}};
  a.ev[1] = {4999990000ull, 15, 1, {0xF8, 0, 0}};
  a.ev[2] = {5000001000ull, 0, 2, {0xC0, 7, 0}};
  int n = put(a);
  EvBatch b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(3, b.n);
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(b.ev[i].tUs == a.ev[i].tUs);
    TEST_ASSERT_EQUAL(a.ev[i].track, b.ev[i].track);
    TEST_ASSERT_EQUAL(a.ev[i].len, b.ev[i].len);
    TEST_ASSERT_EQUAL_MEMORY(a.ev[i].b, b.ev[i].b, a.ev[i].len);
  }
  checkTruncated<EvBatch>(n);
}

void test_ev_batch_of_40_fits() {
  EvBatch a;
  a.n = EvBatch::kMax;
  for (int i = 0; i < a.n; ++i) a.ev[i] = {1000000ull + i * 25u, static_cast<uint8_t>(i % 17), 3, {0x90, 60, 127}};
  Writer w(buf, kMaxPayload);
  encode(a, w);
  TEST_ASSERT_TRUE(w.ok());
  TEST_ASSERT_TRUE(w.size() <= kMaxPayload);
  uint8_t out[kMaxEncoded];
  TEST_ASSERT_TRUE(frame(Msg::Ev, 0, a, out) > 0);
}

void test_state_set() {
  StateSet a;
  a.chunk = 17;
  a.total = 900;
  a.offset = 480;
  a.len = StateSet::kMaxData;
  for (int i = 0; i < a.len; ++i) a.data[i] = static_cast<uint8_t>(i);
  int n = put(a);
  TEST_ASSERT_TRUE(n <= kMaxPayload);
  StateSet b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(17, b.chunk);
  TEST_ASSERT_EQUAL(900, b.total);
  TEST_ASSERT_EQUAL(480, b.offset);
  TEST_ASSERT_EQUAL(a.len, b.len);
  TEST_ASSERT_EQUAL_MEMORY(a.data, b.data, a.len);
  checkTruncated<StateSet>(n);
}

void test_ack_nack() {
  int n = put(Ack{300});
  Ack a;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, a));
  TEST_ASSERT_EQUAL(300, a.id);
  checkTruncated<Ack>(n);
  n = put(Nack{5, -7});
  Nack b;
  Reader r2(buf, n);
  TEST_ASSERT_TRUE(decode(r2, b));
  TEST_ASSERT_EQUAL(5, b.id);
  TEST_ASSERT_EQUAL(-7, b.err);
  checkTruncated<Nack>(n);
}

void test_status_with_and_without_scope() {
  Status a;
  a.cpuPct = 42;
  a.stalls = 3;
  a.late = 9;
  a.voices = 31;
  a.outPeak = 32767;
  for (int i = 0; i < 16; ++i) a.trackPeak[i] = static_cast<uint8_t>(i * 10);
  a.lost = 2;
  a.streamVoices = 14;
  a.underruns = 3;
  a.minReserveMs = 41;
  a.psram = 1;
  a.psramFill = 57;
  a.gatePos = 63 * 256 + 128;
  for (int i = 0; i < 16; ++i) a.follower[i] = static_cast<uint8_t>(255 - i);
  a.stackFree = 70000;
  int n = put(a);
  Status b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(42, b.cpuPct);
  TEST_ASSERT_EQUAL(3, b.stalls);
  TEST_ASSERT_EQUAL(9, b.late);
  TEST_ASSERT_EQUAL(31, b.voices);
  TEST_ASSERT_EQUAL(32767, b.outPeak);
  TEST_ASSERT_EQUAL_MEMORY(a.trackPeak, b.trackPeak, 16);
  TEST_ASSERT_EQUAL(2, b.lost);
  TEST_ASSERT_EQUAL(0, b.scopeN);
  TEST_ASSERT_EQUAL(14, b.streamVoices);
  TEST_ASSERT_EQUAL(3, b.underruns);
  TEST_ASSERT_EQUAL(41, b.minReserveMs);
  TEST_ASSERT_EQUAL(1, b.psram);
  TEST_ASSERT_EQUAL(57, b.psramFill);
  TEST_ASSERT_EQUAL(63 * 256 + 128, b.gatePos);
  TEST_ASSERT_EQUAL_MEMORY(a.follower, b.follower, 16);
  TEST_ASSERT_EQUAL_UINT32(70000, b.stackFree);
  checkTruncated<Status>(n);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, Status::kStackUnknown);
  TEST_ASSERT_EQUAL_UINT32(Status::kStackUnknown, Status().stackFree);  // not measured by default

  a.scopeN = Status::kScopeMax;
  for (int i = 0; i < a.scopeN; ++i) a.scope[i] = static_cast<int8_t>(i - 128);
  n = put(a);
  TEST_ASSERT_TRUE(n <= kMaxPayload);
  Reader r2(buf, n);
  TEST_ASSERT_TRUE(decode(r2, b));
  TEST_ASSERT_EQUAL(Status::kScopeMax, b.scopeN);
  TEST_ASSERT_EQUAL_MEMORY(a.scope, b.scope, a.scopeN);
  TEST_ASSERT_EQUAL(41, b.minReserveMs);  // after the scope: still in place
  TEST_ASSERT_EQUAL(57, b.psramFill);
  TEST_ASSERT_EQUAL_MEMORY(a.follower, b.follower, 16);
  TEST_ASSERT_EQUAL_UINT32(70000, b.stackFree);
}

void test_debug() {
  Debug a;
  a.op = static_cast<uint8_t>(DebugOp::StreamBench);
  a.a = 12;
  a.b = kBenchSdOnly;
  a.secs = 600;
  const int n = put(a);
  Debug b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DebugOp::StreamBench), b.op);
  TEST_ASSERT_EQUAL(12, b.a);
  TEST_ASSERT_EQUAL(kBenchSdOnly, b.b);
  TEST_ASSERT_EQUAL(600, b.secs);
  checkTruncated<Debug>(n);
}

void test_protocol_and_debug_id() {
  TEST_ASSERT_EQUAL(28, kProtocol);  // 26: raw sample extraction, unchanged model layout.
                                     // 13: recording (Rec*); 14: one-shot (RenderStartReq::src, normalize 2);
                                     // 15: FM SAT (Instrument::fmSat); 16: CHIP SID / AY, SID filter, STEP;
                                     // 17: FsCopy; 18: REC -> SD (RecStartReq::toSd, RecLevelRep::left,
                                     // RecStopRep::ended, RecResult::CardFull); 19: KIT 16 lanes (SynthModel size);
                                     // 20: live monitor metronome (0xF9 accent level); 21: parameter-lock bypass
  TEST_ASSERT_EQUAL(41, static_cast<int>(Msg::Debug));  // after Perf (40)
  TEST_ASSERT_EQUAL(42, static_cast<int>(Msg::WtRender));
  TEST_ASSERT_EQUAL(43, static_cast<int>(Msg::RenderOut));
  TEST_ASSERT_EQUAL(48, static_cast<int>(Msg::RecCancel));
  TEST_ASSERT_EQUAL(49, static_cast<int>(Msg::FsCopy));
}

void test_wt_render() {
  WtRenderReq a;
  a.index = 77;
  a.from = 123456;
  a.to = 9876543;
  a.mode = 1;
  a.norm = 1;
  a.seam = 0;
  memset(a.path, 'x', sizeof a.path - 1);
  memcpy(a.path, "/wavetables/", 12);
  int n = put(a);
  WtRenderReq b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(77, b.index);
  TEST_ASSERT_EQUAL_UINT32(123456, b.from);
  TEST_ASSERT_EQUAL_UINT32(9876543, b.to);
  TEST_ASSERT_EQUAL(1, b.mode);
  TEST_ASSERT_EQUAL(1, b.norm);
  TEST_ASSERT_EQUAL(0, b.seam);
  TEST_ASSERT_EQUAL_STRING(a.path, b.path);
  checkTruncated<WtRenderReq>(n);
  a.mode = 2;  // no such mode
  n = put(a);
  Reader r2(buf, n);
  TEST_ASSERT_FALSE(decode(r2, b));

  WtRenderRep c;
  c.result = 5;
  c.err = 2;
  c.pitched = 58;
  n = put(c);
  WtRenderRep d;
  Reader r3(buf, n);
  TEST_ASSERT_TRUE(decode(r3, d));
  TEST_ASSERT_EQUAL(5, d.result);
  TEST_ASSERT_EQUAL(2, d.err);
  TEST_ASSERT_EQUAL(58, d.pitched);
  checkTruncated<WtRenderRep>(n);
}

void test_progress() {
  Progress a{3, 4096, 1u << 20};
  int n = put(a);
  Progress b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(3, b.op);
  TEST_ASSERT_EQUAL_UINT32(4096, b.done);
  TEST_ASSERT_EQUAL_UINT32(1u << 20, b.total);
  TEST_ASSERT_EQUAL(Progress::kNoItem, b.item);
  checkTruncated<Progress>(n);
  a.item = Progress::kWtItem + 2;
  n = put(a);
  Reader r2(buf, n);
  TEST_ASSERT_TRUE(decode(r2, b));
  TEST_ASSERT_EQUAL(Progress::kWtItem + 2, b.item);
}

void test_bank_messages() {
  BankProjectReq pq;
  strcpy(pq.project, "SONG_01");
  int n = put(pq);
  BankProjectReq pq2;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, pq2));
  TEST_ASSERT_EQUAL_STRING("SONG_01", pq2.project);

  BankSetsRep s;
  s.result = 9;
  s.missing = 2;
  s.failed = 1;
  maskSet(s.samples, 127);
  maskSet(s.wavetables, 31);
  n = put(s);
  BankSetsRep s2;
  Reader r2(buf, n);
  TEST_ASSERT_TRUE(decode(r2, s2));
  TEST_ASSERT_EQUAL(9, s2.result);
  TEST_ASSERT_EQUAL(2, s2.missing);
  TEST_ASSERT_EQUAL(1, s2.failed);
  TEST_ASSERT_TRUE(maskGet(s2.samples, 127));
  TEST_ASSERT_FALSE(maskGet(s2.samples, 126));
  TEST_ASSERT_TRUE(maskGet(s2.wavetables, 31));
  checkTruncated<BankSetsRep>(n);

  BankImportReq iq;
  iq.kind = 1;
  iq.hasCrc = 1;
  iq.crc = 0xDEADBEEF;
  strcpy(iq.path, "/samples/drums/kick 01.wav");
  n = put(iq);
  BankImportReq iq2;
  Reader r3(buf, n);
  TEST_ASSERT_TRUE(decode(r3, iq2));
  TEST_ASSERT_EQUAL(1, iq2.kind);
  TEST_ASSERT_EQUAL(1, iq2.hasCrc);
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, iq2.crc);
  TEST_ASSERT_EQUAL_STRING(iq.path, iq2.path);

  BankImportRep ip{0, 0x01020304, 12345, 44100, 48};
  n = put(ip);
  BankImportRep ip2;
  Reader r4(buf, n);
  TEST_ASSERT_TRUE(decode(r4, ip2));
  TEST_ASSERT_EQUAL_UINT32(0x01020304, ip2.crc);
  TEST_ASSERT_EQUAL_UINT32(12345, ip2.frames);
  TEST_ASSERT_EQUAL_UINT32(44100, ip2.rate);
  TEST_ASSERT_EQUAL(48, ip2.root);
  checkTruncated<BankImportRep>(n);

  SampleInfoRep si{0, 1, 999, 22050, 36, 1};
  n = put(si);
  SampleInfoRep si2;
  Reader r5(buf, n);
  TEST_ASSERT_TRUE(decode(r5, si2));
  TEST_ASSERT_EQUAL(1, si2.cached);
  TEST_ASSERT_EQUAL_UINT32(999, si2.frames);
  TEST_ASSERT_EQUAL_UINT32(22050, si2.rate);
  TEST_ASSERT_EQUAL(36, si2.root);
  TEST_ASSERT_EQUAL(1, si2.loop);
  checkTruncated<SampleInfoRep>(n);

  BankIndexRep bi;
  bi.count = 7;
  bi.capacity = 6000000;
  bi.free = 1234567;
  bi.unused = 4096;
  bi.gen = 42;
  bi.builtins = 0xFF;
  bi.n = 128;
  for (int i = 0; i < 128; ++i) bi.rate[i] = static_cast<uint16_t>(i * 300);
  maskSet(bi.samples, 5);
  n = put(bi);
  TEST_ASSERT_TRUE(n <= kMaxPayload);
  BankIndexRep bi2;
  Reader r6(buf, n);
  TEST_ASSERT_TRUE(decode(r6, bi2));
  TEST_ASSERT_EQUAL(7, bi2.count);
  TEST_ASSERT_EQUAL_UINT32(1234567, bi2.free);
  TEST_ASSERT_EQUAL_UINT32(42, bi2.gen);
  TEST_ASSERT_EQUAL(0xFF, bi2.builtins);
  TEST_ASSERT_TRUE(maskGet(bi2.samples, 5));
  TEST_ASSERT_EQUAL_UINT16(127 * 300, bi2.rate[127]);
  checkTruncated<BankIndexRep>(n);
  bi.n = 2;
  n = put(bi);
  Reader r7(buf, n);
  TEST_ASSERT_TRUE(decode(r7, bi2));
  TEST_ASSERT_EQUAL_UINT16(300, bi2.rate[1]);
  TEST_ASSERT_EQUAL_UINT16(0, bi2.rate[2]);

  BankClearRep cr{0, 17};
  n = put(cr);
  BankClearRep cr2;
  Reader r8(buf, n);
  TEST_ASSERT_TRUE(decode(r8, cr2));
  TEST_ASSERT_EQUAL(17, cr2.removed);

  WtFrameReq wq;
  wq.frame = 63;
  strcpy(wq.name, "*SAWSQR");
  n = put(wq);
  WtFrameReq wq2;
  Reader r9(buf, n);
  TEST_ASSERT_TRUE(decode(r9, wq2));
  TEST_ASSERT_EQUAL(63, wq2.frame);
  TEST_ASSERT_EQUAL_STRING("*SAWSQR", wq2.name);

  WtFrameRep wr;
  for (int i = 0; i < WtFrameRep::kPoints; ++i) wr.pts[i] = static_cast<int8_t>(i - 128);
  n = put(wr);
  WtFrameRep wr2;
  Reader r10(buf, n);
  TEST_ASSERT_TRUE(decode(r10, wr2));
  TEST_ASSERT_EQUAL_INT8_ARRAY(wr.pts, wr2.pts, WtFrameRep::kPoints);
  checkTruncated<WtFrameRep>(n);
}

void test_editor_and_preview_messages() {
  WavePeaksReq pq{7, 123456, 9876543, 480, 240};
  int n = put(pq);
  WavePeaksReq pq2;
  Reader r1(buf, n);
  TEST_ASSERT_TRUE(decode(r1, pq2));
  TEST_ASSERT_EQUAL(7, pq2.index);
  TEST_ASSERT_EQUAL_UINT32(123456, pq2.col0);
  TEST_ASSERT_EQUAL_UINT32(9876543, pq2.span);
  TEST_ASSERT_EQUAL(480, pq2.width);
  TEST_ASSERT_EQUAL(240, pq2.cols);
  checkTruncated<WavePeaksReq>(n);

  static WavePeaksRep pr;  // a full one: 482 bytes
  pr.cols = WavePeaksRep::kMaxCols;
  for (int i = 0; i < pr.cols; ++i) {
    pr.mn[i] = static_cast<int8_t>(-i / 2);
    pr.mx[i] = static_cast<int8_t>(i / 2);
  }
  n = put(pr);
  TEST_ASSERT_EQUAL(2 + 2 * WavePeaksRep::kMaxCols, n);
  static WavePeaksRep pr2;
  Reader r2(buf, n);
  TEST_ASSERT_TRUE(decode(r2, pr2));
  TEST_ASSERT_EQUAL(pr.cols, pr2.cols);
  TEST_ASSERT_EQUAL_INT8_ARRAY(pr.mn, pr2.mn, pr.cols);
  TEST_ASSERT_EQUAL_INT8_ARRAY(pr.mx, pr2.mx, pr.cols);
  checkTruncated<WavePeaksRep>(n);
  // A short one only carries its columns; too many columns is malformed.
  pr.cols = 3;
  TEST_ASSERT_EQUAL(8, put(pr));
  buf[1] = WavePeaksRep::kMaxCols + 1;
  Reader r3(buf, 2 + 2 * 241);
  TEST_ASSERT_FALSE(decode(r3, pr2));

  OnsetsReq oq{9, 80};
  n = put(oq);
  OnsetsReq oq2;
  Reader r4(buf, n);
  TEST_ASSERT_TRUE(decode(r4, oq2));
  TEST_ASSERT_EQUAL(9, oq2.index);
  TEST_ASSERT_EQUAL(80, oq2.skip);

  static OnsetsRep orp;
  orp.total = 128;
  orp.n = OnsetsRep::kMax;
  for (int i = 0; i < orp.n; ++i) {
    orp.pos[i] = 100000u * i + 7;
    orp.strength[i] = static_cast<uint16_t>(1000 - i);
  }
  n = put(orp);
  TEST_ASSERT_EQUAL(3 + 6 * OnsetsRep::kMax, n);
  static OnsetsRep orp2;
  Reader r5(buf, n);
  TEST_ASSERT_TRUE(decode(r5, orp2));
  TEST_ASSERT_EQUAL(128, orp2.total);
  TEST_ASSERT_EQUAL(OnsetsRep::kMax, orp2.n);
  TEST_ASSERT_EQUAL_UINT32_ARRAY(orp.pos, orp2.pos, orp.n);
  TEST_ASSERT_EQUAL_UINT16_ARRAY(orp.strength, orp2.strength, orp.n);
  checkTruncated<OnsetsRep>(n);

  PreviewFileReq fq;
  strcpy(fq.path, "/samples/drums/long loop 120.wav");
  n = put(fq);
  PreviewFileReq fq2;
  Reader r6(buf, n);
  TEST_ASSERT_TRUE(decode(r6, fq2));
  TEST_ASSERT_EQUAL_STRING(fq.path, fq2.path);
  checkTruncated<PreviewFileReq>(n);

  PreviewFileRep fr{0, 1323000, 48000};
  n = put(fr);
  PreviewFileRep fr2;
  Reader r7(buf, n);
  TEST_ASSERT_TRUE(decode(r7, fr2));
  TEST_ASSERT_EQUAL_UINT32(1323000, fr2.frames);
  TEST_ASSERT_EQUAL_UINT32(48000, fr2.rate);
  checkTruncated<PreviewFileRep>(n);
}

void test_profile_rep() {
  ProfileRep a;
  a.running = 1;
  a.blocks = 3445;
  a.cyclesPerUs = 600;
  for (int i = 0; i < ProfileRep::kStages; ++i) a.cycles[i] = 5000000000ull * (i + 1) + 7;
  int n = put(a);
  ProfileRep b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(1, b.running);
  TEST_ASSERT_EQUAL_UINT32(3445, b.blocks);
  TEST_ASSERT_EQUAL_UINT32(600, b.cyclesPerUs);
  TEST_ASSERT_EQUAL_MEMORY(a.cycles, b.cycles, sizeof a.cycles);
  checkTruncated<ProfileRep>(n);
}

void test_profile_rep_24_stages() {
  TEST_ASSERT_EQUAL(25, ProfileRep::kStages);  // + STRING, LOFI
  ProfileRep a;
  for (int i = 0; i < ProfileRep::kStages; ++i) {
    a.cycles[i] = 1000ull * (i + 1);
    a.count[i] = static_cast<uint32_t>(i + 100);
  }
  int n = put(a);
  ProfileRep b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL_UINT64(24000, b.cycles[23]);
  TEST_ASSERT_EQUAL_UINT32(123, b.count[23]);
  TEST_ASSERT_EQUAL_MEMORY(a.count, b.count, sizeof a.count);
}

void test_log() {
  Log a;
  strcpy(a.text, "bank: 12 samples, 3.1 MB free");
  int n = put(a);
  Log b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL_STRING(a.text, b.text);
  checkTruncated<Log>(n);
}

void test_small_messages() {
  int n = put(Byte{77});
  Byte b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(77, b.v);
  checkTruncated<Byte>(n);

  n = put(PreviewNote{5, 61, 1234, 7});
  PreviewNote pn;
  Reader r2(buf, n);
  TEST_ASSERT_TRUE(decode(r2, pn));
  TEST_ASSERT_EQUAL(5, pn.instr);
  TEST_ASSERT_EQUAL(61, pn.note);
  TEST_ASSERT_EQUAL_UINT32(1234, pn.holdMs);
  TEST_ASSERT_EQUAL(7, pn.track);
  checkTruncated<PreviewNote>(n);

  n = put(PreviewSlice{3, 9, 48, 25000});
  PreviewSlice ps;
  Reader r3(buf, n);
  TEST_ASSERT_TRUE(decode(r3, ps));
  TEST_ASSERT_EQUAL(3, ps.instr);
  TEST_ASSERT_EQUAL(9, ps.slice);
  TEST_ASSERT_EQUAL(48, ps.root);
  TEST_ASSERT_EQUAL_UINT32(25000, ps.holdMs);
  checkTruncated<PreviewSlice>(n);
}

void test_frame_round_trip() {
  Time t{777};
  uint8_t out[kMaxEncoded];
  int n = frame(Msg::Time, 9, t, out);
  Decoder d;
  bool got = false;
  for (int i = 0; i < n; ++i) got = d.feed(out[i]);
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(static_cast<int>(Msg::Time), d.type());
  TEST_ASSERT_EQUAL(9, d.seq());
  Reader r(d.payload(), d.size());
  Time b;
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_TRUE(b.tUs == 777);
}

void test_msg_numbers_stable() {
  TEST_ASSERT_EQUAL(1, static_cast<int>(Msg::Hello));
  TEST_ASSERT_EQUAL(4, static_cast<int>(Msg::StateSet));
  TEST_ASSERT_EQUAL(34, static_cast<int>(Msg::Status));
  TEST_ASSERT_EQUAL(37, static_cast<int>(Msg::Log));
  TEST_ASSERT_EQUAL(38, static_cast<int>(Msg::FsRmdir));
  TEST_ASSERT_EQUAL(39, static_cast<int>(Msg::WtFrame));
  TEST_ASSERT_EQUAL(22, static_cast<int>(Msg::WavePeaks));
  TEST_ASSERT_EQUAL(23, static_cast<int>(Msg::Onsets));
  TEST_ASSERT_EQUAL(26, static_cast<int>(Msg::PreviewFile));
  TEST_ASSERT_EQUAL(27, static_cast<int>(Msg::PreviewStop));
}

void test_render_messages() {
  RenderStartReq q;
  q.target = RenderStartReq::kBank;
  strcpy(q.path, "/samples/render/DEMO_P01.wav");
  int n = put(q);
  RenderStartReq q2;
  Reader r1(buf, n);
  TEST_ASSERT_TRUE(decode(r1, q2));
  TEST_ASSERT_EQUAL(RenderStartReq::kBank, q2.target);
  TEST_ASSERT_EQUAL_STRING(q.path, q2.path);
  checkTruncated<RenderStartReq>(n);

  RenderBlocks b;
  b.first = 70000;
  b.n = 3;
  b.evN[0] = 2;
  b.evN[1] = 0;
  b.evN[2] = 1 | RenderBlocks::kCont;
  b.ev[0] = {0, 3, 3, {0x90, 60, 100}};
  b.ev[1] = {127, 15, 2, {0xC0, 5, 0}};
  b.ev[2] = {64, 0, 1, {0xFF, 0, 0}};
  n = put(b);
  RenderBlocks b2;
  Reader r2(buf, n);
  TEST_ASSERT_TRUE(decode(r2, b2));
  TEST_ASSERT_EQUAL_UINT32(70000, b2.first);
  TEST_ASSERT_EQUAL(3, b2.n);
  TEST_ASSERT_EQUAL(1 | RenderBlocks::kCont, b2.evN[2]);
  TEST_ASSERT_EQUAL(127, b2.ev[1].off);
  TEST_ASSERT_EQUAL(15, b2.ev[1].track);
  TEST_ASSERT_EQUAL(2, b2.ev[1].len);
  TEST_ASSERT_EQUAL(5, b2.ev[1].b[1]);
  TEST_ASSERT_EQUAL(100, b2.ev[0].b[2]);
  TEST_ASSERT_EQUAL(0xFF, b2.ev[2].b[0]);
  checkTruncated<RenderBlocks>(n);

  RenderBlocks full;  // the largest frame fits a payload
  full.n = RenderBlocks::kMaxBlocks;
  for (int i = 0; i < full.n; ++i) full.evN[i] = RenderBlocks::kMaxEvents / RenderBlocks::kMaxBlocks;
  for (RenderEv& e : full.ev) e = {1, 2, 3, {0x90, 1, 2}};
  put(full);

  b.evN[0] |= RenderBlocks::kCont;  // only the last record may go on
  n = put(b);
  Reader r3(buf, n);
  TEST_ASSERT_FALSE(decode(r3, b2));

  RenderEndReq e{0, 1, 1};
  n = put(e);
  RenderEndReq e2;
  Reader r4(buf, n);
  TEST_ASSERT_TRUE(decode(r4, e2));
  TEST_ASSERT_EQUAL(1, e2.normalize);
  TEST_ASSERT_EQUAL(1, e2.trim);
  checkTruncated<RenderEndReq>(n);

  RenderRep p;
  p.result = static_cast<uint8_t>(RenderResult::Bank);
  p.bank = 9;
  p.blocks = 1234;
  p.frames = 157952;
  p.peak = 30000;
  p.clips = 7;
  p.crc = 0xDEADBEEF;
  n = put(p);
  RenderRep p2;
  Reader r5(buf, n);
  TEST_ASSERT_TRUE(decode(r5, p2));
  TEST_ASSERT_EQUAL(9, p2.bank);
  TEST_ASSERT_EQUAL_UINT32(1234, p2.blocks);
  TEST_ASSERT_EQUAL_UINT32(157952, p2.frames);
  TEST_ASSERT_EQUAL(30000, p2.peak);
  TEST_ASSERT_EQUAL_UINT32(7, p2.clips);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, p2.crc);
  checkTruncated<RenderRep>(n);
}

void test_export_messages() {
  RenderStartReq q;
  q.target = RenderStartReq::kExport;
  strcpy(q.path, "/export/DEMO/P01x4");
  q.bits = 16;
  q.outs = 20;
  q.skipFrames = 88215;
  q.estFrames = 1234567;
  int n = put(q);
  RenderStartReq q2;
  Reader r1(buf, n);
  TEST_ASSERT_TRUE(decode(r1, q2));
  TEST_ASSERT_EQUAL(RenderStartReq::kExport, q2.target);
  TEST_ASSERT_EQUAL_STRING(q.path, q2.path);
  TEST_ASSERT_EQUAL(16, q2.bits);
  TEST_ASSERT_EQUAL(20, q2.outs);
  TEST_ASSERT_EQUAL_UINT32(88215, q2.skipFrames);
  TEST_ASSERT_EQUAL_UINT32(1234567, q2.estFrames);
  checkTruncated<RenderStartReq>(n);
  q.target = RenderStartReq::kExport + 1;
  n = put(q);
  Reader r1b(buf, n);
  TEST_ASSERT_FALSE(decode(r1b, q2));

  RenderOut o;
  o.idx = 19;
  o.tap = 19;
  memset(o.name, 'A', sizeof o.name - 1);
  n = put(o);
  RenderOut o2;
  Reader r2(buf, n);
  TEST_ASSERT_TRUE(decode(r2, o2));
  TEST_ASSERT_EQUAL(19, o2.idx);
  TEST_ASSERT_EQUAL(19, o2.tap);
  TEST_ASSERT_EQUAL(63, static_cast<int>(strlen(o2.name)));
  checkTruncated<RenderOut>(n);
  o.tap = 20;  // no such tap
  n = put(o);
  Reader r2b(buf, n);
  TEST_ASSERT_FALSE(decode(r2b, o2));

  RenderEndReq e;
  e.frames = 4000000;
  n = put(e);
  RenderEndReq e2;
  Reader r3(buf, n);
  TEST_ASSERT_TRUE(decode(r3, e2));
  TEST_ASSERT_EQUAL_UINT32(4000000, e2.frames);

  RenderRep p;  // the largest reply fits a payload
  p.quiet = 345;
  p.outMax = 6;
  p.outN = RenderRep::kMaxOut;
  for (int i = 0; i < RenderRep::kMaxOut; ++i) {
    p.peaks[i] = static_cast<uint16_t>(1000 + i);
    p.clipsN[i] = 70000u + i;
  }
  n = put(p);
  RenderRep p2;
  Reader r4(buf, n);
  TEST_ASSERT_TRUE(decode(r4, p2));
  TEST_ASSERT_EQUAL_UINT32(345, p2.quiet);
  TEST_ASSERT_EQUAL(6, p2.outMax);
  TEST_ASSERT_EQUAL(RenderRep::kMaxOut, p2.outN);
  TEST_ASSERT_EQUAL(1019, p2.peaks[19]);
  TEST_ASSERT_EQUAL_UINT32(70019u, p2.clipsN[19]);
  checkTruncated<RenderRep>(n);
  buf[n - 4 * RenderRep::kMaxOut - 2 * RenderRep::kMaxOut - 1] = RenderRep::kMaxOut + 1;  // outN
  Reader r5(buf, n);
  TEST_ASSERT_FALSE(decode(r5, p2));
}

void test_fw_messages() {
  FwFromFileReq q;
  strcpy(q.path, "/firmware/teensy.hex");
  int n = put(q);
  FwFromFileReq q2;
  Reader r1(buf, n);
  TEST_ASSERT_TRUE(decode(r1, q2));
  TEST_ASSERT_EQUAL_STRING(q.path, q2.path);
  checkTruncated<FwFromFileReq>(n);
  FwFromFileReq empty;
  n = put(empty);
  Reader r2(buf, n);
  TEST_ASSERT_FALSE(decode(r2, q2));

  FwRep p{static_cast<uint8_t>(FwResult::BadImage), 812345};
  n = put(p);
  FwRep p2;
  Reader r3(buf, n);
  TEST_ASSERT_TRUE(decode(r3, p2));
  TEST_ASSERT_EQUAL(static_cast<int>(FwResult::BadImage), p2.result);
  TEST_ASSERT_EQUAL_UINT32(812345, p2.bytes);
  checkTruncated<FwRep>(n);
}

void test_ev_batch_carries_a_hint() {
  EvBatch a;
  a.n = 2;
  a.ev[0] = {1000, 2, 3, {mt::kSynthHint, 62, 0x40}};
  a.ev[1] = {126000, 2, 3, {0x92, 62, 100}};
  const int n = put(a);
  EvBatch b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(2, b.n);
  TEST_ASSERT_EQUAL_UINT64(1000, b.ev[0].tUs);
  TEST_ASSERT_EQUAL_UINT8(mt::kSynthHint, b.ev[0].b[0]);
  TEST_ASSERT_EQUAL_UINT8(0x40, b.ev[0].b[2]);
  TEST_ASSERT_EQUAL_UINT64(126000, b.ev[1].tUs);
}

void test_rec_msgs_roundtrip() {
  RecStartReq a;
  a.source = 2;
  a.gain = -3;
  a.thresh = -40;
  a.mon = 1;
  a.number = 4321;
  int n = put(a);
  RecStartReq a2;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, a2));
  TEST_ASSERT_EQUAL(2, a2.source);
  TEST_ASSERT_EQUAL(-3, a2.gain);
  TEST_ASSERT_EQUAL(-40, a2.thresh);
  TEST_ASSERT_EQUAL(1, a2.mon);
  TEST_ASSERT_EQUAL(4321, a2.number);
  TEST_ASSERT_EQUAL(0, a2.toSd);
  checkTruncated<RecStartReq>(n - 1);  // toSd may be left out (an older ESP): 0
  a.toSd = 1;
  n = put(a);
  TEST_ASSERT_EQUAL(7, n);
  Reader ra(buf, n);
  TEST_ASSERT_TRUE(decode(ra, a2));
  TEST_ASSERT_EQUAL(1, a2.toSd);
  TEST_ASSERT_EQUAL(4321, a2.number);
  Reader ra6(buf, n - 1);
  RecStartReq a3;
  a3.toSd = 1;
  TEST_ASSERT_TRUE(decode(ra6, a3));
  TEST_ASSERT_EQUAL(0, a3.toSd);

  RecStartRep b;
  b.result = static_cast<uint8_t>(RecResult::NoPsram);
  b.number = 65000;
  n = put(b);
  RecStartRep b2;
  Reader rb(buf, n);
  TEST_ASSERT_TRUE(decode(rb, b2));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(RecResult::NoPsram), b2.result);
  TEST_ASSERT_EQUAL(65000, b2.number);
  checkTruncated<RecStartRep>(n);

  RecSetReq c;
  c.gain = 47;
  c.mon = 1;
  c.source = 2;
  n = put(c);
  RecSetReq c2;
  Reader rc(buf, n);
  TEST_ASSERT_TRUE(decode(rc, c2));
  TEST_ASSERT_EQUAL(47, c2.gain);
  TEST_ASSERT_EQUAL(1, c2.mon);
  TEST_ASSERT_EQUAL(2, c2.source);
  checkTruncated<RecSetReq>(n);

  RecLevelRep d;
  d.result = static_cast<uint8_t>(RecResult::CardSlow);
  d.state = 2;
  d.peak = 32767;
  d.clips = 0x01020304;
  d.frames = 13230000;
  d.codec = 1;
  d.left = 0xF0E0D0C0;
  n = put(d);
  RecLevelRep d2;
  Reader rd(buf, n);
  TEST_ASSERT_TRUE(decode(rd, d2));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(RecResult::CardSlow), d2.result);
  TEST_ASSERT_EQUAL(2, d2.state);
  TEST_ASSERT_EQUAL(32767, d2.peak);
  TEST_ASSERT_EQUAL_HEX32(0x01020304, d2.clips);
  TEST_ASSERT_EQUAL(13230000, d2.frames);
  TEST_ASSERT_EQUAL(1, d2.codec);
  TEST_ASSERT_EQUAL_HEX32(0xF0E0D0C0, d2.left);
  checkTruncated<RecLevelRep>(n - 4);
  for (int k = n - 3; k < n; ++k) {  // a cut field: no
    Reader rk(buf, k);
    RecLevelRep dk;
    TEST_ASSERT_FALSE(decode(rk, dk));
  }
  Reader rd0(buf, n - 4);  // no left at all: 0
  RecLevelRep d3;
  d3.left = 5;
  TEST_ASSERT_TRUE(decode(rd0, d3));
  TEST_ASSERT_EQUAL_UINT32(0, d3.left);

  RecStopRep e;
  e.result = static_cast<uint8_t>(RecResult::Bank);
  e.bank = 3;
  e.number = 12;
  e.crc = 0xDEADBEEF;
  e.frames = 547200;
  e.peak = 20000;
  e.clips = 7;
  e.ended = static_cast<uint8_t>(RecEnd::Space);
  n = put(e);
  RecStopRep e2;
  Reader re(buf, n);
  TEST_ASSERT_TRUE(decode(re, e2));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(RecResult::Bank), e2.result);
  TEST_ASSERT_EQUAL(3, e2.bank);
  TEST_ASSERT_EQUAL(12, e2.number);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, e2.crc);
  TEST_ASSERT_EQUAL(547200, e2.frames);
  TEST_ASSERT_EQUAL(20000, e2.peak);
  TEST_ASSERT_EQUAL(7, e2.clips);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(RecEnd::Space), e2.ended);
  checkTruncated<RecStopRep>(n - 1);
  Reader re0(buf, n - 1);
  RecStopRep e3;
  e3.ended = 2;
  TEST_ASSERT_TRUE(decode(re0, e3));
  TEST_ASSERT_EQUAL(0, e3.ended);
}

void test_msg_rec_ids() {
  TEST_ASSERT_EQUAL(static_cast<int>(Msg::RenderOut) + 1, static_cast<int>(Msg::RecStart));
  TEST_ASSERT_EQUAL(static_cast<int>(Msg::RecStart) + 1, static_cast<int>(Msg::RecSet));
  TEST_ASSERT_EQUAL(static_cast<int>(Msg::RecSet) + 1, static_cast<int>(Msg::RecLevel));
  TEST_ASSERT_EQUAL(static_cast<int>(Msg::RecLevel) + 1, static_cast<int>(Msg::RecStop));
  TEST_ASSERT_EQUAL(static_cast<int>(Msg::RecStop) + 1, static_cast<int>(Msg::RecCancel));
  TEST_ASSERT_EQUAL(48, static_cast<int>(Msg::RecCancel));
  TEST_ASSERT_EQUAL(static_cast<int>(RecResult::BadArg) + 1, static_cast<int>(RecResult::CardFull));
  TEST_ASSERT_EQUAL(0, static_cast<int>(RecEnd::Stop));
  TEST_ASSERT_EQUAL(1, static_cast<int>(RecEnd::Length));
  TEST_ASSERT_EQUAL(2, static_cast<int>(RecEnd::Space));
}

// 14: the one-shot source (the preview track's tap), bank only; normalize 2 = -0.3 dBFS.
void test_render_oneshot_fields() {
  RenderStartReq q;
  q.target = RenderStartReq::kBank;
  q.src = RenderStartReq::kSrcPreview;
  int n = put(q);
  RenderStartReq q2;
  Reader r1(buf, n);
  TEST_ASSERT_TRUE(decode(r1, q2));
  TEST_ASSERT_EQUAL(RenderStartReq::kSrcPreview, q2.src);
  TEST_ASSERT_EQUAL(RenderStartReq::kBank, q2.target);
  checkTruncated<RenderStartReq>(n);
  q.src = RenderStartReq::kSrcPreview + 1;
  n = put(q);
  Reader r2(buf, n);
  TEST_ASSERT_FALSE(decode(r2, q2));
  q.src = RenderStartReq::kSrcPreview;
  q.target = RenderStartReq::kFile;  // the preview source: bank only
  n = put(q);
  Reader r3(buf, n);
  TEST_ASSERT_FALSE(decode(r3, q2));
  q.target = RenderStartReq::kExport;
  n = put(q);
  Reader r4(buf, n);
  TEST_ASSERT_FALSE(decode(r4, q2));

  RenderEndReq e{0, 2, 1};
  n = put(e);
  RenderEndReq e2;
  Reader r5(buf, n);
  TEST_ASSERT_TRUE(decode(r5, e2));
  TEST_ASSERT_EQUAL(2, e2.normalize);
  e.normalize = 3;
  n = put(e);
  Reader r6(buf, n);
  TEST_ASSERT_FALSE(decode(r6, e2));
}

void test_wt_editor_transport() {
  WtFrame16Req q; strcpy(q.name, "*BASIC"); q.frame = 63; q.half = 1;
  int n = put(q); checkTruncated<WtFrame16Req>(n);
  WtFrame16Req q2; Reader r(buf, n); TEST_ASSERT_TRUE(decode(r, q2));
  TEST_ASSERT_EQUAL_STRING(q.name, q2.name); TEST_ASSERT_EQUAL(63, q2.frame); TEST_ASSERT_EQUAL(1,q2.half);
  q.half = 2; n = put(q); Reader bad(buf,n); TEST_ASSERT_FALSE(decode(bad,q2));
  WtFrame16Rep rep; for(int i=0;i<128;++i) rep.pts[i] = static_cast<int16_t>(i*503-32768);
  n=put(rep); TEST_ASSERT_EQUAL(257,n); checkTruncated<WtFrame16Rep>(n);
  WtFrame16Rep rep2; Reader rr(buf,n); TEST_ASSERT_TRUE(decode(rr,rep2));
  TEST_ASSERT_EQUAL_INT16_ARRAY(rep.pts,rep2.pts,128);
  WtAuditionReq a; a.token=65535; a.action=WtAuditionAction::Part1; memcpy(a.pts,rep.pts,sizeof a.pts);
  n=put(a); TEST_ASSERT_EQUAL(259,n); checkTruncated<WtAuditionReq>(n);
  WtAuditionReq a2; Reader ar(buf,n); TEST_ASSERT_TRUE(decode(ar,a2));
  TEST_ASSERT_EQUAL(65535,a2.token); TEST_ASSERT_EQUAL_INT16_ARRAY(a.pts,a2.pts,128);
  a.action=WtAuditionAction::Stop; n=put(a); TEST_ASSERT_EQUAL(3,n); checkTruncated<WtAuditionReq>(n);
  a.action=static_cast<WtAuditionAction>(3); n=put(a); Reader ab(buf,n); TEST_ASSERT_FALSE(decode(ab,a2));
  TEST_ASSERT_EQUAL(50,static_cast<int>(Msg::WtFrame16)); TEST_ASSERT_EQUAL(51,static_cast<int>(Msg::WtAudition));
}

void test_sample_edit_wire_contract() {
  SampleEditReq q{0x12345678u, 7, 0x98765432u, 1023, 8193, 1};
  int n=put(q); TEST_ASSERT_EQUAL(23,n); checkTruncated<SampleEditReq>(n);
  const uint8_t golden[]={0x78,0x56,0x34,0x12,7,0x32,0x54,0x76,0x98,0xff,3,0,0,1,0x20,0,0,1,0,0,0,1,0};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(golden,buf,n);
  SampleEditReq got; Reader r(buf,n); TEST_ASSERT_TRUE(decode(r,got));
  TEST_ASSERT_EQUAL_UINT32(q.token,got.token); TEST_ASSERT_EQUAL(q.index,got.index);
  TEST_ASSERT_EQUAL_UINT32(q.crc,got.crc); TEST_ASSERT_EQUAL_UINT32(q.from,got.from);
  TEST_ASSERT_EQUAL_UINT32(q.to,got.to); TEST_ASSERT_EQUAL(q.normalize,got.normalize);
  SampleEditRep a{q.token,0,0xaabbccddu,7170,22050,48};
  n=put(a); TEST_ASSERT_EQUAL(18,n); checkTruncated<SampleEditRep>(n);
  SampleEditRep b; Reader rr(buf,n); TEST_ASSERT_TRUE(decode(rr,b));
  TEST_ASSERT_EQUAL_UINT32(a.token,b.token); TEST_ASSERT_EQUAL_UINT32(a.crc,b.crc);
  TEST_ASSERT_EQUAL_UINT32(a.frames,b.frames); TEST_ASSERT_EQUAL_UINT32(a.rate,b.rate); TEST_ASSERT_EQUAL(48,b.root);
  SampleEditCancelReq c{q.token}; n=put(c); TEST_ASSERT_EQUAL(4,n); checkTruncated<SampleEditCancelReq>(n);
  SampleEditCancelReq cc; Reader cr(buf,n); TEST_ASSERT_TRUE(decode(cr,cc)); TEST_ASSERT_EQUAL_UINT32(c.token,cc.token);
  TEST_ASSERT_EQUAL(52,static_cast<int>(Msg::SampleEdit)); TEST_ASSERT_EQUAL(53,static_cast<int>(Msg::SampleEditCancel));
}

void test_sample_edit_rejects_invalid_identity_flags_range_and_trailing() {
  SampleEditReq q{9,0,0x1234,100,200,0}, out;
  auto bad=[&]() { const int n=put(q); Reader r(buf,n); TEST_ASSERT_FALSE(decode(r,out)); };
  q.token=0; bad(); q.token=9; q.index=128; bad(); q.index=0;
  q.normalize=2; bad(); q.normalize=0; q.to=100; bad(); q.to=99; bad(); q.to=200;
  int n=put(q); buf[n]=0; Reader extra(buf,n+1); TEST_ASSERT_FALSE(decode(extra,out));
  SampleEditCancelReq c{0}, co; n=put(c); Reader zero(buf,n); TEST_ASSERT_FALSE(decode(zero,co));
  c.token=9;n=put(c);buf[n]=0;Reader cx(buf,n+1);TEST_ASSERT_FALSE(decode(cx,co));
}

void test_sample_edit_reply_rejects_invalid_success_and_accepts_cancellation() {
  SampleEditRep q{9,0,1234,10,44100,60}, out;
  auto bad=[&]() { const int n=put(q); Reader r(buf,n); TEST_ASSERT_FALSE(decode(r,out)); };
  q.rate=0;bad();q.rate=44100;q.frames=0;bad();q.frames=10;q.root=128;bad();q.root=60;
  q.token=0;bad();q.token=9;q.result=255;bad();
  q.result=static_cast<uint8_t>(mt::BankResult::Running);bad();
  q.result=static_cast<uint8_t>(mt::BankResult::Cancelled);q.crc=q.frames=q.rate=0; const int n=put(q);Reader r(buf,n);
  TEST_ASSERT_TRUE(decode(r,out));TEST_ASSERT_EQUAL(static_cast<uint8_t>(mt::BankResult::Cancelled),out.result);TEST_ASSERT_EQUAL_UINT32(9,out.token);
  buf[n]=0; Reader extra(buf,n+1);TEST_ASSERT_FALSE(decode(extra,out));
}

void test_sample_stretch_wire_ratio_boundaries_and_invalid_mode_combinations() {
  SampleEditReq q{11,127,0,1,UINT32_MAX,0}, out;
  q.mode=1;
  for(uint32_t ratio:{32768u,65536u,131072u}) {
    q.ratioQ16=ratio; const int n=put(q); TEST_ASSERT_EQUAL(23,n);
    Reader r(buf,n); TEST_ASSERT_TRUE(decode(r,out));
    TEST_ASSERT_EQUAL_UINT8(1,out.mode); TEST_ASSERT_EQUAL_UINT32(ratio,out.ratioQ16);
  }
  auto bad=[&]() { const int n=put(q); Reader r(buf,n); TEST_ASSERT_FALSE(decode(r,out)); };
  q.ratioQ16=32767;bad(); q.ratioQ16=131073;bad(); q.ratioQ16=65536;
  q.normalize=1;bad();q.normalize=0;q.mode=2;bad();q.mode=0;q.ratioQ16=98304;bad();
  q.ratioQ16=65536;const int n=put(q);Reader old18(buf,18);TEST_ASSERT_FALSE(decode(old18,out));
  Reader exact(buf,n);TEST_ASSERT_TRUE(decode(exact,out));
}

void test_sample_tempo_wire_golden_known_unknown_and_cancel_ids() {
  SampleTempoReq q{0x01020304u,127,0xf00dbeefu,17,8194}, got;
  int n=put(q); TEST_ASSERT_EQUAL(17,n); checkTruncated<SampleTempoReq>(n);
  const uint8_t reqGolden[]={4,3,2,1,127,0xef,0xbe,0x0d,0xf0,17,0,0,0,2,0x20,0,0};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reqGolden,buf,n);
  Reader request(buf,n);TEST_ASSERT_TRUE(decode(request,got));
  TEST_ASSERT_EQUAL_UINT32(q.token,got.token);TEST_ASSERT_EQUAL_UINT32(q.crc,got.crc);
  TEST_ASSERT_EQUAL_UINT32(q.from,got.from);TEST_ASSERT_EQUAL_UINT32(q.to,got.to);TEST_ASSERT_EQUAL(127,got.index);
  SampleTempoRep rep{q.token,0,1200,875,600,2400,1}, out;
  n=put(rep);TEST_ASSERT_EQUAL(14,n);checkTruncated<SampleTempoRep>(n);
  const uint8_t repGolden[]={4,3,2,1,0,0xb0,4,0x6b,3,0x58,2,0x60,9,1};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(repGolden,buf,n);
  Reader response(buf,n);TEST_ASSERT_TRUE(decode(response,out));TEST_ASSERT_EQUAL_UINT16(1200,out.bpm10);
  TEST_ASSERT_EQUAL_UINT16(875,out.confidence);TEST_ASSERT_EQUAL_UINT16(600,out.halfBpm10);
  TEST_ASSERT_EQUAL_UINT16(2400,out.doubleBpm10);TEST_ASSERT_EQUAL_UINT8(1,out.flags);
  rep=SampleTempoRep{};rep.token=q.token;n=put(rep);Reader unknown(buf,n);TEST_ASSERT_TRUE(decode(unknown,out));
  TEST_ASSERT_EQUAL_UINT16(0,out.bpm10);TEST_ASSERT_EQUAL_UINT16(0,out.confidence);
  rep.result=static_cast<uint8_t>(mt::BankResult::Cancelled);n=put(rep);Reader cancelled(buf,n);
  TEST_ASSERT_TRUE(decode(cancelled,out));
  TEST_ASSERT_EQUAL(54,static_cast<int>(Msg::SampleTempo));TEST_ASSERT_EQUAL(55,static_cast<int>(Msg::SampleTempoCancel));
}

void test_sample_tempo_request_rejects_invalid_identity_range_and_extra_bytes() {
  SampleTempoReq q{1,0,0,0,1},out;
  auto bad=[&]() { const int n=put(q);Reader r(buf,n);TEST_ASSERT_FALSE(decode(r,out)); };
  q.token=0;bad();q.token=1;q.index=128;bad();q.index=0;q.to=0;bad();q.from=2;q.to=1;bad();
  q.from=UINT32_MAX-1;q.to=UINT32_MAX;q.token=UINT32_MAX;int n=put(q);Reader limits(buf,n);TEST_ASSERT_TRUE(decode(limits,out));
  buf[n]=0;Reader extra(buf,n+1);TEST_ASSERT_FALSE(decode(extra,out));
}

void test_sample_tempo_reply_rejects_nonterminal_and_out_of_bounds_metadata() {
  SampleTempoRep q{1,0,1200,1000,600,2400,0},out;
  auto bad=[&]() { const int n=put(q);Reader r(buf,n);TEST_ASSERT_FALSE(decode(r,out)); };
  q.token=0;bad();q.token=1;q.result=255;bad();q.result=static_cast<uint8_t>(mt::BankResult::Running);bad();q.result=0;
  q.bpm10=399;bad();q.bpm10=2401;bad();q.bpm10=1200;
  q.confidence=1001;bad();q.confidence=1000;q.halfBpm10=199;bad();q.halfBpm10=1201;bad();q.halfBpm10=600;
  q.doubleBpm10=799;bad();q.doubleBpm10=4801;bad();q.doubleBpm10=2400;q.flags=2;bad();q.flags=0;
  q.result=static_cast<uint8_t>(mt::BankResult::Busy);bad();q.result=0;
  for(uint16_t bpm:{400,2400}) {
    q.bpm10=bpm;q.halfBpm10=bpm/2;q.doubleBpm10=bpm*2;const int n=put(q);Reader r(buf,n);TEST_ASSERT_TRUE(decode(r,out));
  }
  q=SampleTempoRep{};q.token=1;q.confidence=1;bad();q.confidence=0;q.halfBpm10=600;bad();
  q.halfBpm10=0;q.doubleBpm10=2400;bad();q.doubleBpm10=0;q.flags=1;bad();q.flags=0;
  int n=put(q);buf[n]=0;Reader extra(buf,n+1);TEST_ASSERT_FALSE(decode(extra,out));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_writer_reader_primitives);
  RUN_TEST(test_writer_overflow);
  RUN_TEST(test_hello);
  RUN_TEST(test_time);
  RUN_TEST(test_ev_batch_round_trip_out_of_order);
  RUN_TEST(test_ev_batch_of_40_fits);
  RUN_TEST(test_state_set);
  RUN_TEST(test_ack_nack);
  RUN_TEST(test_status_with_and_without_scope);
  RUN_TEST(test_progress);
  RUN_TEST(test_bank_messages);
  RUN_TEST(test_editor_and_preview_messages);
  RUN_TEST(test_log);
  RUN_TEST(test_small_messages);
  RUN_TEST(test_frame_round_trip);
  RUN_TEST(test_msg_numbers_stable);
  RUN_TEST(test_profile_rep);
  RUN_TEST(test_profile_rep_24_stages);
  RUN_TEST(test_render_messages);
  RUN_TEST(test_render_oneshot_fields);
  RUN_TEST(test_export_messages);
  RUN_TEST(test_fw_messages);
  RUN_TEST(test_ev_batch_carries_a_hint);
  RUN_TEST(test_debug);
  RUN_TEST(test_protocol_and_debug_id);
  RUN_TEST(test_wt_render);
  RUN_TEST(test_rec_msgs_roundtrip);
  RUN_TEST(test_msg_rec_ids);
  RUN_TEST(test_wt_editor_transport);
  RUN_TEST(test_sample_edit_wire_contract);
  RUN_TEST(test_sample_edit_rejects_invalid_identity_flags_range_and_trailing);
  RUN_TEST(test_sample_edit_reply_rejects_invalid_success_and_accepts_cancellation);
  RUN_TEST(test_sample_stretch_wire_ratio_boundaries_and_invalid_mode_combinations);
  RUN_TEST(test_sample_tempo_wire_golden_known_unknown_and_cancel_ids);
  RUN_TEST(test_sample_tempo_request_rejects_invalid_identity_range_and_extra_bytes);
  RUN_TEST(test_sample_tempo_reply_rejects_nonterminal_and_out_of_bounds_metadata);
  return UNITY_END();
}
