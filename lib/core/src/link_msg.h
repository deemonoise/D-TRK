#pragma once
#include <stdint.h>
#include "link_frame.h"

// Link messages: frame type ids and payload codecs (little-endian), shared by both firmwares.
namespace mt::link {

constexpr uint16_t kProtocol = 1;

// Frame types. Append only: the numbers are the protocol.
enum class Msg : uint8_t {
  Hello = 1, Time, Ev, StateSet, Ack, Nack,
  FsOpen, FsRead, FsWrite, FsClose, FsStat, FsList, FsRemove, FsRename, FsMkdir,
  BankSync, AssetsSave, BankImport, SampleInfo, BankIndex, BankClear,
  WavePeaks, Onsets,
  PreviewNote, PreviewSlice, PreviewFile, PreviewStop,
  RenderStart, RenderBlocks, RenderEnd,
  Meters, Phones, Profile,
  Status, Progress, FwFromFile, Log,
  FsRmdir,
  WtFrame,
};

// Fs* replies: err first, 0 = ok.
constexpr int16_t kErrOk = 0;
constexpr int16_t kErrNoCard = -1;     // no card in the synth board, or it does not mount
constexpr int16_t kErrNotFound = -2;
constexpr int16_t kErrExists = -3;     // mkdir / rename target already there
constexpr int16_t kErrIo = -4;         // the card failed the operation
constexpr int16_t kErrBadHandle = -5;
constexpr int16_t kErrTooMany = -6;    // all handles in use
constexpr int16_t kErrBadArg = -7;     // malformed request, bad path or mode
constexpr int16_t kErrIsDir = -8;      // opened a folder for writing
constexpr int16_t kErrNotEmpty = -9;
constexpr int16_t kErrLink = -10;      // ESP side: no reply from the synth board

class Writer {
 public:
  Writer(uint8_t* buf, int cap) : b_(buf), cap_(cap) {}
  void u8(uint8_t v) { put(&v, 1); }
  void u16(uint16_t v);
  void u32(uint32_t v);
  void u64(uint64_t v);
  void bytes(const void* d, int n) { put(d, n); }
  void str(const char* s);  // length byte + bytes (up to 255)
  int size() const { return n_; }
  bool ok() const { return ok_; }

 private:
  void put(const void* d, int n);
  uint8_t* b_;
  int cap_;
  int n_ = 0;
  bool ok_ = true;
};

class Reader {
 public:
  Reader(const uint8_t* buf, int n) : b_(buf), n_(n) {}
  uint8_t u8();
  uint16_t u16();
  uint32_t u32();
  uint64_t u64();
  void bytes(void* d, int n);
  // Length byte + bytes into out (cap includes the terminator; longer strings are cut).
  void str(char* out, int cap);
  int left() const { return n_ - pos_; }
  bool ok() const { return ok_; }  // false after reading past the end

 private:
  bool take(int n);
  const uint8_t* b_;
  int n_;
  int pos_ = 0;
  bool ok_ = true;
};

struct Hello {
  uint16_t proto = kProtocol;
  char fw[16] = {};
  uint32_t bootId = 0;
  uint32_t modelSize = 0;  // sizeof(SynthModel): both sides must agree on the mirror layout
};

struct Time {
  uint64_t tUs = 0;  // ESP engine clock
};

struct Ev {
  uint64_t tUs;
  uint8_t track, len, b[3];
};
// Events for INT tracks; on the wire one base time + a 32-bit offset per event.
struct EvBatch {
  static constexpr int kMax = 40;
  uint8_t n = 0;
  Ev ev[kMax];
};

// One piece of a SynthModel chunk; a chunk larger than one frame is sent as pieces by offset.
struct StateSet {
  static constexpr int kMaxData = 480;
  uint16_t chunk = 0, total = 0, offset = 0, len = 0;  // total = the chunk's size
  uint8_t data[kMaxData];
};

struct Ack {
  uint16_t id = 0;  // chunk / block the reply is for
};
struct Nack {
  uint16_t id = 0;
  int16_t err = 0;
};

struct Status {
  static constexpr int kScopeMax = 256;
  uint8_t cpuPct = 0;
  uint16_t stalls = 0, late = 0;
  uint8_t voices = 0;
  uint16_t outPeak = 0;
  uint8_t trackPeak[16] = {};  // 100 = full scale at MAIN 100 %
  uint16_t lost = 0;
  uint16_t scopeN = 0;  // 0 while the meters are off; the newest output, every 2nd sample, >> 8
  int8_t scope[kScopeMax];
};

// A long request on the synth board is still running: op = its type, done / total in work units,
// item = the sample (0..127) or wavetable (128 + i) of the project it is on, kNoItem if none.
struct Progress {
  static constexpr uint8_t kNoItem = 0xFF;
  static constexpr uint8_t kWtItem = 128;
  uint8_t op = 0;
  uint32_t done = 0, total = 0;
  uint8_t item = kNoItem;
};

// Meters (MIX shown: track peaks + scope in Status), Phones (output level 0..100 %).
struct Byte {
  uint8_t v = 0;
};

// Plays note with instrument instr on the synth's preview track for holdMs (0 = the default 300 ms).
struct PreviewNote {
  uint8_t instr = 0, note = 60;
  uint32_t holdMs = 0;
};
// SAMPLE instrument instr: slice at root, held holdMs (up to 25 s) or until the next preview.
struct PreviewSlice {
  uint8_t instr = 0, slice = 0, root = 60;
  uint32_t holdMs = 0;
};

// Sample bank (on the synth board). Every request is answered by a frame of its type and seq;
// BankSync, AssetsSave, BankImport and BankClear may run long and send Progress every 100 ms until
// then. result: mt::BankResult (bank_ops.h). Masks: bit i of the project's sample / wavetable list.
constexpr int kSampleMaskBytes = 16;  // kProjSamples bits
constexpr int kWtMaskBytes = 4;       // kProjWavetables bits
inline bool maskGet(const uint8_t* m, int i) { return (m[i >> 3] >> (i & 7)) & 1; }
inline void maskSet(uint8_t* m, int i) { m[i >> 3] |= static_cast<uint8_t>(1 << (i & 7)); }

// BankSync: the mirrored lists against the cache, missing ones imported from /projects/<project>/
// ("" = no folder: only the cache). AssetsSave: the cached ones written there (overwritten).
struct BankProjectReq {
  char project[17] = {};
};
// missing: entries not cached (after the sync / so not written by the save), bits in the masks;
// failed: files the save could not write (result WriteFail then).
struct BankSetsRep {
  uint8_t result = 0;
  uint8_t missing = 0, failed = 0;
  uint8_t samples[kSampleMaskBytes] = {};
  uint8_t wavetables[kWtMaskBytes] = {};
};
// A WAV from the card into the cache: a sample (kind 0) or a wavetable (kind 1). crc = the data
// the caller expects (hasCrc): the file is not read when that data is cached already.
struct BankImportReq {
  uint8_t kind = 0;
  uint8_t hasCrc = 0;
  uint32_t crc = 0;
  char path[256] = {};
};
struct BankImportRep {
  uint8_t result = 0;
  uint32_t crc = 0, frames = 0, rate = 0;  // wavetable: frames = 0, rate = 0
  uint8_t root = 60;
};
// The bank entry of project sample index.
struct SampleInfoReq {
  uint8_t index = 0;
};
struct SampleInfoRep {
  uint8_t result = 0;
  uint8_t cached = 0;
  uint32_t frames = 0, rate = 0;
  uint8_t root = 60, loop = 0;
};
// BankIndex (empty request): the bank and the mirrored lists' state in it.
struct BankIndexRep {
  static constexpr int kRates = 128;  // kProjSamples
  uint8_t result = 0;  // NoBank: not mounted, the rest is zero
  uint8_t count = 0;   // entries
  uint32_t capacity = 0, free = 0, unused = 0;  // bytes; unused = cache the project does not use
  uint32_t gen = 0;    // SampleBank::generation
  uint8_t samples[kSampleMaskBytes] = {};  // cached
  uint8_t wavetables[kWtMaskBytes] = {};
  uint8_t builtins = 0;  // bit i: built-in wavetable i cached
  uint8_t n = 0;         // rates sent: the project's sample count
  uint16_t rate[kRates] = {};  // of each cached sample, 0 if missing
};
// BankClear: removes the entries the project does not use, or (compact) closes the holes.
struct BankClearReq {
  uint8_t compact = 0;
};
struct BankClearRep {
  uint8_t result = 0;
  uint8_t removed = 0;
};
// Level 0 of frame (0..63) of wavetable name (built-in "*NAME" or a name of the project's list).
struct WtFrameReq {
  uint8_t frame = 0;
  char name[17] = {};
};
struct WtFrameRep {
  static constexpr int kPoints = 256;  // kWtFrameLen
  uint8_t result = 0;
  int8_t pts[kPoints] = {};  // >> 8
};

// Sample editor, on the bank data of project sample index (result: mt::BankResult).
// WavePeaks: min / max (>> 8) of cols columns of a zoom grid from grid column col0, the grid putting
// span frames on width columns (mt::wavePeaks).
struct WavePeaksReq {
  uint8_t index = 0;
  uint32_t col0 = 0, span = 0;
  uint16_t width = 0;
  uint8_t cols = 0;
};
struct WavePeaksRep {
  static constexpr int kMaxCols = 240;
  uint8_t result = 0;
  uint8_t cols = 0;
  int8_t mn[kMaxCols] = {}, mx[kMaxCols] = {};
};
// Onsets: mt::detectOnsets of the whole sample (total, up to kMaxOnsets); a reply carries up to kMax
// of them from the skip-th on.
struct OnsetsReq {
  uint8_t index = 0;
  uint8_t skip = 0;
};
struct OnsetsRep {
  static constexpr int kMax = 80;
  uint8_t result = 0;
  uint8_t total = 0, n = 0;
  uint32_t pos[kMax] = {};
  uint16_t strength[kMax] = {};
};
// PreviewFile: a WAV on the card (PCM 8/16/24 bit, channels mixed to mono, rate <= kPreviewMaxRate)
// streamed to the output from its start, ending the previous one; the reply has its frames and
// rate. PreviewStop (empty) ends it.
constexpr uint32_t kPreviewMaxRate = 48000;
struct PreviewFileReq {
  char path[256] = {};
};
struct PreviewFileRep {
  uint8_t result = 0;
  uint32_t frames = 0, rate = 0;
};

// Render (WAV / resample) on the synth board. RenderStart parks the live output, resets the synth and
// opens <path>.tmp (target file: stereo 16-bit) or a temporary file for the bank (target bank: the
// mono mix goes into the cache at the end; path unused). RenderBlocks carry the events of the next
// blocks (mt::OfflineSequence on the ESP), rendered and written before the reply; a block with more
// events than fit a frame goes on in the next one (kCont on its count, the frame's last record).
// RenderEnd finalises (or aborts: the file removed) and the live output resumes. Every reply is a
// RenderRep of the request's type; a request repeated with the same seq gets the same reply again.
enum class RenderResult : uint8_t {
  Ok, NoSd, OpenFail, WriteFail, ReadFail, Busy,
  NoRender,  // RenderBlocks / RenderEnd without a RenderStart (or after a timeout / Hello)
  BadOrder,  // RenderBlocks not starting at the block being filled
  Bank,      // the bank write failed: RenderRep::bank has the mt::BankResult
};
struct RenderStartReq {
  static constexpr uint8_t kFile = 0, kBank = 1;
  uint8_t target = kFile;
  char path[128] = {};
};
struct RenderEv {
  uint8_t off, track, len, b[3];
};
struct RenderBlocks {
  static constexpr int kMaxBlocks = 8;
  static constexpr int kMaxEvents = 80;
  static constexpr uint8_t kCont = 0x80;
  uint32_t first = 0;  // block of the first record
  uint8_t n = 0;       // records
  uint8_t evN[kMaxBlocks] = {};
  RenderEv ev[kMaxEvents];
};
// normalize: scaled so the peak lands at -1 dBFS (the bank: the mono mix's peak); trim: cut after
// the last block above -60 dBFS.
struct RenderEndReq {
  uint8_t abort = 0, normalize = 0, trim = 0;
};
// blocks: rendered so far; at the end frames / crc of the result (the bank: its data key, file: the
// mono mix's crc in its "mtcr"), peak (before normalizing) and clips of the render.
struct RenderRep {
  uint8_t result = 0;
  uint8_t bank = 0;
  uint32_t blocks = 0, frames = 0;
  uint16_t peak = 0;
  uint32_t clips = 0, crc = 0;
};

// FwFromFile: a .hex on the card (Intel HEX of this board's firmware) into the OTA area of the flash,
// checked (record checksums, size, the image's header and target id); Progress (bytes of the file)
// meanwhile. Ok: the reply goes out, then the image replaces the running firmware and the board
// reboots (a new Hello). Any error: the OTA area erased, the running firmware untouched.
enum class FwResult : uint8_t { Ok, NoSd, OpenFail, ReadFail, BadHex, TooBig, BadImage, FlashFail, Busy };
struct FwFromFileReq {
  char path[128] = {};
};
struct FwRep {
  uint8_t result = 0;
  uint32_t bytes = 0;  // the image
};

// Profile (PROJ -> SYS -> CPU profile): Byte request, v 1 = start (clears the sums), 0 = stop. The
// reply: the render cycles per Synth::ProfStage summed since the start, the blocks rendered, and the
// cycles per us to convert them.
struct ProfileRep {
  static constexpr int kStages = 11;  // Synth::kProfStages
  uint8_t running = 0;
  uint32_t blocks = 0;
  uint32_t cyclesPerUs = 0;
  uint64_t cycles[kStages] = {};
};

struct Log {
  static constexpr int kMax = 200;
  char text[kMax + 1] = {};
};

// Payload -> bytes; w.ok() false when it does not fit.
void encode(const Hello& m, Writer& w);
void encode(const Time& m, Writer& w);
void encode(const EvBatch& m, Writer& w);
void encode(const StateSet& m, Writer& w);
void encode(const Ack& m, Writer& w);
void encode(const Nack& m, Writer& w);
void encode(const Status& m, Writer& w);
void encode(const Progress& m, Writer& w);
void encode(const Log& m, Writer& w);
void encode(const Byte& m, Writer& w);
void encode(const PreviewNote& m, Writer& w);
void encode(const ProfileRep& m, Writer& w);
void encode(const PreviewSlice& m, Writer& w);
void encode(const BankProjectReq& m, Writer& w);
void encode(const BankSetsRep& m, Writer& w);
void encode(const BankImportReq& m, Writer& w);
void encode(const BankImportRep& m, Writer& w);
void encode(const SampleInfoReq& m, Writer& w);
void encode(const SampleInfoRep& m, Writer& w);
void encode(const BankIndexRep& m, Writer& w);
void encode(const BankClearReq& m, Writer& w);
void encode(const BankClearRep& m, Writer& w);
void encode(const WtFrameReq& m, Writer& w);
void encode(const WtFrameRep& m, Writer& w);
void encode(const WavePeaksReq& m, Writer& w);
void encode(const WavePeaksRep& m, Writer& w);
void encode(const OnsetsReq& m, Writer& w);
void encode(const OnsetsRep& m, Writer& w);
void encode(const PreviewFileReq& m, Writer& w);
void encode(const PreviewFileRep& m, Writer& w);
void encode(const RenderStartReq& m, Writer& w);
void encode(const RenderBlocks& m, Writer& w);
void encode(const RenderEndReq& m, Writer& w);
void encode(const RenderRep& m, Writer& w);
void encode(const FwFromFileReq& m, Writer& w);
void encode(const FwRep& m, Writer& w);

// False on a truncated / malformed payload.
bool decode(Reader& r, Hello& m);
bool decode(Reader& r, Time& m);
bool decode(Reader& r, EvBatch& m);
bool decode(Reader& r, StateSet& m);
bool decode(Reader& r, Ack& m);
bool decode(Reader& r, Nack& m);
bool decode(Reader& r, Status& m);
bool decode(Reader& r, Progress& m);
bool decode(Reader& r, Log& m);
bool decode(Reader& r, Byte& m);
bool decode(Reader& r, PreviewNote& m);
bool decode(Reader& r, PreviewSlice& m);
bool decode(Reader& r, BankProjectReq& m);
bool decode(Reader& r, BankSetsRep& m);
bool decode(Reader& r, BankImportReq& m);
bool decode(Reader& r, BankImportRep& m);
bool decode(Reader& r, SampleInfoReq& m);
bool decode(Reader& r, SampleInfoRep& m);
bool decode(Reader& r, BankIndexRep& m);
bool decode(Reader& r, BankClearReq& m);
bool decode(Reader& r, BankClearRep& m);
bool decode(Reader& r, WtFrameReq& m);
bool decode(Reader& r, WtFrameRep& m);
bool decode(Reader& r, WavePeaksReq& m);
bool decode(Reader& r, WavePeaksRep& m);
bool decode(Reader& r, OnsetsReq& m);
bool decode(Reader& r, OnsetsRep& m);
bool decode(Reader& r, PreviewFileReq& m);
bool decode(Reader& r, PreviewFileRep& m);
bool decode(Reader& r, ProfileRep& m);
bool decode(Reader& r, RenderStartReq& m);
bool decode(Reader& r, RenderBlocks& m);
bool decode(Reader& r, RenderEndReq& m);
bool decode(Reader& r, RenderRep& m);
bool decode(Reader& r, FwFromFileReq& m);
bool decode(Reader& r, FwRep& m);

// Encodes m as a whole frame of type t into out (kMaxEncoded bytes); 0 when it does not fit.
template <class M>
int frame(Msg t, uint8_t seq, const M& m, uint8_t* out) {
  uint8_t p[kMaxPayload];
  Writer w(p, kMaxPayload);
  encode(m, w);
  return w.ok() ? encode(static_cast<uint8_t>(t), seq, p, w.size(), out) : 0;
}

}  // namespace mt::link
