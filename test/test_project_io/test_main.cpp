#include <stdio.h>
#include <string.h>
#include <unity.h>
#include <vector>
#include "bytebeat_lib.h"
#include "inst_codec.h"
#include "project_io.h"
#include "scene.h"
#include "sample_set.h"
#include "scale.h"

using namespace mt;

// Legacy song chunks (PROJ chain, CHN2, SCNS): limits of the old model.
static constexpr int kOldChainMax = 64;
static constexpr int kOldScenes = 8;
static constexpr uint16_t kOldSceneEmpty = 0xFFFF;

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
  p.songLen = 5;
  const uint8_t ch[] = {0, 3, 3, 15, 7};
  const uint16_t mu[] = {0, 0x0001, 0x8001, 0xFFFF, 0x1234};
  memcpy(p.song, ch, sizeof(ch));
  memcpy(p.songMute, mu, sizeof(mu));
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
  p.perfMap[3] = static_cast<uint8_t>(PerfFx::ReverbThrow);
  p.perfDepth[3] = 70;
  p.perfDepth[0] = 6;  // Repeat 1/32
  p.perfXy[0] = static_cast<uint8_t>(PerfFx::Repeat);
  p.perfXy[1] = 0;  // None
  p.gen[4] = GenCfg{static_cast<uint8_t>(GenMode::Pitch), 16, 0, 40, 4, 0, 777};
  p.evo[4] = EvoCfg{55, 3, 12, kEvoRhythm | kEvoShift};
  p.tracks[6].humanize = 40;
  p.tracks[2].pan = 0;
  p.tracks[4].pan = 47;
  p.tracks[9].pan = 127;
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
  i7.crushBits = 101;
  i7.crushRate = 7;
}

static void assertSame(const Project& x, const Project& y) {
  TEST_ASSERT_EQUAL_STRING(x.name, y.name);
  TEST_ASSERT_EQUAL(x.bpm, y.bpm);
  TEST_ASSERT_EQUAL(x.scaleRoot, y.scaleRoot);
  TEST_ASSERT_EQUAL(x.scaleType, y.scaleType);
  TEST_ASSERT_EQUAL(x.songLen, y.songLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(x.song, y.song, x.songLen);
  TEST_ASSERT_EQUAL_UINT16_ARRAY(x.songMute, y.songMute, x.songLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(x.perfMap, y.perfMap, kPerfButtons);
  TEST_ASSERT_EQUAL_INT8_ARRAY(x.perfDepth, y.perfDepth, kPerfButtons);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(x.perfXy, y.perfXy, 2);
  TEST_ASSERT_EQUAL_MEMORY(x.gen, y.gen, sizeof(x.gen));
  for (int t = 0; t < kTracks; ++t) {
    TEST_ASSERT_EQUAL(x.evo[t].amount, y.evo[t].amount);
    TEST_ASSERT_EQUAL(x.evo[t].every, y.evo[t].every);
    TEST_ASSERT_EQUAL(x.evo[t].home, y.evo[t].home);
    TEST_ASSERT_EQUAL(x.evo[t].mask, y.evo[t].mask);
  }
  for (int t = 0; t < kTracks; ++t) {
    const TrackCfg &c = x.tracks[t], &d = y.tracks[t];
    TEST_ASSERT_EQUAL(c.humanize, d.humanize);
    TEST_ASSERT_EQUAL(c.pan, d.pan);
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
    TEST_ASSERT_EQUAL(0, n.drive);  // the track insert's now: zeroed on load
    TEST_ASSERT_EQUAL(m.rsend, n.rsend);
    TEST_ASSERT_EQUAL(m.velCut, n.velCut);
    TEST_ASSERT_EQUAL(m.velMac, n.velMac);
    TEST_ASSERT_EQUAL(0, n.crushBits);
    TEST_ASSERT_EQUAL(0, n.crushRate);
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
  b.songLen = 3;
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_TRUE(b.patterns[3].isEmpty());
  TEST_ASSERT_EQUAL(0, b.songLen);
}

void test_empty_patterns_not_written() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  // Everything but patterns: project, track and audio chunks (< 2 KB) plus the instrument records
  // (INST 48, FMIN 16, FLTR 8, SLCE 72, SYNI 48, LOFI 8, LFOX 16, KITS 176 = 392 bytes each). One written
  // empty pattern would add ~28 KB, so the bound proves none is.
  TEST_ASSERT_TRUE(out.buf.size() < 2048 + kInstruments * 400);
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
  for (int i = 0; i < kOldChainMax; ++i) pr.push_back(99);
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
  TEST_ASSERT_EQUAL(kOldChainMax, b.songLen);  // the PROJ chain, migrated
  for (int i = 0; i < kOldChainMax; ++i) TEST_ASSERT_TRUE(b.song[i] < kPatterns && b.songMute[i] == 0);
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
  TEST_ASSERT_EQUAL(100, b.masterVol);
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

// DRUM machines past 16 (the second engine) survive a save / load.
void test_drum2_machine_roundtrip() {
  Instrument& m = a.instruments[4];
  instrSetType(m, InstrType::Drum);
  drumSetMachine(m, static_cast<uint8_t>(DrumMachine::Hhr));
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Drum), static_cast<int>(b.instruments[4].type));
  TEST_ASSERT_EQUAL(23, b.instruments[4].machine);
}

// The filter types after HP (LADDER .. FORMANT) survive a save / load; past them clamps to OFF.
void test_fltr_new_modes_roundtrip() {
  const FltMode modes[] = {FltMode::Ladder, FltMode::Ms20, FltMode::Comb, FltMode::Formant};
  for (int k = 0; k < 4; ++k) a.instruments[k].fltMode = static_cast<uint8_t>(modes[k]);
  a.instruments[4].fltMode = static_cast<uint8_t>(FltMode::Count);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  for (int k = 0; k < 4; ++k) TEST_ASSERT_EQUAL(static_cast<int>(modes[k]), b.instruments[k].fltMode);
  TEST_ASSERT_EQUAL(static_cast<int>(FltMode::Off), b.instruments[4].fltMode);
}

// The new LFO targets (LFO-to-LFO, SEMI2) survive a project save / load.
void test_lfo_dests_roundtrip() {
  Instrument& m = a.instruments[6];
  m.lfoDest = static_cast<uint8_t>(LfoDest::Depth2);
  m.lfo[0].dest = static_cast<uint8_t>(LfoDest::Rate1);
  m.lfo[1].dest = static_cast<uint8_t>(LfoDest::Semi2);
  m.lfo[2].dest = static_cast<uint8_t>(LfoDest::Rtrg4);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  const Instrument& r = b.instruments[6];
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Depth2), r.lfoDest);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Rate1), r.lfo[0].dest);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Semi2), r.lfo[1].dest);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Rtrg4), r.lfo[2].dest);
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
  m.synFold = 77;
  m.synRing = 33;
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
  TEST_ASSERT_EQUAL(77, r.synFold);
  TEST_ASSERT_EQUAL(33, r.synRing);
  TEST_ASSERT_EQUAL(2, b.wavetableCount);
  TEST_ASSERT_EQUAL_STRING("pad", b.wavetables[0].name);
  TEST_ASSERT_EQUAL_HEX32(0xCAFEBABE, b.wavetables[0].crc);
  TEST_ASSERT_EQUAL_STRING("pad_table_name16", b.wavetables[1].name);
  TEST_ASSERT_EQUAL_HEX32(7, b.wavetables[1].crc);
}

// Type 7 is a former type: an instrument stored with it loads as CHIP, the rest of the record intact.
void test_type7_loads_as_chip() {
  Instrument& m = a.instruments[5];
  instrSetType(m, InstrType::Fm);
  strcpy(m.name, "OLD7");
  m.vol = 77;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  std::vector<uint8_t> f(out.buf.begin(), out.buf.end() - 12);  // without the CRC tail
  size_t pos = 8;
  bool patched = false;
  while (pos + 8 <= f.size()) {
    const uint32_t size = f[pos + 4] | (f[pos + 5] << 8) | (f[pos + 6] << 16) |
                          (static_cast<uint32_t>(f[pos + 7]) << 24);
    if (memcmp(f.data() + pos, "INST", 4) == 0) {
      f[pos + 8 + 1 + 5 * kInstRecSize + 8] = 7;  // INST byte 8 of record 5
      patched = true;
    }
    pos += 8 + size;
  }
  TEST_ASSERT_TRUE(patched);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const Instrument& r = b.instruments[5];
  TEST_ASSERT_TRUE(r.type == InstrType::Chip);
  TEST_ASSERT_EQUAL_STRING("OLD7", r.name);
  TEST_ASSERT_EQUAL(77, r.vol);
}

// STRING: the model in machine, DAMP / BRIGHT / STRUCT / POS / EXC in the macros.
void test_string_instrument_roundtrip() {
  Instrument& m = a.instruments[6];
  instrSetType(m, InstrType::String);
  m.machine = static_cast<uint8_t>(StringModel::Modal);
  m.macro[kMacDamp] = 10;
  m.macro[kMacBright] = 90;
  m.macro[kMacStruct] = 64;
  m.macro[kMacPos] = 20;
  m.macro[kMacExc] = 127;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(out.buf)));
  const Instrument& r = b.instruments[6];
  TEST_ASSERT_TRUE(r.type == InstrType::String);
  TEST_ASSERT_EQUAL(static_cast<int>(StringModel::Modal), r.machine);
  for (int k = 0; k < kFmMacros; ++k) TEST_ASSERT_EQUAL(m.macro[k], r.macro[k]);
  TEST_ASSERT_EQUAL(10, r.macro[kMacDamp]);
  TEST_ASSERT_EQUAL(90, r.macro[kMacBright]);
  TEST_ASSERT_EQUAL(64, r.macro[kMacStruct]);
  TEST_ASSERT_EQUAL(20, r.macro[kMacPos]);
  TEST_ASSERT_EQUAL(127, r.macro[kMacExc]);
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
  TEST_ASSERT_EQUAL(0, r.synFold);
  TEST_ASSERT_EQUAL(0, r.synRing);
}

void test_syni_garbage_clamped() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> in = {1};
  std::vector<uint8_t> r(kSynRecSize, 0xFF);
  r[0] = 9;    // osc 1
  r[1] = 200;  // osc 2
  r[34] = 100; // semi
  r[41] = 200; // fold
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
  TEST_ASSERT_EQUAL(127, m.synFold);  // 200 clamped
  TEST_ASSERT_EQUAL(127, m.synRing);  // 0xFF clamped
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
  TEST_ASSERT_EQUAL(0, b.instruments[0].synFold);  // bytes 41 / 42 once reserved 0: off
  TEST_ASSERT_EQUAL(0, b.instruments[0].synRing);
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

// ---- Song (SNG3), legacy song chunks (CHN2, SCNS, PROJ chain), TLEN ----

void test_sng3_round_trip() {
  a.songLen = 3;
  a.song[0] = 2;
  a.song[1] = 15;
  a.song[2] = 7;
  a.songMute[0] = 0x0005;
  a.songMute[2] = 0xFFFF;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "SNG3"));
  LoadNotes notes;
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b, &notes)));
  TEST_ASSERT_EQUAL(3, b.songLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(a.song, b.song, 3);
  TEST_ASSERT_EQUAL_UINT16_ARRAY(a.songMute, b.songMute, 3);
  TEST_ASSERT_FALSE(notes.songTransposeDropped);
}

void test_sng3_garbage_clamped() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "SNG3", {2, 200, 0x34, 0x12, 5, 0, 0});  // pattern 200 -> last
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(2, b.songLen);
  TEST_ASSERT_EQUAL(kPatterns - 1, b.song[0]);
  TEST_ASSERT_EQUAL_HEX16(0x1234, b.songMute[0]);
  TEST_ASSERT_EQUAL(5, b.song[1]);
  f = fileHeader();
  putChunk(f, "SNG3", {2, 1, 0, 0});  // count 2, one record
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

void test_save_writes_no_legacy_song_chunks() {
  fillFull(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_FALSE(hasChunk(out.buf, "CHN2"));
  TEST_ASSERT_FALSE(hasChunk(out.buf, "SCNS"));
  // PROJ keeps its size; songMode, chainLen and the chain are 0 (old firmware sees no chain).
  TEST_ASSERT_EQUAL(0, memcmp(out.buf.data() + 8, "PROJ", 4));
  const uint32_t size = out.buf[12] | (out.buf[13] << 8);
  TEST_ASSERT_EQUAL(17 + 2 + 4 + kOldChainMax, size);
  for (uint32_t i = 21; i < size; ++i) TEST_ASSERT_EQUAL(0, out.buf[16 + i]);
}

// A PROJ payload of the old firmware with the given chain.
static std::vector<uint8_t> oldProj(std::initializer_list<uint8_t> chain) {
  std::vector<uint8_t> pr(17, 0);
  memcpy(pr.data(), "old", 3);
  put16(pr, 120);
  pr.push_back(0);
  pr.push_back(0);
  pr.push_back(1);  // songMode
  pr.push_back(static_cast<uint8_t>(chain.size()));
  pr.insert(pr.end(), chain.begin(), chain.end());
  pr.resize(17 + 2 + 4 + kOldChainMax, 0);
  return pr;
}

// CHN2 + SCNS (written after it) become song slots: repeats expanded, scene masks, else the
// previous slot's mask; a transpose is dropped and reported.
void test_chn2_scns_migrated() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PROJ", oldProj({9, 9}));  // CHN2 wins over the PROJ chain
  putChunk(f, "CHN2", {3, 1, 0, 2, 1, 2, 0, 1, 0, 3, 2, 1, 2});  // {P1 x2 sc1} {P2} {P3 tr+2 sc2}
  std::vector<uint8_t> sc;
  put16(sc, 0x0003);
  for (int i = 1; i < kOldScenes; ++i) put16(sc, kOldSceneEmpty);
  putChunk(f, "SCNS", sc);
  finish(f);
  LoadNotes notes;
  VecSource in(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b, &notes)));
  TEST_ASSERT_EQUAL(4, b.songLen);
  const uint8_t pats[] = {1, 1, 2, 3};
  const uint16_t mutes[] = {3, 3, 3, 3};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pats, b.song, 4);
  TEST_ASSERT_EQUAL_UINT16_ARRAY(mutes, b.songMute, 4);
  TEST_ASSERT_TRUE(notes.songTransposeDropped);
}

// A file without CHN2 (oldest firmware): the PROJ chain, one slot per item, no mutes.
void test_proj_chain_migrated() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PROJ", oldProj({4, 0, 7}));
  finish(f);
  LoadNotes notes;
  VecSource in(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b, &notes)));
  TEST_ASSERT_EQUAL(3, b.songLen);
  const uint8_t pats[] = {4, 0, 7};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pats, b.song, 3);
  for (int i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_HEX16(0, b.songMute[i]);
  TEST_ASSERT_FALSE(notes.songTransposeDropped);
}

// Expanded repeats stop at kSongMax; garbage CHN2 values are clamped.
void test_chn2_migration_capped_and_clamped() {
  std::vector<uint8_t> chn = {10};
  for (int i = 0; i < 10; ++i) {
    const uint8_t r[] = {200, 0, 40, 9};  // pattern 200, repeat 40 (-> 16), no such scene
    chn.insert(chn.end(), r, r + 4);
  }
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "CHN2", chn);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kSongMax, b.songLen);
  for (int i = 0; i < kSongMax; ++i) TEST_ASSERT_TRUE(b.song[i] == kPatterns - 1 && b.songMute[i] == 0);
  f = fileHeader();
  putChunk(f, "CHN2", {1, 3, 0, 0, 0});  // repeat 0 counts as 1
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(1, b.songLen);
  TEST_ASSERT_EQUAL(3, b.song[0]);
}

// SNG3 wins over the legacy chunks.
void test_sng3_skips_migration() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "SNG3", {1, 6, 1, 0});
  putChunk(f, "CHN2", {1, 3, 5, 2, 0});
  finish(f);
  LoadNotes notes;
  VecSource in(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b, &notes)));
  TEST_ASSERT_EQUAL(1, b.songLen);
  TEST_ASSERT_EQUAL(6, b.song[0]);
  TEST_ASSERT_EQUAL_HEX16(1, b.songMute[0]);
  TEST_ASSERT_FALSE(notes.songTransposeDropped);
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

void test_tspd_round_trip_and_default() {
  a.reset();
  a.patterns[2].steps[0][0].note = 60;  // stored pattern
  a.patterns[2].trackSpeed[5] = static_cast<uint8_t>(TrackSpeed::X2);
  a.patterns[4].trackSpeed[15] = static_cast<uint8_t>(TrackSpeed::Quarter);  // stored only for its speed
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(static_cast<int>(TrackSpeed::X2), b.patterns[2].trackSpeed[5]);
  TEST_ASSERT_EQUAL(0, b.patterns[2].trackSpeed[4]);
  TEST_ASSERT_EQUAL(static_cast<int>(TrackSpeed::Quarter), b.patterns[4].trackSpeed[15]);
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"TSPD"});
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.patterns[2].trackSpeed[5]);
}

// TPAN: the track pans; a file without it (older firmware) loads every track centred.
void test_tpan_round_trip_and_default() {
  a.reset();
  TEST_ASSERT_EQUAL(64, a.tracks[0].pan);  // fresh project: centre
  a.tracks[3].pan = 10;
  a.tracks[7].pan = 127;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "TPAN"));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  for (int t = 0; t < kTracks; ++t) TEST_ASSERT_EQUAL(a.tracks[t].pan, b.tracks[t].pan);
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"TPAN"});
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  for (int t = 0; t < kTracks; ++t) TEST_ASSERT_EQUAL(64, b.tracks[t].pan);
}

// TPAN values over 127 are clamped; tracks the chunk lacks stay centred.
void test_tpan_garbage_clamped() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TRKS", trks(kTracks, kTracks));
  putChunk(f, "TPAN", {2, 200, 5});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(127, b.tracks[0].pan);
  TEST_ASSERT_EQUAL(5, b.tracks[1].pan);
  TEST_ASSERT_EQUAL(64, b.tracks[2].pan);
}

// TINS: the tracks' insert settings round trip; the instruments' old fields load as 0.
void test_tins_round_trip() {
  a.tracks[2].drive = 90;
  a.tracks[2].crushBits = 30;
  a.tracks[2].crushRate = 7;
  a.tracks[15].drive = 127;
  a.tracks[2].driveModel = static_cast<uint8_t>(DriveModel::Fold);
  a.tracks[9].driveModel = static_cast<uint8_t>(DriveModel::Asym);
  a.instruments[0].drive = 50;  // a file with TINS: no migration
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "TINS"));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  for (int t = 0; t < kTracks; ++t) {
    TEST_ASSERT_EQUAL(a.tracks[t].drive, b.tracks[t].drive);
    TEST_ASSERT_EQUAL(a.tracks[t].crushBits, b.tracks[t].crushBits);
    TEST_ASSERT_EQUAL(a.tracks[t].crushRate, b.tracks[t].crushRate);
    TEST_ASSERT_EQUAL(a.tracks[t].driveModel, b.tracks[t].driveModel);
  }
  TEST_ASSERT_EQUAL(0, b.instruments[0].drive);
}

// Longer records (a newer firmware's fields) read the known part; values clamped; tracks the chunk
// lacks stay off; a record size 0 or a short chunk is bad.
void test_tins_longer_records_and_garbage() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TRKS", trks(kTracks, kTracks));
  putChunk(f, "TINS", {2, 5, 10, 20, 30, 99, 99, 200, 1, 2, 3, 4});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(10, b.tracks[0].drive);
  TEST_ASSERT_EQUAL(20, b.tracks[0].crushBits);
  TEST_ASSERT_EQUAL(30, b.tracks[0].crushRate);
  TEST_ASSERT_EQUAL(0, b.tracks[0].driveModel);  // 99: unknown model -> TANH
  TEST_ASSERT_EQUAL(127, b.tracks[1].drive);
  TEST_ASSERT_EQUAL(1, b.tracks[1].crushBits);
  TEST_ASSERT_EQUAL(2, b.tracks[1].crushRate);
  TEST_ASSERT_EQUAL(3, b.tracks[1].driveModel);
  TEST_ASSERT_EQUAL(0, b.tracks[2].drive);
  f = fileHeader();
  putChunk(f, "TINS", {1, 1, 40});  // an older, shorter record: drive only
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(40, b.tracks[0].drive);
  TEST_ASSERT_EQUAL(0, b.tracks[0].crushBits);
  f = fileHeader();
  putChunk(f, "TINS", {1, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  f = fileHeader();
  putChunk(f, "TINS", {2, 3, 1, 2, 3});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

// A feature-1 file: 3-byte records, no model -> TANH (sounds as it did).
void test_tins_three_byte_records() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TINS", {2, 3, 50, 0, 0, 60, 1, 2});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(50, b.tracks[0].drive);
  TEST_ASSERT_EQUAL(60, b.tracks[1].drive);
  TEST_ASSERT_EQUAL(2, b.tracks[1].crushRate);
  TEST_ASSERT_EQUAL(0, b.tracks[0].driveModel);
  TEST_ASSERT_EQUAL(0, b.tracks[1].driveModel);
}

// A record assigns only the fields it holds: the rest keep their (default) values, not 0.
void test_tins_record_assigns_present_fields_only() {
  TrackCfg c;
  c.drive = 11;
  c.crushBits = 22;
  c.crushRate = 33;
  c.driveModel = 2;
  const uint8_t r[] = {40, 200, 7, 4, 99};
  readTinsRecord(r, 1, c);
  TEST_ASSERT_EQUAL(40, c.drive);
  TEST_ASSERT_EQUAL(22, c.crushBits);
  TEST_ASSERT_EQUAL(33, c.crushRate);
  TEST_ASSERT_EQUAL(2, c.driveModel);
  readTinsRecord(r, 3, c);
  TEST_ASSERT_EQUAL(127, c.crushBits);  // clamped
  TEST_ASSERT_EQUAL(7, c.crushRate);
  TEST_ASSERT_EQUAL(2, c.driveModel);
  readTinsRecord(r, 4, c);
  TEST_ASSERT_EQUAL(4, c.driveModel);
  TEST_ASSERT_EQUAL_HEX16(0x5555, c.gateMask);  // not in a 4-byte record: the default
  const uint8_t bad[] = {0, 0, 0, static_cast<uint8_t>(DriveModel::Count)};
  readTinsRecord(bad, 4, c);
  TEST_ASSERT_EQUAL(0, c.driveModel);
}

// The trance gate fields after driveModel: mask lo, hi, rate, depth, len, attack, release. A
// 4-byte (feature 2) record keeps the gate's defaults (mask 0x5555, off); values clamped; a
// longer record's tail is skipped.
void test_tins_gate_fields() {
  a.tracks[1].gateMask = 0xA5C3;
  a.tracks[1].gateRate = static_cast<uint8_t>(GateRate::R8);
  a.tracks[1].gateDepth = 99;
  a.tracks[1].gateLen = 40;
  a.tracks[1].gateAtk = 3;
  a.tracks[1].gateRel = 120;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  for (int t = 0; t < kTracks; ++t) {
    TEST_ASSERT_EQUAL_HEX16(a.tracks[t].gateMask, b.tracks[t].gateMask);
    TEST_ASSERT_EQUAL(a.tracks[t].gateRate, b.tracks[t].gateRate);
    TEST_ASSERT_EQUAL(a.tracks[t].gateDepth, b.tracks[t].gateDepth);
    TEST_ASSERT_EQUAL(a.tracks[t].gateLen, b.tracks[t].gateLen);
    TEST_ASSERT_EQUAL(a.tracks[t].gateAtk, b.tracks[t].gateAtk);
    TEST_ASSERT_EQUAL(a.tracks[t].gateRel, b.tracks[t].gateRel);
  }
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TINS", {1, 4, 50, 0, 0, 2});  // feature 2
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const TrackCfg d;
  TEST_ASSERT_EQUAL(2, b.tracks[0].driveModel);
  TEST_ASSERT_EQUAL_HEX16(0x5555, b.tracks[0].gateMask);
  TEST_ASSERT_EQUAL(d.gateRate, b.tracks[0].gateRate);
  TEST_ASSERT_EQUAL(0, b.tracks[0].gateDepth);
  TEST_ASSERT_EQUAL(100, b.tracks[0].gateLen);
  TEST_ASSERT_EQUAL(d.gateAtk, b.tracks[0].gateAtk);
  TEST_ASSERT_EQUAL(d.gateRel, b.tracks[0].gateRel);
  f = fileHeader();
  putChunk(f, "TINS", {2, 12, 0, 0, 0, 0, 0x34, 0x12, 9, 200, 0, 255, 255, 77,
                       0, 0, 0, 0, 0xFF, 0x00, 0, 5, 101, 6, 7, 77});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL_HEX16(0x1234, b.tracks[0].gateMask);
  TEST_ASSERT_EQUAL(static_cast<int>(GateRate::R16), b.tracks[0].gateRate);  // unknown rate
  TEST_ASSERT_EQUAL(127, b.tracks[0].gateDepth);
  TEST_ASSERT_EQUAL(1, b.tracks[0].gateLen);
  TEST_ASSERT_EQUAL(127, b.tracks[0].gateAtk);
  TEST_ASSERT_EQUAL(127, b.tracks[0].gateRel);
  TEST_ASSERT_EQUAL_HEX16(0x00FF, b.tracks[1].gateMask);
  TEST_ASSERT_EQUAL(0, b.tracks[1].gateRate);
  TEST_ASSERT_EQUAL(5, b.tracks[1].gateDepth);
  TEST_ASSERT_EQUAL(100, b.tracks[1].gateLen);
  TEST_ASSERT_EQUAL(6, b.tracks[1].gateAtk);
  TEST_ASSERT_EQUAL(7, b.tracks[1].gateRel);
  TEST_ASSERT_EQUAL(77, b.tracks[0].modSend);  // byte 11 (feature 5)
  TEST_ASSERT_EQUAL(77, b.tracks[1].modSend);
}

// A file without TINS (older firmware): each track takes the drive / crush of its instrument, a KIT
// track the KIT's own (not its lane instruments'); then the instruments' fields are 0.
void test_no_tins_migrates_instrument_insert() {
  a.instruments[1].drive = 40;
  a.instruments[1].crushBits = 20;
  a.tracks[0].instr = 1;
  a.tracks[5].instr = 1;
  Instrument& k = a.instruments[2];
  instrSetType(k, InstrType::Kit);
  k.drive = 10;
  k.kit[3].instr = 3;
  a.instruments[3].drive = 70;
  a.instruments[3].crushRate = 33;
  a.tracks[6].instr = 2;
  a.tracks[7].instr = 4;  // a clean instrument
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"TINS"});
  b.tracks[7].drive = 99;  // the load resets it first
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(40, b.tracks[0].drive);
  TEST_ASSERT_EQUAL(20, b.tracks[0].crushBits);
  TEST_ASSERT_EQUAL(40, b.tracks[5].drive);
  TEST_ASSERT_EQUAL(10, b.tracks[6].drive);  // the KIT's own, not lane 3's 70
  TEST_ASSERT_EQUAL(0, b.tracks[6].crushBits);
  TEST_ASSERT_EQUAL(0, b.tracks[6].crushRate);
  TEST_ASSERT_EQUAL(0, b.tracks[7].drive);
  for (const Instrument& m : b.instruments) {
    TEST_ASSERT_EQUAL(0, m.drive);
    TEST_ASSERT_EQUAL(0, m.crushBits);
    TEST_ASSERT_EQUAL(0, m.crushRate);
  }
}

void test_tspd_absent_without_speeds_and_bad_values() {
  a.reset();
  a.patterns[1].steps[0][0].note = 60;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(withoutChunks(out.buf, {"TSPD"}) == out.buf);  // no chunk written
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PATN", patn(1, 8));
  std::vector<uint8_t> sp(1 + kTracks, 0);
  sp[0] = 1;
  sp[1] = 200;  // unknown: x1
  sp[2] = static_cast<uint8_t>(TrackSpeed::X4);
  putChunk(f, "TSPD", sp);
  sp[0] = 40;   // no such pattern: skipped
  putChunk(f, "TSPD", sp);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.patterns[1].trackSpeed[0]);
  TEST_ASSERT_EQUAL(static_cast<int>(TrackSpeed::X4), b.patterns[1].trackSpeed[1]);
  f = fileHeader();
  putChunk(f, "TSPD", {1, 2, 3});  // wrong size
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
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

// AUDI bytes 14..25 (master / send fx): dlyMode, dlyChar, rvbMode, rvbShim, modType, modRate,
// modDepth, modFb, modLevel, eqLow, eqHigh, shimInt. Round trip; a 14-byte AUDI keeps the defaults
// (OFF / flat); unknown modes load as 0, values clamped; a longer AUDI's tail is skipped.
void test_audi_master_fx_round_trip() {
  a.dlyMode = 1;
  a.dlyChar = 99;
  a.rvbMode = 1;
  a.rvbShim = 77;
  a.modType = 4;
  a.modRate = 12;
  a.modDepth = 34;
  a.modFb = 56;
  a.modLevel = 78;
  a.eqLow = 10;
  a.eqHigh = 120;
  a.shimInt = 2;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(1, b.dlyMode);
  TEST_ASSERT_EQUAL(99, b.dlyChar);
  TEST_ASSERT_EQUAL(1, b.rvbMode);
  TEST_ASSERT_EQUAL(77, b.rvbShim);
  TEST_ASSERT_EQUAL(4, b.modType);
  TEST_ASSERT_EQUAL(12, b.modRate);
  TEST_ASSERT_EQUAL(34, b.modDepth);
  TEST_ASSERT_EQUAL(56, b.modFb);
  TEST_ASSERT_EQUAL(78, b.modLevel);
  TEST_ASSERT_EQUAL(10, b.eqLow);
  TEST_ASSERT_EQUAL(120, b.eqHigh);
  TEST_ASSERT_EQUAL(2, b.shimInt);
}

void test_audi_master_fx_old_and_garbage() {
  const Project d;
  TEST_ASSERT_EQUAL(0, d.dlyMode);
  TEST_ASSERT_EQUAL(0, d.rvbMode);
  TEST_ASSERT_EQUAL(0, d.modType);
  TEST_ASSERT_EQUAL(64, d.eqLow);
  TEST_ASSERT_EQUAL(64, d.eqHigh);
  TEST_ASSERT_EQUAL(0, d.shimInt);
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "AUDI", {50, 1, 4, 10, 20, 30, 1, 2, 3, 4, 5, 6, 7, 64});  // 14 bytes: older firmware
  finish(f);
  b.modType = 3;  // the load resets it first
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(d.dlyMode, b.dlyMode);
  TEST_ASSERT_EQUAL(d.dlyChar, b.dlyChar);
  TEST_ASSERT_EQUAL(d.rvbShim, b.rvbShim);
  TEST_ASSERT_EQUAL(0, b.modType);
  TEST_ASSERT_EQUAL(d.modRate, b.modRate);
  TEST_ASSERT_EQUAL(d.modLevel, b.modLevel);
  TEST_ASSERT_EQUAL(64, b.eqLow);
  TEST_ASSERT_EQUAL(0, b.shimInt);
  f = fileHeader();
  putChunk(f, "AUDI", {50, 1, 4, 10, 20, 30, 1, 2, 3, 4, 5, 6, 7, 64,  //
                       9, 200, 9, 200, 9, 200, 200, 200, 200, 200, 200, 9, 1, 2, 3});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.dlyMode);  // unknown: DIGITAL
  TEST_ASSERT_EQUAL(127, b.dlyChar);
  TEST_ASSERT_EQUAL(0, b.rvbMode);
  TEST_ASSERT_EQUAL(127, b.rvbShim);
  TEST_ASSERT_EQUAL(0, b.modType);
  TEST_ASSERT_EQUAL(127, b.modRate);
  TEST_ASSERT_EQUAL(127, b.modDepth);
  TEST_ASSERT_EQUAL(127, b.modFb);
  TEST_ASSERT_EQUAL(127, b.modLevel);
  TEST_ASSERT_EQUAL(127, b.eqLow);
  TEST_ASSERT_EQUAL(127, b.eqHigh);
  TEST_ASSERT_EQUAL(0, b.shimInt);
  // A partial tail (up to modType): the rest keep the defaults.
  f = fileHeader();
  putChunk(f, "AUDI", {50, 1, 4, 10, 20, 30, 1, 2, 3, 4, 5, 6, 7, 64, 2, 5, 1, 6, 2});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(2, b.dlyMode);
  TEST_ASSERT_EQUAL(5, b.dlyChar);
  TEST_ASSERT_EQUAL(1, b.rvbMode);
  TEST_ASSERT_EQUAL(6, b.rvbShim);
  TEST_ASSERT_EQUAL(2, b.modType);
  TEST_ASSERT_EQUAL(d.modRate, b.modRate);
  TEST_ASSERT_EQUAL(64, b.eqHigh);
}

// TINS byte 11: the track's MOD send. Round trip; an 11-byte (feature 3) record keeps 0.
void test_tins_mod_send() {
  a.tracks[3].modSend = 90;
  a.tracks[15].modSend = 127;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  for (int t = 0; t < kTracks; ++t) TEST_ASSERT_EQUAL(a.tracks[t].modSend, b.tracks[t].modSend);
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TINS", {1, 11, 50, 0, 0, 2, 0x55, 0x55, 1, 0, 100, 0, 0});
  finish(f);
  b.tracks[0].modSend = 9;  // the load resets it first
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(50, b.tracks[0].drive);
  TEST_ASSERT_EQUAL(0, b.tracks[0].modSend);
  TrackCfg c;
  c.modSend = 5;
  const uint8_t r[12] = {0, 0, 0, 0, 0, 0, 1, 0, 100, 0, 0, 200};
  readTinsRecord(r, 11, c);
  TEST_ASSERT_EQUAL(5, c.modSend);  // not in the record
  readTinsRecord(r, 12, c);
  TEST_ASSERT_EQUAL(127, c.modSend);  // clamped
}

// ---- PERF map (PRF2, legacy PRFM), the old DJ filter byte ----

static uint8_t pf(PerfFx f) { return static_cast<uint8_t>(f); }

// Each button's depth is its effect's default (0 for None).
static void assertDefaultDepths(const Project& p) {
  for (int i = 0; i < kPerfButtons; ++i)
    TEST_ASSERT_EQUAL_INT8(perfRange(static_cast<PerfFx>(p.perfMap[i])).def, p.perfDepth[i]);
}

// A new project: the default map at its effects' default depths.
void test_new_project_perf_depths() {
  Project p;
  p.perfDepth[0] = 1;
  p.reset();
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kPerfDefaultMap, p.perfMap, kPerfButtons);
  assertDefaultDepths(p);
  TEST_ASSERT_EQUAL_INT8(4, p.perfDepth[0]);  // Repeat 1/16
}

// A new project's XY pad: FILTER on X, CRUSH on Y.
void test_new_project_perf_xy() {
  Project p;
  p.perfXy[0] = p.perfXy[1] = 0;
  p.reset();
  TEST_ASSERT_EQUAL(pf(PerfFx::Filter), p.perfXy[0]);
  TEST_ASSERT_EQUAL(pf(PerfFx::Crush), p.perfXy[1]);
}

// Old PRFM bytes (pre-2026-10-13 enum) are mapped to the new effects.
void test_prfm_legacy_mapped() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PRFM", {1, 2, 3, 4, 5, 6, 7, 8});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const uint8_t want[kPerfButtons] = {pf(PerfFx::Repeat),     pf(PerfFx::Repeat), pf(PerfFx::Filter),
                                      pf(PerfFx::Filter),     pf(PerfFx::DelayThrow), pf(PerfFx::Crush),
                                      pf(PerfFx::None),       pf(PerfFx::None)};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, b.perfMap, kPerfButtons);
  assertDefaultDepths(b);
}

// PRF2 wins over PRFM in either chunk order; out-of-range PRF2 values are None.
void test_prf2_wins_over_prfm() {
  const std::vector<uint8_t> prf2 = {pf(PerfFx::Freeze), pf(PerfFx::Scratch), pf(PerfFx::Drive), 0,
                                     pf(PerfFx::Downsample), pf(PerfFx::Reverse), 200, pf(PerfFx::Gate)};
  const uint8_t want[kPerfButtons] = {pf(PerfFx::Freeze), pf(PerfFx::Scratch), pf(PerfFx::Drive), 0,
                                      pf(PerfFx::Downsample), pf(PerfFx::Reverse), 0, pf(PerfFx::Gate)};
  for (int order = 0; order < 2; ++order) {
    std::vector<uint8_t> f = fileHeader();
    if (order == 0) putChunk(f, "PRFM", {1, 2, 3, 4, 5, 6, 7, 8});
    putChunk(f, "PRF2", prf2);
    if (order == 1) putChunk(f, "PRFM", {1, 2, 3, 4, 5, 6, 7, 8});
    finish(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, b.perfMap, kPerfButtons);
    assertDefaultDepths(b);  // an 8-byte PRF2: no depths
  }
  // A saved file holds PRF2, not PRFM.
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "PRF2"));
  TEST_ASSERT_FALSE(hasChunk(out.buf, "PRFM"));
}

// PXY: X, Y; out-of-range = None; extra bytes skipped; files without it keep FILTER / CRUSH.
void test_pxy_chunk() {
  std::vector<uint8_t> f = fileHeader();
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(pf(PerfFx::Filter), b.perfXy[0]);
  TEST_ASSERT_EQUAL(pf(PerfFx::Crush), b.perfXy[1]);

  f = fileHeader();
  putChunk(f, "PXY ", {pf(PerfFx::Gate), 200, 7, 7});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(pf(PerfFx::Gate), b.perfXy[0]);
  TEST_ASSERT_EQUAL(0, b.perfXy[1]);

  f = fileHeader();
  putChunk(f, "PXY ", {pf(PerfFx::Gate)});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

// PRF2 depths are clamped into their effect's range; None and unknown effects get 0.
void test_prf2_depths_clamped() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "PRF2", {pf(PerfFx::Repeat), pf(PerfFx::Repeat), pf(PerfFx::Filter), pf(PerfFx::Filter),
                       pf(PerfFx::Roll), 0, 200, pf(PerfFx::Crush),
                       2, 100, static_cast<uint8_t>(-100), 30, 0, 9, 9, 7});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  const int8_t want[kPerfButtons] = {2, 6, -64, 30, 1, 0, 0, 7};
  TEST_ASSERT_EQUAL_INT8_ARRAY(want, b.perfDepth, kPerfButtons);
  // Saved as 8 effects then 8 depths.
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(b, out));
  size_t pos = 8;
  while (pos + 8 <= out.buf.size() && memcmp(out.buf.data() + pos, "PRF2", 4) != 0)
    pos += 8 + (out.buf[pos + 4] | (out.buf[pos + 5] << 8) | (out.buf[pos + 6] << 16) |
                (static_cast<uint32_t>(out.buf[pos + 7]) << 24));
  TEST_ASSERT_TRUE(pos + 8 + 2 * kPerfButtons <= out.buf.size());
  TEST_ASSERT_EQUAL(2 * kPerfButtons, out.buf[pos + 4]);
  TEST_ASSERT_EQUAL(pf(PerfFx::Filter), out.buf[pos + 8 + 2]);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(-64), out.buf[pos + 8 + kPerfButtons + 2]);
}

// AUDI byte 13 (the old DJ filter) is ignored on load and written neutral (64).
void test_audi_dj_byte_ignored() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "AUDI", {50, 1, 4, 10, 20, 30, 1, 2, 3, 4, 5, 6, 7, 10});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(50, b.masterVol);
  TEST_ASSERT_EQUAL(7, b.scDepth);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(b, out));
  size_t pos = 8;
  while (pos + 8 <= out.buf.size() && memcmp(out.buf.data() + pos, "AUDI", 4) != 0)
    pos += 8 + (out.buf[pos + 4] | (out.buf[pos + 5] << 8) | (out.buf[pos + 6] << 16) |
                (static_cast<uint32_t>(out.buf[pos + 7]) << 24));
  TEST_ASSERT_TRUE(pos + 8 + 26 <= out.buf.size());
  TEST_ASSERT_EQUAL(26, out.buf[pos + 4]);
  TEST_ASSERT_EQUAL(64, out.buf[pos + 8 + 13]);
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

// TFOL: the envelope followers round trip; a file without TFOL has them off.
void test_tfol_round_trip_and_missing() {
  a.tracks[1].folSrc = 1;
  a.tracks[1].folDest = static_cast<uint8_t>(LfoDest::Vol);
  a.tracks[1].folAmt = -64;
  a.tracks[1].folAtk = 5;
  a.tracks[1].folRel = 90;
  a.tracks[15].folSrc = 16;  // self-follow
  a.tracks[15].folAmt = 63;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "TFOL"));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  for (int t = 0; t < kTracks; ++t) {
    TEST_ASSERT_EQUAL(a.tracks[t].folSrc, b.tracks[t].folSrc);
    TEST_ASSERT_EQUAL(a.tracks[t].folDest, b.tracks[t].folDest);
    TEST_ASSERT_EQUAL(a.tracks[t].folAmt, b.tracks[t].folAmt);
    TEST_ASSERT_EQUAL(a.tracks[t].folAtk, b.tracks[t].folAtk);
    TEST_ASSERT_EQUAL(a.tracks[t].folRel, b.tracks[t].folRel);
  }
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TRKS", trks(kTracks, kTracks));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.tracks[1].folSrc);
  TEST_ASSERT_EQUAL(0, b.tracks[1].folAmt);
}

// Longer records read the known part; a short one keeps the defaults of what it lacks; a source out
// of range is off, an unknown target CUTOFF, values clamped; record size 0 or a short chunk is bad.
void test_tfol_records_and_garbage() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TFOL", {3, 7, 2, static_cast<uint8_t>(LfoDest::Pan), 0x9C, 200, 3, 9, 9,  // amt -100
                       40, 99, 10, 1, 2, 0, 0, 1, 2, 3, 4, 5, 0, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(2, b.tracks[0].folSrc);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Pan), b.tracks[0].folDest);
  TEST_ASSERT_EQUAL(-64, b.tracks[0].folAmt);
  TEST_ASSERT_EQUAL(127, b.tracks[0].folAtk);
  TEST_ASSERT_EQUAL(3, b.tracks[0].folRel);
  TEST_ASSERT_EQUAL(0, b.tracks[1].folSrc);  // 40: out of range
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Cutoff), b.tracks[1].folDest);  // 99: unknown
  TEST_ASSERT_EQUAL(1, b.tracks[2].folSrc);
  f = fileHeader();
  putChunk(f, "TFOL", {1, 2, 4, static_cast<uint8_t>(LfoDest::Vol)});  // an older, shorter record
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(4, b.tracks[0].folSrc);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Vol), b.tracks[0].folDest);
  TEST_ASSERT_EQUAL(0, b.tracks[0].folAmt);
  TEST_ASSERT_EQUAL(70, b.tracks[0].folRel);
  f = fileHeader();
  putChunk(f, "TFOL", {1, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  f = fileHeader();
  putChunk(f, "TFOL", {2, 5, 1, 2, 3, 4, 5});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

// ---- TGEN (track generators) ----

void test_tgen_round_trip_and_missing() {
  a.gen[2] = GenCfg{static_cast<uint8_t>(GenMode::Gate), 13, 77, 36, 3, 81, 0xBEEF};
  a.gen[15].mode = static_cast<uint8_t>(GenMode::Pitch);
  a.patterns[0].steps[2][5].fx[3] = {Fx::FLP, 33};
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "TGEN"));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  for (int t = 0; t < kTracks; ++t) TEST_ASSERT_EQUAL_MEMORY(&a.gen[t], &b.gen[t], sizeof(GenCfg));
  TEST_ASSERT_TRUE(b.patterns[0].steps[2][5].fx[3].cmd == Fx::FLP);
  TEST_ASSERT_EQUAL(33, b.patterns[0].steps[2][5].fx[3].val);
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TRKS", trks(kTracks, kTracks));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  for (int t = 0; t < kTracks; ++t) {
    TEST_ASSERT_EQUAL(0, b.gen[t].mode);
    TEST_ASSERT_EQUAL(t + 1, b.gen[t].seed);
  }
}

// A longer record: the known part; a shorter one: defaults for the rest; values clamped; bad sizes.
void test_tgen_records_and_garbage() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TGEN", {2, 10, 9, 40, 250, 200, 0, 101, 0x34, 0x12, 7, 7,  // clamped, 2 extra bytes
                       1, 3, 50, 60, 4, 20, 0xFF, 0xFF, 0, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.gen[0].mode);
  TEST_ASSERT_EQUAL(kGenLenMax, b.gen[0].len);
  TEST_ASSERT_EQUAL(100, b.gen[0].flip);
  TEST_ASSERT_EQUAL(127, b.gen[0].low);
  TEST_ASSERT_EQUAL(1, b.gen[0].oct);
  TEST_ASSERT_EQUAL(100, b.gen[0].density);
  TEST_ASSERT_EQUAL(0x1234, b.gen[0].seed);
  TEST_ASSERT_EQUAL(1, b.gen[1].mode);
  TEST_ASSERT_EQUAL(3, b.gen[1].len);
  TEST_ASSERT_EQUAL(0xFFFF, b.gen[1].seed);
  TEST_ASSERT_EQUAL(3, b.gen[2].seed);  // not in the chunk: default
  f = fileHeader();
  putChunk(f, "TGEN", {1, 3, 2, 12, 0});  // an older, shorter record
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(2, b.gen[0].mode);
  TEST_ASSERT_EQUAL(12, b.gen[0].len);
  TEST_ASSERT_EQUAL(0, b.gen[0].flip);
  TEST_ASSERT_EQUAL(48, b.gen[0].low);
  TEST_ASSERT_EQUAL(1, b.gen[0].seed);
  f = fileHeader();
  putChunk(f, "TGEN", {1, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  f = fileHeader();
  putChunk(f, "TGEN", {2, 8, 1, 2, 3, 4, 5, 6, 7, 8});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

// ---- TEVO (track evolve) ----

// TEVO: evolve round trip; a file without it has evolve off.
void test_tevo_round_trip_and_missing() {
  a.evo[0] = EvoCfg{70, 2, 30, kEvoNote | kEvoShift};
  a.evo[15] = EvoCfg{100, 16, 0, kEvoMaskAll};
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "TEVO"));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  for (int t = 0; t < kTracks; ++t) {
    TEST_ASSERT_EQUAL(a.evo[t].amount, b.evo[t].amount);
    TEST_ASSERT_EQUAL(a.evo[t].every, b.evo[t].every);
    TEST_ASSERT_EQUAL(a.evo[t].home, b.evo[t].home);
    TEST_ASSERT_EQUAL(a.evo[t].mask, b.evo[t].mask);
  }
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TRKS", trks(kTracks, kTracks));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.evo[0].amount);
  TEST_ASSERT_EQUAL(EvoCfg().mask, b.evo[0].mask);
  TEST_ASSERT_EQUAL(0, b.evo[15].amount);
}

// Longer records read the known part, a short one keeps the defaults it lacks, values are clamped;
// record size 0 is bad.
void test_tevo_records_and_garbage() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TEVO", {2, 5, 200, 0, 255, 0xFF, 9, 50, 40, 10, 2, 9});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(100, b.evo[0].amount);
  TEST_ASSERT_EQUAL(1, b.evo[0].every);
  TEST_ASSERT_EQUAL(100, b.evo[0].home);
  TEST_ASSERT_EQUAL(kEvoMaskAll, b.evo[0].mask);
  TEST_ASSERT_EQUAL(50, b.evo[1].amount);
  TEST_ASSERT_EQUAL(kEvoEveryMax, b.evo[1].every);  // 40
  TEST_ASSERT_EQUAL(10, b.evo[1].home);
  TEST_ASSERT_EQUAL(kEvoRhythm, b.evo[1].mask);
  TEST_ASSERT_EQUAL(0, b.evo[2].amount);  // not in the chunk: default
  f = fileHeader();
  putChunk(f, "TEVO", {1, 2, 60, 8});  // an older, shorter record
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(60, b.evo[0].amount);
  TEST_ASSERT_EQUAL(8, b.evo[0].every);
  TEST_ASSERT_EQUAL(EvoCfg().home, b.evo[0].home);
  TEST_ASSERT_EQUAL(EvoCfg().mask, b.evo[0].mask);
  f = fileHeader();
  putChunk(f, "TEVO", {1, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  f = fileHeader();
  putChunk(f, "TEVO", {2, 4, 1, 2, 3, 4});  // says 2 records, holds 1
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

// ---- TUNE (project tuning) ----

static std::vector<uint8_t> tunePayload(uint8_t count, uint8_t root, const char* name, const int32_t* mc, int n) {
  std::vector<uint8_t> v = {count, root};
  char nm[16] = {};
  strncpy(nm, name, 16);
  v.insert(v.end(), nm, nm + 16);
  for (int i = 0; i < n; ++i)
    for (int k = 0; k < 4; ++k) v.push_back(static_cast<uint8_t>(static_cast<uint32_t>(mc[i]) >> (8 * k)));
  return v;
}

void test_tune_round_trip() {
  a.tuning.count = 3;
  a.tuning.root = 57;
  strcpy(a.tuning.name, "ABCDEFGHIJKLMNOP");
  a.tuning.mc[0] = 231000;
  a.tuning.mc[1] = -50;
  a.tuning.mc[2] = 1901955;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "TUNE"));
  LoadNotes notes;
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b, &notes)));
  TEST_ASSERT_EQUAL(0, memcmp(&a.tuning, &b.tuning, sizeof(TuningTable)));
  TEST_ASSERT_FALSE(notes.tuningDropped);
  // AUTO root survives too.
  a.tuning.root = kTuneRootAuto;
  VecSink o2;
  TEST_ASSERT_TRUE(saveProject(a, o2));
  VecSource i2(o2.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(i2, b)));
  TEST_ASSERT_EQUAL(kTuneRootAuto, b.tuning.root);
}

void test_tune_12tet_not_written() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_FALSE(hasChunk(out.buf, "TUNE"));
  // A root set without a table changes nothing in the file either.
  a.tuning.root = 40;
  VecSink o2;
  TEST_ASSERT_TRUE(saveProject(a, o2));
  TEST_ASSERT_TRUE(out.buf == o2.buf);
  b.tuning.count = 5;  // loading resets it
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(0, b.tuning.count);
}

void test_tune_bad_chunks_reset_to_12tet() {
  const int32_t ok[2] = {600000, 1200000};
  const int32_t zeroPeriod[2] = {600000, 0};
  int32_t many[65];
  for (int i = 0; i < 65; ++i) many[i] = (i + 1) * 18000;
  struct Case {
    std::vector<uint8_t> pay;
    uint32_t size;
  } cases[] = {
      {tunePayload(2, 60, "Z", zeroPeriod, 2), 26},
      {tunePayload(65, 60, "Z", many, 65), 18 + 65 * 4},
      {tunePayload(0, 60, "Z", ok, 0), 18},
      {tunePayload(2, 60, "Z", ok, 1), 22},          // short
      {tunePayload(2, 200, "Z", ok, 2), 26},         // root not a note
      {{2}, 1},                                       // no header
  };
  for (auto& c : cases) {
    std::vector<uint8_t> f = fileHeader();
    putChunkSized(f, "TUNE", c.size, c.pay);
    finish(f);
    LoadNotes notes;
    VecSource in(f);
    TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b, &notes)));
    TEST_ASSERT_EQUAL(0, b.tuning.count);
    TEST_ASSERT_TRUE(notes.tuningDropped);
  }
  // A good chunk with extra bytes after it (a newer writer) still loads.
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> pay = tunePayload(2, 60, "TRITONE", ok, 2);
  pay.push_back(9);
  putChunkSized(f, "TUNE", static_cast<uint32_t>(pay.size()), pay);
  finish(f);
  LoadNotes notes;
  VecSource in(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b, &notes)));
  TEST_ASSERT_EQUAL(2, b.tuning.count);
  TEST_ASSERT_EQUAL_STRING("TRITONE", b.tuning.name);
  TEST_ASSERT_EQUAL(1200000, b.tuning.mc[1]);
  TEST_ASSERT_FALSE(notes.tuningDropped);
}


// SFOL: seed and follow actions round trip; a file without SFOL plays in order, seed 1.
void test_sfol_round_trip_and_missing() {
  a.songLen = 3;
  a.song[0] = 1;
  a.song[1] = 2;
  a.song[2] = 3;
  a.songFol[0] = {static_cast<uint8_t>(FollowAct::Jump), 2, 75};
  a.songFol[2] = {static_cast<uint8_t>(FollowAct::Random), 0, 10};
  a.seed = 4321;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "SFOL"));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(4321, b.seed);
  for (int i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_MEMORY(&a.songFol[i], &b.songFol[i], sizeof(SongFollow));
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "SNG3", {1, 4, 0, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(1, b.seed);
  TEST_ASSERT_EQUAL(static_cast<int>(FollowAct::Next), b.songFol[0].act);
  TEST_ASSERT_EQUAL(100, b.songFol[0].chance);
}

// Values clamped (unknown action NEXT, target <= 127, chance <= 100); a longer record reads the known
// part, a shorter one keeps defaults; record size 0 with records or a short chunk is bad. Empty song: seed only.
void test_sfol_records_and_garbage() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "SFOL", {0x34, 0x12, 3, 4,  3, 1, 50, 9,  9, 200, 150, 9,  4, 0, 100, 9});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0x1234, b.seed);
  TEST_ASSERT_EQUAL(static_cast<int>(FollowAct::Jump), b.songFol[0].act);
  TEST_ASSERT_EQUAL(1, b.songFol[0].target);
  TEST_ASSERT_EQUAL(50, b.songFol[0].chance);
  TEST_ASSERT_EQUAL(static_cast<int>(FollowAct::Next), b.songFol[1].act);
  TEST_ASSERT_EQUAL(kSongMax - 1, b.songFol[1].target);
  TEST_ASSERT_EQUAL(100, b.songFol[1].chance);
  TEST_ASSERT_EQUAL(static_cast<int>(FollowAct::Random), b.songFol[2].act);
  f = fileHeader();
  putChunk(f, "SFOL", {5, 0, 1, 1, static_cast<uint8_t>(FollowAct::Stay)});  // an older, shorter record
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(static_cast<int>(FollowAct::Stay), b.songFol[0].act);
  TEST_ASSERT_EQUAL(100, b.songFol[0].chance);
  f = fileHeader();
  putChunk(f, "SFOL", {7, 0, 0, 3});  // no slots: the seed alone
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(7, b.seed);
  f = fileHeader();
  putChunk(f, "SFOL", {1, 0, 2, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  f = fileHeader();
  putChunk(f, "SFOL", {1, 0, 2, 3, 0, 0, 100});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

// ---- SCEN (scenes and the crossfader) ----

void test_scen_round_trip() {
  sceneSetTrk(a.scenes[2], 0, kScnVol, 10);
  sceneSetTrk(a.scenes[2], 15, kScnRes, 99);
  sceneSetTrk(a.scenes[7], 3, kScnDty, 4);
  sceneSetMst(a.scenes[7], kScnMain, 180);
  sceneSetMst(a.scenes[7], kScnDTime, 16);
  a.sceneA = 7;
  a.sceneB = 2;
  a.xfade = 77;
  syncSceneLive(a);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "SCEN"));
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_EQUAL(0, memcmp(a.scenes, b.scenes, sizeof a.scenes));
  TEST_ASSERT_EQUAL(7, b.sceneA);
  TEST_ASSERT_EQUAL(2, b.sceneB);
  TEST_ASSERT_EQUAL(77, b.xfade);
  // The live ends are synced by the load.
  TEST_ASSERT_EQUAL(0, memcmp(&b.sceneLive[0], &b.scenes[7], sizeof(Scene)));
  TEST_ASSERT_EQUAL(0, memcmp(&b.sceneLive[1], &b.scenes[2], sizeof(Scene)));
}

void test_scen_default_not_written_and_absent_loads_default() {
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_FALSE(hasChunk(out.buf, "SCEN"));
  a.xfade = 1;  // only the fader moved: written
  VecSink out2;
  TEST_ASSERT_TRUE(saveProject(a, out2));
  TEST_ASSERT_TRUE(hasChunk(out2.buf, "SCEN"));
  sceneSetTrk(b.scenes[0], 0, kScnVol, 3);  // loading resets them
  b.sceneA = 5;
  b.xfade = 9;
  syncSceneLive(b);
  VecSource in(out.buf);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadProject(in, b)));
  TEST_ASSERT_TRUE(sceneEmpty(b.scenes[0]) && sceneEmpty(b.sceneLive[0]));
  TEST_ASSERT_EQUAL(0, b.sceneA);
  TEST_ASSERT_EQUAL(1, b.sceneB);
  TEST_ASSERT_EQUAL(0, b.xfade);
}

// A SCEN body: count scenes, nTrk / nMst parameters; scene 0 track 0 and the master get every value v
// with full masks.
static std::vector<uint8_t> scenPayload(uint8_t count, uint8_t tracks, uint8_t nTrk, uint8_t nMst, uint8_t v,
                                        uint8_t sa = 0, uint8_t sb = 1, uint8_t xf = 0) {
  std::vector<uint8_t> p = {count, tracks, nTrk, nMst, sa, sb, xf};
  for (int s = 0; s < count; ++s) {
    for (int t = 0; t < tracks; ++t) {
      const uint16_t m = s == 0 && t == 0 ? 0xFFFF : 0;
      p.push_back(static_cast<uint8_t>(m));
      p.push_back(static_cast<uint8_t>(m >> 8));
      for (int k = 0; k < nTrk; ++k) p.push_back(m ? v : 0);
    }
    const uint16_t mm = s == 0 ? 0xFFFF : 0;
    p.push_back(static_cast<uint8_t>(mm));
    p.push_back(static_cast<uint8_t>(mm >> 8));
    for (int k = 0; k < nMst; ++k) p.push_back(mm ? v : 0);
  }
  return p;
}

void test_scen_longer_and_shorter_records() {
  // More parameters than known: the extras skipped, the high mask bits dropped; values clamped.
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "SCEN", scenPayload(9, 17, 16, 16, 250, 9, 3, 200));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(kScnTrkBits, b.scenes[0].trkMask[0]);
  TEST_ASSERT_EQUAL(kScnMstBits, b.scenes[0].mstMask);
  TEST_ASSERT_EQUAL(127, b.scenes[0].trk[0][kScnVol]);
  TEST_ASSERT_EQUAL(0, b.scenes[0].trk[0][kScnDty]);  // out of range: TNH
  TEST_ASSERT_EQUAL(kMasterVolMax, b.scenes[0].mst[kScnMain]);
  TEST_ASSERT_EQUAL(kDlyTimeMax, b.scenes[0].mst[kScnDTime]);
  TEST_ASSERT_TRUE(sceneEmpty(b.scenes[1]));
  TEST_ASSERT_EQUAL(0, b.sceneA);  // 9: out of range
  TEST_ASSERT_EQUAL(3, b.sceneB);
  TEST_ASSERT_EQUAL(127, b.xfade);
  // Fewer: the missing parameters unset.
  f = fileHeader();
  putChunk(f, "SCEN", scenPayload(2, 16, 3, 2, 20));
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0x7, b.scenes[0].trkMask[0]);
  TEST_ASSERT_EQUAL(20, b.scenes[0].trk[0][kScnDrv]);
  TEST_ASSERT_EQUAL(0x3, b.scenes[0].mstMask);
  TEST_ASSERT_EQUAL(kDlyTimeMax, b.scenes[0].mst[kScnDTime]);  // 20 clamped
}

// ---- LOFI: machine + macros in FMIN, the formula in LOFI ----

void test_lofi_round_trip() {
  Instrument& m = a.instruments[5];
  instrSetType(m, InstrType::Lofi);
  lofiSetMachine(m, static_cast<uint8_t>(LofiMachine::Apc));
  m.macro[2] = 77;
  m.lofiFormula = static_cast<uint8_t>(byteFormulaCount() - 1);
  m.lofiSync = 1;
  a.instruments[6].lofiFormula = 3;  // kept on any type
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "LOFI"));
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(out.buf)));
  const Instrument& r = b.instruments[5];
  TEST_ASSERT_TRUE(r.type == InstrType::Lofi);
  TEST_ASSERT_EQUAL(static_cast<int>(LofiMachine::Apc), r.machine);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(m.macro, r.macro, kFmMacros);
  TEST_ASSERT_EQUAL(byteFormulaCount() - 1, r.lofiFormula);
  TEST_ASSERT_EQUAL(1, r.lofiSync);
  TEST_ASSERT_EQUAL(0, b.instruments[6].lofiSync);
  TEST_ASSERT_EQUAL(3, b.instruments[6].lofiFormula);
}

void test_file_without_lofi_chunk() {
  instrSetType(a.instruments[0], InstrType::Lofi);
  a.instruments[0].lofiFormula = 4;
  a.instruments[0].lofiSync = 1;
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"LOFI"});
  TEST_ASSERT_FALSE(hasChunk(f, "LOFI"));
  b.instruments[0].lofiFormula = 9;
  b.instruments[0].lofiSync = 1;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_TRUE(b.instruments[0].type == InstrType::Lofi);
  TEST_ASSERT_EQUAL(0, b.instruments[0].lofiFormula);
  TEST_ASSERT_EQUAL(0, b.instruments[0].lofiSync);
}

// LOFI: count, record size, then 8-byte records (formula, sync, 6 reserved). Longer records: the known part;
// shorter: defaults. Out of range machine / formula -> 0.
void test_lofi_garbage_clamped_and_sizes() {
  std::vector<uint8_t> f = fileHeader();
  std::vector<uint8_t> inst = {2};
  for (int i = 0; i < 2; ++i) {
    std::vector<uint8_t> r(kInstRecSize, 0);
    r[8] = static_cast<uint8_t>(InstrType::Lofi);
    inst.insert(inst.end(), r.begin(), r.end());
  }
  putChunk(f, "INST", inst);
  std::vector<uint8_t> fm = {1};
  std::vector<uint8_t> r(kFmRecSize, 0);
  r[0] = static_cast<uint8_t>(LofiMachine::Count);  // machine out of range for LOFI
  fm.insert(fm.end(), r.begin(), r.end());
  putChunk(f, "FMIN", fm);
  std::vector<uint8_t> lo = {2, 12};  // 2 records of 12 bytes (a newer, longer record)
  for (int i = 0; i < 2; ++i) {
    std::vector<uint8_t> x(12, 0xAA);
    x[0] = i == 0 ? 200 : 2;  // formula: out of range, valid
    lo.insert(lo.end(), x.begin(), x.end());
  }
  putChunk(f, "LOFI", lo);
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.instruments[0].machine);
  TEST_ASSERT_EQUAL(0, b.instruments[0].lofiFormula);
  TEST_ASSERT_EQUAL(2, b.instruments[1].lofiFormula);
  TEST_ASSERT_EQUAL(1, b.instruments[1].lofiSync);  // 0xAA: clamped to on
  // An 8-byte record from before the sync flag (byte 1 = 0): sync off.
  f = fileHeader();
  putChunk(f, "LOFI", {1, 8, 3, 0, 0, 0, 0, 0, 0, 0});
  finish(f);
  b.instruments[0].lofiSync = 1;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(3, b.instruments[0].lofiFormula);
  TEST_ASSERT_EQUAL(0, b.instruments[0].lofiSync);
  // A zero record size: bad value (as TINS).
  f = fileHeader();
  putChunk(f, "LOFI", {kInstruments, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  // Records shorter than the chunk claims: bad value.
  f = fileHeader();
  putChunk(f, "LOFI", {3, 8, 1, 0, 0, 0, 0, 0, 0, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

// ---- Generative 2: TVAR, TGEN BYTE fields, LFOB ----

// TVAR: VAR round trip; a file without it has VAR off.
void test_tvar_round_trip_and_missing() {
  a.var[0] = VarCfg{static_cast<uint8_t>(VarMode::Ca2d), 5, 30, 3, 0, 40, 70, 2, 1};
  a.var[15] = VarCfg{static_cast<uint8_t>(VarMode::Markov), 16, 110, 4, 64, 100, 0, 1, 0};
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "TVAR"));
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(out.buf)));
  for (int t = 0; t < kTracks; ++t) {
    const VarCfg &x = a.var[t], &y = b.var[t];
    TEST_ASSERT_EQUAL(x.mode, y.mode);
    TEST_ASSERT_EQUAL(x.every, y.every);
    TEST_ASSERT_EQUAL(x.rule, y.rule);
    TEST_ASSERT_EQUAL(x.rule2, y.rule2);
    TEST_ASSERT_EQUAL(x.reset, y.reset);
    TEST_ASSERT_EQUAL(x.noise, y.noise);
    TEST_ASSERT_EQUAL(x.amount, y.amount);
    TEST_ASSERT_EQUAL(x.order, y.order);
    TEST_ASSERT_EQUAL(x.rhythm, y.rhythm);
  }
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"TVAR"});
  b.var[0].mode = 3;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.var[0].mode);
  TEST_ASSERT_EQUAL(VarCfg().every, b.var[0].every);
  TEST_ASSERT_EQUAL(VarCfg().rule, b.var[15].rule);
}

// Longer records: the known part; a shorter one keeps the defaults it lacks; values clamped; bad sizes.
void test_tvar_records_and_garbage() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TVAR", {2, 10, 9, 0, 77, 9, 200, 101, 101, 0, 5, 0xEE,  // clamped, 1 extra byte
                       3, 40, 30, 4, 64, 100, 100, 3, 1, 0xEE});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.var[0].mode);
  TEST_ASSERT_EQUAL(1, b.var[0].every);
  TEST_ASSERT_EQUAL(77, b.var[0].rule);
  TEST_ASSERT_EQUAL(0, b.var[0].rule2);
  TEST_ASSERT_EQUAL(kVarResetMax, b.var[0].reset);
  TEST_ASSERT_EQUAL(100, b.var[0].noise);
  TEST_ASSERT_EQUAL(100, b.var[0].amount);
  TEST_ASSERT_EQUAL(1, b.var[0].order);
  TEST_ASSERT_EQUAL(1, b.var[0].rhythm);
  TEST_ASSERT_EQUAL(3, b.var[1].mode);
  TEST_ASSERT_EQUAL(kVarEveryMax, b.var[1].every);
  TEST_ASSERT_EQUAL(4, b.var[1].rule2);
  TEST_ASSERT_EQUAL(2, b.var[1].order);
  TEST_ASSERT_EQUAL(VarCfg().rule, b.var[2].rule);  // not in the chunk: default
  f = fileHeader();
  putChunk(f, "TVAR", {1, 2, 1, 4});  // an older, shorter record
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(1, b.var[0].mode);
  TEST_ASSERT_EQUAL(4, b.var[0].every);
  TEST_ASSERT_EQUAL(VarCfg().rule, b.var[0].rule);
  TEST_ASSERT_EQUAL(VarCfg().amount, b.var[0].amount);
  f = fileHeader();
  putChunk(f, "TVAR", {1, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  f = fileHeader();
  putChunk(f, "TVAR", {2, 2, 1, 2});  // says 2 records, holds 1
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
}

// TGEN BYTE fields round trip; an old 8-byte record leaves them at their defaults; clamped.
void test_tgen_byte_fields() {
  GenCfg& g = a.gen[4];
  g.mode = static_cast<uint8_t>(GenMode::BytePitch);
  g.formula = static_cast<uint8_t>(byteFormulaCount() - 1);
  g.a = 1;
  g.b = 127;
  g.c = 64;
  g.zoom = 12;
  a.gen[5].mode = static_cast<uint8_t>(GenMode::ByteGate);
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(out.buf)));
  const GenCfg& r = b.gen[4];
  TEST_ASSERT_EQUAL(static_cast<int>(GenMode::BytePitch), r.mode);
  TEST_ASSERT_EQUAL(byteFormulaCount() - 1, r.formula);
  TEST_ASSERT_EQUAL(1, r.a);
  TEST_ASSERT_EQUAL(127, r.b);
  TEST_ASSERT_EQUAL(64, r.c);
  TEST_ASSERT_EQUAL(12, r.zoom);
  TEST_ASSERT_EQUAL(static_cast<int>(GenMode::ByteGate), b.gen[5].mode);
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "TGEN", {1, 8, 3, 9, 20, 50, 2, 40, 7, 0});  // a record from before the BYTE fields
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(3, b.gen[0].mode);
  TEST_ASSERT_EQUAL(7, b.gen[0].seed);
  TEST_ASSERT_EQUAL(GenCfg().formula, b.gen[0].formula);
  TEST_ASSERT_EQUAL(GenCfg().a, b.gen[0].a);
  TEST_ASSERT_EQUAL(GenCfg().b, b.gen[0].b);
  TEST_ASSERT_EQUAL(GenCfg().c, b.gen[0].c);
  TEST_ASSERT_EQUAL(10, b.gen[0].zoom);
  f = fileHeader();
  putChunk(f, "TGEN", {1, 15, 4, 9, 20, 50, 2, 40, 7, 0, 250, 200, 200, 200, 13, 0xEE, 0xEE});  // longer
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(4, b.gen[0].mode);
  TEST_ASSERT_EQUAL(0, b.gen[0].formula);
  TEST_ASSERT_EQUAL(127, b.gen[0].a);
  TEST_ASSERT_EQUAL(127, b.gen[0].c);
  TEST_ASSERT_EQUAL(12, b.gen[0].zoom);
}

// LFOB: per instrument 4 x {formula, A} of LFO BYTE; files without it: formula 0, its default A.
void test_lfob_round_trip_and_missing() {
  Instrument& m = a.instruments[3];
  m.lfoWave = static_cast<uint8_t>(LfoWave::Byte);
  for (int i = 0; i < kLfos; ++i) {
    m.lfoByte[i][0] = static_cast<uint8_t>((byteFormulaCount() - 1 - i) % byteFormulaCount());
    m.lfoByte[i][1] = static_cast<uint8_t>(10 + 30 * i);
  }
  VecSink out;
  TEST_ASSERT_TRUE(saveProject(a, out));
  TEST_ASSERT_TRUE(hasChunk(out.buf, "LFOB"));
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(out.buf)));
  TEST_ASSERT_EQUAL(static_cast<int>(LfoWave::Byte), b.instruments[3].lfoWave);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(&m.lfoByte[0][0], &b.instruments[3].lfoByte[0][0], 2 * kLfos);
  const std::vector<uint8_t> f = withoutChunks(out.buf, {"LFOB"});
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  for (int i = 0; i < kLfos; ++i) {
    TEST_ASSERT_EQUAL(0, b.instruments[3].lfoByte[i][0]);
    TEST_ASSERT_EQUAL(byteFormula(0).a, b.instruments[3].lfoByte[i][1]);
  }
}

// LFOB: count, record size, then 8-byte records. Longer: the known part; shorter: defaults; clamped.
void test_lfob_garbage_clamped_and_sizes() {
  std::vector<uint8_t> f = fileHeader();
  putChunk(f, "LFOB", {2, 10, 250, 200, 2, 5, 1, 6, 3, 7, 0xEE, 0xEE,
                       1, 1, 2, 2, 3, 3, 4, 4, 0xEE, 0xEE});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(0, b.instruments[0].lfoByte[0][0]);
  TEST_ASSERT_EQUAL(127, b.instruments[0].lfoByte[0][1]);
  TEST_ASSERT_EQUAL(2, b.instruments[0].lfoByte[1][0]);
  TEST_ASSERT_EQUAL(7, b.instruments[0].lfoByte[3][1]);
  TEST_ASSERT_EQUAL(4, b.instruments[1].lfoByte[3][0]);
  TEST_ASSERT_EQUAL(0, b.instruments[2].lfoByte[0][0]);  // not in the chunk: default
  TEST_ASSERT_EQUAL(byteFormula(0).a, b.instruments[2].lfoByte[0][1]);
  f = fileHeader();
  putChunk(f, "LFOB", {1, 2, 3, 9});  // a shorter record: LFO 1 only
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadBytes(f)));
  TEST_ASSERT_EQUAL(3, b.instruments[0].lfoByte[0][0]);
  TEST_ASSERT_EQUAL(9, b.instruments[0].lfoByte[0][1]);
  TEST_ASSERT_EQUAL(0, b.instruments[0].lfoByte[1][0]);
  TEST_ASSERT_EQUAL(byteFormula(0).a, b.instruments[0].lfoByte[1][1]);
  f = fileHeader();
  putChunk(f, "LFOB", {1, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
  f = fileHeader();
  putChunk(f, "LFOB", {3, 8, 1, 0, 0, 0, 0, 0, 0, 0});
  finish(f);
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::BadValue), static_cast<int>(loadBytes(f)));
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
  RUN_TEST(test_drum2_machine_roundtrip);
  RUN_TEST(test_fltr_new_modes_roundtrip);
  RUN_TEST(test_lfo_dests_roundtrip);
  RUN_TEST(test_file_without_fltr);
  RUN_TEST(test_fltr_garbage_clamped);
  RUN_TEST(test_audi_delay_garbage_clamped);
  RUN_TEST(test_drum_type_roundtrip);
  RUN_TEST(test_fm_machine_out_of_range);
  RUN_TEST(test_slices_roundtrip);
  RUN_TEST(test_slices_bad_order_truncated);
  RUN_TEST(test_slices_count_clamped);
  RUN_TEST(test_syn_roundtrip);
  RUN_TEST(test_type7_loads_as_chip);
  RUN_TEST(test_string_instrument_roundtrip);
  RUN_TEST(test_file_without_syn_chunks);
  RUN_TEST(test_syni_garbage_clamped);
  RUN_TEST(test_wtbl_dup_bad_and_repeat);
  RUN_TEST(test_kits_round_trip_and_defaults);
  RUN_TEST(test_kits_garbage_clamped);
  RUN_TEST(test_patn_vel_mask_only_on_drum_tracks);
  RUN_TEST(test_sng3_round_trip);
  RUN_TEST(test_sng3_garbage_clamped);
  RUN_TEST(test_save_writes_no_legacy_song_chunks);
  RUN_TEST(test_chn2_scns_migrated);
  RUN_TEST(test_proj_chain_migrated);
  RUN_TEST(test_chn2_migration_capped_and_clamped);
  RUN_TEST(test_sng3_skips_migration);
  RUN_TEST(test_tlen_round_trip_and_default);
  RUN_TEST(test_tlen_clamped_to_length);
  RUN_TEST(test_tspd_round_trip_and_default);
  RUN_TEST(test_tspd_absent_without_speeds_and_bad_values);
  RUN_TEST(test_tpan_round_trip_and_default);
  RUN_TEST(test_tpan_garbage_clamped);
  RUN_TEST(test_tins_round_trip);
  RUN_TEST(test_tfol_round_trip_and_missing);
  RUN_TEST(test_tgen_round_trip_and_missing);
  RUN_TEST(test_tgen_records_and_garbage);
  RUN_TEST(test_tevo_round_trip_and_missing);
  RUN_TEST(test_tevo_records_and_garbage);
  RUN_TEST(test_tfol_records_and_garbage);
  RUN_TEST(test_tins_gate_fields);
  RUN_TEST(test_tins_longer_records_and_garbage);
  RUN_TEST(test_tins_three_byte_records);
  RUN_TEST(test_tins_record_assigns_present_fields_only);
  RUN_TEST(test_no_tins_migrates_instrument_insert);
  RUN_TEST(test_audi_sound_fx_defaults_and_clamps);
  RUN_TEST(test_read_file_names);
  RUN_TEST(test_prfm_legacy_mapped);
  RUN_TEST(test_prf2_wins_over_prfm);
  RUN_TEST(test_pxy_chunk);
  RUN_TEST(test_prf2_depths_clamped);
  RUN_TEST(test_new_project_perf_depths);
  RUN_TEST(test_new_project_perf_xy);
  RUN_TEST(test_audi_dj_byte_ignored);
  RUN_TEST(test_audi_master_fx_round_trip);
  RUN_TEST(test_audi_master_fx_old_and_garbage);
  RUN_TEST(test_tins_mod_send);
  RUN_TEST(test_sfol_round_trip_and_missing);
  RUN_TEST(test_sfol_records_and_garbage);
  RUN_TEST(test_tune_round_trip);
  RUN_TEST(test_tune_12tet_not_written);
  RUN_TEST(test_tune_bad_chunks_reset_to_12tet);
  RUN_TEST(test_scen_round_trip);
  RUN_TEST(test_scen_default_not_written_and_absent_loads_default);
  RUN_TEST(test_scen_longer_and_shorter_records);
  RUN_TEST(test_lofi_round_trip);
  RUN_TEST(test_file_without_lofi_chunk);
  RUN_TEST(test_lofi_garbage_clamped_and_sizes);
  RUN_TEST(test_tvar_round_trip_and_missing);
  RUN_TEST(test_tvar_records_and_garbage);
  RUN_TEST(test_tgen_byte_fields);
  RUN_TEST(test_lfob_round_trip_and_missing);
  RUN_TEST(test_lfob_garbage_clamped_and_sizes);
  return UNITY_END();
}
