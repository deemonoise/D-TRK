#include "project_io.h"
#include <string.h>
#include <strings.h>
#include "file_rules.h"
#include "groove.h"
#include "inst_codec.h"
#include "sample_set.h"
#include "scale.h"

namespace mt {
namespace {

constexpr size_t kProjSize = 17 + 2 + 4 + kChainMax;
constexpr size_t kTrackSize = 9 + 6 + 1;
constexpr size_t kTrksSize = 1 + kTracks * kTrackSize;
constexpr size_t kPatHeader = 4;
constexpr uint32_t kOldStepBytes = 6;  // PATN before 6 fx slots: note, vel, 2 x (cmd, val)
constexpr int kOldTracks = 8;          // PATN of the 8-track firmware
constexpr size_t kToutSize = 3;
// masterVol, preview, dlyTime, dlyFb, dlyTone, dlyLevel, rvbSize, rvbDamp, rvbLevel, compAmt, compRel,
// scTrack, scDepth, djFilter + 64.
constexpr size_t kAudiSize = 14;
constexpr size_t kAudiFxSize = 13;  // older files: up to scDepth
constexpr size_t kAudiDlySize = 6;  // older files: up to dlyLevel
constexpr size_t kAudiMinSize = 2;  // older files: masterVol, preview
constexpr size_t kSmplSize = 24;  // name[16] u32 crc u32 frames
constexpr size_t kKitLaneSize = sizeof(KitLane);         // 22: name[16], pad, instr, vol, pitch, decay, note
constexpr size_t kKitRecSize = kKitLanes * kKitLaneSize;  // 176: one KIT instrument
constexpr size_t kMaxRecA = kSliceRecSize > kInstRecSize ? kSliceRecSize : kInstRecSize;
constexpr size_t kMaxRec = kKitRecSize > kMaxRecA ? kKitRecSize : kMaxRecA;
static_assert(kInstRecSize <= kMaxRec, "readRecords buffer");
static_assert(kKitRecSize <= kMaxRec, "readRecords buffer");
static_assert(kFmRecSize <= kMaxRec, "readRecords buffer");
static_assert(kFltRecSize <= kMaxRec, "readRecords buffer");
static_assert(kSliceRecSize <= kMaxRec, "readRecords buffer");
static_assert(kSmplSize <= kMaxRec, "readRecords buffer");
constexpr size_t kWtblSize = 20;  // name[16] u32 crc
constexpr size_t kChn2Rec = 4;    // pattern, transpose, repeat, scene
static_assert(kSynRecSize <= kMaxRec, "readRecords buffer");
static_assert(kWtblSize <= kMaxRec, "readRecords buffer");

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
uint8_t clampu(int v, int lo, int hi) { return static_cast<uint8_t>(v < lo ? lo : (v > hi ? hi : v)); }

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

bool trackLenSet(const Pattern& p) {
  for (uint8_t n : p.trackLen)
    if (n) return true;
  return false;
}

bool patternStored(const Pattern& p) {
  return p.length != kDefaultSteps || p.res != Resolution::Sixteenth || p.swing != 50 || !p.isEmpty() ||
         trackLenSet(p);
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
    uint8_t b[kMaxRec];
    if (!in.read(b, recSize)) return LoadErr::Truncated;
    f(i, b);
  }
  return in.skip(size - 1 - static_cast<uint32_t>(n) * recSize) ? LoadErr::Ok : LoadErr::Truncated;
}

LoadErr readTout(CrcSource& in, uint32_t size, Project& p) {
  // TOUT present: tracks it lacks are new (kOldTracks files) and get the fresh-project default,
  // not the all-MIDI preset of loadProject() for files older than TOUT.
  for (TrackCfg& c : p.tracks) c.out = TrackOut::Int;
  return readRecords(in, size, kToutSize, kTracks, [&](int t, const uint8_t* b) {
    TrackCfg& c = p.tracks[t];
    c.out = b[0] < static_cast<int>(TrackOut::Count) ? static_cast<TrackOut>(b[0]) : TrackOut::Midi;
    c.instr = clampu(b[1], 0, kInstruments - 1);
    c.vol = clampu(b[2], 0, 127);
  });
}

LoadErr readInst(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kInstRecSize, kInstruments,
                     [&](int i, const uint8_t* b) { unpackInst(b, p.instruments[i]); });
}

LoadErr readFmin(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kFmRecSize, kInstruments,
                     [&](int i, const uint8_t* b) { unpackFm(b, p.instruments[i]); });
}

LoadErr readFltr(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kFltRecSize, kInstruments,
                     [&](int i, const uint8_t* b) { unpackFlt(b, p.instruments[i]); });
}

LoadErr readSlce(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kSliceRecSize, kInstruments,
                     [&](int i, const uint8_t* b) { unpackSlices(b, p.instruments[i]); });
}

LoadErr readSyni(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kSynRecSize, kInstruments,
                     [&](int i, const uint8_t* b) { unpackSyn(b, p.instruments[i]); });
}

LoadErr readKits(CrcSource& in, uint32_t size, Project& p) {
  return readRecords(in, size, kKitRecSize, kInstruments, [&](int i, const uint8_t* b) {
    for (int k = 0; k < kKitLanes; ++k, b += kKitLaneSize) {
      KitLane& l = p.instruments[i].kit[k];
      memcpy(l.sample, b, kSampleNameMax);
      l.sample[kSampleNameMax] = 0;
      l.instr = b[17] < kInstruments ? b[17] : kNoInstr;
      l.vol = clampu(b[18], 0, 127);
      const int pt = static_cast<int8_t>(b[19]);
      l.pitch = static_cast<int8_t>(pt < -24 ? -24 : (pt > 24 ? 24 : pt));
      l.decay = b[20];
      l.note = b[21] < 128 ? b[21] : static_cast<uint8_t>(60 + k);
    }
  });
}

LoadErr readAudi(CrcSource& in, uint32_t size, Project& p) {
  if (size < kAudiMinSize) return LoadErr::BadValue;
  uint8_t b[kAudiSize];
  const uint32_t n = size < kAudiSize ? size : kAudiSize;
  if (!in.read(b, n)) return LoadErr::Truncated;
  p.masterVol = clampu(b[0], 0, kMasterVolMax);
  p.preview = b[1] != 0;
  if (n >= kAudiDlySize) {
    p.dlyTime = clampu(b[2], 1, kDlyTimeMax);
    p.dlyFb = clampu(b[3], 0, 127);
    p.dlyTone = clampu(b[4], 0, 127);
    p.dlyLevel = clampu(b[5], 0, 127);
  }
  if (n >= kAudiFxSize) {
    p.rvbSize = clampu(b[6], 0, 127);
    p.rvbDamp = clampu(b[7], 0, 127);
    p.rvbLevel = clampu(b[8], 0, 127);
    p.compAmt = clampu(b[9], 0, 127);
    p.compRel = clampu(b[10], 0, 127);
    p.scTrack = b[11] <= kTracks ? b[11] : 0;
    p.scDepth = clampu(b[12], 0, 127);
  }
  if (n >= kAudiSize) p.djFilter = static_cast<int8_t>(clampu(b[13], 0, 127) - 64);
  return in.skip(size - n) ? LoadErr::Ok : LoadErr::Truncated;
}

// Empty and repeated (ignoring case) names are skipped. A repeated chunk replaces the list.
LoadErr readSmpl(CrcSource& in, uint32_t size, Project& p) {
  for (ProjSample& s : p.samples) s = ProjSample{};
  p.sampleCount = 0;
  p.hasSampleList = true;
  return readRecords(in, size, kSmplSize, kProjSamples, [&](int, const uint8_t* b) {
    char nm[kSampleNameMax + 1];
    memcpy(nm, b, kSampleNameMax);
    nm[kSampleNameMax] = 0;
    if (p.sampleCount >= kProjSamples || !projectBaseValid(nm) || projSampleFind(p, nm) >= 0) return;
    ProjSample& s = p.samples[p.sampleCount++];
    memcpy(s.name, nm, sizeof(nm));
    s.crc = rd32(b + 16);
    s.frames = rd32(b + 20);
  });
}

// Like SMPL: empty, invalid and repeated (ignoring case) names are skipped; a repeated chunk
// replaces the list.
LoadErr readWtbl(CrcSource& in, uint32_t size, Project& p) {
  for (ProjWavetable& w : p.wavetables) w = ProjWavetable{};
  p.wavetableCount = 0;
  return readRecords(in, size, kWtblSize, kProjWavetables, [&](int, const uint8_t* b) {
    char nm[kSampleNameMax + 1];
    memcpy(nm, b, kSampleNameMax);
    nm[kSampleNameMax] = 0;
    if (p.wavetableCount >= kProjWavetables || !projectBaseValid(nm) || projWtFind(p, nm) >= 0) return;
    ProjWavetable& w = p.wavetables[p.wavetableCount++];
    memcpy(w.name, nm, sizeof(nm));
    w.crc = rd32(b + 16);
  });
}

// CHN2: count, then per item pattern, transpose (int8), repeat, scene. Replaces PROJ's chain (still
// written there for old firmware); files without it keep tr 0 / rep 1 / no scene.
LoadErr readChn2(CrcSource& in, uint32_t size, Project& p) {
  int n = 0;
  const LoadErr e = readRecords(in, size, kChn2Rec, kChainMax, [&](int i, const uint8_t* b) {
    p.chain[i] = clampu(b[0], 0, kPatterns - 1);
    const int tr = static_cast<int8_t>(b[1]);
    p.chainTr[i] = static_cast<int8_t>(tr < -kChainTrMax ? -kChainTrMax : (tr > kChainTrMax ? kChainTrMax : tr));
    p.chainRep[i] = clampu(b[2], 1, kChainRepMax);
    p.chainScene[i] = b[3] <= kScenes ? b[3] : 0;  // no such scene: none
    n = i + 1;
  });
  if (e == LoadErr::Ok) p.chainLen = static_cast<uint8_t>(n);
  return e;
}

// GROV: the patterns' groove (kPatterns bytes), then the tracks' humanize (kTracks bytes). Files
// without it: groove OFF, no humanize.
// PRFM: the PERF effect of each track button (PerfFx); files without it: the default 1..8.
LoadErr readPrfm(CrcSource& in, uint32_t size, Project& p) {
  uint8_t b[kPerfButtons];
  if (size < sizeof(b)) return LoadErr::BadValue;
  if (!in.read(b, sizeof(b))) return LoadErr::Truncated;
  for (int i = 0; i < kPerfButtons; ++i) p.perfMap[i] = b[i] < static_cast<uint8_t>(PerfFx::Count) ? b[i] : 0;
  return in.skip(size - sizeof(b)) ? LoadErr::Ok : LoadErr::Truncated;
}

constexpr size_t kGrovSize = kPatterns + kTracks;
LoadErr readGrov(CrcSource& in, uint32_t size, Project& p) {
  uint8_t b[kGrovSize];
  if (size < kGrovSize) return LoadErr::BadValue;
  if (!in.read(b, kGrovSize)) return LoadErr::Truncated;
  for (int i = 0; i < kPatterns; ++i) p.patterns[i].groove = b[i] < grooveCount() ? b[i] : 0;
  for (int t = 0; t < kTracks; ++t) p.tracks[t].humanize = clampu(b[kPatterns + t], 0, 100);
  return in.skip(size - kGrovSize) ? LoadErr::Ok : LoadErr::Truncated;
}

// TLEN: pattern index + kTracks track lengths (0 = the pattern length), after its PATN.
LoadErr readTlen(CrcSource& in, uint32_t size, Project& p) {
  uint8_t b[1 + kTracks];
  if (size != sizeof(b)) return LoadErr::BadValue;
  if (!in.read(b, sizeof(b))) return LoadErr::Truncated;
  if (b[0] >= kPatterns) return LoadErr::Ok;
  Pattern& pt = p.patterns[b[0]];
  for (int t = 0; t < kTracks; ++t) pt.trackLen[t] = clampu(b[1 + t], 0, pt.length);
  return LoadErr::Ok;
}

// SCNS: kScenes mute masks, LE uint16.
LoadErr readScns(CrcSource& in, uint32_t size, Project& p) {
  uint8_t b[2 * kScenes];
  if (size != sizeof(b)) return LoadErr::BadValue;
  if (!in.read(b, sizeof(b))) return LoadErr::Truncated;
  for (int i = 0; i < kScenes; ++i) p.scenes[i] = static_cast<uint16_t>(b[2 * i] | (b[2 * i + 1] << 8));
  return LoadErr::Ok;
}

LoadErr readPatn(CrcSource& in, uint32_t size, Project& p) {
  uint8_t h[kPatHeader];
  if (size < kPatHeader) return LoadErr::BadValue;
  if (!in.read(h, sizeof(h))) return LoadErr::Truncated;
  const int len = h[1];
  // Track count and step size from the body size: kTracks or kOldTracks (files of the 8-track
  // firmware) x 14-byte (6 slots) or 6-byte (2 slots, older) steps. The four products differ.
  const uint32_t body = size - kPatHeader;
  int tracks = 0;
  uint32_t stepBytes = 0;
  const auto match = [&](int tr, uint32_t sb) {
    if (stepBytes || !len || body != static_cast<uint32_t>(tr) * len * sb) return;
    tracks = tr;
    stepBytes = sb;
  };
  match(kTracks, sizeof(Step));
  match(kTracks, kOldStepBytes);
  match(kOldTracks, sizeof(Step));
  match(kOldTracks, kOldStepBytes);
  if (!stepBytes && body != 0) return LoadErr::BadValue;
  if (h[0] >= kPatterns) return in.skip(body) ? LoadErr::Ok : LoadErr::Truncated;
  Pattern& pt = p.patterns[h[0]];
  pt.clear();
  pt.length = clampu(len, kMinSteps, kMaxSteps);
  pt.res = h[2] < static_cast<int>(Resolution::Count) ? static_cast<Resolution>(h[2]) : Resolution::Sixteenth;
  pt.swing = h[3] < 50 || h[3] > 75 ? 50 : h[3];
  const int slots = (static_cast<int>(stepBytes) - 2) / 2;
  for (int t = 0; t < tracks; ++t)  // clear() above emptied the tracks a kOldTracks file lacks
    for (int s = 0; s < len; ++s) {
      uint8_t b[sizeof(Step)];
      if (!in.read(b, stepBytes)) return LoadErr::Truncated;
      if (s >= kMaxSteps) continue;
      Step& st = pt.steps[t][s];
      st.note = validNote(b[0]);
      st.vel = b[1];  // 0..127, or a lane mask on a drum track: masked in loadProject
      for (int k = 0; k < slots; ++k) {
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

  uint8_t chainN = p.chainLen > kChainMax ? kChainMax : p.chainLen;
  if (!o.chunk("CHN2", 1 + chainN * kChn2Rec) || !o.write(&chainN, 1)) return false;
  for (int i = 0; i < chainN; ++i) {
    const uint8_t b[kChn2Rec] = {p.chain[i], static_cast<uint8_t>(p.chainTr[i]), p.chainRep[i], p.chainScene[i]};
    if (!o.write(b, sizeof(b))) return false;
  }

  uint8_t sc[2 * kScenes];
  for (int i = 0; i < kScenes; ++i) wr16(sc + 2 * i, p.scenes[i]);
  if (!o.chunk("SCNS", sizeof(sc)) || !o.write(sc, sizeof(sc))) return false;

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

  if (!o.chunk("INST", 1 + kInstruments * kInstRecSize)) return false;
  count = kInstruments;
  if (!o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments) {
    uint8_t b[kInstRecSize];
    packInst(m, b);
    if (!o.write(b, sizeof(b))) return false;
  }
  if (!o.chunk("FMIN", 1 + kInstruments * kFmRecSize) || !o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments) {
    uint8_t b[kFmRecSize];
    packFm(m, b);
    if (!o.write(b, sizeof(b))) return false;
  }
  if (!o.chunk("FLTR", 1 + kInstruments * kFltRecSize) || !o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments) {
    uint8_t b[kFltRecSize];
    packFlt(m, b);
    if (!o.write(b, sizeof(b))) return false;
  }
  if (!o.chunk("SLCE", 1 + kInstruments * kSliceRecSize) || !o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments) {
    uint8_t b[kSliceRecSize];
    packSlices(m, b);
    if (!o.write(b, sizeof(b))) return false;
  }

  if (!o.chunk("SYNI", 1 + kInstruments * kSynRecSize) || !o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments) {
    uint8_t b[kSynRecSize];
    packSyn(m, b);
    if (!o.write(b, sizeof(b))) return false;
  }

  if (!o.chunk("KITS", 1 + kInstruments * kKitRecSize) || !o.write(&count, 1)) return false;
  for (const Instrument& m : p.instruments)
    for (const KitLane& l : m.kit) {
      uint8_t b[kKitLaneSize] = {0};
      memcpy(b, l.sample, strnlen(l.sample, kSampleNameMax));
      b[17] = l.instr;
      b[18] = l.vol;
      b[19] = static_cast<uint8_t>(l.pitch);
      b[20] = l.decay;
      b[21] = l.note;
      if (!o.write(b, sizeof(b))) return false;
    }

  count = kTracks;
  if (!o.chunk("TOUT", 1 + kTracks * kToutSize) || !o.write(&count, 1)) return false;
  for (const TrackCfg& c : p.tracks) {
    const uint8_t b[kToutSize] = {static_cast<uint8_t>(c.out), c.instr, c.vol};
    if (!o.write(b, sizeof(b))) return false;
  }

  const uint8_t au[kAudiSize] = {p.masterVol, static_cast<uint8_t>(p.preview ? 1 : 0),
                                 p.dlyTime,   p.dlyFb,
                                 p.dlyTone,   p.dlyLevel,
                                 p.rvbSize,   p.rvbDamp,
                                 p.rvbLevel,  p.compAmt,
                                 p.compRel,   p.scTrack,
                                 p.scDepth,   static_cast<uint8_t>(p.djFilter + 64)};
  if (!o.chunk("AUDI", kAudiSize) || !o.write(au, sizeof(au))) return false;

  // Always written, empty or not.
  count = p.sampleCount > kProjSamples ? kProjSamples : p.sampleCount;
  if (!o.chunk("SMPL", 1 + count * kSmplSize) || !o.write(&count, 1)) return false;
  for (int i = 0; i < count; ++i) {
    uint8_t b[kSmplSize] = {0};
    memcpy(b, p.samples[i].name, strnlen(p.samples[i].name, kSampleNameMax));
    wr32(b + 16, p.samples[i].crc);
    wr32(b + 20, p.samples[i].frames);
    if (!o.write(b, sizeof(b))) return false;
  }

  count = p.wavetableCount > kProjWavetables ? kProjWavetables : p.wavetableCount;
  if (!o.chunk("WTBL", 1 + count * kWtblSize) || !o.write(&count, 1)) return false;
  for (int i = 0; i < count; ++i) {
    uint8_t b[kWtblSize] = {0};
    memcpy(b, p.wavetables[i].name, strnlen(p.wavetables[i].name, kSampleNameMax));
    wr32(b + 16, p.wavetables[i].crc);
    if (!o.write(b, sizeof(b))) return false;
  }

  for (int i = 0; i < kPatterns; ++i) {
    const Pattern& pt = p.patterns[i];
    if (!patternStored(pt)) continue;
    const uint8_t len = clampu(pt.length, kMinSteps, kMaxSteps);
    const uint8_t ph[kPatHeader] = {static_cast<uint8_t>(i), len, static_cast<uint8_t>(pt.res), pt.swing};
    if (!o.chunk("PATN", kPatHeader + kTracks * len * sizeof(Step)) || !o.write(ph, sizeof(ph))) return false;
    for (int t = 0; t < kTracks; ++t)
      for (int s = 0; s < len; ++s) {
        const Step& st = pt.steps[t][s];
        uint8_t b[sizeof(Step)] = {st.note, st.vel};
        for (int k = 0; k < kFxSlots; ++k) {
          b[2 + k * 2] = static_cast<uint8_t>(st.fx[k].cmd);
          b[3 + k * 2] = st.fx[k].val;
        }
        if (!o.write(b, sizeof(b))) return false;
      }
    if (trackLenSet(pt)) {
      uint8_t tl[1 + kTracks] = {static_cast<uint8_t>(i)};
      memcpy(tl + 1, pt.trackLen, kTracks);
      if (!o.chunk("TLEN", sizeof(tl)) || !o.write(tl, sizeof(tl))) return false;
    }
  }
  {
    uint8_t g[kGrovSize];
    for (int i = 0; i < kPatterns; ++i) g[i] = p.patterns[i].groove;
    for (int t = 0; t < kTracks; ++t) g[kPatterns + t] = p.tracks[t].humanize;
    if (!o.chunk("GROV", sizeof(g)) || !o.write(g, sizeof(g))) return false;
  }
  if (!o.chunk("PRFM", kPerfButtons) || !o.write(p.perfMap, kPerfButtons)) return false;

  uint8_t c[12] = {'C', 'R', 'C', ' '};
  wr32(c + 4, 4);
  wr32(c + 8, o.crc());
  return out.write(c, sizeof(c));
}

bool ProjectFileNames::has(const char* name, bool wt) const {
  const int n = wt ? wavetables : samples;
  for (int i = 0; i < n; ++i)
    if (strcasecmp(wt ? wavetable[i] : sample[i], name) == 0) return true;
  return false;
}

LoadErr readProjectFileNames(ByteSource& src, ProjectFileNames& out) {
  out.samples = out.wavetables = 0;
  CrcSource in(src);
  uint8_t h[8];
  if (!in.read(h, 8)) return LoadErr::Truncated;
  if (memcmp(h, "MTRK", 4) != 0) return LoadErr::BadMagic;
  for (;;) {
    uint8_t ch[8];
    if (!in.raw().read(ch, 8)) return LoadErr::Truncated;
    const uint32_t size = rd32(ch + 4);
    if (memcmp(ch, "CRC ", 4) == 0) {
      uint8_t v[4];
      if (size != 4 || !in.raw().read(v, 4)) return LoadErr::Truncated;
      return rd32(v) == in.crc() ? LoadErr::Ok : LoadErr::BadCrc;
    }
    in.add(ch, 8);
    LoadErr e;
    const bool smpl = memcmp(ch, "SMPL", 4) == 0, wtbl = memcmp(ch, "WTBL", 4) == 0;
    if (smpl || wtbl) {
      int& n = smpl ? out.samples : out.wavetables;
      n = 0;  // a repeated chunk replaces the list, as in loadProject
      const int max = smpl ? kProjSamples : kProjWavetables;
      e = readRecords(in, size, smpl ? kSmplSize : kWtblSize, max, [&](int, const uint8_t* b) {
        char* nm = smpl ? out.sample[n] : out.wavetable[n];
        memcpy(nm, b, kSampleNameMax);
        nm[kSampleNameMax] = 0;
        if (n < max && projectBaseValid(nm)) ++n;
      });
    } else {
      e = in.skip(size) ? LoadErr::Ok : LoadErr::Truncated;
    }
    if (e != LoadErr::Ok) return e;
  }
}

LoadErr loadProject(ByteSource& src, Project& out) {
  out.reset();
  // Files older than TOUT were MIDI-only.
  for (TrackCfg& c : out.tracks) c.out = TrackOut::Midi;
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
      if (rd32(v) != in.crc()) return LoadErr::BadCrc;
      for (Instrument& m : out.instruments) fixInstrument(m);
      for (Pattern& pt : out.patterns)  // a PATN after its TLEN may have shortened the pattern
        for (uint8_t& n : pt.trackLen)
          if (n > pt.length) n = pt.length;
      // Drum tracks are known only now: their steps keep vel as a lane mask, the others' are 0..127.
      for (int t = 0; t < kTracks; ++t) {
        if (out.trackIsDrum(t)) continue;
        for (Pattern& pt : out.patterns)
          for (Step& st : pt.steps[t]) st.vel &= 0x7F;
      }
      return LoadErr::Ok;
    }
    in.add(ch, 8);
    LoadErr e;
    if (memcmp(ch, "PROJ", 4) == 0) e = readProj(in, size, out);
    else if (memcmp(ch, "TRKS", 4) == 0) e = readTrks(in, size, out);
    else if (memcmp(ch, "PATN", 4) == 0) e = readPatn(in, size, out);
    else if (memcmp(ch, "INST", 4) == 0) e = readInst(in, size, out);
    else if (memcmp(ch, "FMIN", 4) == 0) e = readFmin(in, size, out);
    else if (memcmp(ch, "FLTR", 4) == 0) e = readFltr(in, size, out);
    else if (memcmp(ch, "SLCE", 4) == 0) e = readSlce(in, size, out);
    else if (memcmp(ch, "TOUT", 4) == 0) e = readTout(in, size, out);
    else if (memcmp(ch, "AUDI", 4) == 0) e = readAudi(in, size, out);
    else if (memcmp(ch, "SMPL", 4) == 0) e = readSmpl(in, size, out);
    else if (memcmp(ch, "SYNI", 4) == 0) e = readSyni(in, size, out);
    else if (memcmp(ch, "KITS", 4) == 0) e = readKits(in, size, out);
    else if (memcmp(ch, "WTBL", 4) == 0) e = readWtbl(in, size, out);
    else if (memcmp(ch, "CHN2", 4) == 0) e = readChn2(in, size, out);
    else if (memcmp(ch, "TLEN", 4) == 0) e = readTlen(in, size, out);
    else if (memcmp(ch, "GROV", 4) == 0) e = readGrov(in, size, out);
    else if (memcmp(ch, "PRFM", 4) == 0) e = readPrfm(in, size, out);
    else if (memcmp(ch, "SCNS", 4) == 0) e = readScns(in, size, out);
    else e = in.skip(size) ? LoadErr::Ok : LoadErr::Truncated;
    if (e != LoadErr::Ok) return e;
  }
}

}  // namespace mt
