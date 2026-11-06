#include "inst_codec.h"
#include "synth_fm_machines.h"
#include <string.h>
#include <math.h>
#include "bytebeat_lib.h"
#include "mod_matrix.h"

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

void packMod(const Instrument& m, uint8_t* b) { memcpy(b, m.mod, kModRecSize); }
void unpackMod(const uint8_t* b, size_t n, Instrument& m) {
  for (int i = 0; i < kModRoutes && (i + 1) * sizeof(ModRoute) <= n; ++i) {
    ModRoute r;
    memcpy(&r, b + i * sizeof(ModRoute), sizeof r);
    if (r.source >= uint8_t(ModSource::Count) || !modDestValid(r.dest)) r.source = 0;
    if (!modDestValid(r.dest)) r.dest = 0;
    r.amount = clamps(r.amount, -64, 63);
    if (r.polarity >= uint8_t(ModPolarity::Count)) r.polarity = 0;
    if (r.via >= uint8_t(ModSource::Count)) r.via = 0;
    m.mod[i] = r;
  }
}

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
  b[47] = m.driveModel;  // once reserved (0 = TANH)
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
  m.machine = b[0] < kMachineMax || m.type == InstrType::Fm ? b[0] : 0;
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
  // Type 7 is a former type (and anything past Count is unknown): both load as CHIP.
  m.type = b[8] < static_cast<int>(InstrType::Count) && b[8] != static_cast<int>(InstrType::Unused7)
               ? static_cast<InstrType>(b[8])
               : InstrType::Chip;
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
  m.driveModel = b[47] < static_cast<uint8_t>(DriveModel::Count) ? b[47] : 0;
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

namespace {
void packSliceRecord(const Instrument& m, uint8_t* b, size_t size, int limit) {
  memset(b, 0, size);
  b[0] = m.sliceMode;
  b[1] = m.chopMode;
  b[2] = m.chopN > limit ? static_cast<uint8_t>(limit) : m.chopN;
  b[3] = m.chopThresh;
  const int n = m.sliceCount < limit ? m.sliceCount : limit;
  b[4] = static_cast<uint8_t>(n);
  for (int i = 0; i < n; ++i) wr16(b + 8 + 2 * i, m.slices[i]);
}

void unpackSliceRecord(const uint8_t* b, Instrument& m, int limit) {
  m.sliceMode = b[0] < static_cast<int>(SliceMode::Count) ? b[0] : 0;
  m.chopMode = b[1] < static_cast<int>(ChopMode::Count) ? b[1] : 0;
  m.chopN = clampu(b[2], 2, limit);
  m.chopThresh = clampu(b[3], 0, 100);
  const int count = b[4] < limit ? b[4] : limit;
  int n = 0;
  for (; n < count; ++n) {
    const uint16_t v = rd16(b + 8 + 2 * n);
    if (n && v <= m.slices[n - 1]) break;
    m.slices[n] = v;
  }
  // Includes the extended tail when loading a legacy record into the larger runtime model.
  for (int i = n; i < kMaxSlices; ++i) m.slices[i] = 0;
  m.sliceCount = static_cast<uint8_t>(n);
}
}  // namespace

void packSlices(const Instrument& m, uint8_t* b) { packSliceRecord(m, b, kSliceRecSize, kLegacySlices); }
void unpackSlices(const uint8_t* b, Instrument& m) { unpackSliceRecord(b, m, kLegacySlices); }
void packSlicesExt(const Instrument& m, uint8_t* b) { packSliceRecord(m, b, kSliceExtRecSize, kMaxSlices); }
void unpackSlicesExt(const uint8_t* b, Instrument& m) { unpackSliceRecord(b, m, kMaxSlices); }

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
  b[41] = m.synFold;
  b[42] = m.synRing;
  b[43] = m.synPhaseMode;
  b[44] = m.synPhase[0];
  b[45] = m.synPhase[1];
  b[46] = m.synEnvDest;
  b[47] = m.synSubWave;
}

void unpackSyn(const uint8_t* b, Instrument& m) {
  for (int k = 0; k < 2; ++k) {
    const int count = k ? static_cast<int>(SynOsc::HyperSaw) : static_cast<int>(SynOsc::Count);
    m.synOsc[k] = b[k] < count ? b[k] : 0;  // one HyperSaw group: OSC1 only
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
  m.synFold = clampu(b[41], 0, 127);  // 41, 42: once reserved 0 (off)
  m.synRing = clampu(b[42], 0, 127);
  m.synPhaseMode = b[43] < static_cast<uint8_t>(SynPhaseMode::Count) ? b[43] : 0;
  m.synPhase[0] = clampu(b[44], 0, 127);
  m.synPhase[1] = clampu(b[45], 0, 127);
  m.synEnvDest = b[46] < static_cast<uint8_t>(SynEnvDest::Count) ? b[46] : 0;
  m.synSubWave = b[47] < static_cast<uint8_t>(SynSubWave::Count) ? b[47] : 0;
}

void packLofi(const Instrument& m, uint8_t* b) {
  memset(b, 0, kLofiRecSize);
  b[0] = m.lofiFormula;
  b[1] = m.lofiSync;
}

void unpackLofi(const uint8_t* b, size_t n, Instrument& m) {
  if (n > 0) m.lofiFormula = b[0];  // range: fixInstrument
  if (n > 1) m.lofiSync = b[1];     // 0 in records from before the flag; range: fixInstrument
}

void packLfoByte(const Instrument& m, uint8_t* b) { memcpy(b, m.lfoByte, kLfoByteRecSize); }

void unpackLfoByte(const uint8_t* b, size_t n, Instrument& m) {
  memcpy(m.lfoByte, b, n < kLfoByteRecSize ? n : kLfoByteRecSize);  // range: fixInstrument
}

void packFmParams(const Instrument& m, uint8_t* b) { memcpy(b, m.fmParam, kFmParams); }

void unpackFmParams(const uint8_t* b, size_t n, Instrument& m) {
  const size_t count = n < kFmParamRecSize ? n : kFmParamRecSize;
  for (size_t i = 0; i < count; ++i) m.fmParam[i] = clampu(b[i], 0, 127);
}

void packFmEx(const Instrument& m, uint8_t* b) {
  memset(b, 0, kFmExRecSize);
  b[0] = m.fmSat;
  b[1] = m.chipStep ? 1 : 0;
  b[2] = m.synSatType;
}

void unpackFmEx(const uint8_t* b, size_t n, Instrument& m) {
  if (n > 0) m.fmSat = clampu(b[0], 0, 127);
  if (n > 1) m.chipStep = b[1] != 0;
  m.synSatType = n > 2 && b[2] < static_cast<int>(SynSatType::Count) ? b[2] : 0;
}

namespace {
// Find the closest current DEC or held TONE MOD value through the actual mapper.
// Runs only while loading; no tables duplicating the calibrated machine laws.
float migratedControlValue(const Instrument& m, int control, int value) {
  float raw[kFmParams];
  for (int i = 0; i < kFmParams; ++i) raw[i] = m.fmParam[i];
  raw[control] = value;
  FmParams p;
  fmMachine(m.machine, raw, 60.f, p);
  return control == 3 ? p.ampMs : p.op[4].level * p.op[4].sustain;
}
uint8_t closestFmControl(const Instrument& m, int control, float target) {
  int lo = 0, hi = 127;
  while (hi - lo > 1) {
    const int mid = (lo + hi) / 2;
    if (migratedControlValue(m, control, mid) < target) lo = mid;
    else hi = mid;
  }
  const float a = fabsf(migratedControlValue(m, control, lo) - target);
  const float b = fabsf(migratedControlValue(m, control, hi) - target);
  return static_cast<uint8_t>(a <= b ? lo : hi);
}
void migrateTone(Instrument& m) {
  int zone = m.macro[kMacShp] * 5 / 127;
  if (zone > 4) zone = 4;
  // Old SHAPE zones: paired1:1, feedback saw, paired2:1, stack, paired3.5:1.
  MT_CONTROL_DATA static const uint8_t ratio[] = {9, 9, 24, 24, 49};
  m.fmParam[1] = ratio[zone];
  m.fmParam[4] = zone == 1 || zone == 3 ? m.macro[kMacCol] : 0;
  const float scale = zone <= 1 ? 1.5f : 3.f;
  const float held = m.macro[kMacCol] * (scale / 127.f);
  // New feedback lives on the modulator, so MOD must remain active in the old saw zone.
  // A tiny base also lets MENV create the old zero-COLOR, nonzero-SWEEP pluck.
  const float pluck = m.macro[kMacSwp] * (scale / (6.f * 127.f));
  m.fmParam[5] = closestFmControl(m, 5, held > pluck ? held : pluck);
  m.fmParam[6] = m.macro[kMacSwp];
  // Old melodic voices were held by the instrument ADSR, not the DEC macro.
  m.fmParam[3] = 127;
}
void migrateChord(Instrument& m) {
  int chord = m.macro[kMacShp] * 12 / 127;
  if (chord > 11) chord = 11;
  // MAJ, MIN, SUS2, SUS4,7,m7,Maj7,dim,aug,5,oct,m9.
  // Current signatures are voiced below their reference note: compensate with TUNE,
  // leaving the instrument transpose and every sequence note intact.
  MT_CONTROL_DATA static const uint8_t type[] = {4,3,5,6,8,7,10,12,21,36,0,25};
  MT_CONTROL_DATA static const uint8_t tune[] = {76,76,76,76,76,76,76,73,72,71,64,76};
  m.fmParam[0] = tune[chord]; m.fmParam[4] = type[chord];
  m.fmParam[1] = m.macro[kMacCol];
  m.fmParam[5] = chord == 10 ? 0 : 64;
  m.fmParam[6] = m.macro[kMacSwp]; m.fmParam[3] = 127;
}
}  // namespace

bool migrateLegacyFm(Instrument& m) {
  if (m.type != InstrType::Fm) return false;
  const uint8_t old = m.machine;
  MT_CONTROL_DATA static const FmMachine machine[] = {
      FmMachine::BdModern, FmMachine::SdBasic, FmMachine::CyAlloy, FmMachine::PcCarbon,
      FmMachine::Tone, FmMachine::Chord, FmMachine::CpVintage, FmMachine::HhBasic};
  if (old >= sizeof(machine) / sizeof(machine[0])) {
    fmSetMachine(m, static_cast<uint8_t>(FmMachine::Tone));
    return false;
  }
  fmSetMachine(m, static_cast<uint8_t>(machine[old]));
  for (int k = 0; k < kFmMacros; ++k) m.fmParam[fmMacroParam(m.machine, k)] = m.macro[k];
  m.fmParam[7] = m.fmSat;
  if (old == 4) migrateTone(m);
  else if (old == 5) migrateChord(m);
  else {
    // Closest new base pitches to the old55/180/400/200Hz at C4.
    m.fmParam[0] = old == 0 ? 61 : old == 1 ? 58 : old == 2 ? 71 : old == 3 ? 59 : 64;
    if (old == 6) {
      // Old SWEEP was filter Q. Current SPCR near its middle can add repeats hundreds
      // of milliseconds apart: use the compact clap rather than inventing a new rhythm.
      m.fmParam[1] = 64;
    }
    if (old == 7) {  // TONE/TDEC replace the old hat's colour/tail controls; keep reset on.
      m.fmParam[1] = m.macro[kMacCol]; m.fmParam[2] = m.macro[kMacCon]; m.fmParam[6] = 127;
    }
    float ms = fmDecayMs(m.macro[kMacDec]);
    if (old == 1) {  // Old snare's CONTOUR could extend the noise beyond its base decay.
      const float length = .3f + 1.2f * m.macro[kMacCon] / 127.f;
      if (length > 1.f) ms *= length;
    }
    m.fmParam[3] = closestFmControl(m, 3, ms);
  }
  return true;
}

void fixInstrument(Instrument& m) {
  // SYNTH: unpackSyn already clamps every field; wavetable names are resolved by the firmware.
  int n = 0;
  if (m.type == InstrType::Fm) n = static_cast<int>(FmMachine::Count);
  else if (m.type == InstrType::Drum) n = static_cast<int>(DrumMachine::Count);
  else if (m.type == InstrType::String) n = static_cast<int>(StringModel::Count);
  else if (m.type == InstrType::Lofi) n = static_cast<int>(LofiMachine::Count);
  if (n && m.machine >= n) {
    if (m.type == InstrType::Fm) fmSetMachine(m, static_cast<uint8_t>(FmMachine::Tone));
    else m.machine = 0;
  }
  for (uint8_t& value : m.fmParam) if (value > 127) value = 127;
  if (m.type == InstrType::Drum && m.machine == static_cast<uint8_t>(DrumMachine::Unused22))
    m.machine = kDrum2First;  // was BDF: BDA
  // COMB needs the voice's delay line, which STRING takes (the synth would run it as Off).
  if (m.type == InstrType::String && m.fltMode == static_cast<uint8_t>(FltMode::Comb))
    m.fltMode = static_cast<uint8_t>(FltMode::Off);
  if (m.lofiFormula >= byteFormulaCount()) m.lofiFormula = 0;
  if (m.lofiSync > 1) m.lofiSync = 1;
  if (m.fmSat > 127) m.fmSat = 127;
  if (m.synSatType >= static_cast<uint8_t>(SynSatType::Count)) m.synSatType = 0;
  for (uint8_t* l : m.lfoByte) {
    if (l[0] >= byteFormulaCount()) l[0] = 0;
    if (l[1] > 127) l[1] = 127;
  }
}

}  // namespace mt
