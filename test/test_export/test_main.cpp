#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "export.h"
#include "wav.h"

using namespace mt;

void setUp() {}
void tearDown() {}

static Project* mkProj() {  // static storage: a Project is ~460 KB
  static Project p;
  p.reset();
  strcpy(p.name, "DEMO");
  p.bpm = 120;
  Instrument& m = p.instruments[0];
  instrSetType(m, InstrType::Chip);
  m.wave = static_cast<uint8_t>(Wave::Saw);
  m.attack = 0;
  m.sustain = 127;
  m.release = 0;
  for (int t = 0; t < 4; ++t) {
    p.tracks[t].out = TrackOut::Int;
    p.tracks[t].instr = 0;
    p.tracks[t].pan = static_cast<uint8_t>(t * 30 + 19);
    for (int i = t; i < 16; i += 4) p.patterns[0].steps[t][i].note = static_cast<uint8_t>(48 + t * 5 + i);
  }
  p.patterns[0].length = 16;  // 2 s at 120 BPM
  p.dlyLevel = 0;
  p.rvbLevel = 0;
  return &p;
}

// ---- lengths ----

void test_us_to_frames() {
  TEST_ASSERT_EQUAL_UINT32(128, usToFrames(kRenderBlockUs));
  TEST_ASSERT_EQUAL_UINT32(88215, usToFrames(2000000));  // 2 s on the 2902 us block clock
  TEST_ASSERT_EQUAL_UINT32(0, usToFrames(0));
}

void test_loops_render_every_pass() {
  Project& p = *mkProj();
  RenderSpec s;
  s.loops = 4;
  OfflineSequence q(p, s);
  TEST_ASSERT_EQUAL_UINT32((4 * 2000000u + kRenderBlockUs - 1) / kRenderBlockUs, q.blocksTotal());
  static SeqEv ev[OfflineSequence::kMaxEvents];
  int n = 0, onsLate = 0;
  uint32_t b = 0;
  while (q.nextBlock(ev, n)) {
    for (int i = 0; i < n; ++i)
      if ((ev[i].b[0] & 0xF0) == 0x90 && ev[i].b[2] && b * kRenderBlockUs >= 6000000u) ++onsLate;
    ++b;
  }
  TEST_ASSERT_TRUE(onsLate >= 4);  // the fourth pass plays
}

void test_export_render_spec_tails() {
  Project& p = *mkProj();
  ExportSpec s;
  s.tail = ExportTail::Off;
  TEST_ASSERT_EQUAL_UINT32(0, exportRenderSpec(p, s).tailBlocks);
  s.tail = ExportTail::S2;
  TEST_ASSERT_EQUAL_UINT32((88200 + 127) / 128 + 1, exportRenderSpec(p, s).tailBlocks);
  s.tail = ExportTail::S8;
  TEST_ASSERT_EQUAL_UINT32((4 * 88200 + 127) / 128 + 1, exportRenderSpec(p, s).tailBlocks);
  s.tail = ExportTail::Auto;
  TEST_ASSERT_EQUAL_UINT32(kExportAutoMaxBlocks, exportRenderSpec(p, s).tailBlocks);
  s.tail = ExportTail::Wrap;
  s.loops = 2;
  const RenderSpec r = exportRenderSpec(p, s);
  TEST_ASSERT_EQUAL(3, r.loops);
  TEST_ASSERT_EQUAL_UINT32(1, r.tailBlocks);
  s.mode = RenderSpec::Mode::Song;  // WRAP / loops: pattern only
  p.songLen = 1;
  TEST_ASSERT_EQUAL(1, exportRenderSpec(p, s).loops);
}

void test_export_body_skip_cut() {
  Project& p = *mkProj();
  ExportSpec s;
  s.loops = 4;
  s.tail = ExportTail::Off;
  const uint32_t pass = usToFrames(2000000);
  TEST_ASSERT_EQUAL_UINT32(4 * pass, exportBodyFrames(p, s));
  TEST_ASSERT_EQUAL_UINT32(0, exportSkipFrames(p, s));
  TEST_ASSERT_EQUAL_UINT32(4 * pass, exportCutFrames(p, s, 10000, 0));
  s.tail = ExportTail::S2;
  TEST_ASSERT_EQUAL_UINT32(4 * pass + 88200, exportCutFrames(p, s, 10000, 0));
  TEST_ASSERT_EQUAL_UINT32(100 * 128, exportCutFrames(p, s, 100, 0));  // never past the render
  s.tail = ExportTail::Wrap;
  TEST_ASSERT_EQUAL_UINT32(pass, exportSkipFrames(p, s));
  TEST_ASSERT_EQUAL_UINT32(4 * pass, exportCutFrames(p, s, 10000, 0));
  s.tail = ExportTail::Auto;
  const uint32_t bodyBlocks = (4 * pass + 127) / 128;
  // Silent from the body's end on: the body.
  TEST_ASSERT_EQUAL_UINT32(4 * pass, exportCutFrames(p, s, bodyBlocks + 400, 400 + 50));
  // Loud until 200 blocks after the body: + 35 blocks.
  TEST_ASSERT_EQUAL_UINT32((bodyBlocks + 200 + 35) * 128, exportCutFrames(p, s, bodyBlocks + 545, 345));
  TEST_ASSERT_FALSE(exportAutoDone(p, s, bodyBlocks - 1, 400));  // the body always plays
  TEST_ASSERT_FALSE(exportAutoDone(p, s, bodyBlocks + 100, 100));
  TEST_ASSERT_TRUE(exportAutoDone(p, s, bodyBlocks + 400, 345));
  TEST_ASSERT_TRUE(exportAutoDone(p, s, bodyBlocks + kExportAutoMaxBlocks, 0));
  TEST_ASSERT_EQUAL_UINT32(4 * pass + 2 * 44100, exportEstFrames(p, s));
}

// ---- outputs and names ----

void test_stem_tracks() {
  Project& p = *mkProj();
  ExportSpec s;
  TEST_ASSERT_EQUAL_HEX16(0x000F, exportStemTracks(p, s));
  p.tracks[1].mute = true;
  TEST_ASSERT_EQUAL_HEX16(0x000D, exportStemTracks(p, s));
  s.tracksMask = 0x0004;
  TEST_ASSERT_EQUAL_HEX16(0x0004, exportStemTracks(p, s));
  s.tracksMask = 0xFFFF;
  p.tracks[1].mute = false;
  s.pattern = 1;  // empty pattern
  TEST_ASSERT_EQUAL_HEX16(0, exportStemTracks(p, s));
  p.song[0] = 1;
  p.song[1] = 0;
  p.songLen = 2;
  s.mode = RenderSpec::Mode::Song;
  TEST_ASSERT_EQUAL_HEX16(0x000F, exportStemTracks(p, s));
  p.tracks[5].out = TrackOut::Midi;
  p.patterns[0].steps[5][0].note = 60;
  TEST_ASSERT_EQUAL(1, exportMidiTracks(p, s));
}

void test_dir_and_tags() {
  Project& p = *mkProj();
  ExportSpec s;
  char b[128];
  exportDir(p, s, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("/export/DEMO/P01", b);
  s.pattern = 2;
  s.loops = 4;
  exportDir(p, s, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("/export/DEMO/P03x4", b);
  s.mode = RenderSpec::Mode::Song;
  exportSrcTag(s, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("SONG", b);
}

void test_outputs_names() {
  Project& p = *mkProj();
  ExportSpec s;
  ExportOut o[kExportMaxOut];
  TEST_ASSERT_EQUAL(1, exportOutputs(p, s, o));
  TEST_ASSERT_EQUAL(kExportTapMaster, o[0].tap);
  TEST_ASSERT_EQUAL_STRING("DEMO_P01_MASTER.wav", o[0].name);
  strcpy(p.tracks[2].name, "BA SS!");
  p.tracks[0].name[0] = 0;
  s.stems = ExportStems::Tracks;
  p.dlyLevel = 90;
  TEST_ASSERT_EQUAL(5, exportOutputs(p, s, o));
  TEST_ASSERT_EQUAL(1, o[1].tap);
  TEST_ASSERT_EQUAL_STRING("DEMO_P01_01.wav", o[1].name);
  TEST_ASSERT_EQUAL(3, o[3].tap);
  TEST_ASSERT_EQUAL_STRING("DEMO_P01_03_BA_SS_.wav", o[3].name);
  s.stems = ExportStems::TracksFx;
  p.modType = static_cast<uint8_t>(ModType::Off);
  p.rvbLevel = 0;
  TEST_ASSERT_EQUAL(6, exportOutputs(p, s, o));  // DLY only
  TEST_ASSERT_EQUAL(1 + RenderTaps::kDly, o[5].tap);
  TEST_ASSERT_EQUAL_STRING("DEMO_P01_FX_DLY.wav", o[5].name);
  p.modType = static_cast<uint8_t>(ModType::Phaser);
  p.modLevel = 50;
  p.rvbLevel = 10;
  TEST_ASSERT_EQUAL(8, exportOutputs(p, s, o));
  TEST_ASSERT_EQUAL_STRING("DEMO_P01_FX_MOD.wav", o[5].name);
  TEST_ASSERT_EQUAL_STRING("DEMO_P01_FX_RVB.wav", o[7].name);
  for (int t = 0; t < kTracks; ++t) {  // the longest set: every path fits RenderStartReq::path
    p.tracks[t].out = TrackOut::Int;
    p.patterns[0].steps[t][0].note = 60;
    strcpy(p.tracks[t].name, "ABCDEFGH");
  }
  strcpy(p.name, "ABCDEFGHIJKLMNOP");
  s.mode = RenderSpec::Mode::Pattern;
  s.pattern = 15;
  s.loops = 16;
  p.patterns[15] = p.patterns[0];
  TEST_ASSERT_EQUAL(kExportMaxOut, exportOutputs(p, s, o));
  char dir[128];
  exportDir(p, s, dir, sizeof dir);
  for (int i = 0; i < kExportMaxOut; ++i) TEST_ASSERT_TRUE(strlen(dir) + 1 + strlen(o[i].name) + 4 < 128);
}

void test_passes_and_bytes() {
  TEST_ASSERT_EQUAL(1, exportPasses(20, 20));
  TEST_ASSERT_EQUAL(4, exportPasses(20, 6));
  TEST_ASSERT_EQUAL(1, exportPasses(1, 6));
  TEST_ASSERT_EQUAL(3, exportPasses(3, 1));
  TEST_ASSERT_EQUAL_UINT64(2ull * (44 + 44100ull * 6), exportBytes(2, 44100, 24));
  TEST_ASSERT_EQUAL_UINT64(44 + 44100ull * 4, exportBytes(1, 44100, 16));
}

// ---- WAV and PCM ----

struct MemSource : ByteSource {
  const uint8_t* d;
  size_t n, at = 0;
  MemSource(const uint8_t* b, size_t len) : d(b), n(len) {}
  bool read(void* o, size_t k) override {
    if (at + k > n) return false;
    memcpy(o, d + at, k);
    at += k;
    return true;
  }
  bool skip(size_t k) override {
    if (at + k > n) return false;
    at += k;
    return true;
  }
};

void test_wav_header_pcm() {
  for (int bits : {24, 16}) {
    std::vector<uint8_t> f(kWavPcmHeaderBytes + 10 * bits / 8 * 2, 0);
    wavHeaderPcm(f.data(), 10, 44100, 2, static_cast<uint16_t>(bits));
    MemSource src(f.data(), f.size());
    WavInfo w;
    TEST_ASSERT_EQUAL(static_cast<int>(WavErr::Ok), static_cast<int>(wavParse(src, w)));
    TEST_ASSERT_EQUAL(2, w.channels);
    TEST_ASSERT_EQUAL(bits, w.bits);
    TEST_ASSERT_EQUAL_UINT32(44100, w.rate);
    TEST_ASSERT_EQUAL_UINT32(10, w.frames());
    TEST_ASSERT_EQUAL_UINT32(kWavPcmHeaderBytes, w.dataOffset);
    TEST_ASSERT_FALSE(w.hasCrc);
  }
}

static int32_t s24(const uint8_t* b) { return static_cast<int32_t>(b[0] << 8 | b[1] << 16 | b[2] << 24) >> 8; }

void test_pcm_24() {
  ExportPcm pc;
  float l[3] = {1000.25f, -32767.f, 40000.f}, r[3] = {0.f, 0.5f, -50000.f};
  uint8_t o[18];
  TEST_ASSERT_EQUAL_UINT32(18, pc.put(l, r, 3, 1.f, o));
  TEST_ASSERT_EQUAL_INT32(256064, s24(o));
  TEST_ASSERT_EQUAL_INT32(0, s24(o + 3));
  TEST_ASSERT_EQUAL_INT32(-32767 * 256, s24(o + 6));
  TEST_ASSERT_EQUAL_INT32(128, s24(o + 9));
  TEST_ASSERT_EQUAL_INT32(8388607, s24(o + 12));
  TEST_ASSERT_EQUAL_INT32(-8388607, s24(o + 15));
  TEST_ASSERT_EQUAL_UINT32(2, pc.clips);
  TEST_ASSERT_EQUAL_UINT16(32767, pc.peak);
  ExportPcm g;
  float a[1] = {100.f}, b[1] = {-10.f};
  g.put(a, b, 1, 0.5f, o);  // gain applies
  TEST_ASSERT_EQUAL_INT32(50 * 256, s24(o));
  TEST_ASSERT_EQUAL_UINT16(50, g.peak);
}

void test_pcm_16_dither() {
  ExportPcm a, b;
  a.bits = b.bits = 16;
  a.seed(7);
  b.seed(7);
  static float l[1000], r[1000];
  for (int i = 0; i < 1000; ++i) l[i] = r[i] = 100.4f;
  static uint8_t oa[4000], ob[4000];
  TEST_ASSERT_EQUAL_UINT32(4000, a.put(l, r, 1000, 1.f, oa));
  b.put(l, r, 1000, 1.f, ob);
  TEST_ASSERT_EQUAL_MEMORY(oa, ob, sizeof oa);  // deterministic per seed
  long sum = 0;
  bool varied = false;
  for (int i = 0; i < 2000; ++i) {
    const int16_t v = static_cast<int16_t>(oa[2 * i] | oa[2 * i + 1] << 8);
    TEST_ASSERT_TRUE(v >= 99 && v <= 102);
    varied |= v != 100;
    sum += v;
  }
  TEST_ASSERT_TRUE(varied);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.4f, sum / 2000.f);  // dither keeps the mean
  float big[1] = {40000.f};
  a.put(big, big, 1, 1.f, oa);
  TEST_ASSERT_EQUAL_INT16(32767, static_cast<int16_t>(oa[0] | oa[1] << 8));
  TEST_ASSERT_EQUAL_UINT32(2, a.clips);
}

// ---- writer ----

// Files under a temp folder; ExportIo over stdio.
struct PosixIo : ExportIo {
  FILE* f[kExportMaxOut] = {};
  int writes[kExportMaxOut] = {};
  int failAt = -1;  // writes before a failure (-1 never)
  bool open(int i, const char* p, uint64_t) override {
    f[i] = fopen(p, "w+b");
    return f[i] != nullptr;
  }
  bool write(int i, const void* d, uint32_t n) override {
    ++writes[i];
    if (failAt == 0) return false;
    if (failAt > 0) --failAt;
    return fwrite(d, 1, n, f[i]) == n;
  }
  bool seal(int i, uint64_t size, const void* hdr, uint32_t n) override {
    fflush(f[i]);
    const bool ok = ftruncate(fileno(f[i]), static_cast<off_t>(size)) == 0 && fseek(f[i], 0, SEEK_SET) == 0 &&
                    fwrite(hdr, 1, n, f[i]) == n;
    fclose(f[i]);
    f[i] = nullptr;
    return ok;
  }
  void close(int i) override {
    if (f[i]) fclose(f[i]);
    f[i] = nullptr;
  }
  bool remove(const char* p) override { return ::remove(p) == 0; }
  bool rename(const char* a, const char* b) override { return ::rename(a, b) == 0; }
};

static std::string tmpDir() {
  static char d[] = "/tmp/mt_export_XXXXXX";
  static bool made = false;
  if (!made) made = mkdtemp(d) != nullptr;
  return d;
}

static bool exists(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0;
}

static std::vector<uint8_t> slurp(const std::string& p) {
  std::vector<uint8_t> v;
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) return v;
  int c;
  while ((c = fgetc(f)) != EOF) v.push_back(static_cast<uint8_t>(c));
  fclose(f);
  return v;
}

static Synth* fxSynth(Project& p) {
  static int16_t dline[kSynthRate / 4];
  static Synth* s = nullptr;
  delete s;
  s = new Synth(p);
  s->setDelayBuffer(dline, kSynthRate / 4);
  return s;
}

struct WriterRig {
  PosixIo io;
  ExportWriter w{io};
  std::vector<uint8_t> mem;
  std::vector<float> taps;
  WriterRig(int maxOut, uint32_t cap) : mem(maxOut * cap), taps(maxOut * 2 * Synth::kBlock) {}
};

// Renders p's pattern 0 (tail 40 blocks) into outputs [first, first + n) of outs; the master float
// copy (x 256) of every rendered block into master when given.
static bool renderInto(Project& p, WriterRig& rig, const ExportOut* outs, int first, int n, uint32_t cap,
                       uint32_t skip, uint32_t cut, std::vector<int32_t>* master = nullptr, int maxOut = 4) {
  Synth& synth = *fxSynth(p);
  synth.reset();
  rig.w.begin(tmpDir().c_str(), 24, skip, rig.mem.data(), cap, rig.taps.data(), maxOut);
  for (int i = 0; i < n; ++i)
    if (!rig.w.add(i, outs[first + i].tap, outs[first + i].name, 0)) return false;
  synth.setTaps(&rig.w.taps());
  RenderSpec s;
  s.tailBlocks = 40;
  OfflineRender::Guard g(p);
  OfflineRender r(p, synth, s);
  int16_t l[Synth::kBlock], rr[Synth::kBlock];
  int k = 0;
  bool ok = true;
  while (ok && r.renderBlock(l, rr)) {
    ok = rig.w.block(synth);
    if (master)
      for (int i = 0; i < Synth::kBlock; ++i) {
        master->push_back(static_cast<int32_t>(lrintf(synth.masterL()[i] * 256.f)));
        master->push_back(static_cast<int32_t>(lrintf(synth.masterR()[i] * 256.f)));
      }
    if (++k % 8 == 0 && ok) ok = rig.w.flush();
  }
  synth.setTaps(nullptr);
  if (!ok) {
    rig.w.abort();
    return false;
  }
  return rig.w.end(cut);
}

static void fillOuts(ExportOut* o) {
  o[0].tap = kExportTapMaster;
  strcpy(o[0].name, "T_MASTER.wav");
  o[1].tap = 1 + 2;
  strcpy(o[1].name, "T_03.wav");
  o[2].tap = 1 + RenderTaps::kDly;
  strcpy(o[2].name, "T_FX_DLY.wav");
}

void test_writer_files() {
  Project& p = *mkProj();
  p.instruments[0].send = 90;
  p.dlyLevel = 100;
  ExportOut o[3];
  fillOuts(o);
  WriterRig rig(4, 16384);
  std::vector<int32_t> master;
  const uint32_t cut = 30000;
  TEST_ASSERT_TRUE(renderInto(p, rig, o, 0, 3, 16384, 0, cut, &master));
  for (int i = 0; i < 3; ++i) {
    const std::string f = tmpDir() + "/" + o[i].name;
    TEST_ASSERT_TRUE(exists(f));
    TEST_ASSERT_FALSE(exists(f + ".tmp"));
    const std::vector<uint8_t> d = slurp(f);
    TEST_ASSERT_EQUAL_UINT32(kWavPcmHeaderBytes + cut * 6, d.size());
    MemSource src(d.data(), d.size());
    WavInfo w;
    TEST_ASSERT_EQUAL(0, static_cast<int>(wavParse(src, w)));
    TEST_ASSERT_EQUAL(24, w.bits);
    TEST_ASSERT_EQUAL_UINT32(cut, w.frames());
    if (i == 0)
      for (uint32_t k = 0; k < 2 * cut; k += 97) TEST_ASSERT_EQUAL_INT32(master[k], s24(&d[kWavPcmHeaderBytes + 3 * k]));
    TEST_ASSERT_TRUE(rig.w.peak(i) > 0);
  }
  // Pieces of at least the threshold: a few writes per file, never one per block.
  for (int i = 0; i < 3; ++i)
    TEST_ASSERT_TRUE(rig.io.writes[i] <= static_cast<int>(rig.w.written(i) / rig.w.threshold()) + 2);
}

void test_writer_skip() {
  Project& p = *mkProj();
  ExportOut o[3];
  fillOuts(o);
  std::vector<int32_t> master;
  WriterRig a(4, 16384);
  TEST_ASSERT_TRUE(renderInto(p, a, o, 0, 1, 16384, 0, 0, &master));
  const std::vector<uint8_t> whole = slurp(tmpDir() + "/" + o[0].name);
  WriterRig b(4, 16384);
  TEST_ASSERT_TRUE(renderInto(p, b, o, 0, 1, 16384, 1000, 5000));
  const std::vector<uint8_t> part = slurp(tmpDir() + "/" + o[0].name);
  TEST_ASSERT_EQUAL_UINT32(kWavPcmHeaderBytes + 5000 * 6, part.size());
  TEST_ASSERT_EQUAL_MEMORY(&whole[kWavPcmHeaderBytes + 1000 * 6], &part[kWavPcmHeaderBytes], 5000 * 6);
}

void test_writer_two_passes_equal_one() {
  Project& p = *mkProj();
  p.instruments[0].send = 90;
  p.dlyLevel = 100;
  ExportOut o[3];
  fillOuts(o);
  std::vector<std::vector<uint8_t>> one;
  {
    WriterRig rig(4, 16384);
    TEST_ASSERT_TRUE(renderInto(p, rig, o, 0, 3, 16384, 0, 20000));
    for (int i = 0; i < 3; ++i) one.push_back(slurp(tmpDir() + "/" + o[i].name));
  }
  WriterRig r1(2, 8192), r2(2, 8192);
  TEST_ASSERT_TRUE(renderInto(p, r1, o, 0, 2, 8192, 0, 20000, nullptr, 2));
  TEST_ASSERT_TRUE(renderInto(p, r2, o, 2, 1, 8192, 0, 20000, nullptr, 2));
  for (int i = 0; i < 3; ++i) {
    const std::vector<uint8_t> two = slurp(tmpDir() + "/" + o[i].name);
    TEST_ASSERT_EQUAL_UINT32(one[i].size(), two.size());
    TEST_ASSERT_EQUAL_MEMORY(one[i].data(), two.data(), two.size());
  }
}

void test_writer_abort_and_failure_leave_nothing() {
  Project& p = *mkProj();
  ExportOut o[3];
  fillOuts(o);
  strcpy(o[0].name, "F_MASTER.wav");
  strcpy(o[1].name, "F_03.wav");
  WriterRig rig(4, 8192);
  rig.io.failAt = 3;
  TEST_ASSERT_FALSE(renderInto(p, rig, o, 0, 2, 8192, 0, 0));
  for (int i = 0; i < 2; ++i) {
    TEST_ASSERT_FALSE(exists(tmpDir() + "/" + o[i].name));
    TEST_ASSERT_FALSE(exists(tmpDir() + "/" + o[i].name + ".tmp"));
  }
  WriterRig r2(4, 8192);
  r2.w.begin(tmpDir().c_str(), 24, 0, r2.mem.data(), 8192, r2.taps.data(), 4);
  TEST_ASSERT_TRUE(r2.w.add(0, 0, o[0].name, 0));
  TEST_ASSERT_TRUE(exists(tmpDir() + "/" + o[0].name + ".tmp"));
  TEST_ASSERT_FALSE(r2.w.add(2, 0, "X.wav", 0));   // out of order
  TEST_ASSERT_FALSE(r2.w.add(1, 20, "X.wav", 0));  // no such tap
  r2.w.abort();
  TEST_ASSERT_FALSE(exists(tmpDir() + "/" + o[0].name + ".tmp"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_us_to_frames);
  RUN_TEST(test_loops_render_every_pass);
  RUN_TEST(test_export_render_spec_tails);
  RUN_TEST(test_export_body_skip_cut);
  RUN_TEST(test_stem_tracks);
  RUN_TEST(test_dir_and_tags);
  RUN_TEST(test_outputs_names);
  RUN_TEST(test_passes_and_bytes);
  RUN_TEST(test_wav_header_pcm);
  RUN_TEST(test_pcm_24);
  RUN_TEST(test_pcm_16_dither);
  RUN_TEST(test_writer_files);
  RUN_TEST(test_writer_skip);
  RUN_TEST(test_writer_two_passes_equal_one);
  RUN_TEST(test_writer_abort_and_failure_leave_nothing);
  return UNITY_END();
}
