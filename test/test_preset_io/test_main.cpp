#include <string.h>
#include <unity.h>
#include <vector>
#include "bytebeat_lib.h"
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
  SAME(drive); SAME(rsend); SAME(velCut); SAME(velMac); SAME(crushBits); SAME(crushRate); SAME(driveModel);
  SAME(lofiFormula); SAME(lofiSync);
  SAME(sliceMode); SAME(chopMode); SAME(chopN); SAME(chopThresh); SAME(sliceCount);
  for (int k = 0; k < kMaxSlices; ++k) SAME(slices[k]);
  for (int k = 0; k < 2; ++k) {
    SAME(synOsc[k]);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(a.synWt[k], b.synWt[k], msg);
  }
  SAME(synSemi); SAME(synSync); SAME(synSub); SAME(synSubOct); SAME(synNoise); SAME(synEAtk); SAME(synEDec);
  SAME(synFold); SAME(synRing);
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
  m.driveModel = static_cast<uint8_t>(DriveModel::Fold);
  return m;
}

void test_roundtrip() {
  const Instrument a = sample();
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  TEST_ASSERT_EQUAL(kPresetSize, out.buf.size());
  TEST_ASSERT_EQUAL(236, kPresetSize);
  TEST_ASSERT_EQUAL(228, kPresetSizeV6);
  TEST_ASSERT_EQUAL(220, kPresetSizeV5);
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
  a.synFold = 77;
  a.synRing = 33;
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

// Type 7 is a former type: a preset stored with it loads as CHIP.
void test_type7_preset_loads_as_chip() {
  Instrument a;
  strcpy(a.name, "OLD7");
  instrSetType(a, InstrType::Fm);
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::Fm), out.buf[5]);
  out.buf[5] = 7;   // header type
  out.buf[16] = 7;  // INST byte 8
  const size_t p = out.buf.size() - 4;
  const uint32_t c = crc32(out.buf.data(), p);
  for (int i = 0; i < 4; ++i) out.buf[p + i] = static_cast<uint8_t>(c >> (8 * i));
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_TRUE(b.type == InstrType::Chip);
  TEST_ASSERT_EQUAL_STRING("OLD7", b.name);
}

void test_roundtrip_string() {
  Instrument a;
  strcpy(a.name, "BELL");
  instrSetType(a, InstrType::String);
  a.machine = static_cast<uint8_t>(StringModel::Modal);
  a.macro[kMacDamp] = 10;
  a.macro[kMacBright] = 90;
  a.macro[kMacStruct] = 64;
  a.macro[kMacPos] = 20;
  a.macro[kMacExc] = 127;
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  TEST_ASSERT_EQUAL(static_cast<int>(InstrType::String), out.buf[5]);
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  assertSameInst(a, b, "string");
  TEST_ASSERT_TRUE(b.type == InstrType::String);
  TEST_ASSERT_EQUAL(static_cast<int>(StringModel::Modal), b.machine);
  TEST_ASSERT_EQUAL(127, b.macro[kMacExc]);
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

// FREE (bit 1 of the sync byte) survives, alone (rate 0..127 kept) and with TEMPO.
void test_lfo_free_flag_roundtrip() {
  Instrument a = sample();
  a.lfoSync = kLfoFree;
  a.lfoRate = 100;
  a.lfo[1] = {0, 5, 10, 0, kLfoTempo | kLfoFree};
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_EQUAL(kLfoFree, b.lfoSync);
  TEST_ASSERT_EQUAL(100, b.lfoRate);
  TEST_ASSERT_EQUAL(kLfoTempo | kLfoFree, b.lfo[1].sync);
  TEST_ASSERT_EQUAL(5, b.lfo[1].rate);
  TEST_ASSERT_TRUE(lfoTempo(b.lfo[1].sync) && lfoFree(b.lfo[1].sync));
  TEST_ASSERT_FALSE(lfoTempo(b.lfoSync));
}


// The recommended insert's drive model: INST record byte 47 (once reserved, 0 = TANH); an unknown
// model loads as TANH.
void test_drive_model_byte() {
  Instrument a = sample();
  a.driveModel = static_cast<uint8_t>(DriveModel::Asym);
  uint8_t r[kInstRecSize];
  packInst(a, r);
  TEST_ASSERT_EQUAL(static_cast<int>(DriveModel::Asym), r[47]);
  Instrument b;
  unpackInst(r, b);
  TEST_ASSERT_EQUAL(static_cast<int>(DriveModel::Asym), b.driveModel);
  r[47] = static_cast<uint8_t>(DriveModel::Count);
  unpackInst(r, b);
  TEST_ASSERT_EQUAL(0, b.driveModel);
}

// SYNTH fold / ring: SYNI bytes 41 / 42 (once reserved, 0 = off); clamped to 0..127.
void test_syn_fold_ring_bytes() {
  Instrument a = synthInst();
  uint8_t r[kSynRecSize];
  packSyn(a, r);
  TEST_ASSERT_EQUAL(77, r[41]);
  TEST_ASSERT_EQUAL(33, r[42]);
  for (size_t k = 43; k < kSynRecSize; ++k) TEST_ASSERT_EQUAL(0, r[k]);
  Instrument b;
  unpackSyn(r, b);
  TEST_ASSERT_EQUAL(77, b.synFold);
  TEST_ASSERT_EQUAL(33, b.synRing);
  for (size_t k = 41; k < kSynRecSize; ++k) r[k] = 0;
  unpackSyn(r, b);
  TEST_ASSERT_EQUAL(0, b.synFold);
  TEST_ASSERT_EQUAL(0, b.synRing);
  r[41] = 200;
  r[42] = 200;
  unpackSyn(r, b);
  TEST_ASSERT_EQUAL(127, b.synFold);
  TEST_ASSERT_EQUAL(127, b.synRing);
}

Project proj;

// Loading a preset onto a track: its recommended insert overwrites the track's insert when the
// track (INT) plays that instrument; the project's instrument keeps no insert.
void test_take_preset_insert() {
  proj.reset();
  proj.tracks[2].instr = 5;
  proj.tracks[2].out = TrackOut::Int;
  proj.tracks[2].drive = 99;
  proj.tracks[2].crushBits = 99;
  proj.tracks[2].crushRate = 99;
  proj.tracks[2].driveModel = static_cast<uint8_t>(DriveModel::Rect);
  proj.tracks[2].gateDepth = 77;  // not part of the recommendation
  Instrument& m = proj.instruments[5];
  m.drive = 40;
  m.crushBits = 70;
  m.crushRate = 0;
  m.driveModel = static_cast<uint8_t>(DriveModel::Clip);
  takePresetInsert(proj, 5, 2);
  TEST_ASSERT_EQUAL(40, proj.tracks[2].drive);
  TEST_ASSERT_EQUAL(70, proj.tracks[2].crushBits);
  TEST_ASSERT_EQUAL(0, proj.tracks[2].crushRate);
  TEST_ASSERT_EQUAL(static_cast<int>(DriveModel::Clip), proj.tracks[2].driveModel);
  TEST_ASSERT_EQUAL(77, proj.tracks[2].gateDepth);
  TEST_ASSERT_EQUAL(0, m.drive);
  TEST_ASSERT_EQUAL(0, m.crushBits);
  TEST_ASSERT_EQUAL(0, m.crushRate);
  TEST_ASSERT_EQUAL(0, m.driveModel);
}

// A track that does not play the instrument (e.g. a KIT track while a lane's instrument gets the
// preset), a MIDI track or no track (-1): the track insert stays, the instrument's is dropped.
void test_take_preset_insert_other_track() {
  proj.reset();
  proj.tracks[0].instr = 1;
  proj.tracks[0].out = TrackOut::Int;
  proj.tracks[0].drive = 12;
  proj.tracks[1].instr = 5;
  proj.tracks[1].out = TrackOut::Midi;
  proj.tracks[1].drive = 13;
  for (int t : {0, 1, -1}) {
    proj.instruments[5].drive = 60;
    proj.instruments[5].driveModel = 2;
    takePresetInsert(proj, 5, t);
    TEST_ASSERT_EQUAL(0, proj.instruments[5].drive);
    TEST_ASSERT_EQUAL(0, proj.instruments[5].driveModel);
  }
  TEST_ASSERT_EQUAL(12, proj.tracks[0].drive);
  TEST_ASSERT_EQUAL(13, proj.tracks[1].drive);
}

// Saving a preset from a track: the track's insert is the preset's recommendation (no track
// playing the instrument: none), so loading it back onto a track restores the insert.
void test_preset_insert_for_save_roundtrip() {
  proj.reset();
  proj.tracks[3].instr = 7;
  proj.tracks[3].out = TrackOut::Int;
  proj.tracks[3].drive = 50;
  proj.tracks[3].crushBits = 20;
  proj.tracks[3].crushRate = 10;
  proj.tracks[3].driveModel = static_cast<uint8_t>(DriveModel::Fold);
  Instrument m = proj.instruments[7];
  presetInsertForSave(proj, 7, 3, m);
  TEST_ASSERT_EQUAL(50, m.drive);
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(m, out));
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  proj.tracks[3].drive = proj.tracks[3].crushBits = proj.tracks[3].crushRate = proj.tracks[3].driveModel = 0;
  applyPreset(proj.instruments[7], b, true);
  takePresetInsert(proj, 7, 3);
  TEST_ASSERT_EQUAL(50, proj.tracks[3].drive);
  TEST_ASSERT_EQUAL(20, proj.tracks[3].crushBits);
  TEST_ASSERT_EQUAL(10, proj.tracks[3].crushRate);
  TEST_ASSERT_EQUAL(static_cast<int>(DriveModel::Fold), proj.tracks[3].driveModel);
  Instrument n = proj.instruments[7];
  n.drive = 33;
  presetInsertForSave(proj, 7, 0, n);  // track 0 plays another instrument
  TEST_ASSERT_EQUAL(0, n.drive);
  TEST_ASSERT_EQUAL(0, n.driveModel);
}

// ---- Version 6: the LOFI record ----

// A file with v as its version, cut to size (the CRC refreshed).
static std::vector<uint8_t> asVersion(std::vector<uint8_t> v, uint8_t ver, size_t size) {
  v.erase(v.begin() + (size - 4), v.end() - 4);
  v[4] = ver;
  const size_t p = v.size() - 4;
  const uint32_t c = crc32(v.data(), p);
  for (int i = 0; i < 4; ++i) v[p + i] = static_cast<uint8_t>(c >> (8 * i));
  return v;
}

static Instrument lofi() {
  Instrument m;
  strcpy(m.name, "BYTES");
  instrSetType(m, InstrType::Lofi);
  lofiSetMachine(m, static_cast<uint8_t>(LofiMachine::Apc));
  m.macro[1] = 90;
  m.lofiFormula = 5;
  m.lofiSync = 1;
  return m;
}

void test_v6_lofi_roundtrip() {
  const Instrument a = lofi();
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  std::vector<uint8_t> v = asVersion(out.buf, 6, kPresetSizeV6);  // a v6 file: no LFOB record
  TEST_ASSERT_EQUAL(kPresetSizeV6, v.size());
  VecSource in(v);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  assertSameInst(a, b, "v6");
  TEST_ASSERT_TRUE(b.type == InstrType::Lofi);
  TEST_ASSERT_EQUAL(static_cast<int>(LofiMachine::Apc), b.machine);
  TEST_ASSERT_EQUAL(5, b.lofiFormula);
  TEST_ASSERT_EQUAL(1, b.lofiSync);
  // The sync flag is byte 1 of the LOFI record (the last kLofiRecSize bytes before the CRC in v6);
  // a record from before it (0 there) loads sync off.
  TEST_ASSERT_EQUAL(1, v[v.size() - 4 - kLofiRecSize + 1]);
  v[v.size() - 4 - kLofiRecSize + 1] = 0;
  v = asVersion(v, 6, kPresetSizeV6);
  VecSource in0(v);
  Instrument c;
  c.lofiSync = 1;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in0, c)));
  TEST_ASSERT_EQUAL(0, c.lofiSync);
}

void test_v5_file_loads_formula_0() {
  Instrument a = sample();
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  const std::vector<uint8_t> v = asVersion(out.buf, 5, kPresetSizeV5);
  TEST_ASSERT_EQUAL(kPresetSizeV5, v.size());
  VecSource in(v);
  Instrument b;
  b.lofiFormula = 9;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  assertSameInst(a, b, "v5");
  TEST_ASSERT_EQUAL(0, b.lofiFormula);
}

void test_lofi_preset_clamped() {
  Instrument a = lofi();
  a.machine = 9;         // past LofiMachine::Count
  a.lofiFormula = 250;   // past the library
  a.lofiSync = 7;        // flag: 0 / 1
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_EQUAL(0, b.machine);
  TEST_ASSERT_EQUAL(0, b.lofiFormula);
  TEST_ASSERT_EQUAL(1, b.lofiSync);
}

// ---- Version 7: the LFOB record (LFO BYTE formula, A per LFO) ----

void test_v7_lfob_roundtrip() {
  Instrument a = lofi();
  a.lfoWave = static_cast<uint8_t>(LfoWave::Byte);
  for (int i = 0; i < kLfos; ++i) {
    a.lfoByte[i][0] = static_cast<uint8_t>(i + 1);
    a.lfoByte[i][1] = static_cast<uint8_t>(100 + i);
  }
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  TEST_ASSERT_EQUAL(7, kPresetVersion);
  TEST_ASSERT_EQUAL(7, out.buf[4]);
  TEST_ASSERT_EQUAL(kPresetSize, out.buf.size());
  TEST_ASSERT_EQUAL(kPresetSizeV6 + kLfoByteRecSize, kPresetSize);
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  assertSameInst(a, b, "v7");
  TEST_ASSERT_EQUAL(static_cast<int>(LfoWave::Byte), b.lfoWave);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(&a.lfoByte[0][0], &b.lfoByte[0][0], 2 * kLfos);
}

void test_v6_file_loads_lfob_defaults() {
  Instrument a = sample();
  for (int i = 0; i < kLfos; ++i) a.lfoByte[i][0] = a.lfoByte[i][1] = 3;
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  const std::vector<uint8_t> v = asVersion(out.buf, 6, kPresetSizeV6);
  VecSource in(v);
  Instrument b;
  for (int i = 0; i < kLfos; ++i) b.lfoByte[i][0] = b.lfoByte[i][1] = 9;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  for (int i = 0; i < kLfos; ++i) {
    TEST_ASSERT_EQUAL(0, b.lfoByte[i][0]);
    TEST_ASSERT_EQUAL(byteFormula(0).a, b.lfoByte[i][1]);
  }
}

void test_lfob_preset_clamped() {
  Instrument a = sample();
  a.lfoByte[0][0] = 250;  // past the library
  a.lfoByte[0][1] = 200;  // A: 0..127
  a.lfoByte[3][0] = static_cast<uint8_t>(byteFormulaCount() - 1);
  VecSink out;
  TEST_ASSERT_TRUE(savePreset(a, out));
  VecSource in(out.buf);
  Instrument b;
  TEST_ASSERT_EQUAL(static_cast<int>(LoadErr::Ok), static_cast<int>(loadPreset(in, b)));
  TEST_ASSERT_EQUAL(0, b.lfoByte[0][0]);
  TEST_ASSERT_EQUAL(127, b.lfoByte[0][1]);
  TEST_ASSERT_EQUAL(byteFormulaCount() - 1, b.lfoByte[3][0]);
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
  RUN_TEST(test_type7_preset_loads_as_chip);
  RUN_TEST(test_roundtrip_string);
  RUN_TEST(test_v2_file_loads_synth_defaults);
  RUN_TEST(test_apply_synth_copies_tables);
  RUN_TEST(test_v3_file_loads_with_sound_fx_defaults);
  RUN_TEST(test_sound_fx_fields_clamped);
  RUN_TEST(test_lfos_roundtrip);
  RUN_TEST(test_lfo_free_flag_roundtrip);
  RUN_TEST(test_lfo_sync_bits);
  RUN_TEST(test_lfo_dest_out_of_range);
  RUN_TEST(test_drive_model_byte);
  RUN_TEST(test_syn_fold_ring_bytes);
  RUN_TEST(test_take_preset_insert);
  RUN_TEST(test_take_preset_insert_other_track);
  RUN_TEST(test_preset_insert_for_save_roundtrip);
  RUN_TEST(test_v6_lofi_roundtrip);
  RUN_TEST(test_v5_file_loads_formula_0);
  RUN_TEST(test_lofi_preset_clamped);
  RUN_TEST(test_v7_lfob_roundtrip);
  RUN_TEST(test_v6_file_loads_lfob_defaults);
  RUN_TEST(test_lfob_preset_clamped);
  return UNITY_END();
}
