#include <string.h>
#include <unity.h>
#include <vector>
#include "preset_io.h"

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

// Field by field: Instrument has padding, which copies do not keep.
static void assertSameInst(const Instrument& a, const Instrument& b, const char* msg) {
  TEST_ASSERT_EQUAL_STRING_MESSAGE(a.name, b.name, msg);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(a.sample, b.sample, msg);
#define SAME(f) TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(a.f), static_cast<int>(b.f), msg)
  SAME(type); SAME(vol); SAME(transpose); SAME(fine);
  SAME(attack); SAME(decay); SAME(sustain); SAME(release); SAME(mono); SAME(glide);
  SAME(wave); SAME(duty); SAME(pwmRate); SAME(pwmDepth);
  SAME(root); SAME(start); SAME(end); SAME(loop); SAME(loopStart); SAME(reverse);
  SAME(machine);
  for (int k = 0; k < kFmMacros; ++k) SAME(macro[k]);
  SAME(lfoWave); SAME(lfoRate); SAME(lfoDepth); SAME(lfoDest);
  SAME(fltMode); SAME(cutoff); SAME(reso); SAME(fenv); SAME(fAtk); SAME(fDec); SAME(keytrack); SAME(send);
  SAME(drive); SAME(rsend); SAME(velCut); SAME(velMac); SAME(crushBits); SAME(crushRate);
  SAME(sliceMode); SAME(chopMode); SAME(chopN); SAME(chopThresh); SAME(sliceCount);
  for (int k = 0; k < kMaxSlices; ++k) SAME(slices[k]);
  for (int k = 0; k < 2; ++k) {
    SAME(synOsc[k]);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(a.synWt[k], b.synWt[k], msg);
  }
  SAME(synSemi); SAME(synSync); SAME(synSub); SAME(synSubOct); SAME(synNoise); SAME(synEAtk); SAME(synEDec);
#undef SAME
}

void setUp() {}
void tearDown() {}

static Instrument sample() {
  Instrument m;
  strcpy(m.name, "ACID");
  m.type = InstrType::Chip;
  m.wave = static_cast<uint8_t>(Wave::Saw);
  m.fltMode = static_cast<uint8_t>(FltMode::Lp);
  m.cutoff = 30;
  m.reso = 100;
  m.fenv = 50;
  m.lfoDest = static_cast<uint8_t>(LfoDest::Cutoff);
  m.lfoDepth = -20;
  m.send = 77;
  m.drive = 77;
  m.rsend = 12;
  m.velCut = -20;
  m.velMac = 33;
  m.crushBits = 90;
  m.crushRate = 40;
  return m;
}

void test_roundtrip() {
  const Instrument a = sample();
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  TEST_ASSERT_EQUAL(kPresetSize, out.buf.size());
  TEST_ASSERT_EQUAL(220, kPresetSize);
  TEST_ASSERT_EQUAL(204, kPresetSizeV3);
  TEST_ASSERT_EQUAL(156, kPresetSizeV2);
  TEST_ASSERT_EQUAL(84, kPresetSizeV1);
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  assertSameInst(a, b, "roundtrip");
}

void test_drum_roundtrip() {
  Instrument a;
  a.type = InstrType::Drum;
  drumSetMachine(a, static_cast<uint8_t>(DrumMachine::Hh9));
  a.macro[kMacDec] = 99;
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Drum), out.buf[5]);
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_TRUE(b.type == InstrType::Drum);
  TEST_ASSERT_EQUAL(static_cast<int>(DrumMachine::Hh9), b.machine);
  TEST_ASSERT_EQUAL(99, b.macro[kMacDec]);
}

static void expectUntouched(std::vector<uint8_t> buf, LoadErr want) {
  VecSource in(buf);
  Instrument b;
  strcpy(b.name, "KEEP");
  TEST_ASSERT_EQUAL(static_cast<int>(want), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_EQUAL_STRING("KEEP", b.name);
}

void test_bad_files() {
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(sample(), out));
  std::vector<uint8_t> v = out.buf;
  v[0] = 'X';
  expectUntouched(v, LoadErr::BadMagic);
  v = out.buf;
  v[4] = 99;
  expectUntouched(v, LoadErr::BadVersion);
  v = out.buf;
  v[20] ^= 0xFF;
  expectUntouched(v, LoadErr::BadCrc);
  v = out.buf;
  v.resize(v.size() - 3);
  expectUntouched(v, LoadErr::Truncated);
  v = out.buf;
  v.resize(5);
  expectUntouched(v, LoadErr::Truncated);
}

void test_load_clamps() {
  Instrument a = sample();
  a.transpose = 100;  // out of range: clamped like a project's
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_TRUE(b.transpose <= 24);
}

void test_apply_sample_found_and_missing() {
  Instrument dst;
  strcpy(dst.sample, "MINE");
  dst.root = 48;
  Instrument src;
  src.type = InstrType::Sample;
  strcpy(src.name, "PAD");
  strcpy(src.sample, "THEIRS");
  src.root = 72;
  src.loop = static_cast<uint8_t>(LoopMode::Forward);
  Instrument a = dst;
  applyPreset(a, src, true);
  TEST_ASSERT_EQUAL_STRING("THEIRS", a.sample);
  TEST_ASSERT_EQUAL(72, a.root);
  Instrument b = dst;
  applyPreset(b, src, false);
  TEST_ASSERT_EQUAL_STRING("MINE", b.sample);
  TEST_ASSERT_EQUAL(48, b.root);
  TEST_ASSERT_EQUAL(static_cast<int>(LoopMode::Forward), b.loop);  // the rest comes from the preset
  TEST_ASSERT_EQUAL_STRING("PAD", b.name);
}

void test_apply_non_sample_ignores_found() {
  Instrument dst;
  strcpy(dst.sample, "MINE");
  Instrument src;
  src.type = InstrType::Drum;
  drumSetMachine(src, static_cast<uint8_t>(DrumMachine::Sd9));
  applyPreset(dst, src, false);
  TEST_ASSERT_TRUE(dst.type == InstrType::Drum);
  TEST_ASSERT_EQUAL_STRING("", dst.sample);  // a whole copy: no sample to keep
}

void test_v2_roundtrip_slices() {
  Instrument a;
  a.type = InstrType::Sample;
  strcpy(a.sample, "BREAK");
  a.sliceMode = static_cast<uint8_t>(SliceMode::Fx);
  a.chopMode = static_cast<uint8_t>(ChopMode::Trans);
  a.chopN = 16;
  a.chopThresh = 80;
  a.sliceCount = 4;
  const uint16_t pos[4] = {0, 0x1000, 0x4000, 0xC000};
  memcpy(a.slices, pos, sizeof(pos));
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  TEST_ASSERT_EQUAL(kPresetVersion, out.buf[4]);
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  assertSameInst(a, b, "v2 slices");
}

void test_v1_file_loads_without_slices() {
  const Instrument a = sample();
  uint8_t f[kPresetSizeV1] = {'M', 'T', 'I', '1', 1, static_cast<uint8_t>(a.type), 0, 0};
  size_t p = 8;
  packInst(a, f + p);
  p += kInstRecSize;
  packFm(a, f + p);
  p += kFmRecSize;
  packFlt(a, f + p);
  p += kFltRecSize;
  TEST_ASSERT_EQUAL(80, p);
  const uint32_t c = crc32(f, p);
  for (int i = 0; i < 4; ++i) f[p + i] = static_cast<uint8_t>(c >> (8 * i));
  std::vector<uint8_t> v(f, f + sizeof(f));
  VecSource in(v);
  Instrument b;
  b.sliceCount = 5;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  assertSameInst(a, b, "v1");
  TEST_ASSERT_EQUAL(0, b.sliceCount);
}

void test_apply_preset_sample_missing_keeps_slices() {
  Instrument dst;
  strcpy(dst.sample, "a");
  dst.root = 50;
  dst.sliceCount = 2;
  dst.slices[0] = 1;
  dst.slices[1] = 2;
  Instrument src;
  src.type = InstrType::Sample;
  strcpy(src.sample, "b");
  src.sliceMode = static_cast<uint8_t>(SliceMode::Fx);
  src.sliceCount = 1;
  src.slices[0] = 7;
  Instrument x = dst;
  applyPreset(x, src, false);
  TEST_ASSERT_EQUAL_STRING("a", x.sample);
  TEST_ASSERT_EQUAL(50, x.root);
  TEST_ASSERT_EQUAL(2, x.sliceCount);
  TEST_ASSERT_EQUAL(1, x.slices[0]);
  TEST_ASSERT_EQUAL(2, x.slices[1]);
  TEST_ASSERT_EQUAL(static_cast<int>(SliceMode::Fx), x.sliceMode);
  Instrument y = dst;
  applyPreset(y, src, true);
  TEST_ASSERT_EQUAL_STRING("b", y.sample);
  TEST_ASSERT_EQUAL(1, y.sliceCount);
  TEST_ASSERT_EQUAL(7, y.slices[0]);
}

static Instrument synthInst() {
  Instrument a;
  strcpy(a.name, "WTPAD");
  instrSetType(a, InstrType::Synth);
  a.synOsc[0] = static_cast<uint8_t>(SynOsc::Wt);
  a.synOsc[1] = static_cast<uint8_t>(SynOsc::Square);
  strcpy(a.synWt[0], "*FORMANT");
  strcpy(a.synWt[1], "MYTABLE");
  a.synSemi = -12;
  a.synSync = true;
  a.synSub = 90;
  a.synSubOct = 1;
  a.synNoise = 20;
  a.synEAtk = 5;
  a.synEDec = 70;
  a.macro[kMacMix] = 40;
  a.lfoDest = static_cast<uint8_t>(LfoDest::Dec);
  a.lfoDepth = 20;
  return a;
}

void test_v3_roundtrip_synth() {
  const Instrument a = synthInst();
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  TEST_ASSERT_EQUAL(kPresetSize, out.buf.size());
  TEST_ASSERT_EQUAL(kPresetVersion, out.buf[4]);
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Synth), out.buf[5]);
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  assertSameInst(a, b, "v3 synth");
}

// A v2 file (no SYNI record): SYNTH fields get their defaults.
void test_v2_file_loads_synth_defaults() {
  const Instrument a = sample();
  uint8_t f[kPresetSizeV2] = {'M', 'T', 'I', '1', 2, static_cast<uint8_t>(a.type), 0, 0};
  size_t p = 8;
  packInst(a, f + p);
  p += kInstRecSize;
  packFm(a, f + p);
  p += kFmRecSize;
  packFlt(a, f + p);
  p += kFltRecSize;
  packSlices(a, f + p);
  p += kSliceRecSize;
  TEST_ASSERT_EQUAL(kPresetSizeV2 - 4, p);
  const uint32_t c = crc32(f, p);
  for (int i = 0; i < 4; ++i) f[p + i] = static_cast<uint8_t>(c >> (8 * i));
  std::vector<uint8_t> v(f, f + sizeof(f));
  VecSource in(v);
  Instrument b = synthInst();  // junk in the target: replaced by defaults
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  assertSameInst(a, b, "v2");
  TEST_ASSERT_EQUAL_STRING("", b.synWt[0]);
  TEST_ASSERT_EQUAL(0, b.synSemi);
  TEST_ASSERT_FALSE(b.synSync);
}

void test_apply_synth_copies_tables() {
  Instrument dst;
  strcpy(dst.sample, "MINE");
  const Instrument src = synthInst();
  applyPreset(dst, src, false);
  TEST_ASSERT_TRUE(dst.type == InstrType::Synth);
  TEST_ASSERT_EQUAL_STRING("*FORMANT", dst.synWt[0]);
  TEST_ASSERT_EQUAL_STRING("MYTABLE", dst.synWt[1]);
  TEST_ASSERT_EQUAL(-12, dst.synSemi);
}

void test_v3_file_loads_with_sound_fx_defaults() {
  Instrument a = sample();
  a.drive = a.rsend = 0;  // a version 3 file has zeros in those bytes
  a.velCut = a.velMac = 0;
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  std::vector<uint8_t> v = out.buf;
  TEST_ASSERT_EQUAL(kPresetVersion, v[4]);
  v.erase(v.begin() + (kPresetSizeV3 - 4), v.begin() + (kPresetSize - 4));  // a v3 file has no LFO record
  v[4] = 3;
  const size_t p = v.size() - 4;
  const uint32_t c = crc32(v.data(), p);
  for (int i = 0; i < 4; ++i) v[p + i] = static_cast<uint8_t>(c >> (8 * i));
  VecSource in(v);
  Instrument b = sample();
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  assertSameInst(a, b, "v3");
  v[4] = kPresetVersion + 1;  // newer than this firmware
  expectUntouched(v, LoadErr::BadVersion);
}

void test_sound_fx_fields_clamped() {
  Instrument a = sample();
  uint8_t r[kFmRecSize];
  packFm(a, r);
  r[10] = 200;
  r[11] = 255;
  r[12] = static_cast<uint8_t>(-100);
  r[13] = 100;
  Instrument b;
  unpackFm(r, b);
  TEST_ASSERT_EQUAL(127, b.drive);
  TEST_ASSERT_EQUAL(127, b.rsend);
  TEST_ASSERT_EQUAL(-64, b.velCut);
  TEST_ASSERT_EQUAL(63, b.velMac);
}

// LFO 2..4 and the sync flags survive a preset.
void test_lfos_roundtrip() {
  Instrument a = sample();
  a.lfoSync = 1;
  a.lfoRate = 6;
  a.lfoDest = static_cast<uint8_t>(LfoDest::Rtrg4);
  a.lfo[0] = {2, 40, -30, static_cast<uint8_t>(LfoDest::Cutoff), 0};
  a.lfo[1] = {0, 30, 40, static_cast<uint8_t>(LfoDest::Semi2), 0};
  a.lfo[2] = {4, 9, 20, static_cast<uint8_t>(LfoDest::Vol), 1};
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_EQUAL(1, b.lfoSync);
  TEST_ASSERT_EQUAL(6, b.lfoRate);
  TEST_ASSERT_EQUAL(40, b.lfo[0].rate);
  TEST_ASSERT_EQUAL(-30, b.lfo[0].depth);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Cutoff), b.lfo[0].dest);
  TEST_ASSERT_EQUAL(1, b.lfo[2].sync);
  TEST_ASSERT_EQUAL(9, b.lfo[2].rate);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Rtrg4), b.lfoDest);
  TEST_ASSERT_EQUAL(30, b.lfo[1].rate);
  TEST_ASSERT_EQUAL(40, b.lfo[1].depth);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Semi2), b.lfo[1].dest);
}

// A dest byte past the last target (newer firmware / junk) loads as PITCH (0), for LFO 1..4.
void test_lfo_dest_out_of_range() {
  Instrument a = sample();
  a.lfo[0].dest = static_cast<uint8_t>(LfoDest::Rtrg1);  // LFO 2: untouched, survives
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  std::vector<uint8_t> v = out.buf;
  v[8 + kInstRecSize + 9] = 99;  // LFO 1 dest in the FM record
  const size_t lfo = 8 + kInstRecSize + kFmRecSize + kFltRecSize + kSliceRecSize + kSynRecSize;
  v[lfo + 1 + 1 * 5 + 3] = 99;  // LFO 3 dest
  v[lfo + 1 + 2 * 5 + 3] = static_cast<uint8_t>(LfoDest::Count);  // LFO 4 dest
  const size_t p = v.size() - 4;
  const uint32_t c = crc32(v.data(), p);
  for (int i = 0; i < 4; ++i) v[p + i] = static_cast<uint8_t>(c >> (8 * i));
  VecSource in(v);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_EQUAL(0, b.lfoDest);
  TEST_ASSERT_EQUAL(static_cast<int>(LfoDest::Rtrg1), b.lfo[0].dest);
  TEST_ASSERT_EQUAL(0, b.lfo[1].dest);
  TEST_ASSERT_EQUAL(0, b.lfo[2].dest);
}

// The sync byte: bit 0 TEMPO, bit 1 Retrig OFF; both bits round-trip, other bits are dropped.
void test_lfo_sync_bits() {
  for (uint8_t s = 0; s < 4; ++s) {
    Instrument a;
    a.lfoSync = s;
    for (int i = 0; i < kLfos - 1; ++i) a.lfo[i].sync = s;
    uint8_t r[kLfoRecSize];
    packLfo(a, r);
    Instrument b;
    unpackLfo(r, b);
    TEST_ASSERT_EQUAL(s, b.lfoSync);
    for (int i = 0; i < kLfos - 1; ++i) TEST_ASSERT_EQUAL(s, b.lfo[i].sync);
    TEST_ASSERT_EQUAL(s & 1, lfoTempo(b.lfoSync));
    TEST_ASSERT_EQUAL((s >> 1) & 1, lfoFree(b.lfoSync));
  }
  // Old values 0 / 1 read as before; a free-running LFO keeps its rate, a TEMPO one is clamped.
  uint8_t r[kLfoRecSize] = {0};
  r[0] = 1;
  r[1 + 1] = 100;  // LFO 2 rate
  r[1 + 4] = 0;    // LFO 2 free
  r[6 + 1] = 100;  // LFO 3 rate
  r[6 + 4] = 1;    // LFO 3 TEMPO
  r[11 + 4] = 0xF6;  // LFO 4: junk high bits dropped -> 2 (FREE, Retrig OFF)
  r[11 + 1] = 100;
  Instrument b;
  b.lfoRate = 100;
  unpackLfo(r, b);
  TEST_ASSERT_EQUAL(1, b.lfoSync);
  TEST_ASSERT_EQUAL(kLfoSyncSteps - 1, b.lfoRate);
  TEST_ASSERT_EQUAL(0, b.lfo[0].sync);
  TEST_ASSERT_EQUAL(100, b.lfo[0].rate);
  TEST_ASSERT_EQUAL(1, b.lfo[1].sync);
  TEST_ASSERT_EQUAL(kLfoSyncSteps - 1, b.lfo[1].rate);
  TEST_ASSERT_EQUAL(kLfoFree, b.lfo[2].sync);
  TEST_ASSERT_EQUAL(100, b.lfo[2].rate);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip);
  RUN_TEST(test_drum_roundtrip);
  RUN_TEST(test_bad_files);
  RUN_TEST(test_load_clamps);
  RUN_TEST(test_apply_sample_found_and_missing);
  RUN_TEST(test_apply_non_sample_ignores_found);
  RUN_TEST(test_v2_roundtrip_slices);
  RUN_TEST(test_v1_file_loads_without_slices);
  RUN_TEST(test_apply_preset_sample_missing_keeps_slices);
  RUN_TEST(test_v3_roundtrip_synth);
  RUN_TEST(test_v2_file_loads_synth_defaults);
  RUN_TEST(test_apply_synth_copies_tables);
  RUN_TEST(test_v3_file_loads_with_sound_fx_defaults);
  RUN_TEST(test_sound_fx_fields_clamped);
  RUN_TEST(test_lfos_roundtrip);
  RUN_TEST(test_lfo_sync_bits);
  RUN_TEST(test_lfo_dest_out_of_range);
  return UNITY_END();
}
