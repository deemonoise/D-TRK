#include "inst_codec.h"
#include <string.h>

namespace mt {
namespace {

void wr16(uint8_t* b, uint16_t v) {
  b[0] = static_cast<uint8_t>(v);
  b[1] = static_cast<uint8_t>(v >> 8);
}
uint16_t rd16(const uint8_t* b) { return static_cast<uint16_t>(b[0] | (b[1] << 8)); }
uint8_t clampu(int v, int lo, int hi) { return static_cast<uint8_t>(v < lo ? lo : (v > hi ? hi : v)); }
int8_t clamps(int8_t v, int lo, int hi) { return static_cast<int8_t>(v < lo ? lo : (v > hi ? hi : v)); }

}  // namespace

void packInst(const Instrument& m, uint8_t* b) {
  memset(b, 0, kInstRecSize);
  memcpy(b, m.name, 8);
  b[8] = static_cast<uint8_t>(m.type);
  b[9] = m.vol;
  b[10] = static_cast<uint8_t>(m.transpose);
  b[11] = static_cast<uint8_t>(m.fine);
  b[12] = m.attack;
  b[13] = m.decay;
  b[14] = m.sustain;
  b[15] = m.release;
  b[16] = m.mono ? 1 : 0;
  b[17] = m.glide;
  b[18] = m.wave;
  b[19] = m.duty;
  b[20] = m.pwmRate;
  b[21] = m.pwmDepth;
  memcpy(b + 22, m.sample, kSampleNameMax);
  b[38] = m.root;
  wr16(b + 39, m.start);
  wr16(b + 41, m.end);
  b[43] = m.loop;
  wr16(b + 44, m.loopStart);
  b[46] = m.reverse ? 1 : 0;
}

void packFm(const Instrument& m, uint8_t* b) {
  memset(b, 0, kFmRecSize);
  b[0] = m.machine;
  memcpy(b + 1, m.macro, kFmMacros);
  b[6] = m.lfoWave;
  b[7] = m.lfoRate;
  b[8] = static_cast<uint8_t>(m.lfoDepth);
  b[9] = m.lfoDest;
  b[10] = m.drive;  // 10..13: once reserved (0), so older files read the defaults
  b[11] = m.rsend;
  b[12] = static_cast<uint8_t>(m.velCut);
  b[13] = static_cast<uint8_t>(m.velMac);
  b[14] = m.crushBits;  // 14, 15: once reserved (0 = off)
  b[15] = m.crushRate;
}

void unpackFm(const uint8_t* b, Instrument& m) {
  // Machine of either type; fixInstrument() narrows it by type.
  m.machine = b[0] < kMachineMax ? b[0] : 0;
  for (int k = 0; k < kFmMacros; ++k) m.macro[k] = clampu(b[1 + k], 0, 127);
  m.lfoWave = b[6] < static_cast<int>(LfoWave::Count) ? b[6] : 0;
  m.lfoRate = clampu(b[7], 0, 127);
  m.lfoDepth = clamps(static_cast<int8_t>(b[8]), -64, 63);
  m.lfoDest = b[9] < static_cast<int>(LfoDest::Count) ? b[9] : 0;
  m.drive = clampu(b[10], 0, 127);
  m.rsend = clampu(b[11], 0, 127);
  m.velCut = clamps(static_cast<int8_t>(b[12]), -64, 63);
  m.velMac = clamps(static_cast<int8_t>(b[13]), -64, 63);
  m.crushBits = clampu(b[14], 0, 127);
  m.crushRate = clampu(b[15], 0, 127);
}

void unpackInst(const uint8_t* b, Instrument& m) {
  memcpy(m.name, b, 8);
  m.name[8] = 0;
  m.type = b[8] < static_cast<int>(InstrType::Count) ? static_cast<InstrType>(b[8]) : InstrType::Chip;
  m.vol = clampu(b[9], 0, 127);
  m.transpose = clamps(static_cast<int8_t>(b[10]), -24, 24);
  m.fine = clamps(static_cast<int8_t>(b[11]), -50, 50);
  m.attack = clampu(b[12], 0, 127);
  m.decay = clampu(b[13], 0, 127);
  m.sustain = clampu(b[14], 0, 127);
  m.release = clampu(b[15], 0, 127);
  m.mono = b[16] != 0;
  m.glide = b[17];
  m.wave = b[18] < kWaveCount ? b[18] : 0;
  m.duty = clampu(b[19], 1, 99);
  m.pwmRate = clampu(b[20], 0, 127);
  m.pwmDepth = clampu(b[21], 0, 49);
  memcpy(m.sample, b + 22, kSampleNameMax);
  m.sample[kSampleNameMax] = 0;
  m.root = clampu(b[38], 0, 127);
  m.start = rd16(b + 39);
  m.end = rd16(b + 41);
  m.loop = b[43] < static_cast<int>(LoopMode::Count) ? b[43] : 0;
  m.loopStart = rd16(b + 44);
  m.reverse = b[46] != 0;
}

void packFlt(const Instrument& m, uint8_t* b) {
  memset(b, 0, kFltRecSize);
  b[0] = m.fltMode;
  b[1] = m.cutoff;
  b[2] = m.reso;
  b[3] = static_cast<uint8_t>(m.fenv);
  b[4] = m.fAtk;
  b[5] = m.fDec;
  b[6] = m.keytrack;
  b[7] = m.send;  // was reserved (0): older files have no send
}

void unpackFlt(const uint8_t* b, Instrument& m) {
  m.fltMode = b[0] < static_cast<int>(FltMode::Count) ? b[0] : 0;
  m.cutoff = clampu(b[1], 0, 127);
  m.reso = clampu(b[2], 0, 127);
  m.fenv = clamps(static_cast<int8_t>(b[3]), -64, 63);
  m.fAtk = clampu(b[4], 0, 127);
  m.fDec = clampu(b[5], 0, 127);
  m.keytrack = clampu(b[6], 0, 127);
  m.send = clampu(b[7], 0, 127);
}

void packSlices(const Instrument& m, uint8_t* b) {
  memset(b, 0, kSliceRecSize);
  b[0] = m.sliceMode;
  b[1] = m.chopMode;
  b[2] = m.chopN;
  b[3] = m.chopThresh;
  const int n = m.sliceCount < kMaxSlices ? m.sliceCount : kMaxSlices;
  b[4] = static_cast<uint8_t>(n);
  for (int i = 0; i < n; ++i) wr16(b + 8 + 2 * i, m.slices[i]);
}

void unpackSlices(const uint8_t* b, Instrument& m) {
  m.sliceMode = b[0] < static_cast<int>(SliceMode::Count) ? b[0] : 0;
  m.chopMode = b[1] < static_cast<int>(ChopMode::Count) ? b[1] : 0;
  m.chopN = clampu(b[2], 2, kMaxSlices);
  m.chopThresh = clampu(b[3], 0, 100);
  const int count = b[4] < kMaxSlices ? b[4] : kMaxSlices;
  int n = 0;
  for (; n < count; ++n) {
    const uint16_t v = rd16(b + 8 + 2 * n);
    if (n && v <= m.slices[n - 1]) break;
    m.slices[n] = v;
  }
  for (int i = n; i < kMaxSlices; ++i) m.slices[i] = 0;
  m.sliceCount = static_cast<uint8_t>(n);
}

void packLfo(const Instrument& m, uint8_t* b) {
  b[0] = m.lfoSync & 3;
  for (int i = 0; i < kLfos - 1; ++i) {
    const LfoCfg& l = m.lfo[i];
    uint8_t* r = b + 1 + i * 5;
    r[0] = l.wave;
    r[1] = l.rate;
    r[2] = static_cast<uint8_t>(l.depth);
    r[3] = l.dest;
    r[4] = l.sync & 3;
  }
}

void unpackLfo(const uint8_t* b, Instrument& m) {
  m.lfoSync = b[0] & 3;
  if (lfoTempo(m.lfoSync) && m.lfoRate >= kLfoSyncSteps) m.lfoRate = kLfoSyncSteps - 1;
  for (int i = 0; i < kLfos - 1; ++i) {
    LfoCfg& l = m.lfo[i];
    const uint8_t* r = b + 1 + i * 5;
    l.wave = r[0] < static_cast<uint8_t>(LfoWave::Count) ? r[0] : 0;
    l.sync = r[4] & 3;
    l.rate = r[1] > 127 ? 127 : r[1];
    if (lfoTempo(l.sync) && l.rate >= kLfoSyncSteps) l.rate = kLfoSyncSteps - 1;
    const int d = static_cast<int8_t>(r[2]);
    l.depth = static_cast<int8_t>(d < -64 ? -64 : (d > 63 ? 63 : d));
    l.dest = r[3] < static_cast<uint8_t>(LfoDest::Count) ? r[3] : 0;
  }
}

void packSyn(const Instrument& m, uint8_t* b) {
  memset(b, 0, kSynRecSize);
  b[0] = m.synOsc[0];
  b[1] = m.synOsc[1];
  memcpy(b + 2, m.synWt[0], strnlen(m.synWt[0], kSampleNameMax));
  memcpy(b + 18, m.synWt[1], strnlen(m.synWt[1], kSampleNameMax));
  b[34] = static_cast<uint8_t>(m.synSemi);
  b[35] = m.synSync ? 1 : 0;
  b[36] = m.synSub;
  b[37] = m.synSubOct;
  b[38] = m.synNoise;
  b[39] = m.synEAtk;
  b[40] = m.synEDec;
}

void unpackSyn(const uint8_t* b, Instrument& m) {
  for (int k = 0; k < 2; ++k) {
    m.synOsc[k] = b[k] < static_cast<int>(SynOsc::Count) ? b[k] : 0;
    memcpy(m.synWt[k], b + 2 + 16 * k, kSampleNameMax);
    m.synWt[k][kSampleNameMax] = 0;
  }
  m.synSemi = clamps(static_cast<int8_t>(b[34]), -24, 24);
  m.synSync = b[35] != 0;
  m.synSub = clampu(b[36], 0, 127);
  m.synSubOct = clampu(b[37], 0, 1);
  m.synNoise = clampu(b[38], 0, 127);
  m.synEAtk = clampu(b[39], 0, 127);
  m.synEDec = clampu(b[40], 0, 127);
}

void fixInstrument(Instrument& m) {
  // SYNTH: unpackSyn already clamps every field; wavetable names are resolved by the firmware.
  int n = 0;
  if (m.type == InstrType::Fm) n = static_cast<int>(FmMachine::Count);
  else if (m.type == InstrType::Drum) n = static_cast<int>(DrumMachine::Count);
  if (n && m.machine >= n) m.machine = 0;
}

}  // namespace mt
