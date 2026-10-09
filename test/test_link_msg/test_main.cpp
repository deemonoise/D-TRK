#include <unity.h>
#include <string.h>
#include "link_msg.h"

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
  int n = put(a);
  Hello b;
  Reader r(buf, n);
  TEST_ASSERT_TRUE(decode(r, b));
  TEST_ASSERT_EQUAL(kProtocol, b.proto);
  TEST_ASSERT_EQUAL_STRING("1.2.3-abc", b.fw);
  TEST_ASSERT_EQUAL_HEX32(0xCAFEF00D, b.bootId);
  TEST_ASSERT_EQUAL_UINT32(12345, b.modelSize);
  checkTruncated<Hello>(n);
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
  checkTruncated<Status>(n);

  a.scopeN = Status::kScopeMax;
  for (int i = 0; i < a.scopeN; ++i) a.scope[i] = static_cast<int8_t>(i - 128);
  n = put(a);
  TEST_ASSERT_TRUE(n <= kMaxPayload);
  Reader r2(buf, n);
  TEST_ASSERT_TRUE(decode(r2, b));
  TEST_ASSERT_EQUAL(Status::kScopeMax, b.scopeN);
  TEST_ASSERT_EQUAL_MEMORY(a.scope, b.scope, a.scopeN);
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

  n = put(PreviewNote{5, 61, 1234});
  PreviewNote pn;
  Reader r2(buf, n);
  TEST_ASSERT_TRUE(decode(r2, pn));
  TEST_ASSERT_EQUAL(5, pn.instr);
  TEST_ASSERT_EQUAL(61, pn.note);
  TEST_ASSERT_EQUAL_UINT32(1234, pn.holdMs);
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
  RUN_TEST(test_render_messages);
  RUN_TEST(test_fw_messages);
  return UNITY_END();
}
