#include <stdio.h>
#include <string.h>
#include <unity.h>
#include <vector>
#include "inst_codec.h"
#include "project_io.h"
#include "sample_set.h"
#include "scale.h"

using namespace mt;

struct VecSink : ByteSink {
  std::vector<uint8_t> buf;
  bool write(const void* d, size_t n) override {
    const uint8_t* b = static_cast<const uint8_t*>(d);
    buf.insert(buf.end(), b, b + n);
    return true;
  }
};

struct VecSource : ByteSource {
  const std::vector<uint8_t>& buf;
  size_t pos = 0;
  explicit VecSource(const std::vector<uint8_t>& b) : buf(b) {}
  bool read(void* d, size_t n) override {
    if (pos + n > buf.size()) return false;
    memcpy(d, buf.data() + pos, n);
    pos += n;
    return true;
  }
  bool skip(size_t n) override {
    if (pos + n > buf.size()) return false;
    pos += n;
    return true;
  }
};

// Large objects live in static storage.
static Project a, b;
constexpr int kOldTracks = 8;  // tracks of the 8-track firmware

void setUp() {
  a.reset();
  b.reset();
}
void tearDown() {}

static void fillFull(Project& p) {
  strcpy(p.name, "my_song-1");
  p.bpm = 137;
  p.scaleRoot = 9;
  p.scaleType = 2;
  p.songMode = true;
  p.chainLen = 5;
  const uint8_t ch[] = {0, 3, 3, 15, 7};
  memcpy(p.chain, ch, sizeof(ch));
  for (int t = 0; t < kTracks; ++t) {
    TrackCfg& c = p.tracks[t];
    snprintf(c.name, sizeof(c.name), "T%d_x", t);
    c.channel = static_cast<uint8_t>(15 - t);
    c.defVel = static_cast<uint8_t>(90 + t);
    c.defGate = static_cast<uint8_t>(60 + t);
    c.ccA = static_cast<uint8_t>(10 + t);
    c.ccB = static_cast<uint8_t>(20 + t);
    c.program = t == 3 ? kNoProgram : static_cast<uint8_t>(t);
    c.mute = t == 1;
    c.solo = t == 2;
  }
  Pattern& p0 = p.patterns[0];
  p0.length = 4;
  p0.res = Resolution::Eighth;
  p0.swing = 60;
  p0.steps[0][0].note = 36;
  p0.steps[7][3].note = kNoteOff;
  Pattern& p5 = p.patterns[5];
  p5.length = 128;
  p5.res = Resolution::SixteenthTriplet;
  p5.swing = 75;
  p5.groove = 3;
  p.djFilter = -20;
  p.tracks[6].humanize = 40;
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < 128; s += 3) {
      Step& st = p5.steps[t][s];
      st.note = static_cast<uint8_t>((t * 7 + s) % 128);
      st.vel = static_cast<uint8_t>(s % 128);
      st.fx[0] = {Fx::RAT, 3};
      st.fx[1] = {Fx::NDG, static_cast<uint8_t>(-20)};
      st.fx[5] = {Fx::GAT, static_cast<uint8_t>(s + 1)};
    }
  // Non-default params with no notes: still written.
  p.patterns[15].length = 32;
  // Internal audio.
  p.tracks[2].out = TrackOut::Int;
  p.tracks[2].instr = 9;
  p.tracks[2].vol = 77;
  p.tracks[6].out = TrackOut::Int;
  p.tracks[6].instr = 15;
  p.tracks[6].vol = 0;
  p.masterVol = 83;
  p.preview = false;
  p.dlyTime = 16;
  p.dlyFb = 127;
  p.dlyTone = 0;
  p.dlyLevel = 7;
  p.rvbSize = 11;
  p.rvbDamp = 22;
  p.rvbLevel = 33;
  p.compAmt = 44;
  p.compRel = 55;
  p.scTrack = 16;
  p.scDepth = 66;
  Instrument& i1 = p.instruments[1];
  strcpy(i1.name, "BASS_x1");
  i1.type = InstrType::Chip;
  i1.vol = 99;
  i1.transpose = -24;
  i1.fine = 37;
  i1.attack = 1;
  i1.decay = 2;
  i1.sustain = 3;
  i1.release = 127;
  i1.mono = true;
  i1.glide = 200;
  i1.wave = static_cast<uint8_t>(Wave::Wt1) + 15;
  i1.duty = 12;
  i1.pwmRate = 100;
  i1.pwmDepth = 49;
  Instrument& i15 = p.instruments[15];
  strcpy(i15.name, "KIT");
  i15.type = InstrType::Sample;
  i15.vol = 1;
  i15.transpose = 24;
  i15.fine = -50;
  strcpy(i15.sample, "drum_loop-0123ab");  // full 16 chars
  i15.root = 48;
  i15.start = 0x1234;
  i15.end = 0xFEDC;
  i15.loop = static_cast<uint8_t>(LoopMode::PingPong);
  i15.loopStart = 0x8001;
  i15.reverse = true;
  Instrument& i3 = p.instruments[3];
  i3.type = InstrType::Fm;
  i3.machine = static_cast<uint8_t>(FmMachine::Clap);
  for (int k = 0; k < kFmMacros; ++k) i3.macro[k] = static_cast<uint8_t>(10 + k * 20);
  i3.lfoWave = static_cast<uint8_t>(LfoWave::Random);
  i3.lfoRate = 127;
  i3.lfoDepth = -64;
  i3.lfoDest = static_cast<uint8_t>(LfoDest::Vol);
  i3.fltMode = static_cast<uint8_t>(FltMode::Hp);
  i3.cutoff = 20;
  i3.reso = 110;
  i3.fenv = -64;
  i3.fAtk = 5;
  i3.fDec = 0;
  i3.keytrack = 64;
  i3.send = 99;
  Instrument& i7 = p.instruments[7];
  i7.type = InstrType::Drum;
  drumSetMachine(i7, static_cast<uint8_t>(DrumMachine::Hh9));
  i7.fltMode = static_cast<uint8_t>(FltMode::Lp);
  i7.fenv = 63;
  i7.drive = 77;
  i7.rsend = 12;
  i7.velCut = -20;
  i7.velMac = 33;
}

static void assertSame(const Project& x, const Project& y) {
  TEST_ASSERT_EQUAL_STRING(x.name, y.name);
  TEST_ASSERT_EQUAL(x.bpm, y.bpm);
  TEST_ASSERT_EQUAL(x.scaleRoot, y.scaleRoot);
  TEST_ASSERT_EQUAL(x.scaleType, y.scaleType);
  TEST_ASSERT_EQUAL(x.songMode, y.songMode);
  TEST_ASSERT_EQUAL(x.chainLen, y.chainLen);
  TEST_ASSERT_EQUAL_MEMORY(x.chain, y.chain, kChainMax);
  for (int i = 0; i < x.chainLen; ++i) {
    TEST_ASSERT_EQUAL(x.chainTr[i], y.chainTr[i]);
    TEST_ASSERT_EQUAL(x.chainRep[i], y.chainRep[i]);
    TEST_ASSERT_EQUAL(x.chainScene[i], y.chainScene[i]);
  }
  TEST_ASSERT_EQUAL(x.djFilter, y.djFilter);
  TEST_ASSERT_EQUAL_UINT16_ARRAY(x.scenes, y.scenes, kScenes);
  for (int t = 0; t < kTracks; ++t) {
    const TrackCfg &c = x.tracks[t], &d = y.tracks[t];
    TEST_ASSERT_EQUAL(c.humanize, d.humanize);
    TEST_ASSERT_EQUAL_STRING(c.name, d.name);
    TEST_ASSERT_EQUAL(c.channel, d.channel);
    TEST_ASSERT_EQUAL(c.defVel, d.defVel);
    TEST_ASSERT_EQUAL(c.defGate, d.defGate);
    TEST_ASSERT_EQUAL(c.ccA, d.ccA);
    TEST_ASSERT_EQUAL(c.ccB, d.ccB);
    TEST_ASSERT_EQUAL(c.program, d.program);
    TEST_ASSERT_EQUAL(c.mute, d.mute);
    TEST_ASSERT_EQUAL(c.solo, d.solo);
  }
  for (int t = 0; t < kTracks; ++t) {
    TEST_ASSERT_EQUAL(static_cast<int>(x.tracks[t].out), static_cast<int>(y.tracks[t].out));
    TEST_ASSERT_EQUAL(x.tracks[t].instr, y.tracks[t].instr);
    TEST_ASSERT_EQUAL(x.tracks[t].vol, y.tracks[t].vol);
  }
  TEST_ASSERT_EQUAL(x.masterVol, y.masterVol);
  TEST_ASSERT_EQUAL(x.preview, y.preview);
  TEST_ASSERT_EQUAL(x.dlyTime, y.dlyTime);
  TEST_ASSERT_EQUAL(x.dlyFb, y.dlyFb);
  TEST_ASSERT_EQUAL(x.dlyTone, y.dlyTone);
  TEST_ASSERT_EQUAL(x.dlyLevel, y.dlyLevel);
  TEST_ASSERT_EQUAL(x.rvbSize, y.rvbSize);
  TEST_ASSERT_EQUAL(x.rvbDamp, y.rvbDamp);
  TEST_ASSERT_EQUAL(x.rvbLevel, y.rvbLevel);
  TEST_ASSERT_EQUAL(x.compAmt, y.compAmt);
  TEST_ASSERT_EQUAL(x.compRel, y.compRel);
  TEST_ASSERT_EQUAL(x.scTrack, y.scTrack);
  TEST_ASSERT_EQUAL(x.scDepth, y.scDepth);
  for (int i = 0; i < kInstruments; ++i) {
    const Instrument &m = x.instruments[i], &n = y.instruments[i];
    TEST_ASSERT_EQUAL_STRING(m.name, n.name);
    TEST_ASSERT_EQUAL(static_cast<int>(m.type), static_cast<int>(n.type));
    TEST_ASSERT_EQUAL(m.vol, n.vol);
    TEST_ASSERT_EQUAL(m.transpose, n.transpose);
    TEST_ASSERT_EQUAL(m.fine, n.fine);
    TEST_ASSERT_EQUAL(m.attack, n.attack);
    TEST_ASSERT_EQUAL(m.decay, n.decay);
    TEST_ASSERT_EQUAL(m.sustain, n.sustain);
    TEST_ASSERT_EQUAL(m.release, n.release);
    TEST_ASSERT_EQUAL(m.mono, n.mono);
    TEST_ASSERT_EQUAL(m.glide, n.glide);
    TEST_ASSERT_EQUAL(m.wave, n.wave);
    TEST_ASSERT_EQUAL(m.duty, n.duty);
    TEST_ASSERT_EQUAL(m.pwmRate, n.pwmRate);
    TEST_ASSERT_EQUAL(m.pwmDepth, n.pwmDepth);
    TEST_ASSERT_EQUAL_STRING(m.sample, n.sample);
    TEST_ASSERT_EQUAL(m.root, n.root);
    TEST_ASSERT_EQUAL(m.start, n.start);
    TEST_ASSERT_EQUAL(m.end, n.end);
    TEST_ASSERT_EQUAL(m.loop, n.loop);
    TEST_ASSERT_EQUAL(m.loopStart, n.loopStart);
    TEST_ASSERT_EQUAL(m.reverse, n.reverse);
    TEST_ASSERT_EQUAL(m.machine, n.machine);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(m.macro, n.macro, kFmMacros);
    TEST_ASSERT_EQUAL(m.lfoWave, n.lfoWave);
    TEST_ASSERT_EQUAL(m.lfoRate, n.lfoRate);
    TEST_ASSERT_EQUAL(m.lfoDepth, n.lfoDepth);
    TEST_ASSERT_EQUAL(m.lfoDest, n.lfoDest);
    TEST_ASSERT_EQUAL(m.fltMode, n.fltMode);
    TEST_ASSERT_EQUAL(m.cutoff, n.cutoff);
    TEST_ASSERT_EQUAL(m.reso, n.reso);
    TEST_ASSERT_EQUAL(m.fenv, n.fenv);
    TEST_ASSERT_EQUAL(m.fAtk, n.fAtk);
    TEST_ASSERT_EQUAL(m.fDec, n.fDec);
    TEST_ASSERT_EQUAL(m.keytrack, n.keytrack);
    TEST_ASSERT_EQUAL(m.send, n.send);
    TEST_ASSERT_EQUAL(m.drive, n.drive);
    TEST_ASSERT_EQUAL(m.rsend, n.rsend);
    TEST_ASSERT_EQUAL(m.velCut, n.velCut);
    TEST_ASSERT_EQUAL(m.velMac, n.velMac);
    TEST_ASSERT_EQUAL(m.sliceMode, n.sliceMode);
    TEST_ASSERT_EQUAL(m.chopMode, n.chopMode);
    TEST_ASSERT_EQUAL(m.chopN, n.chopN);
    TEST_ASSERT_EQUAL(m.chopThresh, n.chopThresh);
    TEST_ASSERT_EQUAL(m.sliceCount, n.sliceCount);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(m.slices, n.slices, kMaxSlices);
  }
  for (int i = 0; i < kPatterns; ++i) {
    const Pattern &p = x.patterns[i], &q = y.patterns[i];
    TEST_ASSERT_EQUAL(p.length, q.length);
    TEST_ASSERT_EQUAL(static_cast<int>(p.res), static_cast<int>(q.res));
    TEST_ASSERT_EQUAL(p.swing, q.swing);
    TEST_ASSERT_EQUAL(p.groove, q.groove);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p.trackLen, q.trackLen, kTracks);
    for (int t = 0; t < kTracks; ++t)
      TEST_ASSERT_EQUAL_MEMORY(p.steps[t], q.steps[t], sizeof(Step) * p.length);
  }
}

void test_crc32_reference() {
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32("123456789", 9));
  const uint32_t part = crc32("1234", 4);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32("56789", 5, part));
}

void test_round_trip_full_project() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  assertSame(a, b);
}

void test_load_resets_target_first() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  b.patterns[3].steps[0][0].note = 50;
  b.songMode = true;
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_TRUE(b.patterns[3].isEmpty());
  TEST_ASSERT_FALSE(b.songMode);
}

void test_empty_patterns_not_written() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  // PROJ, TRKS (16 x 16), INST, FMIN, FLTR, SLCE (16 x 72), TOUT (16 x 3), AUDI, SYNI (16 x 48), WTBL (empty),
  // KITS (16 x 176), CHN2 (empty), SCNS, GROV (16 + 16)
  TEST_ASSERT_TRUE(out.buf.size() < 1660 + 8 + 1 + 16 * 72 + 8 + 1 + 16 * 48 + 8 + 1 + 8 + 1 + 16 * 176 + 8 + 1 + 8 + 16 + 7 +
                                        8 + 32 + 1);  // + 7: AUDI sound fx bytes, + 1: DJ filter
  a.patterns[2].steps[1][1].note = 60;
  VecSink out2;
  TEST_ASSERT_TRUE(saveProject(a, out2));
  // One pattern of 16 steps: 8 + 4 + 16 tracks * 16 * 14 bytes.
  TEST_ASSERT_EQUAL(out.buf.size() + 8 + 4 + kTracks * 16 * sizeof(Step), out2.buf.size());
}

// Rebuilds a file with an extra chunk inserted after the header and a fresh CRC.
static std::vector<uint8_t> withChunkAfterHeader(const std::vector<uint8_t>& f, const char id[4],
                                                 const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> r(f.begin(), f.begin() + 8);
  r.insert(r.end(), id, id + 4);
  const uint32_t n = payload.size();
  for (int i = 0; i < 4; ++i) r.push_back(static_cast<uint8_t>(n >> (8 * i)));
  r.insert(r.end(), payload.begin(), payload.end());
  r.insert(r.end(), f.begin() + 8, f.end() - 12);
  const uint32_t c = crc32(r.data(), r.size());
  const uint8_t tail[] = {'C', 'R', 'C', ' ', 4, 0, 0, 0,
                          static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16),
                          static_cast<uint8_t>(c >> 24)};
  r.insert(r.end(), tail, tail + 12);
  return r;
}

void test_unknown_chunk_skipped() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  std::vector<uint8_t> payload(37, 0xAB);
  const std::vector<uint8_t> f = withChunkAfterHeader(out.buf, "ZZZZ", payload);
  VecSource in(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  assertSame(a, b);
}

void test_corrupt_byte_bad_crc() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  out.buf[out.buf.size() - 100] ^= 0x01;  // inside pattern step data
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadCrc), static_cast<int>(loadProject(in, b)));
}

void test_truncated_file() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  std::vector<uint8_t> cut(out.buf.begin(), out.buf.begin() + out.buf.size() / 2);
  VecSource in(cut);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Truncated), static_cast<int>(loadProject(in, b)));
}

void test_missing_crc_is_truncated() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  std::vector<uint8_t> cut(out.buf.begin(), out.buf.end() - 12);
  VecSource in(cut);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Truncated), static_cast<int>(loadProject(in, b)));
}

void test_bad_magic() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  out.buf[0] = 'X';
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadMagic), static_cast<int>(loadProject(in, b)));
}

void test_newer_version_rejected() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  out.buf[4] = 2;
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadVersion), static_cast<int>(loadProject(in, b)));
}

static void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
}
static void putChunk(std::vector<uint8_t>& f, const char id[4], const std::vector<uint8_t>& p) {
  f.insert(f.end(), id, id + 4);
  put16(f, static_cast<uint16_t>(p.size()));
  put16(f, 0);
  f.insert(f.end(), p.begin(), p.end());
}

void test_garbage_values_clamped() {
  std::vector<uint8_t> f = {'M', 'T', 'R', 'K', 1, 0, 0, 0};
  std::vector<uint8_t> pr(17, 0);
  memcpy(pr.data(), "ABCDEFGHIJKLMNOPQ", 17);  // no terminator
  put16(pr, 999);
  pr.push_back(50);   // scaleRoot
  pr.push_back(200);  // scaleType
  pr.push_back(7);    // songMode
  pr.push_back(200);  // chainLen
  for (int i = 0; i < kChainMax; ++i) pr.push_back(99);
  putChunk(f, "PROJ", pr);

  std::vector<uint8_t> tr = {static_cast<uint8_t>(kTracks)};
  for (int t = 0; t < kTracks; ++t) {
    for (int i = 0; i < 9; ++i) tr.push_back('Z');  // no terminator
    tr.push_back(40);   // channel
    tr.push_back(0);    // defVel
    tr.push_back(255);  // defGate
    tr.push_back(200);  // ccA
    tr.push_back(130);  // ccB
    tr.push_back(140);  // program
    tr.push_back(0xFF);  // flags
  }
  putChunk(f, "TRKS", tr);

  std::vector<uint8_t> pt = {3, 2, 99, 10};  // index 3, length 2, bad res, swing 10
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < 2; ++s) {
      const uint8_t st[] = {200, 250, 99, 5, static_cast<uint8_t>(Fx::GAT), 7};
      pt.insert(pt.end(), st, st + 6);
    }
  putChunk(f, "PATN", pt);
  std::vector<uint8_t> bad = {40, 16, 2, 50};  // pattern index out of range: ignored
  bad.resize(4 + kTracks * 16 * 6, 0x24);
  putChunk(f, "PATN", bad);

  const uint32_t c = crc32(f.data(), f.size());
  const uint8_t tail[] = {'C', 'R', 'C', ' ', 4, 0, 0, 0,
                          static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16),
                          static_cast<uint8_t>(c >> 24)};
  f.insert(f.end(), tail, tail + 12);

  VecSource in(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(16, strlen(b.name));
  TEST_ASSERT_EQUAL(300, b.bpm);
  TEST_ASSERT_TRUE(b.scaleRoot < 12);
  TEST_ASSERT_TRUE(b.scaleType < static_cast<int>(ScaleType::Count));
  TEST_ASSERT_TRUE(b.songMode);
  TEST_ASSERT_EQUAL(kChainMax, b.chainLen);
  for (int i = 0; i < kChainMax; ++i) TEST_ASSERT_TRUE(b.chain[i] < kPatterns);
  for (int t = 0; t < kTracks; ++t) {
    const TrackCfg& c2 = b.tracks[t];
    TEST_ASSERT_EQUAL(8, strlen(c2.name));
    TEST_ASSERT_TRUE(c2.channel <= 15);
    TEST_ASSERT_TRUE(c2.defVel >= 1 && c2.defVel <= 127);
    TEST_ASSERT_TRUE(c2.defGate >= 1 && c2.defGate <= 200);
    TEST_ASSERT_TRUE(c2.ccA <= 127 && c2.ccB <= 127);
    TEST_ASSERT_TRUE(c2.program <= 127 || c2.program == kNoProgram);
    TEST_ASSERT_TRUE(c2.mute);
    TEST_ASSERT_TRUE(c2.solo);
  }
  const Pattern& p3 = b.patterns[3];
  TEST_ASSERT_EQUAL(kMinSteps, p3.length);
  TEST_ASSERT_TRUE(static_cast<int>(p3.res) < static_cast<int>(Resolution::Count));
  TEST_ASSERT_EQUAL(50, p3.swing);
  const Step& s0 = p3.steps[0][0];
  TEST_ASSERT_EQUAL(kNoteEmpty, s0.note);
  TEST_ASSERT_TRUE(s0.vel <= 127);
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::None), static_cast<int>(s0.fx[0].cmd));
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::GAT), static_cast<int>(s0.fx[1].cmd));
  for (int k = 2; k < kFxSlots; ++k) TEST_ASSERT_TRUE(s0.fx[k].cmd == Fx::None);  // old 6-byte steps: 2 slots
  TEST_ASSERT_TRUE(p3.steps[0][2].isEmpty());  // beyond the stored length
}

// ---- Malformed / forward-compatible chunks ----

static std::vector<uint8_t> fileHeader() { return {'M', 'T', 'R', 'K', 1, 0, 0, 0}; }

static void putChunkSized(std::vector<uint8_t>& f, const char id[4], uint32_t size, const std::vector<uint8_t>& p) {
  f.insert(f.end(), id, id + 4);
  for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>(size >> (8 * i)));
  f.insert(f.end(), p.begin(), p.end());
}

static void finish(std::vector<uint8_t>& f) {
  const uint32_t c = crc32(f.data(), f.size());
  const uint8_t tail[] = {'C', 'R', 'C', ' ', 4, 0, 0, 0,
                          static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16),
                          static_cast<uint8_t>(c >> 24)};
  f.insert(f.end(), tail, tail + 12);
}

// A PATN body: `tracks` tracks of `stepBytes`-byte steps (6 = the 2-slot format, 14 = current).
static std::vector<uint8_t> patn(uint8_t idx, uint8_t len, int extraBytes = 0, int tracks = kTracks,
                                 int stepBytes = 6) {
  std::vector<uint8_t> p = {idx, len, static_cast<uint8_t>(Resolution::Eighth), 60};
  for (int t = 0; t < tracks; ++t)
    for (int s = 0; s < len; ++s) {
      std::vector<uint8_t> st(stepBytes, 0);
      st[0] = static_cast<uint8_t>((t + s) % 128);
      st[1] = 100;
      p.insert(p.end(), st.begin(), st.end());
    }
  if (extraBytes > 0) p.resize(p.size() + extraBytes, 0);
  if (extraBytes < 0) p.resize(p.size() + extraBytes);
  return p;
}

static LoadErr loadBytes(const std::vector<uint8_t>& f) {
  VecSource in(f);
  return loadProject(in, b);
}

void test_patn_length_zero() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PATN", patn(2, 0));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kMinSteps, b.patterns[2].length);
  TEST_ASSERT_TRUE(b.patterns[2].isEmpty());
}

void test_patn_length_over_max() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PATN", patn(1, 200));
  putChunk(f, "PATN", patn(4, 8));  // still parsed after the oversized one
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const Pattern& p1 = b.patterns[1];
  TEST_ASSERT_EQUAL(kMaxSteps, p1.length);
  TEST_ASSERT_EQUAL((7 + 127) % 128, p1.steps[7][127].note);
  TEST_ASSERT_EQUAL(8, b.patterns[4].length);
  TEST_ASSERT_EQUAL((3 + 5) % 128, b.patterns[4].steps[3][5].note);
}

void test_patn_size_mismatch() {
  for (int extra : {-1, 6, 1}) {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "PATN", patn(1, 16, extra));
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  }
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PATN", {1, 16});  // shorter than the header
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

// Header only with a length: an empty pattern of that length.
void test_patn_header_only_len_nonzero() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PATN", {2, 16, static_cast<uint8_t>(Resolution::Eighth), 50});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(16, b.patterns[2].length);
  TEST_ASSERT_TRUE(b.patterns[2].isEmpty());
}

// Files from the 8-track firmware: tracks 1-8 load, 9-16 stay clear. Both step formats.
void test_patn_eight_tracks_loads() {
  for (int stepBytes : {6, 14}) {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "PATN", patn(3, 16, 0, 8, stepBytes));
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
    const Pattern& pt = b.patterns[3];
    TEST_ASSERT_EQUAL(16, pt.length);
    TEST_ASSERT_EQUAL((7 + 15) % 128, pt.steps[7][15].note);
    for (int t = 8; t < kTracks; ++t)
      for (int s = 0; s < 16; ++s) TEST_ASSERT_TRUE(pt.steps[t][s].isEmpty());
  }
}

void test_patn_sixteen_tracks_round_trip() {
  a.patterns[5].length = 24;
  a.patterns[5].steps[15][23].note = 71;
  a.patterns[5].steps[8][0].fx[5] = {Fx::DLY, 99};
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(71, b.patterns[5].steps[15][23].note);
  TEST_ASSERT_EQUAL(static_cast<int>(Fx::DLY), static_cast<int>(b.patterns[5].steps[8][0].fx[5].cmd));
  TEST_ASSERT_EQUAL(99, b.patterns[5].steps[8][0].fx[5].val);
}

static std::vector<uint8_t> trks(uint8_t count, int stored) {
  std::vector<uint8_t> tr = {count};
  for (int t = 0; t < stored; ++t) {
    char nm[9] = {0};
    snprintf(nm, sizeof(nm), "K%d", t);
    tr.insert(tr.end(), nm, nm + 9);
    const uint8_t rest[] = {static_cast<uint8_t>(t), 90, 70, 1, 2, 5, 0};
    tr.insert(tr.end(), rest, rest + 7);
  }
  return tr;
}

void test_trks_count_not_eight() {
  {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "TRKS", trks(3, 3));
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
    TEST_ASSERT_EQUAL_STRING("K2", b.tracks[2].name);
    TEST_ASSERT_EQUAL(2, b.tracks[2].channel);
    const TrackCfg& def = a.tracks[3];  // a is freshly reset
    TEST_ASSERT_EQUAL_STRING(def.name, b.tracks[3].name);
    TEST_ASSERT_EQUAL(def.defVel, b.tracks[3].defVel);
  }
  {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "TRKS", trks(10, 10));  // fewer tracks than the build: the rest keep defaults
    putChunk(f, "PATN", patn(0, 4));
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
    TEST_ASSERT_EQUAL_STRING("K9", b.tracks[9].name);
    const TrackCfg& def = a.tracks[10];
    TEST_ASSERT_EQUAL_STRING(def.name, b.tracks[10].name);
    TEST_ASSERT_EQUAL_STRING(a.tracks[kTracks - 1].name, b.tracks[kTracks - 1].name);
    TEST_ASSERT_EQUAL(4, b.patterns[0].length);
  }
  {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "TRKS", trks(kTracks + 2, kTracks + 2));  // a future build with more tracks: extra skipped
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
    TEST_ASSERT_EQUAL_STRING("K15", b.tracks[kTracks - 1].name);
  }
  {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "TRKS", trks(10, 8));  // count says more than the chunk holds
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  }
}

void test_unknown_chunk_huge_size() {
  std::vector<uint8_t> f = fileHeader();
  putChunkSized(f, "ZZZZ", 0xFFFFFFF0u, std::vector<uint8_t>(100, 0x55));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Truncated), static_cast<int>(loadBytes(f)));
}

void test_proj_chunk_larger_is_skipped() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  // Grow the PROJ chunk (right after the header) by 20 bytes, as a newer minor version might.
  std::vector<uint8_t> f(out.buf.begin(), out.buf.end() - 12);
  const uint32_t size = f[12] | (f[13] << 8) | (f[14] << 16) | (static_cast<uint32_t>(f[15]) << 24);
  TEST_ASSERT_EQUAL(0, memcmp(f.data() + 8, "PROJ", 4));
  const uint32_t grown = size + 20;
  for (int i = 0; i < 4; ++i) f[12 + i] = static_cast<uint8_t>(grown >> (8 * i));
  f.insert(f.begin() + 16 + size, 20, 0xEE);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  assertSame(a, b);
}

// ---- Internal audio chunks ----

// Copy of a saved file without the given chunks, with a fresh CRC.
static std::vector<uint8_t> withoutChunks(const std::vector<uint8_t>& f, std::initializer_list<const char*> ids) {
  std::vector<uint8_t> r(f.begin(), f.begin() + 8);
  size_t pos = 8;
  while (pos + 8 <= f.size() - 12) {
    const uint32_t size = f[pos + 4] | (f[pos + 5] << 8) | (f[pos + 6] << 16) |
                          (static_cast<uint32_t>(f[pos + 7]) << 24);
    bool drop = false;
    for (const char* id : ids) drop = drop || memcmp(f.data() + pos, id, 4) == 0;
    if (!drop) r.insert(r.end(), f.begin() + pos, f.begin() + pos + 8 + size);
    pos += 8 + size;
  }
  finish(r);
  return r;
}

static bool hasChunk(const std::vector<uint8_t>& f, const char* id) {
  size_t pos = 8;
  while (pos + 8 <= f.size()) {
    if (memcmp(f.data() + pos, id, 4) == 0) return true;
    pos += 8 + (f[pos + 4] | (f[pos + 5] << 8) | (f[pos + 6] << 16) | (static_cast<uint32_t>(f[pos + 7]) << 24));
  }
  return false;
}

void test_audio_chunks_written() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "INST"));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "TOUT"));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "AUDI"));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "FMIN"));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "FLTR"));
}

void test_old_file_gets_audio_defaults() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"INST", "TOUT", "AUDI", "FMIN", "FLTR", "SMPL"});
  TEST_ASSERT_FALSE(hasChunk(f, "INST"));
  TEST_ASSERT_FALSE(hasChunk(f, "SMPL"));
  b.masterVol = 3;
  b.sampleCount = 2;
  b.instruments[1].vol = 5;
  b.hasSampleList = true;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  Project* def = new Project();
  for (int t = 0; t < kTracks; ++t) {
    TEST_ASSERT_EQUAL(static_cast<int>(TrackOut::Midi), static_cast<int>(b.tracks[t].out));
    TEST_ASSERT_EQUAL(t, b.tracks[t].instr);
    TEST_ASSERT_EQUAL(100, b.tracks[t].vol);
  }
  TEST_ASSERT_EQUAL(40, b.masterVol);
  TEST_ASSERT_TRUE(b.preview);
  TEST_ASSERT_EQUAL(def->dlyTime, b.dlyTime);
  TEST_ASSERT_EQUAL(def->dlyLevel, b.dlyLevel);
  TEST_ASSERT_EQUAL(0, b.instruments[3].send);
  TEST_ASSERT_EQUAL_STRING("INS2", b.instruments[1].name);
  TEST_ASSERT_EQUAL(def->instruments[1].vol, b.instruments[1].vol);
  TEST_ASSERT_EQUAL(def->instruments[3].machine, b.instruments[3].machine);
  TEST_ASSERT_EQUAL(def->instruments[3].lfoDepth, b.instruments[3].lfoDepth);
  TEST_ASSERT_EQUAL(def->instruments[3].fltMode, b.instruments[3].fltMode);
  TEST_ASSERT_EQUAL(def->instruments[3].cutoff, b.instruments[3].cutoff);
  TEST_ASSERT_EQUAL(a.patterns[5].steps[3][9].note, b.patterns[5].steps[3][9].note);
  TEST_ASSERT_EQUAL(0, b.sampleCount);
  TEST_ASSERT_FALSE(b.hasSampleList);  // old file: storage migrates its sample names
  delete def;
}

// An 8-track file with TOUT: its 8 tracks keep their outs, the new tracks 9-16 get the defaults
// of a fresh project (Int, instr = track), not the all-MIDI preset for files older than TOUT.
void test_old_file_tout_new_tracks_default_int() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TRKS", trks(kOldTracks, kOldTracks));
  std::vector<uint8_t> to = {kOldTracks};
  for (int t = 0; t < kOldTracks; ++t) {
    const uint8_t rec[3] = {static_cast<uint8_t>(TrackOut::Midi), static_cast<uint8_t>(t), 100};
    to.insert(to.end(), rec, rec + 3);
  }
  putChunk(f, "TOUT", to);
  putChunk(f, "PATN", patn(0, 16, 0, kOldTracks));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  for (int t = 0; t < kTracks; ++t) {
    const TrackOut want = t < kOldTracks ? TrackOut::Midi : TrackOut::Int;
    TEST_ASSERT_EQUAL(static_cast<int>(want), static_cast<int>(b.tracks[t].out));
    TEST_ASSERT_EQUAL(t, b.tracks[t].instr);
  }
}

static std::vector<uint8_t> instRecord(const char* name) {
  std::vector<uint8_t> r(48, 0);
  memcpy(r.data(), name, strlen(name) < 8 ? strlen(name) : 8);
  return r;
}

void test_audio_garbage_clamped() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> to = {kTracks};
  for (int t = 0; t < kTracks; ++t) {
    to.push_back(7);    // out
    to.push_back(200);  // instr
    to.push_back(255);  // vol
  }
  putChunk(f, "TOUT", to);
  putChunk(f, "AUDI", {250, 9});
  std::vector<uint8_t> in = {1};
  std::vector<uint8_t> r(48, 0xFF);  // every byte garbage, no terminators
  r[10] = static_cast<uint8_t>(-100);  // transpose
  r[11] = 100;                         // fine
  in.insert(in.end(), r.begin(), r.end());
  putChunk(f, "INST", in);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  for (int t = 0; t < kTracks; ++t) {
    TEST_ASSERT_EQUAL(static_cast<int>(TrackOut::Midi), static_cast<int>(b.tracks[t].out));
    TEST_ASSERT_TRUE(b.tracks[t].instr < kInstruments);
    TEST_ASSERT_EQUAL(127, b.tracks[t].vol);
  }
  TEST_ASSERT_EQUAL(kMasterVolMax, b.masterVol);
  TEST_ASSERT_TRUE(b.preview);
  TEST_ASSERT_EQUAL(3, b.dlyTime);  // a 2-byte AUDI keeps the delay defaults
  TEST_ASSERT_EQUAL(100, b.dlyLevel);
  const Instrument& m = b.instruments[0];
  TEST_ASSERT_EQUAL(8, strlen(m.name));
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Chip), static_cast<int>(m.type));
  TEST_ASSERT_EQUAL(127, m.vol);
  TEST_ASSERT_EQUAL(-24, m.transpose);
  TEST_ASSERT_EQUAL(50, m.fine);
  TEST_ASSERT_EQUAL(127, m.attack);
  TEST_ASSERT_EQUAL(127, m.decay);
  TEST_ASSERT_EQUAL(127, m.sustain);
  TEST_ASSERT_EQUAL(127, m.release);
  TEST_ASSERT_TRUE(m.mono);
  TEST_ASSERT_EQUAL(0, m.wave);
  TEST_ASSERT_EQUAL(99, m.duty);
  TEST_ASSERT_EQUAL(127, m.pwmRate);
  TEST_ASSERT_EQUAL(49, m.pwmDepth);
  TEST_ASSERT_EQUAL(kSampleNameMax, strlen(m.sample));
  TEST_ASSERT_EQUAL(127, m.root);
  TEST_ASSERT_EQUAL(static_cast<int>(LoopMode::Off), m.loop);
  TEST_ASSERT_TRUE(m.reverse);
  TEST_ASSERT_EQUAL_STRING("INS2", b.instruments[1].name);  // not stored: default
}

void test_inst_count_over_max() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> in = {20};
  for (int i = 0; i < 20; ++i) {
    char nm[9];
    snprintf(nm, sizeof(nm), "N%d", i);
    std::vector<uint8_t> r = instRecord(nm);
    r[9] = static_cast<uint8_t>(i);  // vol
    in.insert(in.end(), r.begin(), r.end());
  }
  putChunk(f, "INST", in);
  putChunk(f, "AUDI", {55, 0});  // still parsed after the oversized one
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL_STRING("N0", b.instruments[0].name);
  TEST_ASSERT_EQUAL_STRING("N15", b.instruments[15].name);
  TEST_ASSERT_EQUAL(15, b.instruments[15].vol);
  TEST_ASSERT_EQUAL(55, b.masterVol);
  TEST_ASSERT_FALSE(b.preview);
}

void test_audio_chunks_too_small() {
  {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "INST", {2, 0, 0});  // count says more than the chunk holds
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  }
  {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "TOUT", {8, 1, 2});
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  }
  {
    std::vector<uint8_t> f = fileHeader();
    putChunk(f, "AUDI", {50});
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  }
}

void test_fmin_garbage_clamped() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> in = {1};
  std::vector<uint8_t> r(16, 0xFF);
  r[8] = 100;  // lfoDepth
  in.insert(in.end(), r.begin(), r.end());
  putChunk(f, "FMIN", in);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const Instrument& m = b.instruments[0];
  TEST_ASSERT_EQUAL(0, m.machine);
  for (int k = 0; k < kFmMacros; ++k) TEST_ASSERT_EQUAL(127, m.macro[k]);
  TEST_ASSERT_EQUAL(0, m.lfoWave);
  TEST_ASSERT_EQUAL(127, m.lfoRate);
  TEST_ASSERT_EQUAL(63, m.lfoDepth);
  TEST_ASSERT_EQUAL(0, m.lfoDest);
}

void test_samples_roundtrip() {
  a.sampleCount = 2;
  strcpy(a.samples[0].name, "kick");
  a.samples[0].crc = 0xDEADBEEF;
  a.samples[0].frames = 1234;
  strcpy(a.samples[1].name, "snare_longname16");  // 16 chars, no terminator in the file
  a.samples[1].crc = 1;
  a.samples[1].frames = 7;
  VecSink s;
  TEST_ASSERT_TRUE(saveProject(a, s));
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(s.buf)));
  TEST_ASSERT_EQUAL(2, b.sampleCount);
  TEST_ASSERT_EQUAL_STRING("kick", b.samples[0].name);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, b.samples[0].crc);
  TEST_ASSERT_EQUAL_UINT32(1234, b.samples[0].frames);
  TEST_ASSERT_EQUAL_STRING("snare_longname16", b.samples[1].name);
  TEST_ASSERT_EQUAL_HEX32(1, b.samples[1].crc);
  TEST_ASSERT_EQUAL_UINT32(7, b.samples[1].frames);
}

void test_samples_empty_list_written() {
  VecSink s;
  TEST_ASSERT_TRUE(saveProject(a, s));
  TEST_ASSERT_TRUE(hasChunk(s.buf, "SMPL"));
  b.sampleCount = 4;
  b.hasSampleList = false;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(s.buf)));
  TEST_ASSERT_EQUAL(0, b.sampleCount);
  TEST_ASSERT_TRUE(b.hasSampleList);  // an empty list is still a list
}

void test_samples_dup_and_empty_skipped() {
  a.sampleCount = 4;
  strcpy(a.samples[0].name, "kick");
  a.samples[0].crc = 10;
  strcpy(a.samples[1].name, "KICK");  // same name ignoring case
  a.samples[1].crc = 11;
  // samples[2] has an empty name
  strcpy(a.samples[3].name, "hat");
  VecSink s;
  TEST_ASSERT_TRUE(saveProject(a, s));
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(s.buf)));
  TEST_ASSERT_EQUAL(2, b.sampleCount);
  TEST_ASSERT_EQUAL_STRING("kick", b.samples[0].name);
  TEST_ASSERT_EQUAL_UINT32(10, b.samples[0].crc);  // the first one wins
  TEST_ASSERT_EQUAL_STRING("hat", b.samples[1].name);
}

static std::vector<uint8_t> smplRecord(const char* name, uint32_t crc, uint32_t frames) {
  std::vector<uint8_t> r(24, 0);
  memcpy(r.data(), name, strlen(name) < 16 ? strlen(name) : 16);
  for (int i = 0; i < 4; ++i) {
    r[16 + i] = static_cast<uint8_t>(crc >> (8 * i));
    r[20 + i] = static_cast<uint8_t>(frames >> (8 * i));
  }
  return r;
}

void test_samples_count_over_max() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> sm = {kProjSamples + 2};
  for (int i = 0; i < kProjSamples + 2; ++i) {
    char nm[8];
    snprintf(nm, sizeof(nm), "s%d", i);
    const std::vector<uint8_t> r = smplRecord(nm, static_cast<uint32_t>(i), 100);
    sm.insert(sm.end(), r.begin(), r.end());
  }
  putChunk(f, "SMPL", sm);
  putChunk(f, "AUDI", {55, 0});  // still parsed after the oversized one
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kProjSamples, b.sampleCount);
  TEST_ASSERT_EQUAL_STRING("s127", b.samples[kProjSamples - 1].name);
  TEST_ASSERT_EQUAL_UINT32(127, b.samples[kProjSamples - 1].crc);
  TEST_ASSERT_EQUAL(55, b.masterVol);
}

static std::vector<uint8_t> smplChunk(const char* prefix, int n) {
  std::vector<uint8_t> sm = {static_cast<uint8_t>(n)};
  for (int i = 0; i < n; ++i) {
    char nm[12];
    snprintf(nm, sizeof(nm), "%s%d", prefix, i);
    const std::vector<uint8_t> r = smplRecord(nm, static_cast<uint32_t>(i), 100);
    sm.insert(sm.end(), r.begin(), r.end());
  }
  return sm;
}

void test_samples_two_chunks_last_wins() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "SMPL", smplChunk("a", kProjSamples));
  putChunk(f, "SMPL", smplChunk("b", kProjSamples));
  putChunk(f, "AUDI", {55, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kProjSamples, b.sampleCount);
  TEST_ASSERT_EQUAL_STRING("b0", b.samples[0].name);
  TEST_ASSERT_EQUAL_STRING("b127", b.samples[kProjSamples - 1].name);
  TEST_ASSERT_EQUAL(55, b.masterVol);
}

void test_samples_chunk_too_small() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> sm = {2};
  const std::vector<uint8_t> r = smplRecord("kick", 1, 1);
  sm.insert(sm.end(), r.begin(), r.end());  // count says two
  putChunk(f, "SMPL", sm);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

void test_samples_bad_names_skipped() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> sm = {3};
  for (const char* nm : {"my kick", "a.b", "hat"}) {
    const std::vector<uint8_t> r = smplRecord(nm, 1, 1);
    sm.insert(sm.end(), r.begin(), r.end());
  }
  putChunk(f, "SMPL", sm);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(1, b.sampleCount);
  TEST_ASSERT_EQUAL_STRING("hat", b.samples[0].name);
}

// ---- Filter chunk, DRUM, machine normalization ----

void test_fltr_roundtrip() {
  Instrument& m = a.instruments[5];
  m.fltMode = static_cast<uint8_t>(FltMode::Bp);
  m.cutoff = 33;
  m.reso = 99;
  m.fenv = -40;
  m.fAtk = 7;
  m.fDec = 88;
  m.keytrack = 127;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  const Instrument& r = b.instruments[5];
  TEST_ASSERT_EQUAL(static_cast<int>(FltMode::Bp), r.fltMode);
  TEST_ASSERT_EQUAL(33, r.cutoff);
  TEST_ASSERT_EQUAL(99, r.reso);
  TEST_ASSERT_EQUAL(-40, r.fenv);
  TEST_ASSERT_EQUAL(7, r.fAtk);
  TEST_ASSERT_EQUAL(88, r.fDec);
  TEST_ASSERT_EQUAL(127, r.keytrack);
}

void test_file_without_fltr() {
  a.instruments[0].fltMode = static_cast<uint8_t>(FltMode::Lp);
  a.instruments[0].cutoff = 10;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"FLTR"});
  TEST_ASSERT_FALSE(hasChunk(f, "FLTR"));
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(static_cast<int>(FltMode::Off), b.instruments[0].fltMode);
  TEST_ASSERT_EQUAL(127, b.instruments[0].cutoff);
}

void test_fltr_garbage_clamped() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> in = {1};
  std::vector<uint8_t> r(8, 0xFF);
  r[3] = 100;  // fenv
  in.insert(in.end(), r.begin(), r.end());
  putChunk(f, "FLTR", in);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const Instrument& m = b.instruments[0];
  TEST_ASSERT_EQUAL(static_cast<int>(FltMode::Off), m.fltMode);
  TEST_ASSERT_EQUAL(127, m.cutoff);
  TEST_ASSERT_EQUAL(127, m.reso);
  TEST_ASSERT_EQUAL(63, m.fenv);
  TEST_ASSERT_EQUAL(127, m.fAtk);
  TEST_ASSERT_EQUAL(127, m.fDec);
  TEST_ASSERT_EQUAL(127, m.keytrack);
  TEST_ASSERT_EQUAL(127, m.send);
}

void test_audi_delay_garbage_clamped() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "AUDI", {50, 1, 0, 200, 255, 128});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(1, b.dlyTime);
  TEST_ASSERT_EQUAL(127, b.dlyFb);
  TEST_ASSERT_EQUAL(127, b.dlyTone);
  TEST_ASSERT_EQUAL(127, b.dlyLevel);
  f = fileHeader();
  putChunk(f, "AUDI", {50, 1, 40, 0, 0, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kDlyTimeMax, b.dlyTime);
  TEST_ASSERT_EQUAL(0, b.dlyLevel);
}

void test_drum_type_roundtrip() {
  Instrument& m = a.instruments[2];
  m.type = InstrType::Drum;
  drumSetMachine(m, static_cast<uint8_t>(DrumMachine::Cy9));
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_TRUE(b.instruments[2].type == InstrType::Drum);
  TEST_ASSERT_EQUAL(static_cast<int>(DrumMachine::Cy9), b.instruments[2].machine);
}

void test_fm_machine_out_of_range() {
  Instrument& m = a.instruments[3];
  m.type = InstrType::Fm;
  m.machine = 12;  // a DRUM number on an FM instrument
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(0, b.instruments[3].machine);
}

void test_slices_roundtrip() {
  a.reset();
  Instrument& m = a.instruments[3];
  m.type = InstrType::Sample;
  m.sliceMode = static_cast<uint8_t>(SliceMode::Note);
  m.chopMode = static_cast<uint8_t>(ChopMode::Trans);
  m.chopN = 12;
  m.chopThresh = 70;
  m.sliceCount = 3;
  m.slices[0] = 0x100;
  m.slices[1] = 0x2000;
  m.slices[2] = 0x9000;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "SLCE"));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  const Instrument& r = b.instruments[3];
  TEST_ASSERT_EQUAL(static_cast<int>(SliceMode::Note), r.sliceMode);
  TEST_ASSERT_EQUAL(static_cast<int>(ChopMode::Trans), r.chopMode);
  TEST_ASSERT_EQUAL(12, r.chopN);
  TEST_ASSERT_EQUAL(70, r.chopThresh);
  TEST_ASSERT_EQUAL(3, r.sliceCount);
  TEST_ASSERT_EQUAL_HEX16(0x100, r.slices[0]);
  TEST_ASSERT_EQUAL_HEX16(0x2000, r.slices[1]);
  TEST_ASSERT_EQUAL_HEX16(0x9000, r.slices[2]);
  const Instrument& z = b.instruments[0];
  TEST_ASSERT_EQUAL(0, z.sliceCount);
  TEST_ASSERT_EQUAL(0, z.sliceMode);
  TEST_ASSERT_EQUAL(8, z.chopN);
  TEST_ASSERT_EQUAL(50, z.chopThresh);
}

static void putSliceRec(uint8_t* r, uint8_t count) {
  memset(r, 0, kSliceRecSize);
  r[4] = count;
  const uint16_t pos[3] = {10, 5, 20};
  for (int i = 0; i < 3; ++i) {
    r[8 + 2 * i] = static_cast<uint8_t>(pos[i]);
    r[9 + 2 * i] = static_cast<uint8_t>(pos[i] >> 8);
  }
}

void test_slices_bad_order_truncated() {
  uint8_t r[kSliceRecSize];
  Instrument m;
  putSliceRec(r, 3);
  unpackSlices(r, m);
  TEST_ASSERT_EQUAL(1, m.sliceCount);
  TEST_ASSERT_EQUAL(10, m.slices[0]);
  putSliceRec(r, 40);
  r[0] = 9;    // sliceMode out of range
  r[1] = 9;    // chopMode out of range
  r[2] = 1;    // chopN below 2
  r[3] = 200;  // chopThresh over 100
  Instrument n;
  unpackSlices(r, n);
  TEST_ASSERT_EQUAL(1, n.sliceCount);
  TEST_ASSERT_EQUAL(0, n.sliceMode);
  TEST_ASSERT_EQUAL(0, n.chopMode);
  TEST_ASSERT_EQUAL(2, n.chopN);
  TEST_ASSERT_EQUAL(100, n.chopThresh);
}

void test_slices_count_clamped() {
  uint8_t r[kSliceRecSize];
  memset(r, 0, sizeof(r));
  r[4] = 40;
  for (int i = 0; i < kMaxSlices; ++i) {
    const uint16_t v = static_cast<uint16_t>(100 + i * 1000);
    r[8 + 2 * i] = static_cast<uint8_t>(v);
    r[9 + 2 * i] = static_cast<uint8_t>(v >> 8);
  }
  Instrument m;
  unpackSlices(r, m);
  TEST_ASSERT_EQUAL(kMaxSlices, m.sliceCount);
  TEST_ASSERT_EQUAL(100 + 31 * 1000, m.slices[31]);
}


// ---- SYNTH fields (SYNI) and the project wavetable list (WTBL) ----

static void fillSyn(Instrument& m) {
  m.type = InstrType::Synth;
  m.synOsc[0] = static_cast<uint8_t>(SynOsc::Wt);
  m.synOsc[1] = static_cast<uint8_t>(SynOsc::Square);
  strcpy(m.synWt[0], "pad_table_name16");  // 16 chars, no terminator in the file
  strcpy(m.synWt[1], "*SAWSQR");
  m.synSemi = -12;
  m.synSync = true;
  m.synSub = 99;
  m.synSubOct = 1;
  m.synNoise = 17;
  m.synEAtk = 5;
  m.synEDec = 120;
}

void test_syn_roundtrip() {
  fillSyn(a.instruments[4]);
  a.wavetableCount = 2;
  strcpy(a.wavetables[0].name, "pad");
  a.wavetables[0].crc = 0xCAFEBABE;
  strcpy(a.wavetables[1].name, "pad_table_name16");
  a.wavetables[1].crc = 7;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "SYNI"));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "WTBL"));
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(out.buf)));
  const Instrument &m = a.instruments[4], &r = b.instruments[4];
  TEST_ASSERT_TRUE(r.type == InstrType::Synth);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(m.synOsc, r.synOsc, 2);
  TEST_ASSERT_EQUAL_STRING(m.synWt[0], r.synWt[0]);
  TEST_ASSERT_EQUAL_STRING(m.synWt[1], r.synWt[1]);
  TEST_ASSERT_EQUAL(m.synSemi, r.synSemi);
  TEST_ASSERT_EQUAL(m.synSync, r.synSync);
  TEST_ASSERT_EQUAL(m.synSub, r.synSub);
  TEST_ASSERT_EQUAL(m.synSubOct, r.synSubOct);
  TEST_ASSERT_EQUAL(m.synNoise, r.synNoise);
  TEST_ASSERT_EQUAL(m.synEAtk, r.synEAtk);
  TEST_ASSERT_EQUAL(m.synEDec, r.synEDec);
  TEST_ASSERT_EQUAL(2, b.wavetableCount);
  TEST_ASSERT_EQUAL_STRING("pad", b.wavetables[0].name);
  TEST_ASSERT_EQUAL_HEX32(0xCAFEBABE, b.wavetables[0].crc);
  TEST_ASSERT_EQUAL_STRING("pad_table_name16", b.wavetables[1].name);
  TEST_ASSERT_EQUAL_HEX32(7, b.wavetables[1].crc);
}

void test_file_without_syn_chunks() {
  fillSyn(a.instruments[0]);
  a.wavetableCount = 1;
  strcpy(a.wavetables[0].name, "pad");
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"SYNI", "WTBL"});
  TEST_ASSERT_FALSE(hasChunk(f, "SYNI"));
  b.wavetableCount = 3;
  b.instruments[0].synSub = 50;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const Instrument def;
  const Instrument& r = b.instruments[0];
  TEST_ASSERT_EQUAL(0, b.wavetableCount);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(def.synOsc, r.synOsc, 2);
  TEST_ASSERT_EQUAL_STRING("", r.synWt[0]);
  TEST_ASSERT_EQUAL(def.synSemi, r.synSemi);
  TEST_ASSERT_EQUAL(def.synSub, r.synSub);
  TEST_ASSERT_EQUAL(def.synEDec, r.synEDec);
}

void test_syni_garbage_clamped() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> in = {1};
  std::vector<uint8_t> r(kSynRecSize, 0xFF);
  r[0] = 9;    // osc 1
  r[1] = 200;  // osc 2
  r[34] = 100; // semi
  in.insert(in.end(), r.begin(), r.end());
  putChunk(f, "SYNI", in);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const Instrument& m = b.instruments[0];
  TEST_ASSERT_EQUAL(0, m.synOsc[0]);
  TEST_ASSERT_EQUAL(0, m.synOsc[1]);
  TEST_ASSERT_EQUAL(16, static_cast<int>(strlen(m.synWt[0])));
  TEST_ASSERT_EQUAL(16, static_cast<int>(strlen(m.synWt[1])));
  TEST_ASSERT_EQUAL(24, m.synSemi);
  TEST_ASSERT_TRUE(m.synSync);
  TEST_ASSERT_EQUAL(127, m.synSub);
  TEST_ASSERT_EQUAL(1, m.synSubOct);
  TEST_ASSERT_EQUAL(127, m.synNoise);
  TEST_ASSERT_EQUAL(127, m.synEAtk);
  TEST_ASSERT_EQUAL(127, m.synEDec);
  f = fileHeader();
  in = {1};
  r.assign(kSynRecSize, 0);
  r[34] = static_cast<uint8_t>(-100);
  r[35] = 5;
  in.insert(in.end(), r.begin(), r.end());
  putChunk(f, "SYNI", in);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(-24, b.instruments[0].synSemi);
  TEST_ASSERT_TRUE(b.instruments[0].synSync);
}

static std::vector<uint8_t> wtblChunk(std::initializer_list<const char*> names) {
  std::vector<uint8_t> c = {static_cast<uint8_t>(names.size())};
  uint8_t crc = 1;
  for (const char* nm : names) {
    std::vector<uint8_t> r(20, 0);
    memcpy(r.data(), nm, strlen(nm));
    r[16] = crc++;
    c.insert(c.end(), r.begin(), r.end());
  }
  return c;
}

void test_wtbl_dup_bad_and_repeat() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "WTBL", wtblChunk({"old"}));
  putChunk(f, "WTBL", wtblChunk({"pad", "", "PAD", "*SAWSQR", "a.b", "lead"}));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(2, b.wavetableCount);  // the second chunk replaces the first
  TEST_ASSERT_EQUAL_STRING("pad", b.wavetables[0].name);
  TEST_ASSERT_EQUAL_HEX32(1, b.wavetables[0].crc);
  TEST_ASSERT_EQUAL_STRING("lead", b.wavetables[1].name);
  TEST_ASSERT_EQUAL_HEX32(6, b.wavetables[1].crc);
}

// ---- KITS ----

void test_kits_round_trip_and_defaults() {
  a.reset();
  Instrument& k = a.instruments[2];
  instrSetType(k, InstrType::Kit);
  strcpy(k.kit[0].sample, "kick");
  k.kit[0].vol = 90;
  k.kit[0].pitch = -3;
  k.kit[0].decay = 40;
  k.kit[5].instr = 7;
  k.kit[5].note = 100;
  strcpy(k.kit[7].sample, "sixteen_chars_nm");  // 16 chars, no terminator in the file
  a.instruments[0].kit[3].note = 63;             // lanes of a non-KIT instrument are stored too
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  const Instrument& r = b.instruments[2];
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Kit), static_cast<int>(r.type));
  TEST_ASSERT_EQUAL_STRING("kick", r.kit[0].sample);
  TEST_ASSERT_EQUAL(90, r.kit[0].vol);
  TEST_ASSERT_EQUAL(-3, r.kit[0].pitch);
  TEST_ASSERT_EQUAL(40, r.kit[0].decay);
  TEST_ASSERT_EQUAL(kNoInstr, r.kit[0].instr);
  TEST_ASSERT_EQUAL(60, r.kit[0].note);
  TEST_ASSERT_EQUAL(7, r.kit[5].instr);
  TEST_ASSERT_EQUAL(100, r.kit[5].note);
  TEST_ASSERT_EQUAL_STRING("sixteen_chars_nm", r.kit[7].sample);
  TEST_ASSERT_EQUAL(67, r.kit[7].note);  // kitSetDefaults: 60 + lane
  TEST_ASSERT_EQUAL(63, b.instruments[0].kit[3].note);

  // An older file without KITS: every instrument keeps Instrument() lanes (none of them is a KIT).
  std::vector<uint8_t> f = fileHeader();
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kNoInstr, b.instruments[0].kit[0].instr);
  TEST_ASSERT_EQUAL(100, b.instruments[0].kit[0].vol);
  TEST_ASSERT_EQUAL_STRING("", b.instruments[0].kit[0].sample);
  TEST_ASSERT_EQUAL(60, b.instruments[0].kit[7].note);
}

void test_kits_garbage_clamped() {
  std::vector<uint8_t> rec(16 * 176, 0);
  rec[17] = 40;   // lane 0 instr -> kNoInstr
  rec[18] = 200;  // vol -> 127
  rec[19] = 100;  // pitch -> 24
  rec[21] = 200;  // note -> 60
  rec[22 + 19] = static_cast<uint8_t>(-100);  // lane 1 pitch -> -24
  rec[22 + 21] = 200;                         // lane 1 note -> 61
  std::vector<uint8_t> body = {16};
  body.insert(body.end(), rec.begin(), rec.end());
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "KITS", body);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kNoInstr, b.instruments[0].kit[0].instr);
  TEST_ASSERT_EQUAL(127, b.instruments[0].kit[0].vol);
  TEST_ASSERT_EQUAL(24, b.instruments[0].kit[0].pitch);
  TEST_ASSERT_EQUAL(60, b.instruments[0].kit[0].note);
  TEST_ASSERT_EQUAL(-24, b.instruments[0].kit[1].pitch);
  TEST_ASSERT_EQUAL(61, b.instruments[0].kit[1].note);
}

void test_patn_vel_mask_only_on_drum_tracks() {
  a.reset();
  instrSetType(a.instruments[2], InstrType::Kit);
  a.tracks[1].instr = 2;
  a.patterns[0].steps[1][3].note = 100;
  a.patterns[0].steps[1][3].vel = 0xA5;  // lane mask on the drum track
  a.patterns[0].steps[0][3].note = 60;
  a.patterns[0].steps[0][3].vel = 0xA5;  // garbage on a melodic track: masked to 0..127
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL_HEX8(0xA5, b.patterns[0].steps[1][3].vel);
  TEST_ASSERT_EQUAL_HEX8(0x25, b.patterns[0].steps[0][3].vel);
}

// ---- Song + live chunks: CHN2, TLEN, SCNS ----

void test_chn2_round_trip() {
  a.reset();
  a.chainLen = 3;
  a.chain[0] = 2;
  a.chainTr[0] = -5;
  a.chainRep[0] = 4;
  a.chainScene[0] = 3;
  a.chain[2] = 7;
  a.chainTr[2] = 12;
  a.chainRep[2] = 1;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(3, b.chainLen);
  TEST_ASSERT_EQUAL(2, b.chain[0]);
  TEST_ASSERT_EQUAL(-5, b.chainTr[0]);
  TEST_ASSERT_EQUAL(4, b.chainRep[0]);
  TEST_ASSERT_EQUAL(3, b.chainScene[0]);
  TEST_ASSERT_EQUAL(7, b.chain[2]);
  TEST_ASSERT_EQUAL(12, b.chainTr[2]);
  TEST_ASSERT_EQUAL(1, b.chainRep[1]);
  TEST_ASSERT_EQUAL(0, b.chainScene[1]);
}

// A file without CHN2 (old firmware): chain from PROJ, tr 0, rep 1, scene 0.
void test_chain_without_chn2_defaults() {
  a.reset();
  a.chainLen = 2;
  a.chain[1] = 5;
  a.chainTr[1] = 7;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"CHN2"});
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(2, b.chainLen);
  TEST_ASSERT_EQUAL(5, b.chain[1]);
  TEST_ASSERT_EQUAL(0, b.chainTr[1]);
  TEST_ASSERT_EQUAL(1, b.chainRep[1]);
  TEST_ASSERT_EQUAL(0, b.chainScene[1]);
}

void test_chn2_garbage_clamped() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "CHN2", {1, 200, 90, 0, 9});  // count 1: pattern 200, transpose +90, repeat 0, scene 9
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(1, b.chainLen);
  TEST_ASSERT_EQUAL(kPatterns - 1, b.chain[0]);
  TEST_ASSERT_EQUAL(kChainTrMax, b.chainTr[0]);
  TEST_ASSERT_EQUAL(1, b.chainRep[0]);
  TEST_ASSERT_EQUAL(0, b.chainScene[0]);
  f = fileHeader();
  putChunk(f, "CHN2", {1, 3, static_cast<uint8_t>(-90), 40, 8});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(-kChainTrMax, b.chainTr[0]);
  TEST_ASSERT_EQUAL(kChainRepMax, b.chainRep[0]);
  TEST_ASSERT_EQUAL(kScenes, b.chainScene[0]);
}

void test_tlen_round_trip_and_default() {
  a.reset();
  a.patterns[2].steps[0][0].note = 60;  // stored pattern
  a.patterns[2].trackLen[5] = 3;
  a.patterns[4].trackLen[15] = 7;       // stored only for its track length
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(3, b.patterns[2].trackLen[5]);
  TEST_ASSERT_EQUAL(0, b.patterns[2].trackLen[4]);
  TEST_ASSERT_EQUAL(7, b.patterns[4].trackLen[15]);
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"TLEN"});
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.patterns[2].trackLen[5]);
}

void test_tlen_clamped_to_length() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PATN", patn(1, 8));
  std::vector<uint8_t> tl(1 + kTracks, 0);
  tl[0] = 1;
  tl[1] = 200;  // track 1: past the length 8
  tl[2] = 5;
  putChunk(f, "TLEN", tl);
  tl[0] = 40;   // no such pattern: skipped
  putChunk(f, "TLEN", tl);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(8, b.patterns[1].trackLen[0]);
  TEST_ASSERT_EQUAL(5, b.patterns[1].trackLen[1]);
  f = fileHeader();
  putChunk(f, "TLEN", {1, 2, 3});  // wrong size
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

void test_scns_round_trip_and_default() {
  a.reset();
  a.scenes[0] = 0x0005;
  a.scenes[7] = 0x8000;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL_HEX16(0x0005, b.scenes[0]);
  TEST_ASSERT_EQUAL_HEX16(0x8000, b.scenes[7]);
  TEST_ASSERT_EQUAL_HEX16(kSceneEmpty, b.scenes[3]);
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"SCNS"});
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL_HEX16(kSceneEmpty, b.scenes[0]);
}

void test_audi_sound_fx_defaults_and_clamps() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "AUDI", {50, 1, 4, 10, 20, 30});  // a 6-byte (older) AUDI
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(30, b.dlyLevel);
  TEST_ASSERT_EQUAL(60, b.rvbSize);
  TEST_ASSERT_EQUAL(70, b.rvbDamp);
  TEST_ASSERT_EQUAL(80, b.rvbLevel);
  TEST_ASSERT_EQUAL(0, b.compAmt);
  TEST_ASSERT_EQUAL(50, b.compRel);
  TEST_ASSERT_EQUAL(0, b.scTrack);
  TEST_ASSERT_EQUAL(64, b.scDepth);
  f = fileHeader();
  putChunk(f, "AUDI", {50, 1, 4, 10, 20, 30, 200, 200, 200, 200, 200, 17, 200});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(127, b.rvbSize);
  TEST_ASSERT_EQUAL(127, b.compAmt);
  TEST_ASSERT_EQUAL(0, b.scTrack);  // no such track: off
  TEST_ASSERT_EQUAL(127, b.scDepth);
}

// Sample / wavetable names of a file without loading it; a bad CRC is reported.
void test_read_file_names() {
  Project p;
  projSampleSet(p, "KICK", 1, 100);
  projSampleSet(p, "Snare2", 2, 200);
  projWtSet(p, "PAD", 3);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(p, out));
  static ProjectFileNames n;
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(readProjectFileNames(in, n)));
  TEST_ASSERT_EQUAL(2, n.samples);
  TEST_ASSERT_EQUAL(1, n.wavetables);
  TEST_ASSERT_TRUE(n.has("kick", false));
  TEST_ASSERT_TRUE(n.has("SNARE2", false));
  TEST_ASSERT_FALSE(n.has("PAD", false));
  TEST_ASSERT_TRUE(n.has("pad", true));
  out.buf[20] ^= 0xFF;  // inside a chunk
  VecSource bad(out.buf);
  TEST_ASSERT_TRUE(readProjectFileNames(bad, n) != LoadErr::Ok);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_reference);
  RUN_TEST(test_samples_bad_names_skipped);
  RUN_TEST(test_round_trip_full_project);
  RUN_TEST(test_load_resets_target_first);
  RUN_TEST(test_empty_patterns_not_written);
  RUN_TEST(test_unknown_chunk_skipped);
  RUN_TEST(test_corrupt_byte_bad_crc);
  RUN_TEST(test_truncated_file);
  RUN_TEST(test_missing_crc_is_truncated);
  RUN_TEST(test_bad_magic);
  RUN_TEST(test_newer_version_rejected);
  RUN_TEST(test_garbage_values_clamped);
  RUN_TEST(test_patn_length_zero);
  RUN_TEST(test_patn_length_over_max);
  RUN_TEST(test_patn_size_mismatch);
  RUN_TEST(test_patn_header_only_len_nonzero);
  RUN_TEST(test_patn_eight_tracks_loads);
  RUN_TEST(test_patn_sixteen_tracks_round_trip);
  RUN_TEST(test_trks_count_not_eight);
  RUN_TEST(test_unknown_chunk_huge_size);
  RUN_TEST(test_proj_chunk_larger_is_skipped);
  RUN_TEST(test_audio_chunks_written);
  RUN_TEST(test_old_file_gets_audio_defaults);
  RUN_TEST(test_old_file_tout_new_tracks_default_int);
  RUN_TEST(test_audio_garbage_clamped);
  RUN_TEST(test_inst_count_over_max);
  RUN_TEST(test_audio_chunks_too_small);
  RUN_TEST(test_fmin_garbage_clamped);
  RUN_TEST(test_samples_roundtrip);
  RUN_TEST(test_samples_empty_list_written);
  RUN_TEST(test_samples_dup_and_empty_skipped);
  RUN_TEST(test_samples_count_over_max);
  RUN_TEST(test_samples_chunk_too_small);
  RUN_TEST(test_samples_two_chunks_last_wins);
  RUN_TEST(test_fltr_roundtrip);
  RUN_TEST(test_file_without_fltr);
  RUN_TEST(test_fltr_garbage_clamped);
  RUN_TEST(test_audi_delay_garbage_clamped);
  RUN_TEST(test_drum_type_roundtrip);
  RUN_TEST(test_fm_machine_out_of_range);
  RUN_TEST(test_slices_roundtrip);
  RUN_TEST(test_slices_bad_order_truncated);
  RUN_TEST(test_slices_count_clamped);
  RUN_TEST(test_syn_roundtrip);
  RUN_TEST(test_file_without_syn_chunks);
  RUN_TEST(test_syni_garbage_clamped);
  RUN_TEST(test_wtbl_dup_bad_and_repeat);
  RUN_TEST(test_kits_round_trip_and_defaults);
  RUN_TEST(test_kits_garbage_clamped);
  RUN_TEST(test_patn_vel_mask_only_on_drum_tracks);
  RUN_TEST(test_chn2_round_trip);
  RUN_TEST(test_chain_without_chn2_defaults);
  RUN_TEST(test_chn2_garbage_clamped);
  RUN_TEST(test_tlen_round_trip_and_default);
  RUN_TEST(test_tlen_clamped_to_length);
  RUN_TEST(test_scns_round_trip_and_default);
  RUN_TEST(test_audi_sound_fx_defaults_and_clamps);
  RUN_TEST(test_read_file_names);
  return UNITY_END();
}
