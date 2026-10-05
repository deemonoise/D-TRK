#include "project_io.h"
#include <string.h>
#include "scale.h"

namespace mt {
namespace {

constexpr size_t kProjSize = 17 + 2 + 4 + kChainMax;
constexpr size_t kTrackSize = 9 + 6 + 1;
constexpr size_t kTrksSize = 1 + kTracks * kTrackSize;
constexpr size_t kPatHeader = 4;
constexpr size_t kInstSize = 48;  // 47 bytes of fields + 1 reserved
constexpr size_t kToutSize = 3;
constexpr size_t kAudiSize = 2;
constexpr size_t kFminSize = 16;  // 10 bytes of fields + reserved
static_assert(kFminSize <= kInstSize, "readRecords buffer");

uint32_t rd32(const uint8_t* b) {
  return b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24);
}
void wr16(uint8_t* b, uint16_t v) {
  b[0] = static_cast<uint8_t>(v);
  b[1] = static_cast<uint8_t>(v >> 8);
}
void wr32(uint8_t* b, uint32_t v) {
  wr16(b, static_cast<uint16_t>(v));
  wr16(b + 2, static_cast<uint16_t>(v >> 16));
}
uint16_t rd16(const uint8_t* b) { return static_cast<uint16_t>(b[0] | (b[1] << 8)); }
uint8_t clampu(int v, int lo, int hi) { return static_cast<uint8_t>(v < lo ? lo : (v > hi ? hi : v)); }
int8_t clamps(int8_t v, int lo, int hi) { return static_cast<int8_t>(v < lo ? lo : (v > hi ? hi : v)); }

void packInst(const Instrument& m, uint8_t* b) {
  memset(b, 0, kInstSize);
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
  memset(b, 0, kFminSize);
  b[0] = m.machine;
  memcpy(b + 1, m.macro, kFmMacros);
  b[6] = m.lfoWave;
  b[7] = m.lfoRate;
  b[8] = static_cast<uint8_t>(m.lfoDepth);
  b[9] = m.lfoDest;
}

void unpackFm(const uint8_t* b, Instrument& m) {
  m.machine = b[0] < static_cast<int>(FmMachine::Count) ? b[0] : 0;
  for (int k = 0; k < kFmMacros; ++k) m.macro[k] = clampu(b[1 + k], 0, 127);
  m.lfoWave = b[6] < static_cast<int>(LfoWave::Count) ? b[6] : 0;
  m.lfoRate = clampu(b[7], 0, 127);
  m.lfoDepth = clamps(static_cast<int8_t>(b[8]), -64, 63);
  m.lfoDest = b[9] < static_cast<int>(LfoDest::Count) ? b[9] : 0;
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

class CrcSink {
 public:
  explicit CrcSink(ByteSink& out) : out_(out) {}
  bool write(const void* d, size_t n) {
    crc_ = crc32(d, n, crc_);
    return out_.write(d, n);
  }
  bool chunk(const char id[4], uint32_t size) {
    uint8_t h[8];
    memcpy(h, id, 4);
    wr32(h + 4, size);
    return write(h, 8);
  }
  uint32_t crc() const { return crc_; }

 private:
  ByteSink& out_;
  uint32_t crc_ = 0;
};

class CrcSource {
 public:
  explicit CrcSource(ByteSource& in) : in_(in) {}
  bool read(void* d, size_t n) {
    if (!in_.read(d, n)) return false;
    crc_ = crc32(d, n, crc_);
    return true;
  }
  // Skipped bytes still count for the CRC, so they are read.
  bool skip(size_t n) {
    uint8_t buf[64];
    while (n > 0) {
      const size_t k = n < sizeof(buf) ? n : sizeof(buf);
      if (!read(buf, k)) return false;
      n -= k;
    }
    return true;
  }
  void add(const void* d, size_t n) { crc_ = crc32(d, n, crc_); }
  ByteSource& raw() { return in_; }
  uint32_t crc() const { return crc_; }

 private:
  ByteSource& in_;
  uint32_t crc_ = 0;
};

bool patternStored(const Pattern& p) {
  return p.length != kDefaultSteps || p.res != Resolution::Sixteenth || p.swing != 50 || !p.isEmpty();
}

uint8_t validNote(uint8_t n) { return (n < 128 || n == kNoteOff) ? n : kNoteEmpty; }

LoadErr readProj(CrcSource& in, uint32_t size, Project& p) {
  if (size < kProjSize) return LoadErr::BadValue;
  uint8_t b[kProjSize];
  if (!in.read(b, sizeof(b))) return LoadErr::Truncated;
  memcpy(p.name, b, 17);
  p.name[16] = 0;
  const int bpm = b[17] | (b[18] << 8);
  p.bpm = static_cast<uint16_t>(bpm < 20 ? 20 : (bpm > 300 ? 300 : bpm));
  p.scaleRoot = b[19] < 12 ? b[19] : 0;
  p.scaleType = b[20] < static_cast<int>(ScaleType::Count) ? b[20] : 0;
  p.songMode = b[21] != 0;
  p.chainLen = clampu(b[22], 0, kChainMax);
  for (int i = 0; i < kChainMax; ++i) p.chain[i] = clampu(b[23 + i], 0, kPatterns - 1);
  return in.skip(size - kProjSize) ? LoadErr::Ok : LoadErr::Truncated;
}

LoadErr readTrks(CrcSource& in, uint32_t size, Project& p) {
  uint8_t count;
  if (size < 1) return LoadErr::BadValue;
  if (!in.read(&count, 1)) return LoadErr::Truncated;
  if (size < 1 + static_cast<uint32_t>(count) * kTrackSize) return LoadErr::BadValue;
  for (int t = 0; t < count && t < kTracks; ++t) {
    uint8_t b[kTrackSize];
    if (!in.read(b, sizeof(b))) return LoadErr::Truncated;
    TrackCfg& c = p.tracks[t];
    memcpy(c.name, b, 9);
    c.name[8] = 0;
    c.channel = clampu(b[9], 0, 15);
    c.defVel = clampu(b[10], 1, 127);
    c.defGate = clampu(b[11], 1, 200);
    c.ccA = clampu(b[12], 0, 127);
    c.ccB = clampu(b[13], 0, 127);
    c.program = b[14] < 128 ? b[14] : kNoProgram;
    c.mute = b[15] & 1;
    c.solo = b[15] & 2;
  }
  const uint32_t used = 1 + static_cast<uint32_t>(count < kTracks ? count : kTracks) * kTrackSize;
  return in.skip(size - used) ? LoadErr::Ok : LoadErr::Truncated;
}

// Chunk of count records of recSize bytes; records past max are skipped.
template <class F>
LoadErr readRecords(CrcSource& in, uint32_t size, size_t recSize, int max, F&& f) {
  uint8_t count;
  if (size < 1) return LoadErr::BadValue;
  if (!in.read(&count, 1)) return LoadErr::Truncated;
  if (size < 1 + static_cast<uint32_t>(count) * recSize) return LoadErr::BadValue;
  const int n = count < max ? count : max;
  for (int i = 0; i < n; ++i) {
    uint8_t b[kInstSize];  // the largest record
    if (!in.read(b, recSize)) return LoadErr::Truncated;
    f(i, b);
  }
  return in.skip(size - 1 - static_cast<uint32_t>(n) * recSize) ? LoadErr::Ok : LoadErr::Truncated;
}

LoadErr readTout(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kToutSize, kTracks, [&](int t, const uint8_t* b) {
    TrackCfg& c = p.tracks[t];
    c.out = b[0] < static_cast<int>(TrackOut::Count) ? static_cast<TrackOut>(b[0]) : TrackOut::Midi;
    c.instr = clampu(b[1], 0, kInstruments - 1);
    c.vol = clampu(b[2], 0, 127);
  });
}

LoadErr readInst(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kInstSize, kInstruments,
                     [&](int i, const uint8_t* b) { unpackInst(b, p.instruments[i]); });
}

LoadErr readFmin(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kFminSize, kInstruments,
                     [&](int i, const uint8_t* b) { unpackFm(b, p.instruments[i]); });
}

LoadErr readAudi(CrcSource& in, uint32_t size, Project& p) {
  if (size < kAudiSize) return LoadErr::BadValue;
  uint8_t b[kAudiSize];
  if (!in.read(b, sizeof(b))) return LoadErr::Truncated;
  p.masterVol = clampu(b[0], 0, kMasterVolMax);
  p.preview = b[1] != 0;
  return in.skip(size - kAudiSize) ? LoadErr::Ok : LoadErr::Truncated;
}

LoadErr readPatn(CrcSource& in, uint32_t size, Project& p) {
  uint8_t h[kPatHeader];
  if (size < kPatHeader) return LoadErr::BadValue;
  if (!in.read(h, sizeof(h))) return LoadErr::Truncated;
  const int len = h[1];
  if (size != kPatHeader + static_cast<uint32_t>(kTracks) * len * sizeof(Step)) return LoadErr::BadValue;
  if (h[0] >= kPatterns) return in.skip(size - kPatHeader) ? LoadErr::Ok : LoadErr::Truncated;
  Pattern& pt = p.patterns[h[0]];
  pt.clear();
  pt.length = clampu(len, kMinSteps, kMaxSteps);
  pt.res = h[2] < static_cast<int>(Resolution::Count) ? static_cast<Resolution>(h[2]) : Resolution::Sixteenth;
  pt.swing = h[3] < 50 || h[3] > 75 ? 50 : h[3];
  for (int t = 0; t < kTracks; ++t)
    for (int s = 0; s < len; ++s) {
      uint8_t b[6];
      if (!in.read(b, 6)) return LoadErr::Truncated;
      if (s >= kMaxSteps) continue;
      Step& st = pt.steps[t][s];
      st.note = validNote(b[0]);
      st.vel = clampu(b[1], 0, 127);
      for (int k = 0; k < 2; ++k) {
        const uint8_t cmd = b[2 + k * 2];
        st.fx[k].cmd = cmd < static_cast<int>(Fx::Count) ? static_cast<Fx>(cmd) : Fx::None;
        st.fx[k].val = st.fx[k].cmd == Fx::None ? 0 : b[3 + k * 2];
      }
    }
  return LoadErr::Ok;
}

}  // namespace

uint32_t crc32(const void* d, size_t n, uint32_t prev) {
  static const uint32_t kNib[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                    0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                    0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  const uint8_t* b = static_cast<const uint8_t*>(d);
  uint32_t c = ~prev;
  for (size_t i = 0; i < n; ++i) {
    c ^= b[i];
    c = (c >> 4) ^ kNib[c & 15];
    c = (c >> 4) ^ kNib[c & 15];
  }
  return ~c;
}

bool saveProject(const Project& p, ByteSink& out) {
  CrcSink o(out);
  uint8_t h[8] = {'M', 'T', 'R', 'K', 0, 0, 0, 0};
  wr16(h + 4, kProjectVersion);
  if (!o.write(h, 8)) return false;

  uint8_t pr[kProjSize];
  memcpy(pr, p.name, 17);
  pr[16] = 0;
  wr16(pr + 17, p.bpm);
  pr[19] = p.scaleRoot;
  pr[20] = p.scaleType;
  pr[21] = p.songMode ? 1 : 0;
  pr[22] = p.chainLen;
  memcpy(pr + 23, p.chain, kChainMax);
  if (!o.chunk("PROJ", kProjSize) || !o.write(pr, sizeof(pr))) return false;

  uint8_t count = kTracks;
  if (!o.chunk("TRKS", kTrksSize) || !o.write(&count, 1)) return false;
  for (const TrackCfg& c : p.tracks) {
    uint8_t b[kTrackSize];
    memcpy(b, c.name, 9);
    b[8] = 0;
    b[9] = c.channel;
    b[10] = c.defVel;
    b[11] = c.defGate;
    b[12] = c.ccA;
    b[13] = c.ccB;
    b[14] = c.program;
    b[15] = (c.mute ? 1 : 0) | (c.solo ? 2 : 0);
    if (!o.write(b, sizeof(b))) return false;
  }

  if (!o.chunk("INST", 1 + kInstruments * kInstSize)) return false;
  count = kInstruments;
  if (!o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments) {
    uint8_t b[kInstSize];
    packInst(m, b);
    if (!o.write(b, sizeof(b))) return false;
  }
  if (!o.chunk("FMIN", 1 + kInstruments * kFminSize) || !o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments) {
    uint8_t b[kFminSize];
    packFm(m, b);
    if (!o.write(b, sizeof(b))) return false;
  }

  count = kTracks;
  if (!o.chunk("TOUT", 1 + kTracks * kToutSize) || !o.write(&count, 1)) return false;
  for (const TrackCfg& c : p.tracks) {
    const uint8_t b[kToutSize] = {static_cast<uint8_t>(c.out), c.instr, c.vol};
    if (!o.write(b, sizeof(b))) return false;
  }

  const uint8_t au[kAudiSize] = {p.masterVol, static_cast<uint8_t>(p.preview ? 1 : 0)};
  if (!o.chunk("AUDI", kAudiSize) || !o.write(au, sizeof(au))) return false;

  for (int i = 0; i < kPatterns; ++i) {
    const Pattern& pt = p.patterns[i];
    if (!patternStored(pt)) continue;
    const uint8_t len = clampu(pt.length, kMinSteps, kMaxSteps);
    const uint8_t ph[kPatHeader] = {static_cast<uint8_t>(i), len, static_cast<uint8_t>(pt.res), pt.swing};
    if (!o.chunk("PATN", kPatHeader + kTracks * len * sizeof(Step)) || !o.write(ph, sizeof(ph))) return false;
    for (int t = 0; t < kTracks; ++t)
      for (int s = 0; s < len; ++s) {
        const Step& st = pt.steps[t][s];
        const uint8_t b[6] = {st.note, st.vel, static_cast<uint8_t>(st.fx[0].cmd), st.fx[0].val,
                              static_cast<uint8_t>(st.fx[1].cmd), st.fx[1].val};
        if (!o.write(b, 6)) return false;
      }
  }

  uint8_t c[12] = {'C', 'R', 'C', ' '};
  wr32(c + 4, 4);
  wr32(c + 8, o.crc());
  return out.write(c, sizeof(c));
}

LoadErr loadProject(ByteSource& src, Project& out) {
  out.reset();
  CrcSource in(src);
  uint8_t h[8];
  if (!in.read(h, 8)) return LoadErr::Truncated;
  if (memcmp(h, "MTRK", 4) != 0) return LoadErr::BadMagic;
  if ((h[4] | (h[5] << 8)) > kProjectVersion) return LoadErr::BadVersion;

  for (;;) {
    uint8_t ch[8];
    if (!in.raw().read(ch, 8)) return LoadErr::Truncated;
    const uint32_t size = rd32(ch + 4);
    if (memcmp(ch, "CRC ", 4) == 0) {
      uint8_t v[4];
      if (size != 4) return LoadErr::BadValue;
      if (!in.raw().read(v, 4)) return LoadErr::Truncated;
      return rd32(v) == in.crc() ? LoadErr::Ok : LoadErr::BadCrc;
    }
    in.add(ch, 8);
    LoadErr e;
    if (memcmp(ch, "PROJ", 4) == 0) e = readProj(in, size, out);
    else if (memcmp(ch, "TRKS", 4) == 0) e = readTrks(in, size, out);
    else if (memcmp(ch, "PATN", 4) == 0) e = readPatn(in, size, out);
    else if (memcmp(ch, "INST", 4) == 0) e = readInst(in, size, out);
    else if (memcmp(ch, "FMIN", 4) == 0) e = readFmin(in, size, out);
    else if (memcmp(ch, "TOUT", 4) == 0) e = readTout(in, size, out);
    else if (memcmp(ch, "AUDI", 4) == 0) e = readAudi(in, size, out);
    else e = in.skip(size) ? LoadErr::Ok : LoadErr::Truncated;
    if (e != LoadErr::Ok) return e;
  }
}

}  // namespace mt
